#pragma once

#include <stdbool.h>

#include <libinput.h>
#include <runara/runara.h>
#include <wayland-util.h>
#include <pixman.h>

#define BACKEND_DATA(b, type) ((type *)((b)->user_data))
#define COMP_MAX_DAMAGE_RECTS 4
#define COMP_MAX_FRAME_CBS 8 

#define COMP_ALLOC(c, size) comp_alloc(&(c)->arena, (size))

typedef struct vt_backend_t vt_backend_t; 
typedef struct vt_renderer_t vt_renderer_t; 
typedef struct vt_output_t vt_output_t; 

typedef struct {
  FILE* stream;
  bool verbose, quiet;
} log_state_t;

typedef struct {
  struct wl_display* dsp;
  struct wl_event_loop* evloop;
  struct wl_compositor* compositor;
  struct wl_global* xdg_wm_base; 
} wl_state_t;

typedef struct vt_compositor_t vt_compositor_t;


typedef struct {
  struct wl_resource *res;
  struct wl_list link;
} vt_frame_cb;

typedef struct {
  vt_frame_cb* cbs[COMP_MAX_FRAME_CBS];
  uint32_t n_cbs;
} vt_frame_cb_pool;

typedef struct {
  struct wl_resource* surf_res, *buf_res, 
    *xdg_surf_res, *xdg_toplevel_res; 
  RnTexture tex; 
  struct wl_list link;

  uint32_t last_configure_serial;

  char* app_id, *title;

  uint32_t width, height;
  int32_t x, y;

  bool needs_frame_done;

  vt_compositor_t* comp;

  uint32_t _mask_outputs_visible_on;
  uint32_t _mask_outputs_presented_on;

  void* user_data;

  pixman_region32_t pending_damage, current_damage, _scratch_damage;  
  bool damaged;

  vt_frame_cb_pool cb_pool;
} vt_surface_t;

typedef bool (*backend_implement_func_t)(vt_compositor_t* comp);

typedef struct {
  bool (*init)(vt_backend_t* backend);
  bool (*implement)(vt_compositor_t* comp);
  bool (*handle_event)(vt_backend_t* backend);
  bool (*suspend)(vt_backend_t* backend);
  bool (*resume)(vt_backend_t* backend);
  bool (*handle_frame)(vt_backend_t* backend, vt_output_t* output);
  bool (*create_output)(vt_backend_t* backend, vt_output_t* output, void* data);
  bool (*destroy_output)(vt_backend_t* backend, vt_output_t* output);
  bool (*initialize_active_outputs)(vt_backend_t* backend);
  bool (*prepare_output_frame)(vt_backend_t* backend, vt_output_t* output);
  bool (*__handle_input)(vt_backend_t* backend, bool mods, uint32_t key);
  bool (*terminate)(vt_backend_t* backend);
} vt_backend_interface_t;


typedef enum {
  VT_BACKEND_DRM_GBM = 0,
  VT_BACKEND_WAYLAND,
  VT_BACKEND_SURFACELESS,
} vt_backend_platform_t;

struct vt_backend_t {
  void* user_data;
  vt_backend_interface_t impl;

  vt_compositor_t* comp;

  struct wl_list outputs;
  vt_renderer_t *renderer;

  uint32_t _desired_render_buffer_format;
  
  vt_backend_platform_t platform;

  void* native_handle;
};

typedef enum {
  VT_RENDERING_BACKEND_EGL_OPENGL = 0,
} vt_rendering_backend_t;

typedef struct vt_renderer_interface_t {
  bool (*init)(vt_backend_t* backend, vt_renderer_t* r, void* native_handle);
  bool (*setup_renderable_output)(vt_renderer_t* r, vt_output_t* output);
  bool (*resize_renderable_output)(vt_renderer_t* r, vt_output_t* output, int32_t w, int32_t h);
  bool (*destroy_renderable_output)(vt_renderer_t* r, vt_output_t* output);
  bool (*import_buffer)(vt_renderer_t* r, vt_surface_t *surf,
      struct wl_resource* buffer_resource);
  bool (*drop_context)(vt_renderer_t* r);
  void (*set_vsync)(vt_renderer_t* r, bool vsync);
  void (*set_clear_color)(vt_renderer_t* r, vt_output_t* output, uint32_t col);
  void (*stencil_damage_pass)(vt_renderer_t* r, vt_output_t* output); 
  void (*composite_pass)(vt_renderer_t* r, vt_output_t* output); 
  void (*begin_scene)(vt_renderer_t* r, vt_output_t* output);
  void (*begin_frame)(vt_renderer_t* r, vt_output_t* output);
  void (*draw_surface)(vt_renderer_t* r, vt_surface_t *surface, float x, float y);
  void (*draw_rect)(vt_renderer_t* r, float x, float y, float w, float h, uint32_t col);
  void (*end_scene)(vt_renderer_t* r, vt_output_t* output);
  void (*end_frame)(vt_renderer_t* r, vt_output_t* output, const pixman_box32_t* damaged, int32_t n_damaged);
  bool (*destroy)(vt_renderer_t* r);
} vt_renderer_interface_t;

struct vt_renderer_t {
  vt_renderer_interface_t impl;

  vt_rendering_backend_t rendering_backend;
  vt_compositor_t* comp;
  void *user_data;

  vt_backend_t* backend;
  
  uint32_t _desired_render_buffer_format;
};

struct vt_output_t {
  struct wl_list link; 
  vt_backend_t* backend;
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


typedef struct vt_arena {
    uint8_t *base;
    size_t   offset;
    size_t   capacity;
} vt_arena_t;

struct vt_compositor_t {
  wl_state_t  wl;
  struct libinput* input;

  vt_backend_t* backend;
  log_state_t log;

  struct wl_list surfaces; 

  bool running, suspended;
  bool sent_frame_cbs, any_frame_cb_pending;

  uint32_t n_virtual_outputs;


  const char* _cmd_line_backend_path;

  vt_arena_t arena;
};


bool comp_init(vt_compositor_t *c, int argc, char** argv);

void comp_run(vt_compositor_t *c);

uint32_t comp_merge_damaged_regions(pixman_box32_t *merged, pixman_region32_t *region);

bool comp_terminate(vt_compositor_t *c);

void comp_send_frame_callbacks_for_output(vt_compositor_t *c, vt_output_t* output, uint32_t t);

void comp_send_frame_callbacks(vt_compositor_t *c, vt_output_t* output, uint32_t t);

void comp_schedule_repaint(vt_compositor_t *c, vt_output_t* output);

void comp_repaint_scene(vt_compositor_t *c, vt_output_t* output);

uint32_t comp_get_time_msec(void);

void comp_arena_init(vt_arena_t *a, size_t capacity);

void* comp_alloc(vt_arena_t *a, size_t size);

void comp_arena_reset(vt_arena_t *a);

void comp_arena_destroy(vt_arena_t *a);
