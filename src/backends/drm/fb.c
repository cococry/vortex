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

#include "fb.h"

#include "core/compositor.h"
#include "prime.h"
#include <drm/drm_fourcc.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <xf86drmMode.h>

#define _SUBSYS_NAME "DRM"

static bool _drm_fb_add(struct drm_backend_state_t *drm,
                        struct drm_framebuffer_t   *fb,
                        uint32_t width, uint32_t height, uint32_t format,
                        uint32_t handles[4], uint32_t strides[4],
                        uint32_t offsets[4], uint64_t modifier) {
  int ret = -1;

  if (drm->caps[VT_DRM_CAP_ADDFB2_MODIFIERS] &&
      modifier != DRM_FORMAT_MOD_INVALID) {
    uint64_t modifiers[4] = {modifier, modifier, modifier, modifier};
    ret = drmModeAddFB2WithModifiers(drm->drm_fd, width, height, format,
                                     handles, strides, offsets, modifiers,
                                     &fb->id, DRM_MODE_FB_MODIFIERS);
  }

  if (ret != 0 &&
      (modifier == DRM_FORMAT_MOD_INVALID || modifier == DRM_FORMAT_MOD_LINEAR)) {
    ret = drmModeAddFB2(drm->drm_fd, width, height, format, handles, strides,
                        offsets, &fb->id, 0);
  }

  if (ret != 0) {
    VT_ERROR(drm->comp->log,
             "Failed to create DRM framebuffer (%ux%u, format=0x%08x, "
             "modifier=0x%016" PRIx64 "): %s",
             width, height, format, modifier, strerror(errno));
    return false;
  }

  return true;
}

bool drm_fb_init_from_buffer(struct drm_backend_state_t *drm,
                             struct drm_framebuffer_t   *fb,
                             struct vt_buffer_t          *buf) {
  if (!drm || !fb || !buf)
    return false;

  memset(fb, 0, sizeof(*fb));

  struct vt_dmabuf_attr_t attr = {0};
  if (!vt_buffer_get_dmabuf(buf, &attr) || attr.num_planes <= 0 ||
      attr.num_planes > 4) {
    return false;
  }

  uint32_t handles[4] = {0};
  uint32_t strides[4] = {0};
  uint32_t offsets[4] = {0};

  if (!drm_prime_import_dmabuf(drm->drm_fd, &attr, handles))
    return false;

  for (int32_t i = 0; i < attr.num_planes; i++) {
    strides[i] = attr.strides[i];
    offsets[i] = attr.offsets[i];
  }

  if (!_drm_fb_add(drm, fb, attr.width, attr.height, attr.format, handles,
                   strides, offsets, attr.mod)) {
    drm_prime_close_handles(drm->drm_fd, handles);
    return false;
  }

  memcpy(fb->handles, handles, sizeof(handles));
  fb->owns_handles = true;
  fb->buf = vt_buffer_ref(buf);

  return true;
}

bool drm_fb_init_from_gbm(struct drm_backend_state_t *drm,
                          struct drm_framebuffer_t   *fb,
                          struct gbm_bo              *bo) {
  if (!drm || !fb || !bo)
    return false;

  memset(fb, 0, sizeof(*fb));

  uint32_t width = gbm_bo_get_width(bo);
  uint32_t height = gbm_bo_get_height(bo);
  uint32_t format = gbm_bo_get_format(bo);
  uint64_t modifier = gbm_bo_get_modifier(bo);

  int plane_count = gbm_bo_get_plane_count(bo);
  if (plane_count <= 0 || plane_count > 4)
    return false;

  uint32_t handles[4] = {0};
  uint32_t strides[4] = {0};
  uint32_t offsets[4] = {0};

  bool import = gbm_bo_get_device(bo) != drm->gbm_dev;

  if (import) {
    if (!drm_prime_import_gbm_bo(drm->drm_fd, bo, handles))
      return false;
  } else {
    for (int i = 0; i < plane_count; i++) {
      handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
      if (handles[i] == 0)
        return false;
    }
  }

  for (int i = 0; i < plane_count; i++) {
    strides[i] = gbm_bo_get_stride_for_plane(bo, i);
    offsets[i] = gbm_bo_get_offset(bo, i);
  }

  if (!_drm_fb_add(drm, fb, width, height, format, handles, strides, offsets,
                   modifier)) {
    if (import)
      drm_prime_close_handles(drm->drm_fd, handles);
    return false;
  }

  if (import) {
    memcpy(fb->handles, handles, sizeof(handles));
    fb->owns_handles = true;
  }

  return true;
}

void drm_fb_finish(struct drm_backend_state_t *drm,
                   struct drm_framebuffer_t   *fb) {
  if (!drm || !fb)
    return;

  if (fb->id != 0)
    drmModeRmFB(drm->drm_fd, fb->id);

  if (fb->owns_handles)
    drm_prime_close_handles(drm->drm_fd, fb->handles);

  if (fb->buf)
    vt_buffer_unref(&fb->buf);

  memset(fb, 0, sizeof(*fb));
}
