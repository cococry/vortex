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

#include "drm_types.h"
#include "fb.h"
#include "libliftoff.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <xf86drmMode.h>

#include "atomic.h"

#define _SUBSYS_NAME "DRM"

static bool _atomic_add_prop(struct drm_backend_state_t *drm,
                             drmModeAtomicReq *req, uint32_t obj, uint32_t prop,
                             uint64_t value) {
  if (!drm || !req || prop == 0)
    return false;

  if (drmModeAtomicAddProperty(req, obj, prop, value) < 0) {
    VT_ERROR(drm->comp->log,
             "Failed to add atomic property %" PRIu32 " on object %" PRIu32 ".",
             prop, obj);
    return false;
  }

  return true;
}

static bool _atomic_disable(struct drm_backend_state_t *drm,
                            struct drm_output_state_t  *output) {
  if (!drm || !output || !output->crtc)
    return false;

  drmModeAtomicReq *req = drmModeAtomicAlloc();
  if (!req)
    return false;

  struct drm_crtc_t *crtc = output->crtc;
  bool               ok = true;

  struct drm_plane_t *plane;
  wl_array_for_each(plane, &drm->planes) {
    drmModePlane *drm_plane = drmModeGetPlane(drm->drm_fd, plane->id);
    if (!drm_plane)
      continue;

    bool attached = drm_plane->crtc_id == crtc->id;
    drmModeFreePlane(drm_plane);
    if (!attached)
      continue;

    if (!_atomic_add_prop(drm, req, plane->id, plane->props[VT_DRM_PLANE_FB_ID],
                          0) ||
        !_atomic_add_prop(drm, req, plane->id,
                          plane->props[VT_DRM_PLANE_CRTC_ID], 0)) {
      ok = false;
      break;
    }
  }

  if (ok) {
    ok = _atomic_add_prop(drm, req, output->conn_id,
                          output->conn_props[VT_DRM_CONNECTOR_CRTC_ID], 0) &&
         _atomic_add_prop(drm, req, crtc->id, crtc->props[VT_DRM_CRTC_ACTIVE],
                          0) &&
         _atomic_add_prop(drm, req, crtc->id, crtc->props[VT_DRM_CRTC_MODE_ID],
                          0);
  }

  if (ok && drmModeAtomicCommit(drm->drm_fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET,
                                NULL) != 0) {
    VT_WARN(drm->comp->log, "Failed to disable atomic DRM output: %s",
            strerror(errno));
    ok = false;
  }

  drmModeAtomicFree(req);
  return ok;
}

static uint32_t _atomic_commit_flags(const struct drm_kms_commit_t *commit,
                                     bool                           async) {
  uint32_t flags = 0;

  if (commit->test_only) {
    flags |= DRM_MODE_ATOMIC_TEST_ONLY;
  } else if (!commit->modeset) {
    flags |= DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;
  }

  if (commit->modeset)
    flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;

  if (async && !commit->test_only && !commit->modeset)
    flags |= DRM_MODE_PAGE_FLIP_ASYNC;

  return flags;
}

static bool _liftoff_set_drm_layer_props(struct drm_backend_state_t *drm,
                                         struct liftoff_layer *liftoff_layer,
                                         const struct drm_layer_state_t *layer,
                                         bool composited) {
  if (!drm || !liftoff_layer || !layer)
    return false;

  if (!composited && (!layer->has_fb || layer->fb.id == 0))
    return false;

  if (layer->src.x < 0 || layer->src.y < 0 || layer->src.width == 0 ||
      layer->src.height == 0 || layer->dst.width == 0 ||
      layer->dst.height == 0) {
    return false;
  }

  if (!composited &&
      liftoff_layer_set_property(liftoff_layer, "FB_ID", layer->fb.id) < 0) {
    return false;
  }

  if (liftoff_layer_set_property(liftoff_layer, "SRC_X",
                                 (uint64_t)(uint32_t)layer->src.x << 16) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "SRC_Y",
                                 (uint64_t)(uint32_t)layer->src.y << 16) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "SRC_W",
                                 (uint64_t)layer->src.width << 16) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "SRC_H",
                                 (uint64_t)layer->src.height << 16) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "CRTC_X",
                                 (uint64_t)(int64_t)layer->dst.x) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "CRTC_Y",
                                 (uint64_t)(int64_t)layer->dst.y) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "CRTC_W", layer->dst.width) <
          0 ||
      liftoff_layer_set_property(liftoff_layer, "CRTC_H", layer->dst.height) <
          0 ||
      liftoff_layer_set_property(liftoff_layer, "zpos", layer->zpos) < 0) {
    return false;
  }

  if (composited) {
    liftoff_layer_set_fb_composited(liftoff_layer);
  } else if (layer->acquire_fence_fd >= 0) {
    if (liftoff_layer_set_property(liftoff_layer, "IN_FENCE_FD",
                                   (uint64_t)(int64_t)layer->acquire_fence_fd) <
        0) {
      return false;
    }
  } else {
    /* Don't unnecessarily constrain plane selection to planes which
     * advertise IN_FENCE_FD when there is no fence.*/
    liftoff_layer_unset_property(liftoff_layer, "IN_FENCE_FD");
  }

  return true;
}

