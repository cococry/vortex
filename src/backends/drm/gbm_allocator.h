#pragma once

#include "core/core_types.h"
#include <gbm.h>

struct vt_buffer_t *drm_gbm_buffer_allocator_alloc(struct vt_compositor_t *comp,
                                                   struct gbm_device      *gbm,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   uint32_t format);

bool drm_gbm_buffer_allocator_clear(struct vt_buffer_t *buffer, uint32_t color);

bool drm_gbm_buffer_allocator_resize(struct vt_compositor_t *comp,
                                      struct gbm_device *gbm,
                                      struct vt_buffer_t **buffer,
                                      uint32_t width, uint32_t height);
