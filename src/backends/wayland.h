#pragma once

#include "../backend.h"

bool backend_init_wl(vt_backend_t* backend);
  
bool backend_handle_event_wl(vt_backend_t* backend);

bool backend_suspend_wl(vt_backend_t* backend);

bool backend_resume_wl(vt_backend_t* backend);
 
bool backend_handle_frame_wl(vt_backend_t* backend, vt_output_t* output);
  
bool backend_handle_surface_frame_wl(vt_backend_t* backend, vt_surface_t* surf);

bool backend_initialize_active_outputs_wl(vt_backend_t* backend);

bool backend_terminate_wl(vt_backend_t* backend);

bool backend_create_output_wl(vt_backend_t* backend, vt_output_t* output, void* data);
  
bool backend_destroy_output_wl(vt_backend_t* backend, vt_output_t* output);
  
bool backend_prepare_output_frame_wl(vt_backend_t* backend, vt_output_t* output);
  
bool backend___handle_input_wl(vt_backend_t* backend, bool mods, uint32_t key);
