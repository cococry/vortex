#define _GNU_SOURCE

#include <drm.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <gbm.h>
#include <signal.h>
#include <unistd.h>
#include <linux/kd.h>  
#include <sys/ioctl.h>
#include <drm/drm_fourcc.h>
#include <linux/input-event-codes.h>

#include <stdlib.h>
#include <errno.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include "./drm.h"
#include "../../log.h"
#include "../../backend.h"

typedef struct {
  int drm_fd;
  drmEventContext evctx;
  drmModeRes* res;

  struct gbm_device* gbm_dev;

  int32_t tty_fd;

  long kb_mode_before;
} drm_backend_state_t;

static volatile sig_atomic_t vt_acquire_pending, vt_release_pending;

typedef struct {
  struct gbm_bo *current_bo;
  struct gbm_bo *pending_bo;
  struct gbm_bo *prev_bo;
  uint32_t current_fb;
  uint32_t pending_fb;
  uint32_t prev_fb;

  bool needs_modeset;
  bool flip_inflight;
  bool modeset_bootstrapped;

  struct gbm_surface* gbm_surf;

  drmModeModeInfo mode;
  uint32_t conn_id;
  uint32_t crtc_id;
} drm_output_state_t;

typedef enum {
  _DRM_TTY_TEXT = 0,
  _DRM_TTY_GRAPHICS,
} drm_tty_mode_t;

static void _drm_page_flip_handler(int fd, unsigned int frame,
                                   unsigned int sec, unsigned int usec, void *data);

static void _drm_release_all_scanout(vt_output_t* output);

static void  _drm_handle_vt_release(int sig);

static void _drm_handle_vt_acquire(int sig);

static bool _drm_setup_tty(vt_backend_t* backend);

static bool _drm_suspend_tty(vt_backend_t* backend);

static bool _drm_resume_tty(vt_backend_t* backend);

static int  _drm_dispatch(int fd, uint32_t mask, void *data);

static bool _drm_set_tty_mode(vt_compositor_t* c, int tty_fd, drm_tty_mode_t mode);

void 
_drm_page_flip_handler(int fd, unsigned int frame,
                       unsigned int sec, unsigned int usec, void *data) {
  vt_output_t* output = (vt_output_t*)data; 
  drm_output_state_t* drm_output = BACKEND_DATA(output, drm_output_state_t);
  vt_compositor_t* comp = output->backend->comp;

  log_trace(comp->log, "DRM: _drm_page_flip_handler(): Handling page flip event.")

  // Release the old, unused backbuffer
  if (drm_output->prev_bo) {
    drmModeRmFB(fd, drm_output->prev_fb);
    gbm_surface_release_buffer(drm_output->gbm_surf, drm_output->prev_bo);
  }

  // Swap buffers, the previous buffer becomes the currently displayed buffer 
  // and the currently displayed buffer becomes the new, pending frame buffer 
  // (that we got from eglSwapBuffers and is rendered to with OpenGL)
  drm_output->prev_bo = drm_output->current_bo;
  drm_output->prev_fb = drm_output->current_fb;

  drm_output->current_bo = drm_output->pending_bo;
  drm_output->current_fb = drm_output->pending_fb;

  // Reset pending buffer (so we can set it in the next frame)
  drm_output->pending_bo = NULL;
  drm_output->pending_fb = 0;

  drm_output->flip_inflight = false;
  uint32_t t = sec * 1000u + usec / 1000u;

  // Send the frame callbacks to all clients, establishing correct frame pacing
  comp_send_frame_callbacks_for_output(comp, output, t);

  // If a client requested a repaint during the waiting time between 
  // drmModePageFlip() in render_frame and the execution of this handler, 
  // schedule a repaint 
  if(output->needs_repaint)
    comp_schedule_repaint(comp, output);
}

