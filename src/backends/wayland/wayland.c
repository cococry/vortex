#define _GNU_SOURCE
#include "wayland.h"

#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <sys/mman.h>
#include <pthread.h>

#include "xdg-shell-client-protocol.h"
#include "../../render/renderer.h"

#define _WL_DEFAULT_OUTPUT_WIDTH 1280
#define _WL_DEFAULT_OUTPUT_HEIGHT 720

typedef struct {
  bool nested;
  struct wl_display *parent_display;
  struct wl_compositor *parent_compositor;
  struct xdg_wm_base *parent_xdg_wm_base;
  struct vt_compositor_t* comp;

  struct vt_renderer_t* renderer;
} wayland_backend_state_t;

typedef struct {
  struct wl_surface *parent_surface;
  struct xdg_surface *parent_xdg_surface;
  struct xdg_toplevel *parent_xdg_toplevel;
  struct wl_callback* parent_frame_cb;

} wayland_output_state_t;


static void _wl_parent_registry_add(
  void *data, struct wl_registry *reg,
  uint32_t id, const char *iface, uint32_t ver);

static void _wl_parent_registry_remove(void *data, struct wl_registry *reg, uint32_t id);

static void _wl_parent_xdg_wm_base_ping(void *data, struct xdg_wm_base *wm, uint32_t serial);

static void _wl_parent_xdg_surface_configure(
  void *data,
  struct xdg_surface *surf, uint32_t serial);


static void _wl_parent_xdg_toplevel_configure(
  void *data, struct xdg_toplevel *toplevel, 
  int32_t w, int32_t h, struct wl_array *states);

static void  _wl_parent_xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel);

static int _wl_parent_dispatch(int fd, uint32_t mask, void *data);

static void _wl_parent_frame_done(void *data,
		     struct wl_callback *wl_callback,
		     uint32_t time);

static bool _wl_backend_destroy_output(struct vt_backend_t* backend, struct vt_output_t* output);

static bool _wl_backend_create_output(struct vt_backend_t* backend, struct vt_output_t* output, void* data);

static bool _wl_backend_init_active_outputs(struct vt_backend_t* backend);

static bool _wl_backend_init_session(struct vt_session_t* session);

static bool _wl_backend_terminate_session(struct vt_session_t* session);

static struct wl_callback_listener parent_surface_frame_listener = {
  .done = _wl_parent_frame_done 
};

static const struct wl_registry_listener parent_registry_listener = {
  .global = _wl_parent_registry_add,
  .global_remove = _wl_parent_registry_remove,
};

static const struct xdg_wm_base_listener parent_wm_listener = {
  .ping = _wl_parent_xdg_wm_base_ping,
};

static const struct xdg_surface_listener parent_xdg_surface_listener = {
  .configure = _wl_parent_xdg_surface_configure,
};

static const struct xdg_toplevel_listener parent_toplevel_listener = {
  .configure = _wl_parent_xdg_toplevel_configure,
  .close = _wl_parent_xdg_toplevel_close,
};


void 
_wl_parent_registry_add(
  void *data, struct wl_registry *reg,
  uint32_t id, const char *iface, uint32_t ver) {
  wayland_backend_state_t* wl = data;
  if (strcmp(iface, wl_compositor_interface.name) == 0) {
    wl->parent_compositor = wl_registry_bind(reg, id, &wl_compositor_interface, 4);
  } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
    wl->parent_xdg_wm_base = wl_registry_bind(reg, id, &xdg_wm_base_interface, 1);
  } 
}

void 
_wl_parent_registry_remove(void *data, struct wl_registry *reg, uint32_t id) {
  // who cares xd
  (void)data; (void)reg; (void)id;
}


void 
_wl_parent_xdg_wm_base_ping(void *data, struct xdg_wm_base *wm, uint32_t serial) {
  (void)data;
  // Pong that scheiße
  xdg_wm_base_pong(wm, serial);
}