static bool _atomic_prepare_liftoff(struct drm_backend_state_t *drm,
                                    struct drm_kms_commit_t    *commit) {
  if (!drm || !commit || !commit->output)
    return false;

  struct drm_output_state_t *output = commit->output;

  if (!output->liftoff_output || !output->liftoff_composition_layer) {
    return false;
  }

  /*
   * The composition layer belongs to the output and persists between
   * frames. Frames begin with the FB disabled so a composition FB
   * from a previous frame can never accidentally survive into this one.
   */
  if (liftoff_layer_set_property(output->liftoff_composition_layer, "FB_ID",
                                 0) < 0) {
    return false;
  }

  liftoff_layer_unset_property(output->liftoff_composition_layer,
                               "IN_FENCE_FD");

  bool have_composition = false;

  struct drm_layer_state_t *layer;
  wl_array_for_each(layer, &commit->layers) {
    struct liftoff_layer *liftoff_layer = NULL;

    switch (layer->role) {
    case VT_DRM_LAYER_DIRECT:
    case VT_DRM_LAYER_COMPOSITED:

      liftoff_layer = layer->liftoff_layer;

      if (!liftoff_layer) {
        VT_ERROR(drm->comp->log, "Layer %p has no retained Liftoff layer.",
                 layer);
        return false;
      }

      break;
    case VT_DRM_LAYER_COMPOSITED_SCENE:
      if (have_composition) {
        VT_ERROR(drm->comp->log, "multiple composition layers in one commit.");
        return false;
      }

      have_composition = true;

      liftoff_layer = output->liftoff_composition_layer;

      break;

    default:
      VT_ERROR(drm->comp->log, "invalid logical layer role.");
      return false;
    }

    if (!_liftoff_set_drm_layer_props(drm, liftoff_layer, layer,
                                      layer->role == VT_DRM_LAYER_COMPOSITED)) {
      VT_ERROR(drm->comp->log,
               "failed to configure Liftoff layer "
               "role=%d fb=%" PRIu32 ".",
               layer->role, layer->fb.id);
      return false;
    }
  }

  return true;
}

static bool
_atomic_validate_liftoff_allocation(struct drm_backend_state_t *drm,
                                    struct drm_kms_commit_t    *commit) {
  struct drm_output_state_t *output = commit->output;

  struct drm_layer_state_t *layer;
  wl_array_for_each(layer, &commit->layers) {
    struct liftoff_layer *liftoff_layer = NULL;

    switch (layer->role) {
    case VT_DRM_LAYER_DIRECT:
      liftoff_layer = layer->liftoff_layer;

      if (!liftoff_layer)
        return false;

      if (liftoff_layer_needs_composition(liftoff_layer)) {
        VT_WARN(drm->comp->log,
                "final Liftoff allocation changed DIRECT "
                "layer surface=%p into a composited layer.",
                (void *)layer->surface);
        return false;
      }

      break;

    case VT_DRM_LAYER_COMPOSITED_SCENE:
      liftoff_layer = output->liftoff_composition_layer;
      break;

    case VT_DRM_LAYER_COMPOSITED:
      liftoff_layer = layer->liftoff_layer;

      if (!liftoff_layer)
        return false;

      if (!liftoff_layer_needs_composition(liftoff_layer)) {
        VT_WARN(drm->comp->log,
                "final Liftoff allocation changed COMPOSITED "
                "layer surface=%p into a direct layer.",
                (void *)layer->surface);
        return false;
      }

      continue;
    default:
      return false;
    }

    struct liftoff_plane *plane = liftoff_layer_get_plane(liftoff_layer);

    if (!plane) {
      VT_WARN(drm->comp->log,
              "final Liftoff allocation left role=%d "
              "fb=%" PRIu32 " without a hardware plane.",
              layer->role, layer->fb.id);
      return false;
    }

    VT_TRACE(drm->comp->log,
             "Liftoff final allocation "
             "role=%d fb=%" PRIu32 " -> plane=%" PRIu32 ".",
             layer->role, layer->fb.id, liftoff_plane_get_id(plane));
  }

  return true;
}

