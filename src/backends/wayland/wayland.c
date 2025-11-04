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
#include <string.h>

#include "xdg-shell-client-protocol.h"
#include "../../render/renderer.h"
#include "../../core/compositor.h"
#include "../../protocols/linux_dmabuf.h"
#include "../../protocols/linux_explicit_sync.h"

#define _WL_DEFAULT_OUTPUT_WIDTH 1280
#define _WL_DEFAULT_OUTPUT_HEIGHT 720

typedef struct {
  bool nested;
  struct wl_display* parent_display;
  struct wl_compositor* parent_compositor;
  struct xdg_wm_base* parent_xdg_wm_base;
  struct wl_seat* parent_seat;
  struct vt_compositor_t* comp;
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
  } else if (strcmp(iface, wl_seat_interface.name) == 0) {
    wl->parent_seat = wl_registry_bind(reg, id, &wl_seat_interface, 7);
    wl->comp->session->native_handle = wl->parent_seat;
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
    struct vt_renderer_t* r = output->backend->comp->renderer;
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
  struct vt_renderer_t* r = output->backend->comp->renderer;
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

  if(!backend->comp->renderer->impl.destroy_renderable_output(backend->comp->renderer, output)) return false;

  output->user_data = NULL;
   
  wl_list_remove(&output->link_global);
  output = NULL;

  return true;
}

#define _vt_fourcc_code(a, b, c, d) ((__u32)(a) | ((__u32)(b) << 8) | \
  ((__u32)(c) << 16) | ((__u32)(d) << 24))

#define _VT_DRM_FORMAT_ARGB8888	_vt_fourcc_code('A', 'R', '2', '4') /* [31:0] A:R:G:B 8:8:8:8 little endian */
#define _VT_DRM_FORMAT_XRGB8888	_vt_fourcc_code('X', 'R', '2', '4') /* [31:0] A:R:G:B 8:8:8:8 little endian */
#define _VT_DRM_FORMAT_MOD_LINEAR 0x0000000000000000
#define _VT_DRM_FORMAT_MOD_INVALID 0x00FFFFFFFFFFFFFF

static bool init_fake_dmabuf_feedback(struct vt_compositor_t* comp,
                                      struct vt_dmabuf_feedback_t* fb) {
  wl_array_init(&fb->tranches);

  fb->comp = comp;
  fb->dev_main = calloc(1, sizeof(*fb->dev_main)); // no DRM device
  fb->dev_main->dev = 0;
  fb->dev_main->fd = -1;

  // Create a single empty tranche (no formats)
  struct vt_dmabuf_tranche_t* tranche = wl_array_add(&fb->tranches, sizeof(*tranche));
  if (!tranche)
    return false;

  wl_array_init(&tranche->formats);

  tranche->flags = 0; // no scanout
  tranche->target_device = fb->dev_main;

  // add DRM_FORMAT_XRGB8888 + modifiers
  struct vt_dmabuf_drm_format_t* fmt = wl_array_add(&tranche->formats, sizeof(*fmt));
  fmt->format = _VT_DRM_FORMAT_XRGB8888;
  fmt->len = 2;
  fmt->mods = calloc(fmt->len, sizeof(*fmt->mods));
  fmt->mods[0].mod = _VT_DRM_FORMAT_MOD_LINEAR;
  fmt->mods[0]._egl_ext_only = false;
  fmt->mods[1].mod = _VT_DRM_FORMAT_MOD_INVALID;
  fmt->mods[1]._egl_ext_only = false;

  // also ARGB8888 (clients sometimes expect it)
  struct vt_dmabuf_drm_format_t* fmt2 = wl_array_add(&tranche->formats, sizeof(*fmt2));
  fmt2->format = _VT_DRM_FORMAT_ARGB8888;
  fmt2->len = 2;
  fmt2->mods = calloc(fmt2->len, sizeof(*fmt2->mods));
  fmt2->mods[0].mod = _VT_DRM_FORMAT_MOD_LINEAR;
  fmt2->mods[0]._egl_ext_only = false;
  fmt2->mods[1].mod = _VT_DRM_FORMAT_MOD_INVALID;
  fmt2->mods[1]._egl_ext_only = false;

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

  VT_TRACE(c->log, "WL: Initializing Wayland backend...");

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
  if (!wl->parent_compositor || !wl->parent_xdg_wm_base || !wl->comp->session->native_handle) {
    VT_ERROR(c->log, "WL: Required globals not found.");
    return false;
  }
  VT_TRACE(c->log, "WL: Found required globals.");

  xdg_wm_base_add_listener(wl->parent_xdg_wm_base, &parent_wm_listener, c);

  int pfd = wl_display_get_fd(wl->parent_display);
  wl_event_loop_add_fd(c->wl.evloop, pfd,
                       WL_EVENT_READABLE, _wl_parent_dispatch, wl);


  backend->comp->renderer->impl.init(c->backend, backend->comp->renderer, wl->parent_display);

  struct vt_dmabuf_feedback_t *feedback = calloc(1, sizeof(*feedback));

  const uint8_t dmabuf_ver = 4, dmabuf_explicit_sync_ver = 2;
  init_fake_dmabuf_feedback(backend->comp, feedback);
  if(!vt_proto_linux_dmabuf_v1_init(backend->comp, feedback, dmabuf_ver)) {
    VT_ERROR(backend->comp->log, "WL: Failed to initialize DMABUF protocol, will fallback to SHM imports.");
  } else {
    if(!vt_proto_linux_explicit_sync_v1_init(backend->comp, dmabuf_explicit_sync_ver)) {
      VT_ERROR(backend->comp->log, "WL: Failed to initialize DMABUF explicit sync protocol, will not use explicit sync.");
    } else {
      VT_TRACE(c->log, "WL: Successfully initialized DMABUF protocol version %i and DMABUF explicit sync protocol version %i.",
               dmabuf_ver, dmabuf_explicit_sync_ver);
    }
  }
  _wl_backend_init_active_outputs(backend);
  
  VT_TRACE(c->log, "WL: Successfully initialized Wayland backend.");

  return true;

}