void 
_drm_release_all_scanout(vt_output_t* output) {
  drm_backend_state_t* drm = BACKEND_DATA(output->backend, drm_backend_state_t);
  drm_output_state_t* drm_output = BACKEND_DATA(output, drm_output_state_t) ;

  // Basically release all used DRM scanout buffers  
  if (drm_output->pending_bo) {
    drmModeRmFB(drm->drm_fd, drm_output->pending_fb);
    gbm_surface_release_buffer(drm_output->gbm_surf, drm_output->pending_bo);
    drm_output->pending_bo = NULL; drm_output->pending_fb = 0;
  }
  if (drm_output->current_bo) {
    drmModeRmFB(drm->drm_fd, drm_output->current_fb);
    gbm_surface_release_buffer(drm_output->gbm_surf, drm_output->current_bo);
    drm_output->current_bo = NULL; drm_output->current_fb = 0;
  }
  if (drm_output->prev_bo) {
    drmModeRmFB(drm->drm_fd, drm_output->prev_fb);
    gbm_surface_release_buffer(drm_output->gbm_surf, drm_output->prev_bo);
    drm_output->prev_bo = NULL; drm_output->prev_fb = 0;
  }
}


void 
_drm_handle_vt_release(int sig) {
  vt_release_pending = true;
}
void 
_drm_handle_vt_acquire(int sig) {
  vt_acquire_pending = true;
}

bool _drm_setup_tty(vt_backend_t* backend) {
  drm_backend_state_t* drm = BACKEND_DATA(backend, drm_backend_state_t); 
  vt_compositor_t* comp = backend->comp;

  signal(SIGUSR1, _drm_handle_vt_release);
  signal(SIGUSR2, _drm_handle_vt_acquire);

  drm->tty_fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
  if(drm->tty_fd < 0) {
    log_error(comp->log, "DRM: failed to open /dev/tty");
    return false;
  }

  _drm_set_tty_mode(backend->comp, drm->tty_fd, _DRM_TTY_GRAPHICS);

  struct vt_mode tty_mode = {
    .mode = VT_PROCESS,
    .waitv = 1,
    .relsig = SIGUSR1, 
    .acqsig = SIGUSR2 
  };
  if(ioctl(drm->tty_fd, VT_SETMODE, &tty_mode) < 0) {
    log_error(comp->log, "DRM: failed to set TTY switching mode.");
    return false;
  }

  ioctl(drm->tty_fd, KDGKBMODE, &drm->kb_mode_before);

  ioctl(drm->tty_fd, KDSKBMODE, K_OFF);


  return true;
}

bool 
_drm_suspend_tty(vt_backend_t* backend) {
  if(!backend || !backend->user_data) return false;
  if (backend->comp->suspended) return true;
  backend->comp->suspended = true;

  drm_backend_state_t* drm = BACKEND_DATA(backend, drm_backend_state_t); 

  log_trace(backend->comp->log, "DRM: suspending TTY session...");

  // Drain any pending flips
  bool any_inflight;
  do {
    any_inflight = false;
    vt_output_t* output;
    wl_list_for_each(output, &backend->outputs, link) {
      if(!output->user_data) continue;
      if (BACKEND_DATA(output, drm_output_state_t)->flip_inflight) {
        any_inflight = true;
        break;
      }
    }
    if (any_inflight)
      drmHandleEvent(drm->drm_fd, &drm->evctx);
  } while (any_inflight);

  // Drop graphics context 
  if(!backend->renderer->impl.drop_context(backend->renderer)) return false; 

  // Destroy all the graphical outputs
  vt_output_t* output, *tmp;
  wl_list_for_each_safe(output, tmp, &backend->outputs, link) {
    if(!output->user_data) continue;
    _drm_release_all_scanout(output);
    drm_output_state_t* drm_output = BACKEND_DATA(output, drm_output_state_t);
    gbm_surface_destroy(drm_output->gbm_surf);
    drm_output->gbm_surf = NULL;
    if(!backend->renderer->impl.destroy_renderable_output(backend->renderer, output)) return false;
  }


  // Set tty to text mode
  _drm_set_tty_mode(backend->comp, drm->tty_fd, _DRM_TTY_TEXT);

  if(drmDropMaster(drm->drm_fd) != 0) {
    log_warn(backend->comp->log, "DMR: drmDropMaster() failed: %s\n", strerror(errno));
  }
  ioctl(drm->tty_fd, VT_RELDISP, 1);

  return true;
}