static const char *_atomic_layer_role_name(enum drm_layer_role_t role) {
  switch (role) {
  case VT_DRM_LAYER_DIRECT:
    return "DIRECT";
  case VT_DRM_LAYER_COMPOSITED:
    return "COMPOSITED";
  case VT_DRM_LAYER_COMPOSITED_SCENE:
    return "SCENE";
  default:
    return "UNKNOWN";
  }
}

static const char *_atomic_plane_type_name(uint64_t type) {
  switch (type) {
  case DRM_PLANE_TYPE_PRIMARY:
    return "PRIMARY";
  case DRM_PLANE_TYPE_OVERLAY:
    return "OVERLAY";
  case DRM_PLANE_TYPE_CURSOR:
    return "CURSOR";
  default:
    return "UNKNOWN";
  }
}

static struct drm_plane_t *_atomic_find_plane(struct drm_backend_state_t *drm,
                                              uint32_t plane_id) {
  struct drm_plane_t *plane;

  wl_array_for_each(plane, &drm->planes) {
    if (plane->id == plane_id)
      return plane;
  }

  return NULL;
}

static void _atomic_trace_liftoff_allocation(struct drm_backend_state_t *drm,
                                             struct drm_kms_commit_t *commit) {
  if (!drm || !drm->comp || !commit || !commit->output)
    return;

  struct drm_output_state_t *output = commit->output;

  VT_TRACE(drm->comp->log,
           "Liftoff allocation output=%p layers=%zu:", (void *)output->base,
           commit->layers.size / sizeof(struct drm_layer_state_t));

  size_t index = 0;

  struct drm_layer_state_t *layer;
  wl_array_for_each(layer, &commit->layers) {
    const char *role = _atomic_layer_role_name(layer->role);

    if (layer->role == VT_DRM_LAYER_COMPOSITED) {
      VT_TRACE(drm->comp->log,
               "  [%zu] %-10s z=%" PRIu64 " surface=%p -> SCENE", index, role,
               layer->zpos, (void *)layer->surface);

      index++;
      continue;
    }

    struct liftoff_layer *liftoff_layer = NULL;

    if (layer->role == VT_DRM_LAYER_DIRECT) {
      liftoff_layer = layer->liftoff_layer;
    } else if (layer->role == VT_DRM_LAYER_COMPOSITED_SCENE) {
      liftoff_layer = output->liftoff_composition_layer;
    }

    struct liftoff_plane *liftoff_plane =
        liftoff_layer ? liftoff_layer_get_plane(liftoff_layer) : NULL;

    if (!liftoff_plane) {
      VT_TRACE(drm->comp->log,
               "  [%zu] %-10s z=%" PRIu64 " fb=%" PRIu32
               " surface=%p -> NO PLANE",
               index, role, layer->zpos, layer->fb.id, (void *)layer->surface);

      index++;
      continue;
    }

    uint32_t plane_id = liftoff_plane_get_id(liftoff_plane);

    struct drm_plane_t *plane = _atomic_find_plane(drm, plane_id);

    const char *plane_type =
        plane ? _atomic_plane_type_name(plane->type) : "UNKNOWN";

    VT_TRACE(drm->comp->log,
             "  [%zu] %-10s z=%" PRIu64 " fb=%" PRIu32
             " surface=%p -> plane=%" PRIu32 " %s",
             index, role, layer->zpos, layer->fb.id, (void *)layer->surface,
             plane_id, plane_type);

    index++;
  }
}

