#define _GNU_SOURCE

#include <xf86drmMode.h>
#include <xf86drm.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <gbm.h>
#include <unistd.h>
#include <linux/kd.h>  
#include <pthread.h>  
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <drm/drm_fourcc.h>
#include <stdarg.h>

#include <errno.h>

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <pthread.h>

#include "../../render/renderer.h"
#include "../../core/compositor.h"

#include "./drm.h"
#include "./session_drm.h"

#include <linux/input-event-codes.h>

struct drm_backend_master_state_t {
  struct wl_list backends;
  uint32_t x_ptr;
  int32_t vt_fd;
  struct vt_compositor_t* comp;

  struct wl_listener session_terminate_listener,
  seat_disable_listener, seat_enable_listener;
};

struct drm_backend_state_t {
  int drm_fd;
  drmEventContext evctx;
  drmModeRes* res;
  struct gbm_device* gbm_dev;

  struct wl_list outputs;

  struct vt_compositor_t* comp;
  struct gbm_device* native_handle;

  struct wl_list link;

  struct vt_renderer_t* renderer;

  struct vt_backend_t* root_backend;

  char gpu_path[64];

  struct wl_event_source *event_source 
};

struct drm_output_state_t {
  struct gbm_bo *current_bo;
  struct gbm_bo *pending_bo;
  struct gbm_bo *prev_bo;
  struct gbm_bo *older_bo;
  uint32_t older_fb;   
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
};

static void   _drm_page_flip_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data);
static void   _drm_release_all_scanout(struct vt_output_t* output);
static void   _drm_handle_vt_release(int sig);
static void   _drm_handle_vt_acquire(int sig);
static bool   _drm_suspend(struct drm_backend_state_t* backend);
static bool   _drm_resume(struct drm_backend_state_t* backend);
static int    _drm_dispatch(int fd, uint32_t mask, void *data);
static bool   _drm_init_for_device(struct vt_compositor_t* comp, struct drm_backend_state_t* drm, int32_t device_fd, const char* gpu_path);
static bool   _drm_init_active_outputs_for_device(struct drm_backend_state_t* drm);
static bool   _drm_handle_frame_for_device(struct drm_backend_state_t* drm, struct vt_output_t* output);
static bool   _drm_create_output_for_device(struct drm_backend_state_t* drm, struct vt_output_t* output, void* data);
static bool   _drm_destroy_output_for_device(struct drm_backend_state_t* drm, struct vt_output_t* output);
static bool   _drm_terminate_for_device(struct drm_backend_state_t* drm);

static void   _drm_on_session_terminate(struct wl_listener* listener, void* data); 
static void   _drm_on_seat_enable(struct wl_listener* listener, void* data); 
static void   _drm_on_seat_disable(struct wl_listener* listener, void* data); 

void 
_drm_page_flip_handler(int fd, unsigned int frame,
                       unsigned int sec, unsigned int usec, void *data) {
  struct vt_output_t* output = (struct vt_output_t*)data; 
  struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t);
  struct vt_compositor_t* comp = output->backend->comp;

  VT_TRACE(comp->log, "DRM: _drm_page_flip_handler(): Handling page flip event.")

  // Release the old, unused backbuffer
  if (drm_output->older_bo) {
    drmModeRmFB(fd, drm_output->older_fb);
    gbm_surface_release_buffer(drm_output->gbm_surf, drm_output->older_bo);
    drm_output->older_bo = NULL;
  }

  drm_output->older_bo = drm_output->prev_bo;
  drm_output->older_fb = drm_output->prev_fb;

  // Swap buffers, the previous buffer becomes the currently displayed buffer 
  // and the currently displayed buffer becomes the new, pending frame buffer 
  // (that we got from swapping buffers and is rendered to with OpenGL/Vulkan etc)
  drm_output->prev_bo = drm_output->current_bo;
  drm_output->prev_fb = drm_output->current_fb;

  drm_output->current_bo = drm_output->pending_bo;
  drm_output->current_fb = drm_output->pending_fb;

  // Reset pending buffer (so we can set it in the next frame)
  drm_output->pending_bo = NULL;
  drm_output->pending_fb = 0;

  drm_output->flip_inflight = false;
  uint32_t t = vt_util_get_time_msec(); 

  // Send the frame callbacks to all clients, establishing correct frame pacing
  vt_comp_frame_done(comp, output, t);

  if(output->needs_repaint) {
    vt_comp_schedule_repaint(comp, output);
  }
}

