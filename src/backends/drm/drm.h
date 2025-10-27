#pragma once

#include "../../core/core_types.h"

bool backend_init_drm(struct vt_backend_t* backend);
  
bool backend_implement_drm(struct vt_compositor_t* comp);

bool backend_handle_frame_drm(struct vt_backend_t* backend, struct vt_output_t* output);

bool backend_terminate_drm(struct vt_backend_t* backend);

bool backend_prepare_output_frame_drm(struct vt_backend_t* backend, struct vt_output_t* output);