static bool
_atomic_add_cursor(struct drm_backend_state_t *drm,
                   drmModeAtomicReq           *req,
                   struct drm_output_state_t  *output) {
  if (!drm || !req || !output || !output->crtc)
    return false;

  struct drm_plane_t *plane = output->crtc->plane_cursor;
  if (!plane)
    return true;

  struct drm_cursor_state_t *cursor = &output->cursor;

  if (!cursor->visible || !cursor->has_fb || cursor->fb.id == 0) {
    return
        _atomic_add_prop(drm, req, plane->id,
                         plane->props[VT_DRM_PLANE_FB_ID], 0) &&
        _atomic_add_prop(drm, req, plane->id,
                         plane->props[VT_DRM_PLANE_CRTC_ID], 0);
  }

  return
      _atomic_add_prop(drm, req, plane->id,
                       plane->props[VT_DRM_PLANE_FB_ID],
                       cursor->fb.id) &&

      _atomic_add_prop(drm, req, plane->id,
                       plane->props[VT_DRM_PLANE_CRTC_ID],
                       output->crtc->id) &&

      _atomic_add_prop(drm, req, plane->id,
                       plane->props[VT_DRM_PLANE_CRTC_X],
                       (uint64_t)(int64_t)cursor->x) &&

      _atomic_add_prop(drm, req, plane->id,
                       plane->props[VT_DRM_PLANE_CRTC_Y],
                       (uint64_t)(int64_t)cursor->y) &&

      _atomic_add_prop(drm, req, plane->id,
                       plane->props[VT_DRM_PLANE_CRTC_W],
                       cursor->width) &&

      _atomic_add_prop(drm, req, plane->id,
                       plane->props[VT_DRM_PLANE_CRTC_H],
                       cursor->height) &&

      _atomic_add_prop(drm, req, plane->id,
                       plane->props[VT_DRM_PLANE_SRC_X], 0) &&

      _atomic_add_prop(drm, req, plane->id,
                       plane->props[VT_DRM_PLANE_SRC_Y], 0) &&

      _atomic_add_prop(drm, req, plane->id,
                       plane->props[VT_DRM_PLANE_SRC_W],
                       (uint64_t)cursor->width << 16) &&

      _atomic_add_prop(drm, req, plane->id,
                       plane->props[VT_DRM_PLANE_SRC_H],
                       (uint64_t)cursor->height << 16);
}

static drmModeAtomicReq *_atomic_build_req(struct drm_backend_state_t *drm,
                                           struct drm_kms_commit_t    *commit,
                                           uint32_t flags, uint32_t *mode_blob,
                                           int *out_fence_fd) {
  if (!drm || !commit || !commit->output || !commit->output->crtc ||
      !mode_blob || !out_fence_fd) {
    return NULL;
  }

  struct drm_output_state_t *output = commit->output;
  struct drm_crtc_t         *crtc = output->crtc;

  *mode_blob = 0;
  *out_fence_fd = -1;

  drmModeAtomicReq *req = drmModeAtomicAlloc();
  if (!req)
    return NULL;

  /* Connector/CRTC properties still are set by vortex,
   * Liftoff only owns hardware plane assignment. */
  if (commit->modeset) {
    if (drmModeCreatePropertyBlob(drm->drm_fd, &output->mode,
                                  sizeof(output->mode), mode_blob) != 0) {
      VT_ERROR(drm->comp->log, "Failed to create DRM mode property blob: %s",
               strerror(errno));
      goto fail;
    }

    if (!_atomic_add_prop(drm, req, output->conn_id,
                          output->conn_props[VT_DRM_CONNECTOR_CRTC_ID],
                          crtc->id) ||
        !_atomic_add_prop(drm, req, crtc->id, crtc->props[VT_DRM_CRTC_MODE_ID],
                          *mode_blob) ||
        !_atomic_add_prop(drm, req, crtc->id, crtc->props[VT_DRM_CRTC_ACTIVE],
                          commit->active ? 1 : 0)) {
      goto fail;
    }
  }

  if (!_atomic_prepare_liftoff(drm, commit))
    goto fail;

  int ret = liftoff_output_apply(output->liftoff_output, req, flags, NULL);

  if (ret < 0) {
    VT_TRACE(drm->comp->log,
             "final liftoff_output_apply failed "
             "for output=%p: %s",
             (void *)output->base, strerror(-ret));
    goto fail;
  }

  if (!_atomic_add_cursor(drm, req, output))
    goto fail;

  _atomic_trace_liftoff_allocation(drm, commit);

  if (!_atomic_validate_liftoff_allocation(drm, commit)) {
    goto fail;
  }

  if (!commit->test_only && crtc->props[VT_DRM_CRTC_OUT_FENCE_PTR] != 0) {
    if (!_atomic_add_prop(drm, req, crtc->id,
                          crtc->props[VT_DRM_CRTC_OUT_FENCE_PTR],
                          (uint64_t)(uintptr_t)out_fence_fd)) {
      goto fail;
    }
  }

  return req;

fail:
  if (*mode_blob != 0) {
    drmModeDestroyPropertyBlob(drm->drm_fd, *mode_blob);
    *mode_blob = 0;
  }

  drmModeAtomicFree(req);
  return NULL;
}