void 
_wl_parent_xdg_toplevel_configure(void *data,
  struct xdg_toplevel *toplevel, int32_t w, int32_t h, struct wl_array *states) {
  struct vt_output_t* output = data;
  if(!output || !output->user_data) return;
  wayland_output_state_t* wl_output = BACKEND_DATA(output, wayland_output_state_t);
  if(!wl_output) return;
  
  if(w != output->width || h != output->height) {
    struct vt_renderer_t* r = output->renderer;
    if (r && r->impl.resize_renderable_output && output->native_window)
      r->impl.resize_renderable_output(r, output, w, h);

    output->width = w;
    output->height = h;
    vt_comp_schedule_repaint(r->comp, output);
  }

}

void 
_wl_parent_xdg_surface_configure(void *data,
                                         struct xdg_surface *surf, uint32_t serial) {
  struct vt_output_t* output = data;
  // We must acknowledge the size from the request 
  xdg_surface_ack_configure(surf, serial);

  // If we already got a toplevel size, resize now.
  int w = output->width > 0 ? output->width : _WL_DEFAULT_OUTPUT_WIDTH;
  int h = output->height > 0 ? output->height : _WL_DEFAULT_OUTPUT_HEIGHT;
  
  // Handle resize output in renderer
  struct vt_renderer_t* r = output->renderer;
  if (r && r->impl.resize_renderable_output && output->native_window)
    r->impl.resize_renderable_output(r, output, w, h);
  output->width = w;
  output->height = h;
  vt_comp_schedule_repaint(r->comp, output);
}

void 
_wl_parent_xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
  struct vt_output_t* output = data;
  (output->backend, output);
}

int 
_wl_parent_dispatch(int fd, uint32_t mask, void *data) {
  wayland_backend_state_t* wl = data;
  // process incoming events from parent 
  if (wl_display_dispatch(wl->parent_display) < 0) {
    wl->comp->running = false; // parent died
  }
  return 0;
}

void 
_wl_parent_frame_done(void *data,
		     struct wl_callback *wl_callback,
		     uint32_t time) {
  if(!wl_callback || !data) return;
  struct vt_output_t* output = (struct vt_output_t*)data; 

  wayland_backend_state_t* wl = BACKEND_DATA(output->backend, wayland_backend_state_t); 
  wayland_output_state_t* wl_output = BACKEND_DATA(output, wayland_output_state_t); 
  struct vt_compositor_t* comp = output->backend->comp;
  if(!wl || !comp) return;

  // first clean up the previously used data
  wl_callback_destroy(wl_callback);
  wl_output->parent_frame_cb = NULL;

  vt_comp_frame_done(comp, output, time); 

}

bool 
_wl_backend_destroy_output(struct vt_backend_t* backend, struct vt_output_t* output) {
  if(!output || !output->user_data) return false;

  wayland_backend_state_t* wl_backend = BACKEND_DATA(backend, wayland_backend_state_t);

  wayland_output_state_t* wl_output = BACKEND_DATA(output, wayland_output_state_t);
  if(!wl_output) return false;

  if (wl_output->parent_xdg_toplevel) {
    xdg_toplevel_destroy(wl_output->parent_xdg_toplevel);
    wl_output->parent_xdg_toplevel = NULL;
  }

  if (wl_output->parent_xdg_surface) {
    xdg_surface_destroy(wl_output->parent_xdg_surface);
    wl_output->parent_xdg_surface = NULL;
  }

  if (wl_output->parent_surface) {
    wl_surface_destroy(wl_output->parent_surface);
    wl_output->parent_surface = NULL;
  }

  if(!wl_backend->renderer->impl.destroy_renderable_output(output->renderer, output)) return false;

  free(output->user_data);
  output->user_data = NULL;
    
  wl_list_remove(&output->link_global);
  free(output);
  output = NULL;

  if(!wl_list_length(&backend->comp->outputs)) {
    backend->comp->running = false;
  }

  return true;
}

