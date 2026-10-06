#pragma once

#include "drm_types.h"
#include <stdint.h>
#include <xf86drmMode.h>

struct drm_liftoff_test_layer_t {
  struct liftoff_layer *liftoff_layer;

  struct drm_framebuffer_t fb;
  bool                     has_fb;

  struct vt_buffer_use_t *use;
};

drmModeAtomicReq *drm_atomic_create_test_req(struct drm_backend_state_t *drm,
                                             struct drm_output_state_t  *output,
                                             uint32_t *mode_blob,
                                             uint32_t *flags);

bool drm_liftoff_set_layer_props(struct liftoff_layer *liftoff_layer,
                                 const struct vt_output_layer_state_t *layer,
                                 uint32_t fb_id, int in_fence_fd,
                                 uint64_t zpos);

bool drm_liftoff_set_layer_props_composited(
    struct liftoff_layer                 *liftoff_layer,
    const struct vt_output_layer_state_t *layer, uint64_t zpos);

void drm_liftoff_testing_finish(struct drm_backend_state_t      *drm,
                                struct drm_liftoff_test_layer_t *test_layers,
                                size_t                           layer_count);
