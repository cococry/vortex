#pragma once

#include "../core/backend.h"

typedef enum {
  VT_RENDERING_BACKEND_EGL_OPENGL = 0,
} vt_rendering_backend_t;

typedef struct {
  bool (*init)(struct vt_backend_t* backend, struct vt_renderer_t* r, void* native_handle);
  bool (*setup_renderable_output)(struct vt_renderer_t* r, struct vt_output_t* output);
  bool (*resize_renderable_output)(struct vt_renderer_t* r, struct vt_output_t* output, int32_t w, int32_t h);
  bool (*destroy_renderable_output)(struct vt_renderer_t* r, struct vt_output_t* output);
  bool (*import_buffer)(struct vt_renderer_t* r, struct vt_surface_t* surf,
      struct wl_resource* buffer_resource);
  bool (*destroy_surface_texture)(struct vt_renderer_t* r, struct vt_surface_t* surf);
  bool (*drop_context)(struct vt_renderer_t* r);
  void (*set_vsync)(struct vt_renderer_t* r, bool vsync);
  void (*set_clear_color)(struct vt_renderer_t* r, struct vt_output_t* output, uint32_t col);
  void (*stencil_damage_pass)(struct vt_renderer_t* r, struct vt_output_t* output); 
  void (*composite_pass)(struct vt_renderer_t* r, struct vt_output_t* output); 
  void (*begin_scene)(struct vt_renderer_t* r, struct vt_output_t* output);
  void (*begin_frame)(struct vt_renderer_t* r, struct vt_output_t* output);
  void (*draw_surface)(struct vt_renderer_t* r, struct vt_surface_t* surface, float x, float y);
  void (*draw_rect)(struct vt_renderer_t* r, float x, float y, float w, float h, uint32_t col);
  void (*end_scene)(struct vt_renderer_t* r, struct vt_output_t* output);
  void (*end_frame)(struct vt_renderer_t* r, struct vt_output_t* output, const pixman_box32_t* damaged, int32_t n_damaged);
  bool (*destroy)(struct vt_renderer_t* r);
} vt_renderer_interface_t;

struct vt_renderer_t {
  vt_renderer_interface_t impl;

  vt_rendering_backend_t rendering_backend;
  struct vt_compositor_t* comp;
  void *user_data;

  struct vt_backend_t* backend;
  
  uint32_t _desired_render_buffer_format;
};

void vt_renderer_implement(struct vt_renderer_t* renderer, vt_rendering_backend_t backend);