void 
_drm_release_all_scanout(struct vt_output_t* output) {
  struct drm_backend_state_t* drm = BACKEND_DATA(output->backend, struct drm_backend_state_t);
  struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t) ;

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

bool 
_drm_suspend(struct drm_backend_state_t* backend) {
  if (!backend)
    return false;
  if (backend->comp->suspended)
    return true;

  backend->comp->suspended = true;
  VT_TRACE(backend->comp->log, "DRM: Suspending seat session (VT switch away)...");

  // we stop submitting new flips immediately
  struct vt_output_t* output;
  wl_list_for_each(output, &backend->outputs, link_local) {
    output->needs_repaint = false;
  }

  // Drain any in-flight page flips 
  bool any_inflight;
  do {
    any_inflight = false;
    wl_list_for_each(output, &backend->outputs, link_local) {
      if (!output->user_data)
        continue;
      struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t);
      if (drm_output->flip_inflight) {
        any_inflight = true;
        break;
      }
    }
    if (any_inflight)
      drmHandleEvent(backend->drm_fd, &backend->evctx);
  } while (any_inflight);

  // disable scanout on all outputs (causes ~1 frame of black screen but safe)
  wl_list_for_each(output, &backend->outputs, link_local) {
    if (!output->user_data)
      continue;

    struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t);

    VT_TRACE(backend->comp->log, 
             "DRM: Disabling CRTC %u for connector %u.", 
             drm_output->crtc_id, drm_output->conn_id);

    drmModeSetCrtc(backend->drm_fd, drm_output->crtc_id,
                   0, 0, 0, NULL, 0, NULL);
  }

  return true;
}


bool 
_drm_resume(struct drm_backend_state_t* backend) {
  if (!backend)
    return false;
  if (!backend->comp->suspended)
    return true;

  backend->comp->suspended = false;
  VT_TRACE(backend->comp->log, "DRM: Resuming seat session (VT switch back)...");


  struct vt_output_t* output;
  wl_list_for_each(output, &backend->outputs, link_local) {
    if (!output->user_data)
      continue;

    struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t);
    if (!drm_output->current_bo || drm_output->current_fb == 0)
      continue;

    VT_TRACE(backend->comp->log,
             "DRM: Re-enabling CRTC %u with existing framebuffer %u for connector %u.",
             drm_output->crtc_id, drm_output->current_fb, drm_output->conn_id);

    // libseat has already called drmSetMaster() for us.
    // we can safely re-enable CRTCs and resume rendering.
    if (drmModeSetCrtc(backend->drm_fd, drm_output->crtc_id,
                       drm_output->current_fb, 0, 0,
                       &drm_output->conn_id, 1, &drm_output->mode) != 0) {
      VT_ERROR(backend->comp->log,
               "DRM: Failed to restore CRTC %u: %s",
               drm_output->crtc_id, strerror(errno));
      drm_output->needs_modeset = true;
    } else {
      drm_output->needs_modeset = false;
    }

    // mark the output for repaint on the next frame
    output->needs_repaint = true;
    drm_output->flip_inflight = false;
  }

  if (!backend->event_source) {
    backend->event_source = wl_event_loop_add_fd(
      backend->comp->wl.evloop,
      backend->drm_fd,
      WL_EVENT_READABLE,
      _drm_dispatch,
      backend);
  }

  wl_list_for_each(output, &backend->outputs, link_local) {
    pixman_region32_clear(&output->damage);
    pixman_region32_union_rect(&output->damage, &output->damage,
                               0, 0, output->width, output->height);
    vt_comp_schedule_repaint(backend->comp, output);
  }

  uint32_t t = vt_util_get_time_msec();
  vt_comp_frame_done_all(backend->comp, t);
  wl_display_flush_clients(backend->comp->wl.dsp);

  return true;
}

int  
_drm_dispatch(int fd, uint32_t mask, void *data) {
  if(!data) return 0;
  struct drm_backend_state_t* drm = (struct drm_backend_state_t*)data;
  drmHandleEvent(fd, &drm->evctx);
  return 0;
}

