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

#include "prime.h"

#include <string.h>
#include <unistd.h>
#include <xf86drm.h>

void drm_prime_close_handles(int drm_fd, uint32_t handles[4]) {
  if (drm_fd < 0 || !handles)
    return;

  for (size_t i = 0; i < 4; i++) {
    if (handles[i] == 0)
      continue;

    bool duplicate = false;
    for (size_t j = 0; j < i; j++) {
      if (handles[j] == handles[i]) {
        duplicate = true;
        break;
      }
    }

    if (!duplicate)
      drmCloseBufferHandle(drm_fd, handles[i]);
  }
}

bool drm_prime_import_dmabuf(int drm_fd, struct vt_dmabuf_attr_t *attr,
                             uint32_t handles[4]) {
  if (drm_fd < 0 || !attr || !handles || attr->num_planes <= 0 ||
      attr->num_planes > 4) {
    return false;
  }

  memset(handles, 0, sizeof(uint32_t) * 4);

  for (int32_t i = 0; i < attr->num_planes; i++) {
    if (attr->fds[i] < 0 ||
        drmPrimeFDToHandle(drm_fd, attr->fds[i], &handles[i]) != 0) {
      drm_prime_close_handles(drm_fd, handles);
      memset(handles, 0, sizeof(uint32_t) * 4);
      return false;
    }
  }

  return true;
}

bool drm_prime_test_import(int drm_fd, struct vt_dmabuf_attr_t *attr) {
  uint32_t handles[4] = {0};
  if (!drm_prime_import_dmabuf(drm_fd, attr, handles))
    return false;

  drm_prime_close_handles(drm_fd, handles);
  return true;
}

bool drm_prime_import_gbm_bo(int drm_fd, struct gbm_bo *bo,
                             uint32_t handles[4]) {
  if (drm_fd < 0 || !bo || !handles)
    return false;

  int plane_count = gbm_bo_get_plane_count(bo);
  if (plane_count <= 0 || plane_count > 4)
    return false;

  memset(handles, 0, sizeof(uint32_t) * 4);

  for (int i = 0; i < plane_count; i++) {
    int prime_fd = gbm_bo_get_fd_for_plane(bo, i);
    if (prime_fd < 0 && i == 0)
      prime_fd = gbm_bo_get_fd(bo);
    if (prime_fd < 0) {
      drm_prime_close_handles(drm_fd, handles);
      memset(handles, 0, sizeof(uint32_t) * 4);
      return false;
    }

    int ret = drmPrimeFDToHandle(drm_fd, prime_fd, &handles[i]);
    close(prime_fd);
    if (ret != 0) {
      drm_prime_close_handles(drm_fd, handles);
      memset(handles, 0, sizeof(uint32_t) * 4);
      return false;
    }
  }

  return true;
}