bool 
_drm_resume_tty(vt_backend_t* backend) {
  if(!backend || !backend->user_data) return false;
  if (!backend->comp->suspended) return true;

  drm_backend_state_t* drm = BACKEND_DATA(backend, drm_backend_state_t); 

  log_trace(backend->comp->log, "DRM: resuming TTY session...");

  // Try everything to become the master (deep)
  bool is_master = false;
  for (int i = 0; i < 50; i++) {
    if (drmSetMaster(drm->drm_fd) == 0) {
      is_master = true;
      break;
    }
    usleep(20000); 
  }

  if(!is_master) {
    if (drmSetMaster(drm->drm_fd) != 0) {
      ioctl(drm->tty_fd, VT_RELDISP, VT_ACKACQ);
      log_fatal(backend->comp->log, "DRM: we cannot become the master, drmSetMaster still failed: %s", strerror(errno));
      return false;
    }
  }

  log_trace(backend->comp->log, "DRM: Vortex is now the DRM master of %s.", ttyname(drm->tty_fd));
  backend->comp->suspended = false;

  backend->renderer->impl.init(backend, backend->renderer, backend->native_handle);
  vt_output_t* output, *tmp;
  const uint32_t desired_format = backend->renderer->_desired_render_buffer_format;
  wl_list_for_each_safe(output, tmp, &backend->outputs, link) {
    if(!output->user_data) continue;

    drm_output_state_t* drm_output = BACKEND_DATA(output, drm_output_state_t);
    drm_output->gbm_surf = gbm_surface_create(
      drm->gbm_dev,
      drm_output->mode.hdisplay, drm_output->mode.vdisplay,
      !desired_format ? backend->_desired_render_buffer_format : desired_format,
      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
    );
    output->native_window = drm_output->gbm_surf;

    if(!backend->renderer->impl.setup_renderable_output(backend->renderer, output)) return false;
    drm_output->needs_modeset = true;
    drm_output->modeset_bootstrapped = false;
    drm_output->flip_inflight = false;
    output->repaint_pending = false;

    log_trace(backend->comp->log, "DRM: Recreated GBM/rendering surfaces of output (%ux%u@%.2f, ID: %i).",
              output->width, output->height, output->refresh_rate, drm_output->conn_id);
  }

  // Set tty to graphics mode
  _drm_set_tty_mode(backend->comp, drm->tty_fd, _DRM_TTY_GRAPHICS);

  // Ack acquire
  if(ioctl(drm->tty_fd, VT_RELDISP, VT_ACKACQ) < 0) {
    log_error(backend->comp->log, "DRM: VT_RELDISP VT_ACKACQ ioctl failed: %s", strerror(errno));
    return false;
  }

  // Schedule repaint on all outputs
  wl_list_for_each(output, &backend->outputs, link) {
    comp_schedule_repaint(backend->comp, output);
  }

  return true;
}

int  
_drm_dispatch(int fd, uint32_t mask, void *data) {
  if(!data) return 0;
  drm_backend_state_t* drm = (drm_backend_state_t*)data;
  drmHandleEvent(fd, &drm->evctx);
  return 0;
}

bool
_drm_set_tty_mode(vt_compositor_t* c, int tty_fd, drm_tty_mode_t mode) {
  int32_t tty_mode = -1; 
  const char* mode_str;
  switch (mode)  {
    case _DRM_TTY_GRAPHICS: {
      mode_str = "graphics";
      tty_mode = KD_GRAPHICS;
      break;
    }
    case _DRM_TTY_TEXT: {
      mode_str = "text";
      tty_mode = KD_TEXT;
      break;
    }
    default: {
      log_error(c->log, "DRM: Trying to set invalid TTY graphics mode.");
      return false;
    }
  }
  if(ioctl(tty_fd, KDSETMODE, tty_mode) < 0) {
    log_error(c->log, "DRM: Failed to enter TTY %s mode: %m", mode_str);
    return false;
  }

  log_trace(c->log, "DRM: %s is now in %s mode.", ttyname(tty_fd), mode_str);

  return true;
}

