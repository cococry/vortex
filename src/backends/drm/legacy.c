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

#include "core/compositor.h"
#include <errno.h>
#include <string.h>
#include <xf86drmMode.h>

#define _SUBSYS_NAME "DRM"

static bool _legacy_commit(struct drm_backend_state_t *drm,
                           struct drm_kms_commit_t    *commit) {
  if (!drm || !commit || !commit->output || !commit->output->crtc)
    return false;

  if (commit->test_only)
    return false;

  /*
   * The legacy pipeline expects exactly one logical layer: the
   * Vulkan/GL produced framebuffer covering the output.
   */
  if (commit->layers.size != sizeof(struct drm_layer_state_t)) {
    VT_ERROR(drm->comp->log,
             "Legacy DRM commit requires exactly one composition layer, "
             "got %zu.",
             commit->layers.size / sizeof(struct drm_layer_state_t));
    return false;
  }

  struct drm_layer_state_t *layer = commit->layers.data;

  if (!layer || layer->role != VT_DRM_LAYER_COMPOSITED_SCENE ||
      !layer->has_fb || layer->fb.id == 0) {
    VT_ERROR(drm->comp->log,
             "Legacy DRM commit requires one valid composition framebuffer.");
    return false;
  }

  assert(!layer->liftoff_layer);

  struct drm_output_state_t *output = commit->output;
  uint32_t                   fb_id = layer->fb.id;

  if (commit->modeset) {
    if (drmModeSetCrtc(drm->drm_fd, output->crtc->id, fb_id, 0, 0,
                       &output->conn_id, 1, &output->mode) != 0) {
      VT_ERROR(drm->comp->log,
               "drmModeSetCrtc() failed for connector %" PRIu32 ": %s",
               output->conn_id, strerror(errno));
      return false;
    }

    commit->event_pending = false;
    commit->out_fence_fd = -1;

    return true;
  }

  uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT;

  if (commit->async)
    flags |= DRM_MODE_PAGE_FLIP_ASYNC;

  if (drmModePageFlip(drm->drm_fd, output->crtc->id, fb_id, flags,
                      output->base) != 0) {
    int flip_errno = errno;

    if (!commit->async ||
        drmModePageFlip(drm->drm_fd, output->crtc->id, fb_id,
                        DRM_MODE_PAGE_FLIP_EVENT, output->base) != 0) {
      VT_ERROR(drm->comp->log,
               "drmModePageFlip() failed for connector %" PRIu32 ": %s",
               output->conn_id, strerror(commit->async ? errno : flip_errno));
      return false;
    }

    VT_TRACE(drm->comp->log,
             "Legacy async page flip rejected for connector %" PRIu32
             "; fell back to normal page flip.",
             output->conn_id);
  }

  commit->event_pending = true;

  commit->out_fence_fd = -1;

  return true;
}

static bool _legacy_disable(struct drm_backend_state_t *drm,
                            struct drm_output_state_t  *output) {
  if (!drm || !output || !output->crtc)
    return false;

  if (drmModeSetCrtc(drm->drm_fd, output->crtc->id, 0, 0, 0, NULL, 0, NULL) !=
      0) {
    VT_WARN(drm->comp->log, "Failed to disable CRTC %u: %s", output->crtc->id,
            strerror(errno));
    return false;
  }

  return true;
}

const struct drm_kms_impl_t drm_kms_legacy_impl = {
    .name = "legacy",
    .atomic = false,
    .commit = _legacy_commit,
    .disable = _legacy_disable,
};
