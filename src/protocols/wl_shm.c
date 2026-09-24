/*
 * Copyright (c) 2026 Luca Machiedo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _GNU_SOURCE

#include "wl_shm.h"

#include "../core/buffer.h"
#include "../core/util.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#define _SUBSYS_NAME    "VT_PROTO_WL_SHM"
#define _WL_SHM_VERSION 3u

struct proto_wl_shm_t {
  struct vt_compositor_t *comp;
  struct wl_global       *global;
  struct wl_listener      dsp_destroy;

  /* uint32_t wl_shm format codes. */
  struct wl_array formats;
};

struct shm_pool_t {
  struct proto_wl_shm_t *proto;
  struct wl_resource    *res;

  int    fd;
  void  *data;
  size_t size;

  uint32_t refcount;
};

struct shm_buffer_t {
  struct proto_wl_shm_t *proto;
  struct wl_resource    *res;
  struct shm_pool_t     *pool;

  size_t   offset;
  int32_t  width;
  int32_t  height;
  int32_t  stride;
  uint32_t format;
  size_t   byte_size;

  struct vt_buffer_t *buf;
};

static struct proto_wl_shm_t _proto;

static void _wl_shm_bind(struct wl_client *client, void *data, uint32_t version,
                         uint32_t id);
static void _wl_shm_create_pool(struct wl_client   *client,
                                struct wl_resource *resource, uint32_t id,
                                int32_t fd, int32_t size);
static void _wl_shm_release(struct wl_client   *client,
                            struct wl_resource *resource);

static void _wl_shm_pool_create_buffer(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t id, int32_t offset,
                                       int32_t width, int32_t height,
                                       int32_t stride, uint32_t format);
static void _wl_shm_pool_destroy(struct wl_client   *client,
                                 struct wl_resource *resource);
static void _wl_shm_pool_resize(struct wl_client   *client,
                                struct wl_resource *resource, int32_t size);

static void _wl_shm_pool_handle_res_destroy(struct wl_resource *resource);
static void _wl_shm_buffer_handle_res_destroy(struct wl_resource *resource);
static void _wl_shm_handle_display_destroy(struct wl_listener *listener,
                                           void               *data);

static void _wl_shm_buffer_destroy(struct wl_client   *client,
                                   struct wl_resource *resource);

static void _shm_buffer_attachment_destroy(struct vt_buffer_t *buf, void *owner,
                                           void *data);

bool _wl_shm_buffer_get_attr(struct vt_buffer_t   *buf,
                             struct vt_shm_attr_t *o_attr);

static const struct wl_shm_interface _wl_shm_impl = {
    .create_pool = _wl_shm_create_pool,
    .release = _wl_shm_release,
};

static const struct wl_shm_pool_interface _wl_shm_pool_impl = {
    .create_buffer = _wl_shm_pool_create_buffer,
    .destroy = _wl_shm_pool_destroy,
    .resize = _wl_shm_pool_resize,
};

static const struct wl_buffer_interface _wl_shm_buffer_impl = {
    .destroy = _wl_shm_buffer_destroy,
};

static const struct vt_buffer_attachment_implementation_t
    _shm_buffer_attachment_impl = {
        .destroy = _shm_buffer_attachment_destroy,
};

static const struct vt_buffer_implementation_t _shm_buffer_impl = {
    .get_shm = _wl_shm_buffer_get_attr};

static bool _size_add_overflow(size_t a, size_t b, size_t *out) {
  if (a > SIZE_MAX - b)
    return true;
  *out = a + b;
  return false;
}

static bool _size_mul_overflow(size_t a, size_t b, size_t *out) {
  if (a != 0 && b > SIZE_MAX / a)
    return true;
  *out = a * b;
  return false;
}

static bool _format_bpp(uint32_t format, size_t *bpp) {
  if (!bpp)
    return false;

  switch (format) {
  case WL_SHM_FORMAT_ARGB8888:
  case WL_SHM_FORMAT_XRGB8888:
    *bpp = 4;
    return true;
  default:
    return false;
  }
}

static bool _format_is_advertised(uint32_t format) {
  uint32_t *it;
  wl_array_for_each(it, &_proto.formats) {
    if (*it == format)
      return true;
  }
  return false;
}