bool
backend_init_drm(vt_backend_t* backend) {
  if(!backend) return false;
  if(!(backend->user_data = COMP_ALLOC(backend->comp, sizeof(drm_backend_state_t))))  {
    return false;
  }
  backend->_desired_render_buffer_format = GBM_FORMAT_XRGB8888;
  wl_list_init(&backend->outputs);

  drm_backend_state_t* drm = BACKEND_DATA(backend, drm_backend_state_t); 
  vt_compositor_t* comp = backend->comp;

  log_trace(comp->log, "DRM: Initializing DRM/KMS backend...");

  if(!_drm_setup_tty(backend)) {
    backend_terminate_drm(backend);
    log_fatal(backend->comp->log, "DRM: failed to setup TTY for rendering.");
    return false;
  }

  drmDevicePtr devices[16] = {0};
  int num = drmGetDevices2(0, devices, 16);

  if (num < 0) {
    log_fatal(backend->comp->log, "DRM: No valid DRM device found.");
    return false;
  }

  for (int i = 0; i < num; i++) {
    drmDevicePtr dev = devices[i];

    // Only consider primary nodes like /dev/dri/cardX
    if (!(dev->available_nodes & (1 << DRM_NODE_PRIMARY)))
      continue;

    const char *node = dev->nodes[DRM_NODE_PRIMARY];
    drm->drm_fd = open(node, O_RDWR | O_CLOEXEC);
    if (drm->drm_fd < 0) {
      log_error(backend->comp->log, "DRM: Failed to open device %s: %s\n", node, strerror(errno));
      continue;
    }
    break;
  }

  drmFreeDevices(devices, num);

  if (drmSetMaster(drm->drm_fd) != 0) {
    backend_terminate_drm(backend);
    log_fatal(comp->log, "DRM: drmSetMaster failed: %s", strerror(errno));
    return false;
  }

  log_trace(comp->log, "DRM: Vortex is now the DRM master of %s.", ttyname(drm->tty_fd));

  if(!(drm->gbm_dev = gbm_create_device(drm->drm_fd))) {
    log_error(comp->log, "DRM: cannot create GBM device (fd: %i)", drm->drm_fd);
    backend_terminate_drm(backend);
    return false;
  }

  log_trace(comp->log, "DRM: Successfully created GBM device on FD: %i", drm->drm_fd);  

  drm->evctx.version = DRM_EVENT_CONTEXT_VERSION;
  drm->evctx.page_flip_handler = _drm_page_flip_handler;
  drm->evctx.vblank_handler = NULL;

  log_trace(comp->log, "DRM: Successfully initialized DRM/KMS backend.");

  int fd = drm->drm_fd;
  wl_event_loop_add_fd(comp->wl.evloop, fd, WL_EVENT_READABLE, _drm_dispatch, drm);

  backend->native_handle = drm->gbm_dev;

  return true;
}

bool 
backend_implement_drm(vt_compositor_t* comp) {
  if(!comp || !comp->backend) return false;

  log_trace(comp->log, "DRM: Implementing backend...");

  comp->backend->platform = VT_BACKEND_DRM_GBM; 

  comp->backend->impl = (vt_backend_interface_t){
    .init = backend_init_drm,
    .handle_event = backend_handle_event_drm,
    .suspend = backend_suspend_drm,
    .resume = backend_resume_drm,
    .handle_frame = backend_handle_frame_drm,
    .initialize_active_outputs = backend_initialize_active_outputs_drm, 
    .terminate = backend_terminate_drm,
    .create_output = backend_create_output_drm, 
    .destroy_output = backend_destroy_output_drm, 
    .prepare_output_frame = backend_prepare_output_frame_drm,
    .__handle_input = backend___handle_input_drm
  };

  return true;
}

  bool 
backend_handle_event_drm(vt_backend_t* backend) {
  if (vt_release_pending) {
    vt_release_pending = 0;
    backend->impl.suspend(backend);
  }
  if (vt_acquire_pending) {
    vt_acquire_pending = 0;
    if (!backend->impl.resume(backend)) {
      backend->comp->running = false;
    }
  }
  return true;
}