static struct vt_device_drm_t* _drm_find_device_by_gpu_path(struct vt_session_drm_t* session, const char* path) {
  struct vt_device_drm_t* dev;
  wl_list_for_each(dev, &session->devices, link) {
    if(strcmp(dev->path, path) == 0) return dev;
  }
  return NULL;
}

bool _drm_init_for_device(struct vt_compositor_t* comp, struct drm_backend_state_t* drm, int32_t device_fd, const char* gpu_path) {
  if(!drm) return false;
  wl_list_init(&drm->outputs);

  drm->drm_fd = device_fd;
  snprintf(drm->gpu_path, sizeof(drm->gpu_path), "%s", gpu_path);
  drm->comp = comp;

  VT_TRACE(comp->log, "DRM: Initializing DRM/KMS backend...");


  if(!(drm->gbm_dev = gbm_create_device(drm->drm_fd))) {
    VT_ERROR(comp->log, "DRM: cannot create GBM device (fd: %i)", drm->drm_fd);
    _drm_terminate_for_device(drm);
    return false;
  }

  VT_TRACE(comp->log, "DRM: Successfully created GBM device on FD: %i", drm->drm_fd);  

  drm->evctx.version = DRM_EVENT_CONTEXT_VERSION;
  drm->evctx.page_flip_handler = _drm_page_flip_handler;
  drm->evctx.vblank_handler = NULL;

  drm->event_source = wl_event_loop_add_fd(comp->wl.evloop, device_fd, WL_EVENT_READABLE, _drm_dispatch, drm);

  drm->native_handle = drm->gbm_dev;

  drm->renderer = VT_ALLOC(comp, sizeof(struct vt_renderer_t));
  drm->renderer->comp = comp;
  vt_renderer_implement(drm->renderer, VT_RENDERING_BACKEND_EGL_OPENGL);

  drm->renderer->impl.init(comp->backend, drm->renderer, drm->native_handle);
  _drm_init_active_outputs_for_device(drm);

  VT_TRACE(comp->log, "DRM: Successfully initialized DRM/KMS backend for GPU %i.", device_fd);

  return true;
}

bool 
_drm_init_active_outputs_for_device(struct drm_backend_state_t* drm) {
  if(!drm) return false;
  struct vt_compositor_t* comp = drm->comp;

  VT_TRACE(comp->log, "DRM: Initializing active outputs.");

  if (!drm->renderer || !drm->renderer->impl.setup_renderable_output) {
    VT_ERROR(comp->log, "DRM: Renderer backend not initialized before output setup.");
    return false;
  }


  drm->res = drmModeGetResources(drm->drm_fd);
  if (!drm->res) {
    VT_ERROR(comp->log, "DRM: drmModeGetResources() failed: %s", strerror(errno));
    return false;
  }

  for (int i = 0; i < drm->res->count_connectors; i++) {
    drmModeConnector *conn = drmModeGetConnector(drm->drm_fd, drm->res->connectors[i]);
    if (!conn) continue;
    if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
      struct vt_output_t* output = VT_ALLOC(comp, sizeof(struct vt_output_t));
      if (!output) {
        VT_ERROR(comp->log, "DRM: allocation failed for output.");
        drmModeFreeConnector(conn);
        continue;
      }
      pixman_region32_init(&output->damage);
      output->backend = comp->backend;
      if (!_drm_create_output_for_device(drm, output, conn)) {
        VT_ERROR(comp->log, "DRM: Failed to setup internal DRM output output.");
        continue;
      } 
      if(!drm->renderer->impl.setup_renderable_output(drm->renderer, output)) {
        VT_ERROR(comp->log, "DRM: Failed to setup renderable output for DRM output (%ix%i@%.2f)",
                 output->width, output->height, output->refresh_rate);
        _drm_destroy_output_for_device(drm, output);
        continue;
      }
    }
    drmModeFreeConnector(conn);
  }

  if (wl_list_empty(&drm->outputs)) {
    VT_ERROR(comp->log, "DRM: No connected connector found");
    drmModeFreeResources(drm->res);
    return false;
  }

  return true;
}