static void
_atomic_destroy_frame_liftoff_layers(struct drm_kms_commit_t *commit) {
  struct drm_layer_state_t *layer;

  wl_array_for_each(layer, &commit->layers) {
    if (!layer->liftoff_layer)
      continue;

    liftoff_layer_destroy(layer->liftoff_layer);
    layer->liftoff_layer = NULL;
  }
}

static bool _atomic_commit(struct drm_backend_state_t *drm,
                           struct drm_kms_commit_t    *commit) {
  if (!drm || !commit || !commit->output || !commit->output->crtc ||
      !commit->output->liftoff_output || commit->layers.size == 0) {
    return false;
  }

  struct drm_output_state_t *output = commit->output;

  bool want_async = commit->async && !commit->test_only && !commit->modeset;

  const unsigned int max_attempts = want_async ? 2u : 1u;

  for (unsigned int attempt = 0; attempt < max_attempts; attempt++) {
    bool use_async = want_async && attempt == 0;

    uint32_t flags = _atomic_commit_flags(commit, use_async);

    uint32_t mode_blob = 0;
    int      out_fence_fd = -1;

    drmModeAtomicReq *req =
        _atomic_build_req(drm, commit, flags, &mode_blob, &out_fence_fd);

    if (!req) {
      if (out_fence_fd >= 0)
        close(out_fence_fd);

      if (use_async) {
        VT_TRACE(drm->comp->log, "async Liftoff allocation failed; "
                                 "retrying without async page flip.");
        continue;
      }

      return false;
    }

    int ret = drmModeAtomicCommit(drm->drm_fd, req, flags,
                                  commit->test_only ? NULL : output->base);

    int commit_errno = errno;

    drmModeAtomicFree(req);

    if (mode_blob != 0)
      drmModeDestroyPropertyBlob(drm->drm_fd, mode_blob);

    if (ret == 0) {
      if (!commit->test_only) {
        /* Liftoff output-layer state must be synced by removing any liftoff
         * layers created in this frame */
        _atomic_destroy_frame_liftoff_layers(commit);
      }
      commit->event_pending = !commit->test_only && !commit->modeset;

      commit->out_fence_fd = out_fence_fd;
      out_fence_fd = -1;

      VT_TRACE(drm->comp->log,
               "atomic Liftoff commit succeeded "
               "output=%p async=%d flags=0x%" PRIx32 ".",
               (void *)output->base, use_async, flags);

      return true;
    }

    if (out_fence_fd >= 0)
      close(out_fence_fd);

    if (use_async) {
      VT_TRACE(drm->comp->log,
               "async atomic commit failed for output=%p: %s; "
               "retrying with normal page flip.",
               (void *)output->base, strerror(commit_errno));
      continue;
    }

    VT_ERROR(drm->comp->log,
             "Atomic DRM/Liftoff commit failed for output=%p: %s",
             (void *)output->base, strerror(commit_errno));

    return false;
  }

  return false;
}