bool 
backend_suspend_drm(vt_backend_t* backend) {
  if(!_drm_suspend_tty(backend)) {
    log_error(backend->comp->log, "DRM: Failed to suspend backend.");
    return false;
  }
  return true;
}

bool 
backend_resume_drm(vt_backend_t* backend) {
  if(!_drm_resume_tty(backend)) {
    log_error(backend->comp->log, "DRM: Failed to resume backend.");
    return false;
  }
  return true;
}

bool 
backend_handle_frame_drm(vt_backend_t* backend, vt_output_t* output) {
  if(!backend || !backend->user_data) return false;

  drm_backend_state_t* drm = BACKEND_DATA(backend, drm_backend_state_t); 
  vt_compositor_t* comp = backend->comp;

  log_trace(comp->log, "DRM: Handling frame...");

  if (!backend->renderer) {
    log_error(comp->log, "DRM: Renderer backend not initialized before handling frame.");
    return false;
  }

  drm_output_state_t* drm_output = BACKEND_DATA(output, drm_output_state_t); 

  // Retrieve the front buffer that we rendered to with the renderer (e.g EGL) 
  struct gbm_bo *bo = gbm_surface_lock_front_buffer(drm_output->gbm_surf);
  // If we could not get the front buffer, we'll try again next frame.
  if (!bo) {
    log_warn(comp->log, "DRM: Failed to get the GBM front buffer for frame.");
    output->needs_repaint = true; 
    return true;
  }

  uint32_t w = gbm_bo_get_width(bo);
  uint32_t h = gbm_bo_get_height(bo);
  uint32_t fmt = gbm_bo_get_format(bo);
  uint32_t handle = gbm_bo_get_handle(bo).u32;
  uint32_t stride = gbm_bo_get_stride(bo);
  uint32_t fb;
  uint32_t handles[4] = { gbm_bo_get_handle(bo).u32 };
  uint32_t strides[4] = { gbm_bo_get_stride(bo) };
  uint32_t offsets[4] = { 0 };

  uint64_t modifier = gbm_bo_get_modifier(bo);

  int ret;
  // If the BO wants modifiers, add the FB with modifier arguments
  if (modifier != DRM_FORMAT_MOD_INVALID) {
    uint64_t mods[4] = { modifier, 0, 0, 0 };
    ret = drmModeAddFB2WithModifiers(drm->drm_fd, w, h, fmt,
                                     handles, strides, offsets, mods, &fb,
                                     DRM_MODE_FB_MODIFIERS);
  } else {
    ret = drmModeAddFB2(drm->drm_fd, w, h, fmt,
                        handles, strides, offsets, &fb, 0);
  }

  // If we could not create a DRM frame buffer...
  if (ret != 0) {
    gbm_surface_release_buffer(drm_output->gbm_surf, bo);
    // Try again next farme
    output->needs_repaint = true;
    log_error(comp->log, "DRM: canot create DRM frame buffer: drmModeAddFB2(%ux%u, fmt=0x%08x) failed: %s",
              w, h, fmt, strerror(errno));
    return false;
  }

  if (drm_output->needs_modeset) {
    if (drmModeSetCrtc(drm->drm_fd, drm_output->crtc_id, fb,
                       0, 0, &drm_output->conn_id, 1, &drm_output->mode) != 0) {
      drmModeRmFB(drm->drm_fd, fb);
      gbm_surface_release_buffer(drm_output->gbm_surf, bo);
      // Try again next farme
      output->needs_repaint = true;
      log_error(comp->log, "DRM: cannot set CRTC mode: drmModeSetCrtc() failed: %s",
                strerror(errno));
      return false;
    }
    log_trace(comp->log, "DRM: Successfully performed DRM CRTC mode set for output (%ux%u@%.2f, ID: %i)",
              output->width, output->height, output->refresh_rate, drm_output->conn_id);

    // Release the old buffer 
    if (drm_output->current_bo) {
      drmModeRmFB(drm->drm_fd, drm_output->current_fb);
      gbm_surface_release_buffer(drm_output->gbm_surf, drm_output->current_bo);
    }

    // Assign the newly rendered buffer
    drm_output->current_bo = bo;
    drm_output->current_fb = fb;
    drm_output->needs_modeset = false;

    // If we did not yet bootstrap (we just applied the first CRTC), we need to 
    // manually send the frame callbacks to the clients to kick off
    // the client rendering loop, because this path does not invoke page_flip_handler(),
    // which would normally send the frame callbacks.
    if(!drm_output->modeset_bootstrapped) {
      uint32_t t = comp_get_time_msec();
      comp_send_frame_callbacks(comp, output, t);
      wl_display_flush_clients(comp->wl.dsp);
      output->needs_repaint = false;
      drm_output->modeset_bootstrapped = true;
      log_trace(comp->log, "DRM: Successfully bootstrapped the first frame.");
    }
    return true;
  }

  // Set pending buffer and eventually assign to 
  // current in _drm_page_flip_handler() (after flip completes)
  drm_output->pending_bo = bo;
  drm_output->pending_fb = fb;

  if( 
    drmModePageFlip(drm->drm_fd, drm_output->crtc_id, fb,
                    DRM_MODE_PAGE_FLIP_EVENT, output) != 0) {
    // Flip refused; free pending and try again later
    drmModeRmFB(drm->drm_fd, drm_output->pending_fb);
    gbm_surface_release_buffer(drm_output->gbm_surf, drm_output->pending_bo);
    drm_output->pending_bo = NULL; drm_output->pending_fb = 0;
    output->needs_repaint = true;
    log_error(comp->log, "DRM: cannot do a page flip: drmModePageFlip() failed: %s",
              strerror(errno));
    return false;
  }
  log_trace(comp->log, "DRM: Successfully performed drmModePageFlip() call.");

  drm_output->flip_inflight = true;

  // we’ve submitted a frame so clear the desire until something else changes
  output->needs_repaint = false;
}

