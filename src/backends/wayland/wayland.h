#pragma once

#include "../../core/core_types.h"

bool backend_init_wl(struct vt_backend_t* backend);

bool backend_implement_wl(struct vt_compositor_t* comp);
  
bool backend_handle_frame_wl(struct vt_backend_t* backend, struct vt_output_t* output);
  
bool backend_terminate_wl(struct vt_backend_t* backend);

bool backend_prepare_output_frame_wl(struct vt_backend_t* backend, struct vt_output_t* output);
  