bool 
backend_is_dmabuf_importable_wl(struct vt_backend_t* backend, struct vt_dmabuf_attr_t* attr, int32_t device_fd) {
  (void)backend;
  (void)attr;
  (void)device_fd;
  return true;
}

bool 
backend_implement_wl(struct vt_compositor_t* comp) {
  if(!comp || !comp->backend) return false; 

  VT_TRACE(comp->log, "WL: Implementing backend...");

  comp->backend->platform = VT_BACKEND_WAYLAND; 
  comp->backend->impl = (struct vt_backend_interface_t){
    .init = backend_init_wl,
    .is_dmabuf_importable = backend_is_dmabuf_importable_wl,
    .handle_frame = backend_handle_frame_wl,
    .terminate = backend_terminate_wl,
    .prepare_output_frame = backend_prepare_output_frame_wl,
  };

  // No session in Wayland nested
  memset(&comp->session->impl, 0, sizeof(comp->session->impl));
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

  if (!backend->comp->renderer || !backend->comp->renderer->impl.setup_renderable_output) {
    VT_ERROR(backend->comp->log, "WL: Renderer backend not initialized before output setup.");
    return false;
  }

  for (uint32_t i = 0; i < backend->comp->n_virtual_outputs; i++) {
    struct vt_output_t* output = VT_ALLOC(backend->comp, sizeof(struct vt_output_t));
    output->needs_damage_rebuild = true;
    pixman_region32_init(&output->damage);
    output->backend = backend;
    if (!_wl_backend_create_output(backend, output, NULL)) {
      VT_ERROR(backend->comp->log, "WL: Failed to setup internal WL output.");
      continue;
    } 
    if(!backend->comp->renderer->impl.setup_renderable_output(backend->comp->renderer, output)) {
      VT_ERROR(backend->comp->log, "WL: Failed to setup renderable output for WL output (%ix%i@%.2f)",
                output->width, output->height, output->refresh_rate);
      _wl_backend_destroy_output(backend, output);
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
  backend->comp->renderer->impl.set_vsync(backend->comp->renderer, false);

  return true;
}

bool 
backend_terminate_wl(struct vt_backend_t* backend){
  if(!backend || !backend->user_data) return false;
  
  wayland_backend_state_t* wl = BACKEND_DATA(backend, wayland_backend_state_t);

  struct vt_output_t* output, *tmp;
  wl_list_for_each_safe(output, tmp, &backend->comp->outputs, link_global) {
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

  VT_TRACE(backend->comp->log, "WL: Creating WL internal output.");
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
