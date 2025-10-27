#pragma once

#include <stdbool.h>

#include <libinput.h>
#include <runara/runara.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <pixman.h>

#include "surface.h"
#include "util.h"

#include "session.h"


#define BACKEND_DATA(b, type) ((type *)((b)->user_data))
#define VT_MAX_DAMAGE_RECTS 4

#define VT_ALLOC(c, size) vt_util_alloc(&(c)->arena, (size))

struct vt_renderer_t;   
struct vt_surface_t;
struct vt_backend_t;
struct vt_output_t;

struct log_state_t {
  FILE* stream;
  bool verbose, quiet;
} ;

struct wl_state_t {
  struct wl_display* dsp;
  struct wl_event_loop* evloop;
  struct wl_compositor* compositor;
};


typedef bool (*backend_implement_func_t)(struct vt_compositor_t* comp);

struct vt_backend_interface_t {
  bool (*init)(struct vt_backend_t* backend);
  bool (*implement)(struct vt_compositor_t* comp);
  bool (*handle_frame)(struct vt_backend_t* backend, struct vt_output_t* output);
  bool (*prepare_output_frame)(struct vt_backend_t* backend, struct vt_output_t* output);
  bool (*terminate)(struct vt_backend_t* backend);
};

struct vt_backend_t {
  void* user_data;
  struct vt_backend_interface_t impl;

  struct vt_compositor_t* comp;

  vt_backend_platform_t platform;
};

struct vt_output_t {
  struct wl_list link_local, link_global; 
  struct vt_backend_t* backend;
  struct vt_renderer_t* renderer;
  void* native_window;
  void* render_surface;

  uint32_t width, height;
  int32_t x, y;
  float refresh_rate;
  uint32_t format, id;

  bool needs_repaint, repaint_pending;

  void* user_data, *user_data_render;

  struct wl_event_source* repaint_source;

  pixman_region32_t damage; 
}; 

struct vt_compositor_t {
  struct wl_state_t  wl;
  struct libinput* input;

  struct vt_backend_t* backend;
  struct log_state_t log;

  struct wl_list surfaces; 

  bool running, suspended;
  bool sent_frame_cbs, any_frame_cb_pending;

  uint32_t n_virtual_outputs;

  const char* _cmd_line_backend_path;

  struct wl_list outputs;

  struct vt_arena_t arena;
  
  struct vt_session_t* session;
};

