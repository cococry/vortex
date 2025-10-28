#pragma once

#include "src/input/input.h"

bool input_backend_init_wl(struct vt_input_backend_t* backend, void* native_handle);

bool input_backend_terminate_wl(struct vt_input_backend_t* backend); 

bool input_backend_suspend_wl(struct vt_input_backend_t* backend); 

bool input_backend_resume_wl(struct vt_input_backend_t* backend); 