static bool _append_format_unique(uint32_t format) {
  if (_format_is_advertised(format))
    return true;

  uint32_t *slot = wl_array_add(&_proto.formats, sizeof(*slot));
  if (!slot)
    return false;

  *slot = format;
  return true;
}

static struct shm_pool_t *_pool_ref(struct shm_pool_t *pool) {
  if (!pool)
    return NULL;

  assert(pool->refcount > 0);
  pool->refcount++;
  return pool;
}

static void _pool_unref(struct shm_pool_t **pool_ptr) {
  if (!pool_ptr || !*pool_ptr)
    return;

  struct shm_pool_t *pool = *pool_ptr;
  *pool_ptr = NULL;

  assert(pool->refcount > 0);
  if (--pool->refcount != 0)
    return;

  if (pool->data && pool->data != MAP_FAILED)
    munmap(pool->data, pool->size);

  if (pool->fd >= 0)
    close(pool->fd);

  free(pool);
}

static bool _fd_is_large_enough(int fd, size_t size) {
  struct stat st;
  if (fstat(fd, &st) < 0)
    return false;

  if (st.st_size < 0)
    return false;

  return (uint64_t)st.st_size >= (uint64_t)size;
}

static bool _validate_buffer_layout(struct shm_pool_t *pool, int32_t offset,
                                    int32_t width, int32_t height,
                                    int32_t stride, uint32_t format,
                                    size_t *out_byte_size) {
  if (!pool || !out_byte_size)
    return false;

  if (offset < 0 || width <= 0 || height <= 0 || stride <= 0)
    return false;

  size_t bpp = 0;
  if (!_format_bpp(format, &bpp))
    return false;

  size_t row_bytes = 0;
  if (_size_mul_overflow((size_t)width, bpp, &row_bytes))
    return false;

  if ((size_t)stride < row_bytes)
    return false;

  /* End of the actual last row, not end of the last row's padding. */
  size_t rows_before_last = 0;
  if (_size_mul_overflow((size_t)(height - 1), (size_t)stride,
                         &rows_before_last))
    return false;

  size_t payload = 0;
  if (_size_add_overflow(rows_before_last, row_bytes, &payload))
    return false;

  size_t end = 0;
  if (_size_add_overflow((size_t)offset, payload, &end))
    return false;

  if (end > pool->size)
    return false;

  *out_byte_size = payload;
  return true;
}

/* ------------------------------------------------------------------------- */
/* wl_shm                                                                    */
/* ------------------------------------------------------------------------- */

static void _wl_shm_bind(struct wl_client *client, void *data, uint32_t version,
                         uint32_t id) {
  struct proto_wl_shm_t *proto = data;
  if (!proto || !proto->comp)
    return;

  if (version > _WL_SHM_VERSION)
    version = _WL_SHM_VERSION;

  struct wl_resource *resource =
      wl_resource_create(client, &wl_shm_interface, version, id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(resource, &_wl_shm_impl, proto, NULL);

  uint32_t *format;
  wl_array_for_each(format, &proto->formats) {
    wl_shm_send_format(resource, *format);
  }
}

static void _wl_shm_create_pool(struct wl_client   *client,
                                struct wl_resource *resource, uint32_t id,
                                int32_t fd, int32_t size) {
  if (fd < 0 || size <= 0) {
    if (fd >= 0)
      close(fd);
    wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_STRIDE,
                           "wl_shm pool size must be positive");
    return;
  }

  if (!_fd_is_large_enough(fd, (size_t)size)) {
    close(fd);
    wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD,
                           "wl_shm pool fd is smaller than requested mapping");
    return;
  }

  void *mapping =
      mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapping == MAP_FAILED) {
    close(fd);
    wl_resource_post_error(resource, WL_SHM_ERROR_INVALID_FD,
                           "mmap for wl_shm pool failed: %s", strerror(errno));
    return;
  }

  struct shm_pool_t *pool = calloc(1, sizeof(*pool));
  if (!pool) {
    munmap(mapping, (size_t)size);
    close(fd);
    wl_client_post_no_memory(client);
    return;
  }

  pool->proto = &_proto;
  pool->fd = fd;
  pool->data = mapping;
  pool->size = (size_t)size;
  pool->refcount = 1;

  uint32_t version = wl_resource_get_version(resource);
  if (version > _WL_SHM_VERSION)
    version = _WL_SHM_VERSION;

  pool->res = wl_resource_create(client, &wl_shm_pool_interface, version, id);
  if (!pool->res) {
    _pool_unref(&pool);
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(pool->res, &_wl_shm_pool_impl, pool,
                                 _wl_shm_pool_handle_res_destroy);
}

