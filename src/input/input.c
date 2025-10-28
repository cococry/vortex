#include "input.h"
#include "src/input/backends/libinput/libinput.h"
#include "src/input/backends/wayland/wayland_input.h"


void 
vt_input_implement(struct vt_input_backend_t* backend, enum vt_input_backend_platform_t platform) {
  if(!backend) return;

  if(platform == VT_INPUT_LIBINPUT) {
    backend->impl = (struct vt_input_backend_interface_t){
      .init = input_backend_init_li,
      .terminate = input_backend_terminate_li,
      .resume = input_backend_resume_li,
      .suspend = input_backend_suspend_li
    };
  } 
  else if(platform == VT_INPUT_WAYLAND) {
    backend->impl = (struct vt_input_backend_interface_t){
      .init = input_backend_init_wl,
      .terminate = input_backend_terminate_wl,
      .resume = input_backend_resume_wl,
      .suspend = input_backend_suspend_wl
    };
  }
  else {
    VT_ERROR(backend->comp->log, "INPUT: Invalid input backend.");
  }
}