bool 
backend_handle_surface_frame_drm(vt_backend_t* backend, vt_surface_t* surf) {
  // Ingored in DRM
  (void)backend;
  (void)surf;
  return true;
}


bool 
backend_initialize_active_outputs_drm(vt_backend_t* backend) {
  if(!backend || !backend->user_data) return false;

  drm_backend_state_t* drm = BACKEND_DATA(backend, drm_backend_state_t); 
  vt_compositor_t* comp = backend->comp;

  log_trace(comp->log, "DRM: Initializing active outputs.");

  if (!backend->renderer || !backend->renderer->impl.setup_renderable_output) {
    log_error(comp->log, "DRM: Renderer backend not initialized before output setup.");
    return false;
  }


  drm->res = drmModeGetResources(drm->drm_fd);
  if (!drm->res) {
    log_error(comp->log, "DRM: drmModeGetResources() failed: %s", strerror(errno));
    return false;
  }

  for (int i = 0; i < drm->res->count_connectors; i++) {
    drmModeConnector *conn = drmModeGetConnector(drm->drm_fd, drm->res->connectors[i]);
    if (!conn) continue;
    if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
      vt_output_t* output = COMP_ALLOC(comp, sizeof(vt_output_t));
      output->backend = backend;
      if (!output) {
        log_error(comp->log, "DRM: allocation failed for output.");
        drmModeFreeConnector(conn);
        continue;
      }
      if (!backend->impl.create_output(backend, output, conn)) {
        log_error(comp->log, "DRM: Failed to setup internal DRM output output.");
        free(output);
        continue;
      } 
      if(!backend->renderer->impl.setup_renderable_output(backend->renderer, output)) {
        log_error(comp->log, "DRM: Failed to setup renderable output for DRM output (%ix%i@%.2f)",
                  output->width, output->height, output->refresh_rate);
        backend->impl.destroy_output(backend, output);
        free(output);
        continue;
      }
    }
    drmModeFreeConnector(conn);
  }

  if (wl_list_empty(&backend->outputs)) {
    log_error(comp->log, "DRM: No connected connector found");
    drmModeFreeResources(drm->res);
    return false;
  }

  return true;
}