// ===================================================
// =================== PUBLIC API ====================
// ===================================================
bool 
backend_init_wl(struct vt_backend_t* backend) {
  if(!backend) return false;
  if(!(backend->user_data = VT_ALLOC(backend->comp, sizeof(wayland_backend_state_t)))) return false;
  struct vt_compositor_t* c = backend->comp;
  wayland_backend_state_t* wl = BACKEND_DATA(backend, wayland_backend_state_t); 
  wl->comp = backend->comp;

  // Connect to lé display
  wl->parent_display = wl_display_connect(NULL);
  if (!wl->parent_display) {
    VT_ERROR(c->log, "WL: Failed to connect to parent Wayland compositor.");
    return false;
  }
  
  // Bind globals
  struct wl_registry *reg = wl_display_get_registry(wl->parent_display);
  wl_registry_add_listener(reg, &parent_registry_listener, wl);
  wl_display_roundtrip(wl->parent_display);
  if (!wl->parent_compositor || !wl->parent_xdg_wm_base) {
    VT_ERROR(c->log, "WL: Required globals not found.");
    return false;
  }

  xdg_wm_base_add_listener(wl->parent_xdg_wm_base, &parent_wm_listener, c);

  int pfd = wl_display_get_fd(wl->parent_display);
  wl_event_loop_add_fd(c->wl.evloop, pfd,
                       WL_EVENT_READABLE, _wl_parent_dispatch, wl);

  wl->renderer = VT_ALLOC(c, sizeof(struct vt_renderer_t));
  wl->renderer->comp = c;
  vt_renderer_implement(wl->renderer, VT_RENDERING_BACKEND_EGL_OPENGL);

  wl->renderer->impl.init(c->backend, wl->renderer, wl->parent_display);

  _wl_backend_init_active_outputs(backend);

  return true;

}

bool 
backend_implement_wl(struct vt_compositor_t* comp) {
  if(!comp || !comp->backend) return false; 

  VT_TRACE(comp->log, "WL: Implementing backend...");

  comp->backend->platform = VT_BACKEND_WAYLAND; 
  comp->backend->impl = (vt_backend_interface_t){
    .init = backend_init_wl,
    .handle_frame = backend_handle_frame_wl,
    .terminate = backend_terminate_wl,
    .prepare_output_frame = backend_prepare_output_frame_wl,
  };


  comp->session->impl = (struct vt_session_interface_t){
    .init = _wl_backend_init_session,
    .terminate =_wl_backend_terminate_session 
  };
}

bool 
backend_handle_frame_wl(struct vt_backend_t* backend, struct vt_output_t* output){
  // Fully driven by the parent surface's .done event (see _wl_parent_frame_done)
  wayland_output_state_t* wl_output = BACKEND_DATA(output, wayland_output_state_t); 
  wl_output->parent_frame_cb = wl_surface_frame(wl_output->parent_surface);
  wl_callback_add_listener(wl_output->parent_frame_cb, &parent_surface_frame_listener, output);

  wl_surface_commit(wl_output->parent_surface);

  
  return true;
} 

bool 
_wl_backend_init_active_outputs(struct vt_backend_t* backend){
  if(!backend) return false;

  VT_TRACE(backend->comp->log, "WL: Initializing active outputs.");
  
  wayland_backend_state_t* wl_backend = BACKEND_DATA(backend, wayland_backend_state_t);

  if (!wl_backend->renderer || !wl_backend->renderer->impl.setup_renderable_output) {
    VT_ERROR(backend->comp->log, "WL: Renderer backend not initialized before output setup.");
    return false;
  }

  for (uint32_t i = 0; i < backend->comp->n_virtual_outputs; i++) {
    struct vt_output_t* output = VT_ALLOC(backend->comp, sizeof(struct vt_output_t));
    pixman_region32_init(&output->damage);
    output->backend = backend;
    if (!_wl_backend_create_output(backend, output, NULL)) {
      VT_ERROR(backend->comp->log, "WL: Failed to setup internal WL output.");
      free(output);
      continue;
    } 
    if(!wl_backend->renderer->impl.setup_renderable_output(wl_backend->renderer, output)) {
      VT_ERROR(backend->comp->log, "WL: Failed to setup renderable output for WL output (%ix%i@%.2f)",
                output->width, output->height, output->refresh_rate);
      _wl_backend_destroy_output(backend, output);
      free(output);
      continue;
    }
    vt_comp_schedule_repaint(backend->comp, output);
  }
  
  if (wl_list_empty(&backend->comp->outputs)) {
    VT_ERROR(backend->comp->log, "WL: No outputs have been initialized.");
    return false;
  }

  // Nested backend must not wait for the vblank of the compositor for 
  // frame pacing, as the parent compositor already waits.
  wl_backend->renderer->impl.set_vsync(wl_backend->renderer, false);

  return true;
}

