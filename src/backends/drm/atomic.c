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

#include "kms.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <xf86drmMode.h>

#define _SUBSYS_NAME "DRM"

static bool _atomic_add_prop(struct drm_backend_state_t *drm,
                             drmModeAtomicReq *req, uint32_t obj,
                             uint32_t prop, uint64_t value) {
  if (prop == 0 || drmModeAtomicAddProperty(req, obj, prop, value) < 0) {
    VT_ERROR(drm->comp->log,
             "Failed to add atomic property %" PRIu32 " on object %" PRIu32,
             prop, obj);
    return false;
  }

  return true;
}

static bool _atomic_plane_in_commit(struct drm_kms_commit_t *commit,
                                    uint32_t plane_id) {
  for (size_t i = 0; i < commit->plane_count; i++) {
    if (commit->planes[i].plane && commit->planes[i].plane->id == plane_id)
      return true;
  }

  return false;
}

static bool _atomic_add_plane(struct drm_backend_state_t *drm,
                              drmModeAtomicReq *req,
                              struct drm_output_state_t *output,
                              struct drm_kms_plane_state_t *state) {
  if (!state || !state->plane || !state->fb || state->fb->id == 0 ||
      !output || !output->crtc)
    return false;

  struct drm_plane_t *plane = state->plane;
  struct drm_crtc_t  *crtc = output->crtc;

  if (!_atomic_add_prop(drm, req, plane->id,
                        plane->props[VT_DRM_PLANE_FB_ID], state->fb->id) ||
      !_atomic_add_prop(drm, req, plane->id,
                        plane->props[VT_DRM_PLANE_CRTC_ID], crtc->id) ||
      !_atomic_add_prop(drm, req, plane->id,
                        plane->props[VT_DRM_PLANE_CRTC_X],
                        (uint64_t)(int64_t)state->dst.x) ||
      !_atomic_add_prop(drm, req, plane->id,
                        plane->props[VT_DRM_PLANE_CRTC_Y],
                        (uint64_t)(int64_t)state->dst.y) ||
      !_atomic_add_prop(drm, req, plane->id,
                        plane->props[VT_DRM_PLANE_CRTC_W], state->dst.width) ||
      !_atomic_add_prop(drm, req, plane->id,
                        plane->props[VT_DRM_PLANE_CRTC_H], state->dst.height) ||
      !_atomic_add_prop(drm, req, plane->id,
                        plane->props[VT_DRM_PLANE_SRC_X],
                        (uint64_t)state->src.x << 16) ||
      !_atomic_add_prop(drm, req, plane->id,
                        plane->props[VT_DRM_PLANE_SRC_Y],
                        (uint64_t)state->src.y << 16) ||
      !_atomic_add_prop(drm, req, plane->id,
                        plane->props[VT_DRM_PLANE_SRC_W],
                        (uint64_t)state->src.width << 16) ||
      !_atomic_add_prop(drm, req, plane->id,
                        plane->props[VT_DRM_PLANE_SRC_H],
                        (uint64_t)state->src.height << 16)) {
    return false;
  }

  if (state->acquire_fence_fd >= 0 &&
      plane->props[VT_DRM_PLANE_IN_FENCE_FD] != 0 &&
      !_atomic_add_prop(drm, req, plane->id,
                        plane->props[VT_DRM_PLANE_IN_FENCE_FD],
                        state->acquire_fence_fd)) {
    return false;
  }

  return true;
}

static bool _atomic_disable_old_planes(struct drm_backend_state_t *drm,
                                       drmModeAtomicReq *req,
                                       struct drm_kms_commit_t *commit) {
  if (!commit->output || !commit->output->crtc)
    return true;

  struct drm_crtc_t *crtc = commit->output->crtc;
  struct drm_plane_t *plane;
  wl_array_for_each(plane, &drm->planes) {
    if (_atomic_plane_in_commit(commit, plane->id))
      continue;

    drmModePlane *drm_plane = drmModeGetPlane(drm->drm_fd, plane->id);
    if (!drm_plane)
      continue;

    bool attached = drm_plane->crtc_id == crtc->id;
    drmModeFreePlane(drm_plane);

    if (!attached)
      continue;

    if (!_atomic_add_prop(drm, req, plane->id,
                          plane->props[VT_DRM_PLANE_FB_ID], 0) ||
        !_atomic_add_prop(drm, req, plane->id,
                          plane->props[VT_DRM_PLANE_CRTC_ID], 0)) {
      return false;
    }
  }

  return true;
}

