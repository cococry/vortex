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

typedef struct {
  FILE* stream;
  bool verbose, quiet;
} log_state_t;

typedef struct {
  struct wl_display* dsp;
  struct wl_event_loop* evloop;
  struct wl_compositor* compositor;
} wl_state_t;


typedef bool (*backend_implement_func_t)(struct vt_compositor_t* comp);

typedef struct {
  bool (*init)(struct vt_backend_t* backend);
  bool (*implement)(struct vt_compositor_t* comp);
  bool (*handle_frame)(struct vt_backend_t* backend, struct vt_output_t* output);
  bool (*prepare_output_frame)(struct vt_backend_t* backend, struct vt_output_t* output);
  bool (*terminate)(struct vt_backend_t* backend);
} vt_backend_interface_t;


struct vt_backend_t {
  void* user_data;
  vt_backend_interface_t impl;

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
  wl_state_t  wl;
  struct libinput* input;

  struct vt_backend_t* backend;
  log_state_t log;

  struct wl_list surfaces; 

  bool running, suspended;
  bool sent_frame_cbs, any_frame_cb_pending;

  uint32_t n_virtual_outputs;

  const char* _cmd_line_backend_path;

  struct wl_list outputs;

  vt_arena_t arena;
  
  struct vt_session_t* session;
};


bool vt_comp_init(struct vt_compositor_t *c, int argc, char** argv);

void vt_comp_run(struct vt_compositor_t *c);

uint32_t vt_comp_merge_damaged_regions(pixman_box32_t *merged, pixman_region32_t *region);

bool vt_comp_terminate(struct vt_compositor_t *c);

void vt_comp_frame_done(struct vt_compositor_t *c, struct vt_output_t* output, uint32_t t);

void vt_comp_frame_done_all(struct vt_compositor_t *c, uint32_t t);

void vt_comp_schedule_repaint(struct vt_compositor_t *c, struct vt_output_t* output);

void vt_comp_repaint_scene(struct vt_compositor_t *c, struct vt_output_t* output);

void vt_comp_invalidate_all_surfaces(struct vt_compositor_t *comp);
