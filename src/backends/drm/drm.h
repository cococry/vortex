#pragma once

#include "../../backend.h"

bool backend_init_drm(vt_backend_t* backend);
  
bool backend_implement_drm(vt_compositor_t* comp);

bool backend_handle_event_drm(vt_backend_t* backend);

bool backend_suspend_drm(vt_backend_t* backend);

bool backend_resume_drm(vt_backend_t* backend);
 
bool backend_handle_frame_drm(vt_backend_t* backend, vt_output_t* output);
  
bool backend_handle_repaint_drm(vt_backend_t* backend, vt_output_t* output);

bool backend_initialize_active_outputs_drm(vt_backend_t* backend);

bool backend_terminate_drm(vt_backend_t* backend);

bool backend_create_output_drm(vt_backend_t* backend, vt_output_t* output, void* data);
  
bool backend_destroy_output_drm(vt_backend_t* backend, vt_output_t* output);
  
bool backend_prepare_output_frame_drm(vt_backend_t* backend, vt_output_t* output);
  
bool backend___handle_input_drm(vt_backend_t* backend, bool mods, uint32_t key);