bool 
_wl_backend_init_session(struct vt_session_t* session) {
  // no-op
  (void)session;
}

bool 
_wl_backend_terminate_session(struct vt_session_t* session) {
  // no-op
  (void)session;
}

bool 
backend_terminate_wl(struct vt_backend_t* backend){
  if(!backend || !backend->user_data) return false;
  
  wayland_backend_state_t* wl = BACKEND_DATA(backend, wayland_backend_state_t);

  struct vt_output_t* output;
  wl_list_for_each(output, &backend->comp->outputs, link_global) {
    _wl_backend_destroy_output(backend, output);
  }

  if (wl->parent_xdg_wm_base) {
    xdg_wm_base_destroy(wl->parent_xdg_wm_base);
    wl->parent_xdg_wm_base = NULL;
  }

  if (wl->parent_display) {
    wl_display_disconnect(wl->parent_display);
    wl->parent_display = NULL;
  }

  return true;
}


bool 
_wl_backend_create_output(struct vt_backend_t* backend, struct vt_output_t* output, void* data){
  if(!backend || !output) return false;

  VT_TRACE(backend->comp->log, "DMR: Creating WL internal output.");
  if(!(output->user_data = VT_ALLOC(backend->comp, sizeof(wayland_output_state_t)))) {
    return false;
  }

  wayland_backend_state_t* wl = BACKEND_DATA(backend, wayland_backend_state_t);
  wayland_output_state_t* wl_output = BACKEND_DATA(output, wayland_output_state_t);
  wl_output->parent_surface = wl_compositor_create_surface(wl->parent_compositor);
  wl_output->parent_xdg_surface =
    xdg_wm_base_get_xdg_surface(wl->parent_xdg_wm_base, wl_output->parent_surface);
  xdg_surface_add_listener(wl_output->parent_xdg_surface, &parent_xdg_surface_listener, output);

  wl_output->parent_xdg_toplevel = xdg_surface_get_toplevel(wl_output->parent_xdg_surface);
  xdg_toplevel_add_listener(wl_output->parent_xdg_toplevel, &parent_toplevel_listener, output);
  char name[64];
  sprintf(name, "Vortex Nested (%i)", wl_list_length(&backend->comp->outputs));
  xdg_toplevel_set_title(wl_output->parent_xdg_toplevel, name);
  
  VT_TRACE(backend->comp->log, "WL: Created virtual nested output %s.", name);

  wl_output->parent_frame_cb = wl_surface_frame(wl_output->parent_surface);
  wl_callback_add_listener(wl_output->parent_frame_cb, &parent_surface_frame_listener, output);
 
  // Set renderer
  output->renderer = wl->renderer;
  vt_comp_schedule_repaint(backend->comp, output);

  // Trigger initial configure
  wl_surface_commit(wl_output->parent_surface);
  // Get the initial configure immidiately
  wl_display_roundtrip(wl->parent_display);

  output->native_window = wl_output->parent_surface;  

  output->id = wl_list_length(&backend->comp->outputs);
  output->refresh_rate = 60;
  output->x = 0;
  output->y = 0;
  output->width = _WL_DEFAULT_OUTPUT_WIDTH;  
  output->height = _WL_DEFAULT_OUTPUT_HEIGHT; 



  wl_list_insert(&backend->comp->outputs, &output->link_global);
 
  return true;
}

bool 
backend_prepare_output_frame_wl(struct vt_backend_t* backend, struct vt_output_t* output){
  (void)backend;
  (void)output;
  return true;
}
