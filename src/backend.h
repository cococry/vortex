#pragma once

#include <stdbool.h>

#include <libinput.h>
#include <runara/runara.h>
#include <wayland-util.h>

#define BACKEND_DATA(b, type) ((type *)((b)->user_data))

#define COMP_ALLOC(c, size) \
    ({ \
        void *__output = NULL; \
        if ((c) && (size) != 0) { \
            __output = calloc(1, (size)); \
            if (!(__output)) { \
                log_error((c)->log, "[%s] out of memory.", __func__); \
            } \
        } else { \
            __output = NULL; \
        } \
        __output; \
    })

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
  struct wl_resource* surf_res, *buf_res, 
    *xdg_surf_res, *xdg_toplevel_res; 
  RnTexture tex; 
  struct wl_list frame_cbs;
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
} vt_surface_t;

typedef bool (*backend_implement_func_t)(vt_compositor_t* comp);

typedef struct {
  bool (*init)(vt_backend_t* backend);
  bool (*implement)(vt_compositor_t* comp);
  bool (*handle_event)(vt_backend_t* backend);
  bool (*suspend)(vt_backend_t* backend);
  bool (*resume)(vt_backend_t* backend);
  bool (*handle_frame)(vt_backend_t* backend, vt_output_t* output);
  bool (*handle_surface_frame)(vt_backend_t* backend, vt_surface_t* surf);
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
  void (*begin_frame)(vt_renderer_t* r, vt_output_t* output);
  void (*draw_surface)(vt_renderer_t* r, vt_surface_t *surface, int32_t x, int32_t y);
  void (*end_frame)(vt_renderer_t* r, vt_output_t* output);
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

  void* user_data;

  struct wl_event_source* repaint_source;
}; 

typedef struct {
  struct wl_resource *res;
  struct wl_list link;
} vt_frame_cb;

typedef struct {
  struct wl_event_source *timer; 
  uint32_t refresh_interval;
  bool armed;
  uint32_t last_flip_time; 
} vt_frame_clock;

struct vt_compositor_t {
  wl_state_t  wl;
  struct libinput* input;

  vt_backend_t* backend;
  log_state_t log;

  struct wl_list surfaces; 

  bool running, suspended;
  bool sent_frame_cbs;

  uint32_t n_virtual_outputs;

  vt_frame_clock frame_clock;

  const char* _cmd_line_backend_path;
};


bool comp_init(vt_compositor_t *c, int argc, char** argv);

void comp_run(vt_compositor_t *c);

bool comp_terminate(vt_compositor_t *c);

void comp_send_frame_callbacks_for_output(vt_compositor_t *c, vt_output_t* output, uint32_t t);

void comp_send_frame_callbacks(vt_compositor_t *c, vt_output_t* output, uint32_t t);

void comp_schedule_repaint(vt_compositor_t *c, vt_output_t* output);

uint32_t comp_get_time_msec(void);