bool 
_drm_create_output_for_device(struct drm_backend_state_t* drm, struct vt_output_t* output, void* data) {
  if(!drm || !output || !data) return false;

  VT_TRACE(drm->comp->log, "DMR: Creating DRM internal output.");
  if(!(output->user_data = VT_ALLOC(drm->comp, sizeof(struct drm_output_state_t)))) {
    return false;
  }

  drmModeConnector* conn = (drmModeConnector*)data;
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
  struct drm_output_state_t* drm_output  = BACKEND_DATA(output, struct drm_output_state_t);
  drmModeModeInfo preferred_mode  = mode; 
  struct vt_compositor_t* comp    = drm->comp; 

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
    VT_ERROR(comp->log, "DRM: Failed to find CRTC for connector %u", drm_output->conn_id);
    drmModeFreeConnector(conn);
    return false;
  }

  const uint32_t desired_format = drm->renderer->_desired_render_buffer_format;
  if(!(drm_output->gbm_surf = gbm_surface_create(
    drm->gbm_dev,
    drm_output->mode.hdisplay, drm_output->mode.vdisplay,
    desired_format,
    GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
  ))) {
    VT_ERROR(comp->log, "DRM: cannot create GBM surface (%ux%u@%u) for output on connector %i for rendering.", 
             drm_output->mode.hdisplay, drm_output->mode.vdisplay, drm_output->mode.vrefresh, drm_output->conn_id);
    return false;
  } 


  VT_TRACE(comp->log, "DRM: Acknowledged connector: %u, CRTC %u, mode %ux%u@%u (%p)",
           drm_output->conn_id, drm_output->crtc_id,
           drm_output->mode.hdisplay, drm_output->mode.vdisplay, drm_output->mode.vrefresh, output);

  struct drm_backend_master_state_t* drm_master = BACKEND_DATA(output->backend, struct drm_backend_master_state_t); 
  // TODO: Implement correct output layouting e.g vertical monitors
  output->width = (uint32_t)drm_output->mode.hdisplay; 
  output->height = (uint32_t)drm_output->mode.vdisplay; 
  output->x = drm_master->x_ptr;
  output->y = 0; 
  output->height = (uint32_t)drm_output->mode.vdisplay; 
  output->refresh_rate = (uint32_t)drm_output->mode.vrefresh; 
  output->native_window = drm_output->gbm_surf;
  output->format = desired_format; 
  output->id = drm_output->conn_id;
  output->renderer = drm->renderer;

  drm_master->x_ptr += output->width;

  wl_list_insert(&drm->outputs, &output->link_local);
  wl_list_insert(&drm->comp->outputs, &output->link_global);


  return true;
}

bool   
_drm_destroy_output_for_device(struct drm_backend_state_t* drm, struct vt_output_t* output) {
  if(!drm) return false;
  if(!output || !output->user_data) return false;

  VT_TRACE(drm->comp->log, "DRM: Destroying output %p.\n", output);

  _drm_release_all_scanout(output);
  struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t);
  if (drm_output->gbm_surf) {
    gbm_surface_destroy(drm_output->gbm_surf);
    drm_output->gbm_surf = NULL;
  }

  if(!drm->renderer->impl.destroy_renderable_output(drm->renderer, output)) return false;


  output->user_data = NULL;

  wl_list_remove(&output->link_local);

  output = NULL;

  return true;
}