static bool _atomic_commit(struct drm_backend_state_t *drm,
                           struct drm_kms_commit_t    *commit) {
  if (!drm || !commit || !commit->output || !commit->output->crtc ||
      commit->plane_count == 0)
    return false;

  struct drm_output_state_t *output = commit->output;
  struct drm_crtc_t         *crtc = output->crtc;

  drmModeAtomicReq *req = drmModeAtomicAlloc();
  if (!req)
    return false;

  uint32_t mode_blob = 0;
  int      out_fence_fd = -1;
  bool     ok = false;

  if (commit->modeset) {
    if (drmModeCreatePropertyBlob(drm->drm_fd, &output->mode,
                                  sizeof(output->mode), &mode_blob) != 0) {
      VT_ERROR(drm->comp->log, "Failed to create DRM mode property blob: %s",
               strerror(errno));
      goto done;
    }

    if (!_atomic_add_prop(drm, req, output->conn_id,
                          output->conn_props[VT_DRM_CONNECTOR_CRTC_ID],
                          crtc->id) ||
        !_atomic_add_prop(drm, req, crtc->id,
                          crtc->props[VT_DRM_CRTC_MODE_ID], mode_blob) ||
        !_atomic_add_prop(drm, req, crtc->id,
                          crtc->props[VT_DRM_CRTC_ACTIVE], 1)) {
      goto done;
    }
  }

  for (size_t i = 0; i < commit->plane_count; i++) {
    if (!_atomic_add_plane(drm, req, output, &commit->planes[i]))
      goto done;
  }

  if (!_atomic_disable_old_planes(drm, req, commit))
    goto done;

  if (!commit->test_only && crtc->props[VT_DRM_CRTC_OUT_FENCE_PTR] != 0) {
    if (!_atomic_add_prop(drm, req, crtc->id,
                          crtc->props[VT_DRM_CRTC_OUT_FENCE_PTR],
                          (uint64_t)(uintptr_t)&out_fence_fd)) {
      goto done;
    }
  }

  uint32_t flags = 0;
  if (commit->test_only) {
    flags |= DRM_MODE_ATOMIC_TEST_ONLY;
  } else if (!commit->modeset) {
    flags |= DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;
  }

  if (commit->modeset)
    flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;

  if (commit->async && !commit->modeset)
    flags |= DRM_MODE_PAGE_FLIP_ASYNC;

  if (drmModeAtomicCommit(drm->drm_fd, req, flags,
                          commit->test_only ? NULL : output->base) != 0) {
    if (!commit->test_only && commit->async && !commit->modeset) {
      flags &= ~DRM_MODE_PAGE_FLIP_ASYNC;
      if (drmModeAtomicCommit(drm->drm_fd, req, flags, output->base) == 0) {
        ok = true;
      }
    }

    if (!ok) {
      VT_ERROR(drm->comp->log, "Atomic DRM commit failed: %s",
               strerror(errno));
      goto done;
    }
  } else {
    ok = true;
  }

  commit->event_pending = !commit->test_only && !commit->modeset;
  commit->out_fence_fd = out_fence_fd;
  out_fence_fd = -1;

done:
  if (out_fence_fd >= 0)
    close(out_fence_fd);
  if (mode_blob != 0)
    drmModeDestroyPropertyBlob(drm->drm_fd, mode_blob);
  drmModeAtomicFree(req);
  return ok;
}

static bool _atomic_disable(struct drm_backend_state_t *drm,
                            struct drm_output_state_t  *output) {
  if (!drm || !output || !output->crtc)
    return false;

  drmModeAtomicReq *req = drmModeAtomicAlloc();
  if (!req)
    return false;

  struct drm_crtc_t *crtc = output->crtc;
  bool ok = true;

  struct drm_plane_t *plane;
  wl_array_for_each(plane, &drm->planes) {
    drmModePlane *drm_plane = drmModeGetPlane(drm->drm_fd, plane->id);
    if (!drm_plane)
      continue;

    bool attached = drm_plane->crtc_id == crtc->id;
    drmModeFreePlane(drm_plane);
    if (!attached)
      continue;

    if (!_atomic_add_prop(drm, req, plane->id,
                          plane->props[VT_DRM_PLANE_FB_ID], 0) ||
        !_atomic_add_prop(drm, req, plane->id,
                          plane->props[VT_DRM_PLANE_CRTC_ID], 0)) {
      ok = false;
      break;
    }
  }

  if (ok) {
    ok = _atomic_add_prop(drm, req, output->conn_id,
                          output->conn_props[VT_DRM_CONNECTOR_CRTC_ID], 0) &&
         _atomic_add_prop(drm, req, crtc->id,
                          crtc->props[VT_DRM_CRTC_ACTIVE], 0) &&
         _atomic_add_prop(drm, req, crtc->id,
                          crtc->props[VT_DRM_CRTC_MODE_ID], 0);
  }

  if (ok && drmModeAtomicCommit(drm->drm_fd, req,
                                DRM_MODE_ATOMIC_ALLOW_MODESET, NULL) != 0) {
    VT_WARN(drm->comp->log, "Failed to disable atomic DRM output: %s",
            strerror(errno));
    ok = false;
  }

  drmModeAtomicFree(req);
  return ok;
}

const struct drm_kms_impl_t drm_kms_atomic_impl = {
    .name = "atomic",
    .atomic = true,
    .commit = _atomic_commit,
    .disable = _atomic_disable,
};