static void restore_tty(int tty_fd, int saved_kb_mode, struct termios *saved_tio) {
}
bool
backend_terminate_drm(vt_backend_t* backend) {
  if(!backend) return false;
  drm_backend_state_t* drm = BACKEND_DATA(backend, drm_backend_state_t); 
  vt_compositor_t* comp = backend->comp; 


  if (!comp->suspended && drm->drm_fd > 0) {
    bool any_inflight;
    do {
      any_inflight = false;
      vt_output_t* output;
      wl_list_for_each(output, &backend->outputs, link) {
        if (BACKEND_DATA(output, drm_output_state_t)->flip_inflight) {
          any_inflight = true;
          break;
        }
      }
      if (any_inflight)
        drmHandleEvent(drm->drm_fd, &drm->evctx);
    } while (any_inflight);
  }

  if(drm->res)
    drmModeFreeResources(drm->res);

  _drm_set_tty_mode(backend->comp, drm->tty_fd, _DRM_TTY_TEXT);
  ioctl(drm->tty_fd, KDSKBMODE, drm->kb_mode_before);

  vt_output_t *output, *tmp;
  wl_list_for_each_safe(output, tmp, &backend->outputs, link) {
    backend->impl.destroy_output(backend, output);
  }

  if (drm->gbm_dev) {
    gbm_device_destroy(drm->gbm_dev);
    drm->gbm_dev = NULL;
  }


  if (drm->drm_fd > 0) drmDropMaster(drm->drm_fd);

  if (drm->tty_fd > 0) {
    struct vt_mode mode = { .mode = VT_AUTO };
    ioctl(drm->tty_fd, VT_SETMODE, &mode);

    close(drm->tty_fd);
    drm->tty_fd = -1;
  }

  if (drm->drm_fd > 0) {
    close(drm->drm_fd);
    drm->drm_fd = -1;
  }

  if(backend->user_data) {
    free(backend->user_data);
    backend->user_data = NULL;
  }

  return true;
}


bool 
backend_create_output_drm(vt_backend_t* backend, vt_output_t* output, void* data) {
  if(!backend || !output || !data) return false;

  log_trace(backend->comp->log, "DMR: Creating DRM internal output.");
  if(!(output->user_data = COMP_ALLOC(backend->comp, sizeof(drm_output_state_t)))) {
    return false;
  }

  drmModeConnector* conn          = (drmModeConnector*)data;
  drmModeModeInfo mode;
  bool found = false;
  for (int j = 0; j < conn->count_modes; j++) {
    if (conn->modes[j].type & DRM_MODE_TYPE_PREFERRED) {
      mode = conn->modes[j];
      found = true;
      break;
    }
  }
  if (!found) {
    // Fallback to the first mode if none marked preferred
    mode = conn->modes[0];
  }
  drm_backend_state_t* drm        = BACKEND_DATA(backend, drm_backend_state_t);
  drm_output_state_t* drm_output  = BACKEND_DATA(output, drm_output_state_t);
  drmModeModeInfo preferred_mode  = mode; 
  vt_compositor_t* comp           = backend->comp; 

  output->needs_repaint = true;

  drm_output->mode = preferred_mode;
  drm_output->needs_modeset = true;
  drm_output->conn_id = conn->connector_id;

  drmModeEncoder *enc = NULL;

  // Try to find a usable encoder
  if (conn->encoder_id)
    enc = drmModeGetEncoder(drm->drm_fd, conn->encoder_id);

  if (enc) {
    drm_output->crtc_id = enc->crtc_id;
    drmModeFreeEncoder(enc);
  } else if (drm->res->count_encoders > 0) {
    // fallback: try all encoders for this connector
    // (the voices are getting too loud)
    for (int i = 0; i < conn->count_encoders; i++) {
      enc = drmModeGetEncoder(drm->drm_fd, conn->encoders[i]);
      if (!enc) continue;
      for (int j = 0; j < drm->res->count_crtcs; j++) {
        if (enc->possible_crtcs & (1 << j)) {
          drm_output->crtc_id = drm->res->crtcs[j];
          break;
        }
      }
      drmModeFreeEncoder(enc);
      if (drm_output->crtc_id) break;
    }
  }

  if (!drm_output->crtc_id) {
    log_error(comp->log, "DRM: Failed to find CRTC for connector %u", drm_output->conn_id);
    drmModeFreeConnector(conn);
    free(output->user_data);
    return false;
  }

  const uint32_t desired_format = GBM_FORMAT_XRGB8888;
  if(!(drm_output->gbm_surf = gbm_surface_create(
    drm->gbm_dev,
    drm_output->mode.hdisplay, drm_output->mode.vdisplay,
    desired_format,
    GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
  ))) {
    log_error(comp->log, "DRM: cannot create GBM surface (%ux%u@%u) for output on connector %i for rendering.", 
              drm_output->mode.hdisplay, drm_output->mode.vdisplay, drm_output->mode.vrefresh, drm_output->conn_id);
    free(output->user_data);
    return false;
  } 

  // Set the output's native window handle

  wl_list_insert(&backend->outputs, &output->link);

  log_trace(comp->log, "DRM: Acknowledged connector: %u, CRTC %u, mode %ux%u@%u",
            drm_output->conn_id, drm_output->crtc_id,
            drm_output->mode.hdisplay, drm_output->mode.vdisplay, drm_output->mode.vrefresh);

  // TODO: Implement correct output layouting e.g vertical monitors
  static uint32_t x_ptr = 0;
  output->width = (uint32_t)drm_output->mode.hdisplay; 
  output->height = (uint32_t)drm_output->mode.vdisplay; 
  output->x = x_ptr;
  output->y = 0; 
  output->height = (uint32_t)drm_output->mode.vdisplay; 
  output->refresh_rate = (uint32_t)drm_output->mode.vrefresh; 
  output->native_window = drm_output->gbm_surf;
  output->format = desired_format; 
  output->id = drm_output->conn_id;

  x_ptr += output->width;


  return true;
}