static bool 
_drm_terminate_for_device(struct drm_backend_state_t* drm) {
  if(!drm) return false;
  struct vt_compositor_t* comp = drm->comp; 

  if (!comp->suspended && drm->drm_fd > 0) {
    bool any_inflight;
    do {
      any_inflight = false;
      struct vt_output_t* output;
      wl_list_for_each(output, &drm->outputs, link_local) {
        if (BACKEND_DATA(output, struct drm_output_state_t)->flip_inflight) {
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


  struct vt_output_t *output, *tmp;
  wl_list_for_each_safe(output, tmp, &drm->outputs, link_local) {
    _drm_destroy_output_for_device(drm, output);
  }

  drm->renderer->impl.destroy(drm->renderer);

  if (drm->gbm_dev) {
    gbm_device_destroy(drm->gbm_dev);
    drm->gbm_dev = NULL;
  }


  if (drm->drm_fd > 0) drmDropMaster(drm->drm_fd);

  if (drm->drm_fd > 0) {
    close(drm->drm_fd);
    drm->drm_fd = -1;
  }


  return true;
}

void 
_drm_on_session_terminate(struct wl_listener* listener, void* data) {
  if(!data) return;
  struct vt_session_t* session = (struct vt_session_t*)data;
  struct vt_session_drm_t* session_drm = BACKEND_DATA(session, struct vt_session_drm_t); 

  struct vt_device_drm_t* dev, *tmp_dev;
  wl_list_for_each_safe(dev, tmp_dev, &session_drm->devices, link) {
    vt_session_close_device_drm(session, dev);
  }

  // Stop listening
  wl_list_remove(&listener->link);
}

void _drm_on_seat_disable(struct wl_listener* listener, void* data) {
  if (!data)
    return;

  struct vt_session_t* session = (struct vt_session_t*)data;
  struct drm_backend_master_state_t* drm_master =
    BACKEND_DATA(session->comp->backend, struct drm_backend_master_state_t);

  VT_TRACE(session->comp->log, "DRM: Seat disable event (VT switch away)");

  struct drm_backend_state_t* drm;
  wl_list_for_each(drm, &drm_master->backends, link) {
    _drm_suspend(drm);
  }

  VT_TRACE(session->comp->log, "DRM: Seat disable complete (devices paused, not closed).");
}


void _drm_on_seat_enable(struct wl_listener *listener, void *data) {
  if (!data)
    return;

  struct vt_session_t* session = (struct vt_session_t*)data;
  struct drm_backend_master_state_t* drm_master =
    BACKEND_DATA(session->comp->backend, struct drm_backend_master_state_t);

  VT_TRACE(session->comp->log, "DRM: Seat enable event (VT switch back)");

  struct drm_backend_state_t* drm;
  wl_list_for_each(drm, &drm_master->backends, link) {
    _drm_resume(drm);
  }

  VT_TRACE(session->comp->log, "DRM: Seat enable complete (devices resumed).");
}

bool 
_drm_handle_frame_for_device(struct drm_backend_state_t* drm, struct vt_output_t* output) {
  if(!drm || !output) return false;
  struct vt_compositor_t* comp = drm->comp;

  VT_TRACE(comp->log, "DRM: Handling frame...");
  vt_comp_repaint_scene(comp, output);

  if (!drm->renderer) {
    VT_ERROR(comp->log, "DRM: Renderer backend not initialized before handling frame.");
    return false;
  }

  struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t); 

  // Retrieve the front buffer that we rendered to with the renderer 
  struct gbm_bo *bo = gbm_surface_lock_front_buffer(drm_output->gbm_surf);
  // If we could not get the front buffer, we'll try again next frame.
  if (!bo) {
    VT_WARN(comp->log, "DRM: Failed to get the GBM front buffer for frame in output %p.", output);
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
    VT_ERROR(comp->log, "DRM: canot create DRM frame buffer for output %p: drmModeAddFB2(%ux%u, fmt=0x%08x) failed: %s",
             output, w, h, fmt, strerror(errno));
    return false;
  }

  if (drm_output->needs_modeset) {
    if (drmModeSetCrtc(drm->drm_fd, drm_output->crtc_id, fb,
                       0, 0, &drm_output->conn_id, 1, &drm_output->mode) != 0) {
      drmModeRmFB(drm->drm_fd, fb);
      gbm_surface_release_buffer(drm_output->gbm_surf, bo);
      // Try again next farme
      output->needs_repaint = true;
      VT_ERROR(comp->log, "DRM: cannot set CRTC mode for output %p: drmModeSetCrtc() failed: %s",
               output, strerror(errno));
      return false;
    }
    VT_TRACE(comp->log, "DRM: Successfully performed DRM CRTC mode set for output %p (%ux%u@%.2f, ID: %i)", output,
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
      uint32_t t = vt_util_get_time_msec();
      vt_comp_frame_done_all(comp, t);
      wl_display_flush_clients(comp->wl.dsp);
      output->needs_repaint = false;
      drm_output->modeset_bootstrapped = true;
      VT_TRACE(comp->log, "DRM: Successfully bootstrapped the first frame for output %p.", output);
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
    VT_ERROR(comp->log, "DRM: cannot do a page flip: drmModePageFlip() failed: %s",
             strerror(errno));
    return false;
  }
  VT_TRACE(comp->log, "DRM: Successfully performed drmModePageFlip() call.");

  drm_output->flip_inflight = true;

  // we’ve submitted a frame so clear the desire until something else changes
  output->needs_repaint = false;
}

// ===================================================
// =================== PUBLIC API ====================
// ===================================================
bool
backend_init_drm(struct vt_backend_t* backend) {
  if(!backend || !backend->comp || !backend->comp->session) return false;
  if(!(backend->user_data = VT_ALLOC(backend->comp, sizeof(struct drm_backend_master_state_t))))  {
    return false;
  }

  struct drm_backend_master_state_t* drm_master = BACKEND_DATA(backend, struct drm_backend_master_state_t); 
  drm_master->comp = backend->comp;

  wl_list_init(&drm_master->backends);

  // Listen for the session terminate signal so that 
  // we can call our backend-specific handler. 
  drm_master->session_terminate_listener.notify = _drm_on_session_terminate; 
  wl_signal_add(&backend->comp->session->ev_session_terminate, &drm_master->session_terminate_listener);

  // Listen for seat enable/disable signals for correct VT switching 
  // functionality
  struct vt_session_drm_t* session_drm = BACKEND_DATA(backend->comp->session, struct vt_session_drm_t); 
  drm_master->seat_enable_listener.notify = _drm_on_seat_enable; 
  wl_signal_add(&session_drm->ev_seat_enable, &drm_master->seat_enable_listener);

  drm_master->seat_disable_listener.notify = _drm_on_seat_disable; 
  wl_signal_add(&session_drm->ev_seat_disable, &drm_master->seat_disable_listener);

  // Create a DRM backend state for all the enumerated GPUs
  const uint8_t max_gpus = 8;
  struct vt_device_drm_t* gpus[max_gpus];
  uint32_t n_gpus = vt_session_enumerate_cards_drm(backend->comp->session, gpus, max_gpus);

  for(uint32_t i = 0; i < n_gpus; i++) {
    struct drm_backend_state_t* drm_backend = VT_ALLOC(backend->comp, sizeof(struct drm_backend_state_t));
    drm_backend->root_backend = backend;
    if(!_drm_init_for_device(backend->comp, drm_backend, gpus[i]->fd, gpus[i]->path)) {
      VT_ERROR(backend->comp->log, "DRM: Failed to initialize DRM backend for GPU (%i).", gpus[i]->fd);
      return false;
    }
    wl_list_insert(&drm_master->backends, &drm_backend->link);
  }

  return true;
}

bool 
backend_handle_frame_drm(struct vt_backend_t* backend, struct vt_output_t* output) {
  if(!backend || !backend->user_data) return false;

  struct drm_backend_master_state_t* drm_master = BACKEND_DATA(backend, struct drm_backend_master_state_t); 
  struct drm_backend_state_t* drm;
  wl_list_for_each(drm, &drm_master->backends, link) {
    _drm_handle_frame_for_device(drm, output);
  }
}


bool
backend_terminate_drm(struct vt_backend_t* backend) {
  struct vt_device_drm_t* dev, *tmp_dev;
  struct drm_backend_master_state_t* drm_master = BACKEND_DATA(backend, struct drm_backend_master_state_t); 
  struct drm_backend_state_t* drm;
  wl_list_for_each(drm, &drm_master->backends, link) {
    _drm_terminate_for_device(drm);
  }

  if(backend->user_data) {
    backend->user_data = NULL;
  }
}


bool 
backend_prepare_output_frame_drm(struct vt_backend_t* backend, struct vt_output_t* output) {
  (void)backend;
  struct drm_output_state_t* drm_output = BACKEND_DATA(output, struct drm_output_state_t);
  if(drm_output->flip_inflight) return false;
  if(!output->needs_repaint && drm_output->modeset_bootstrapped) return false;

  return true;
}

bool 
backend_implement_drm(struct vt_compositor_t* comp) {
  if(!comp || !comp->backend) return false;

  VT_TRACE(comp->log, "DRM: Implementing backend...");

  comp->backend->platform = VT_BACKEND_DRM_GBM; 

  comp->backend->impl = (struct vt_backend_interface_t){
    .init = backend_init_drm,
    .handle_frame = backend_handle_frame_drm,
    .terminate = backend_terminate_drm,
    .prepare_output_frame = backend_prepare_output_frame_drm,
  };

  comp->session->impl = (struct vt_session_interface_t){
    .init = vt_session_init_drm,
    .terminate = vt_session_terminate_drm
  };

  return true;
}