drmModeAtomicReq *drm_atomic_create_test_req(struct drm_backend_state_t *drm,
                                             struct drm_output_state_t  *output,
                                             uint32_t *mode_blob,
                                             uint32_t *flags) {
  assert(drm && output && output->crtc);
  assert(mode_blob && flags);

  struct drm_crtc_t *crtc = output->crtc;

  *mode_blob = 0;
  *flags = DRM_MODE_ATOMIC_TEST_ONLY;

  drmModeAtomicReq *req = drmModeAtomicAlloc();
  if (!req) {
    VT_ERROR(drm->comp->log, "Failed to allocate atomic DRM test request.");
    return NULL;
  }

  if (output->needs_modeset) {
    if (drmModeCreatePropertyBlob(drm->drm_fd, &output->mode,
                                  sizeof(output->mode), mode_blob) != 0) {
      VT_ERROR(drm->comp->log,
               "Failed to create DRM mode property blob for test: %s",
               strerror(errno));
      goto fail;
    }

    if (!_atomic_add_prop(drm, req, output->conn_id,
                          output->conn_props[VT_DRM_CONNECTOR_CRTC_ID],
                          crtc->id) ||
        !_atomic_add_prop(drm, req, crtc->id, crtc->props[VT_DRM_CRTC_MODE_ID],
                          *mode_blob) ||
        !_atomic_add_prop(drm, req, crtc->id, crtc->props[VT_DRM_CRTC_ACTIVE],
                          1)) {
      goto fail;
    }

    *flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
  }

  return req;

fail:
  if (*mode_blob != 0) {
    drmModeDestroyPropertyBlob(drm->drm_fd, *mode_blob);
    *mode_blob = 0;
  }

  drmModeAtomicFree(req);
  return NULL;
}

bool drm_liftoff_set_layer_props(struct liftoff_layer *liftoff_layer,
                                 const struct vt_output_layer_state_t *layer,
                                 uint32_t fb_id, int in_fence_fd,
                                 uint64_t zpos) {
  assert(liftoff_layer && layer);

  if (fb_id == 0)
    return false;

  if (layer->src.x < 0 || layer->src.y < 0 || layer->src.width == 0 ||
      layer->src.height == 0 || layer->dst.width == 0 ||
      layer->dst.height == 0) {
    return false;
  }

  if (liftoff_layer_set_property(liftoff_layer, "FB_ID", fb_id) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "SRC_X",
                                 (uint64_t)(uint32_t)layer->src.x << 16) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "SRC_Y",
                                 (uint64_t)(uint32_t)layer->src.y << 16) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "SRC_W",
                                 (uint64_t)layer->src.width << 16) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "SRC_H",
                                 (uint64_t)layer->src.height << 16) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "CRTC_X",
                                 (uint64_t)(int64_t)layer->dst.x) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "CRTC_Y",
                                 (uint64_t)(int64_t)layer->dst.y) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "CRTC_W", layer->dst.width) <
          0 ||
      liftoff_layer_set_property(liftoff_layer, "CRTC_H", layer->dst.height) <
          0 ||
      liftoff_layer_set_property(liftoff_layer, "zpos", zpos) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "IN_FENCE_FD",
                                 (uint64_t)(int64_t)in_fence_fd) < 0) {
    return false;
  }

  return true;
}

bool drm_liftoff_set_layer_props_composited(
    struct liftoff_layer                 *liftoff_layer,
    const struct vt_output_layer_state_t *layer, uint64_t zpos) {
  if (!liftoff_layer || !layer || layer->dst.width == 0 ||
      layer->dst.height == 0)
    return false;

  if (liftoff_layer_set_property(liftoff_layer, "CRTC_X",
                                 (uint64_t)(int64_t)layer->dst.x) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "CRTC_Y",
                                 (uint64_t)(int64_t)layer->dst.y) < 0 ||
      liftoff_layer_set_property(liftoff_layer, "CRTC_W", layer->dst.width) <
          0 ||
      liftoff_layer_set_property(liftoff_layer, "CRTC_H", layer->dst.height) <
          0 ||
      liftoff_layer_set_property(liftoff_layer, "zpos", zpos) < 0) {
    return false;
  }

  liftoff_layer_set_fb_composited(liftoff_layer);
  return true;
}

void drm_liftoff_testing_finish(struct drm_backend_state_t      *drm,
                                struct drm_liftoff_test_layer_t *test_layers,
                                size_t                           layer_count) {
  if (!drm || !test_layers)
    return;

  for (size_t i = 0; i < layer_count; i++) {
    struct drm_liftoff_test_layer_t *test = &test_layers[i];
    if (test->liftoff_layer) {
      liftoff_layer_destroy(test->liftoff_layer);
      test->liftoff_layer = NULL;
    }

    if (test->has_fb)
      drm_fb_finish(drm, &test->fb);

    if (test->use)
      vt_buffer_use_unref(&test->use);
  }

  /* no free(test_layers); (arena allocated)*/
}

const struct drm_kms_impl_t drm_kms_atomic_impl = {
    .name = "atomic",
    .atomic = true,
    .commit = _atomic_commit,
    .disable = _atomic_disable,
};