bool 
backend_destroy_output_drm(vt_backend_t* backend, vt_output_t* output) {
  if(!output->user_data) return false;
  _drm_release_all_scanout(output);
  drm_output_state_t* drm_output = BACKEND_DATA(output, drm_output_state_t);
  if (drm_output->gbm_surf) {
    gbm_surface_destroy(drm_output->gbm_surf);
    drm_output->gbm_surf = NULL;
  }

  if(!backend->renderer->impl.destroy_renderable_output(backend->renderer, output)) return false;

  free(output->user_data);
  output->user_data = NULL;
    
  wl_list_remove(&output->link);
  free(output);
  output = NULL;

  if(!wl_list_length(&backend->outputs)) {
    backend->comp->running = false;
  }
  return true;
}

bool 
backend_prepare_output_frame_drm(vt_backend_t* backend, vt_output_t* output) {
  drm_output_state_t* drm_output = BACKEND_DATA(output, drm_output_state_t);
  drm_backend_state_t* drm = BACKEND_DATA(backend, drm_backend_state_t); 
  if(drm_output->flip_inflight) return false;
  if(!output->needs_repaint && drm_output->modeset_bootstrapped) return false;

  return true;
}

bool 
backend___handle_input_drm(vt_backend_t* backend, bool mods, uint32_t key) {
  if(!backend || !backend->user_data) return false;
  drm_backend_state_t* drm = BACKEND_DATA(backend, drm_backend_state_t);
  if(!drm) return false;
  if (mods) {
    if (key == KEY_F1) ioctl(drm->tty_fd, VT_ACTIVATE, 1);
    if (key == KEY_F2) ioctl(drm->tty_fd, VT_ACTIVATE, 2);
    if (key == KEY_F3) ioctl(drm->tty_fd, VT_ACTIVATE, 3);
    if (key == KEY_F4) ioctl(drm->tty_fd, VT_ACTIVATE, 4);
    if (key == KEY_F5) ioctl(drm->tty_fd, VT_ACTIVATE, 5);
    if (key == KEY_F6) ioctl(drm->tty_fd, VT_ACTIVATE, 6);
    if (key == KEY_F7) ioctl(drm->tty_fd, VT_ACTIVATE, 7);
    if (key == KEY_F8) ioctl(drm->tty_fd, VT_ACTIVATE, 8);
    if (key == KEY_F9) ioctl(drm->tty_fd, VT_ACTIVATE, 9);
  }
  return true;
}