static void _wl_shm_release(struct wl_client   *client,
                            struct wl_resource *resource) {
  (void)client;
  wl_resource_destroy(resource);
}

static void _wl_shm_pool_create_buffer(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t id, int32_t offset,
                                       int32_t width, int32_t height,
                                       int32_t stride, uint32_t format) {
  struct shm_pool_t *pool = wl_resource_get_user_data(resource);
  if (!pool)
    return;

  if (!_format_is_advertised(format)) {
    wl_resource_post_error(resource, WL_SHM_POOL_ERROR_INVALID_FORMAT,
                           "wl_shm format 0x%08x was not advertised", format);
    return;
  }

  size_t byte_size = 0;
  if (!_validate_buffer_layout(pool, offset, width, height, stride, format,
                               &byte_size)) {
    wl_resource_post_error(resource, WL_SHM_POOL_ERROR_INVALID_STRIDE,
                           "invalid wl_shm buffer layout: offset=%d size=%dx%d "
                           "stride=%d pool=%zu",
                           offset, width, height, stride, pool->size);
    return;
  }

  struct shm_buffer_t *shm = calloc(1, sizeof(*shm));
  if (!shm) {
    wl_client_post_no_memory(client);
    return;
  }

  shm->proto = pool->proto;
  shm->pool = _pool_ref(pool);
  shm->offset = (size_t)offset;
  shm->width = width;
  shm->height = height;
  shm->stride = stride;
  shm->format = format;
  shm->byte_size = byte_size;

  shm->res = wl_resource_create(client, &wl_buffer_interface, 1, id);
  if (!shm->res) {
    _pool_unref(&shm->pool);
    free(shm);
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(shm->res, &_wl_shm_buffer_impl, shm,
                                 _wl_shm_buffer_handle_res_destroy);
}

static void _wl_shm_pool_destroy(struct wl_client   *client,
                                 struct wl_resource *resource) {
  (void)client;
  wl_resource_destroy(resource);
}

static void _wl_shm_pool_resize(struct wl_client   *client,
                                struct wl_resource *resource, int32_t size) {
  (void)client;

  struct shm_pool_t *pool = wl_resource_get_user_data(resource);
  if (!pool)
    return;

  if (size <= 0 || (size_t)size <= pool->size) {
    wl_resource_post_error(resource, WL_SHM_POOL_ERROR_INVALID_STRIDE,
                           "wl_shm_pool.resize may only grow the pool "
                           "(old=%zu new=%d)",
                           pool->size, size);
    return;
  }

  if (!_fd_is_large_enough(pool->fd, (size_t)size)) {
    wl_resource_post_error(resource, WL_SHM_POOL_ERROR_INVALID_STRIDE,
                           "wl_shm pool backing file is smaller than resize "
                           "request (%d bytes)",
                           size);
    return;
  }

  void *new_mapping =
      mremap(pool->data, pool->size, (size_t)size, MREMAP_MAYMOVE);
  if (new_mapping == MAP_FAILED) {
    wl_resource_post_no_memory(resource);
    return;
  }

  pool->data = new_mapping;
  pool->size = (size_t)size;
}

static void _wl_shm_pool_handle_res_destroy(struct wl_resource *resource) {
  struct shm_pool_t *pool = wl_resource_get_user_data(resource);
  if (!pool)
    return;

  wl_resource_set_user_data(resource, NULL);
  pool->res = NULL;
  _pool_unref(&pool);
}

static void _wl_shm_buffer_destroy(struct wl_client   *client,
                                   struct wl_resource *resource) {
  (void)client;
  wl_resource_destroy(resource);
}

static void _wl_shm_buffer_handle_res_destroy(struct wl_resource *resource) {
  struct shm_buffer_t *shm = wl_resource_get_user_data(resource);
  if (!shm)
    return;

  wl_resource_set_user_data(resource, NULL);
  shm->res = NULL;

  if (shm->buf) {
    vt_buffer_unref(&shm->buf);
    return;
  }

  _pool_unref(&shm->pool);
  free(shm);
}

static void _shm_buffer_attachment_destroy(struct vt_buffer_t *buf, void *owner,
                                           void *data) {
  (void)buf;
  (void)owner;

  struct shm_buffer_t *shm = data;
  if (!shm)
    return;

  _pool_unref(&shm->pool);
  free(shm);
}

struct vt_buffer_t *vt_proto_wl_shm_get_buffer(struct wl_resource *res) {
  if (!res)
    return NULL;

  if (!wl_resource_instance_of(res, &wl_buffer_interface, &_wl_shm_buffer_impl))
    return NULL;

  struct shm_buffer_t *shm = wl_resource_get_user_data(res);

  if (!shm)
    return NULL;

  if (!shm->buf) {
    shm->buf = vt_buffer_create(_proto.comp, (uint32_t)shm->width,
                                (uint32_t)shm->height, &_shm_buffer_impl);
    if (!shm->buf)
      return NULL;

    struct vt_buffer_attachment_t *attachment = vt_buffer_add_attachment(
        shm->buf, shm, shm, &_shm_buffer_attachment_impl);
    if (!attachment) {
      vt_buffer_unref(&shm->buf);
      return NULL;
    }
  }

  return shm->buf;
}

bool _wl_shm_buffer_get_attr(struct vt_buffer_t   *buf,
                             struct vt_shm_attr_t *o_attr) {
  if (!buf)
    return false;

  struct vt_buffer_attachment_t *attachment =
      vt_buffer_find_attachment(buf, NULL, &_shm_buffer_attachment_impl);

  if (!attachment || !attachment->data)
    return false;

  struct shm_buffer_t *shm = attachment->data;
  if (!shm->pool || !shm->pool->data)
    return false;

  /* Reassign values, as wl_shm_pool.resize might have changed them */
  o_attr->data = (uint8_t *)shm->pool->data + shm->offset;
  o_attr->size = shm->byte_size;
  o_attr->width = shm->width;
  o_attr->height = shm->height;
  o_attr->stride = shm->stride;
  o_attr->format = shm->format;

  return true;
}

static void _wl_shm_handle_display_destroy(struct wl_listener *listener,
                                           void               *data) {
  (void)data;

  struct proto_wl_shm_t *proto = wl_container_of(listener, proto, dsp_destroy);

  wl_array_release(&proto->formats);
  proto->global = NULL;
  proto->comp = NULL;
}

bool vt_proto_wl_shm_init(struct vt_compositor_t *comp,
                          const uint32_t *drm_formats, uint32_t n_formats) {
  if (!comp || !comp->wl.dsp || !drm_formats || n_formats == 0)
    return false;

  memset(&_proto, 0, sizeof(_proto));
  _proto.comp = comp;
  wl_array_init(&_proto.formats);

  bool has_argb = false;
  bool has_xrgb = false;

  for (uint32_t i = 0; i < n_formats; i++) {
    uint32_t wl_format = vt_util_convert_drm_format_to_wl_shm(drm_formats[i]);

    if (wl_format == WL_SHM_FORMAT_ARGB8888)
      has_argb = true;
    else if (wl_format == WL_SHM_FORMAT_XRGB8888)
      has_xrgb = true;

    /* Only advertise formats for which we have complete stride validation. */
    size_t bpp = 0;
    if (!_format_bpp(wl_format, &bpp))
      continue;

    if (!_append_format_unique(wl_format)) {
      wl_array_release(&_proto.formats);
      memset(&_proto, 0, sizeof(_proto));
      return false;
    }
  }

  if (!has_argb || !has_xrgb) {
    VT_ERROR(comp->log,
             "wl_shm requires ARGB8888 and XRGB8888 renderer support");
    wl_array_release(&_proto.formats);
    memset(&_proto, 0, sizeof(_proto));
    return false;
  }

  _proto.global = wl_global_create(comp->wl.dsp, &wl_shm_interface,
                                   _WL_SHM_VERSION, &_proto, _wl_shm_bind);
  if (!_proto.global) {
    wl_array_release(&_proto.formats);
    memset(&_proto, 0, sizeof(_proto));
    return false;
  }

  _proto.dsp_destroy.notify = _wl_shm_handle_display_destroy;
  wl_display_add_destroy_listener(comp->wl.dsp, &_proto.dsp_destroy);

  VT_TRACE(comp->log, "Initialized custom wl_shm protocol (version %u)",
           _WL_SHM_VERSION);
  return true;
}
