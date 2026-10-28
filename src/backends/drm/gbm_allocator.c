#include "gbm_allocator.h"

#include <drm/drm_fourcc.h>
#include <gbm.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct gbm_attachment_data {
  struct gbm_bo *bo;
  int            dma_fds[VT_DMABUF_PLANES_CAP];
  uint32_t       format;
};

static const char gbm_attachment_owner_key;

static void _gbm_attachment_destroy(struct vt_buffer_t *buffer,
                                    const void *owner, void *ptr);
static struct gbm_attachment_data             *
_find_gbm_attachment(struct vt_buffer_t *buffer);
static bool _allocated_gbm_get_dmabuf(struct vt_buffer_t      *buffer,
                                      struct vt_dmabuf_attr_t *out);

static const struct vt_buffer_attachment_implementation_t gbm_attachment_impl =
    {
        .destroy = _gbm_attachment_destroy,
        .end_use = NULL,
};

static void _gbm_attachment_destroy(struct vt_buffer_t *buffer,
                                    const void *owner, void *ptr) {
  (void)buffer;
  (void)owner;

  struct gbm_attachment_data *data = ptr;
  if (!data)
    return;

  for (int i = 0; i < VT_DMABUF_PLANES_CAP; i++) {
    if (data->dma_fds[i] >= 0)
      close(data->dma_fds[i]);
  }
  if (data->bo)
    gbm_bo_destroy(data->bo);
  free(data);
}

static struct gbm_attachment_data *
_find_gbm_attachment(struct vt_buffer_t *buffer) {
  if (!buffer)
    return NULL;

  struct vt_buffer_attachment_t *attachment = vt_buffer_find_attachment(
      buffer, &gbm_attachment_owner_key, &gbm_attachment_impl);
  return attachment ? attachment->data : NULL;
}

struct gbm_bo *drm_gbm_buffer_allocator_get_bo(struct vt_buffer_t *buffer) {
  struct gbm_attachment_data *data = _find_gbm_attachment(buffer);
  return data ? data->bo : NULL;
}

static bool _allocated_gbm_get_dmabuf(struct vt_buffer_t      *buffer,
                                      struct vt_dmabuf_attr_t *out) {
  struct gbm_attachment_data *data = _find_gbm_attachment(buffer);
  if (!data || !out)
    return false;

  int count = gbm_bo_get_plane_count(data->bo);
  if (count < 1 || count > VT_DMABUF_PLANES_CAP)
    return false;

  memset(out, 0, sizeof(*out));
  out->width = buffer->width;
  out->height = buffer->height;
  out->format = gbm_bo_get_format(data->bo);
  out->mod = gbm_bo_get_modifier(data->bo);

  for (int i = 0; i < count; i++) {
    out->fds[i] = data->dma_fds[i];
    out->strides[i] = gbm_bo_get_stride_for_plane(data->bo, i);
    out->offsets[i] = gbm_bo_get_offset(data->bo, i);
  }
  out->num_planes = count;
  return true;
}

static const struct vt_buffer_implementation_t allocated_gbm_buffer_impl = {
    .get_dmabuf = _allocated_gbm_get_dmabuf,
    .get_shm = NULL,
};

bool drm_gbm_buffer_allocator_clear(struct vt_buffer_t *buffer,
                                    uint32_t            color) {
  struct gbm_attachment_data *data = _find_gbm_attachment(buffer);
  if (!data || !data->bo || buffer->uses != 0)
    return false;

  if (buffer->width == 0 || buffer->height == 0 ||
      buffer->width > UINT32_MAX / 4u)
    return false;

  uint32_t stride = 0;
  void    *map_token = NULL;
  void    *pixels = gbm_bo_map(data->bo, 0, 0, buffer->width, buffer->height,
                               GBM_BO_TRANSFER_WRITE, &stride, &map_token);
  if (!pixels)
    return false;

  bool ok = stride >= buffer->width * 4u;
  if (ok) {
    const uint32_t pixel =
        UINT32_C(0xff000000) | (color & UINT32_C(0x00ffffff));
    uint8_t *first = pixels;
    for (uint32_t x = 0; x < buffer->width; ++x)
      memcpy(first + (size_t)x * 4, &pixel, sizeof(pixel));

    const size_t row_bytes = (size_t)buffer->width * 4;
    for (uint32_t y = 1; y < buffer->height; ++y)
      memcpy((uint8_t *)pixels + (size_t)y * stride, first, row_bytes);
  }

  gbm_bo_unmap(data->bo, map_token);
  return ok;
}

struct vt_buffer_t *drm_gbm_buffer_allocator_alloc(struct vt_compositor_t *comp,
                                                   struct gbm_device      *gbm,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   uint32_t format) {
  if (!comp || !gbm || width == 0 || height == 0 || width > UINT32_MAX / 4u)
    return NULL;

  const uint32_t usage = GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR;
  struct gbm_bo *bo = gbm_bo_create(gbm, width, height, format, usage);
  if (!bo)
    return NULL;

  struct gbm_attachment_data *data = calloc(1, sizeof(*data));
  if (!data) {
    gbm_bo_destroy(bo);
    return NULL;
  }

  data->format = format;
  data->bo = bo;
  for (int i = 0; i < VT_DMABUF_PLANES_CAP; i++)
    data->dma_fds[i] = -1;

  int plane_count = gbm_bo_get_plane_count(bo);
  if (plane_count < 1 || plane_count > VT_DMABUF_PLANES_CAP) {
    _gbm_attachment_destroy(NULL, NULL, data);
    return NULL;
  }

  /* get_dmabuf callers borrow these FDs for the buffer's lifetime. */
  for (int i = 0; i < plane_count; i++) {
    data->dma_fds[i] = gbm_bo_get_fd_for_plane(bo, i);
    if (data->dma_fds[i] < 0 || !gbm_bo_get_stride_for_plane(bo, i)) {
      _gbm_attachment_destroy(NULL, NULL, data);
      return NULL;
    }
  }

  struct vt_buffer_t *buffer =
      vt_buffer_create(comp, width, height, &allocated_gbm_buffer_impl);
  if (!buffer) {
    _gbm_attachment_destroy(NULL, NULL, data);
    return NULL;
  }

  if (!vt_buffer_add_attachment(buffer, &gbm_attachment_owner_key, data,
                                &gbm_attachment_impl)) {
    _gbm_attachment_destroy(NULL, NULL, data);
    vt_buffer_unref(&buffer);
    return NULL;
  }

  if (!drm_gbm_buffer_allocator_clear(buffer, 0x000000u)) {
    /* destroys the attachment */
    vt_buffer_unref(&buffer);
    return NULL;
  }

  return buffer;
}
