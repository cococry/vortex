#pragma once

#include "../core/core_types.h"
#include <wayland-util.h>

enum vt_rendering_backend_t {
  VT_RENDERING_BACKEND_EGL_OPENGL = 0,
};

struct vt_renderer_interface_t {
  bool (*init)(struct vt_backend_t* backend, struct vt_renderer_t* r, void* native_handle);
  bool (*is_handle_renderable)(struct vt_renderer_t* renderer, void* native_handle);
  bool (*query_dmabuf_formats)(struct vt_compositor_t* comp, void* native_handle, struct wl_array* formats);
  bool (*query_dmabuf_formats_with_renderer)(struct vt_renderer_t* renderer, struct wl_array* formats);
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
  void (*draw_surface)(struct vt_renderer_t* r, struct vt_output_t* output, struct vt_surface_t* surface, float x, float y);
  void (*draw_image)(struct vt_renderer_t* r, struct vt_output_t* output, uint32_t tex_id, uint32_t width, uint32_t height, float x, float y);
  void (*draw_rect)(struct vt_renderer_t* r, float x, float y, float w, float h, uint32_t col);
  void (*end_scene)(struct vt_renderer_t* r, struct vt_output_t* output);
  void (*end_frame)(struct vt_renderer_t* r, struct vt_output_t* output, const pixman_box32_t* damaged, int32_t n_damaged);
  bool (*destroy)(struct vt_renderer_t* r);
};

struct vt_renderer_t {
  struct vt_renderer_interface_t impl;

  enum vt_rendering_backend_t rendering_backend;
  struct vt_compositor_t* comp;
  void *user_data;

  struct vt_backend_t* backend;
  
  uint32_t _desired_render_buffer_format;
};

void vt_renderer_implement(struct vt_renderer_t* renderer, enum vt_rendering_backend_t backend);
