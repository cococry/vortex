#include "renderer.h" 


#include "gl/egl_gl46.h"

void 
vt_renderer_implement(struct vt_renderer_t* renderer, enum vt_rendering_backend_t backend) {
  if(backend == VT_RENDERING_BACKEND_EGL_OPENGL) {
    renderer->impl = (struct vt_renderer_interface_t){
      .init = renderer_init_egl, 
      .setup_renderable_output = renderer_setup_renderable_output_egl, 
      .resize_renderable_output = renderer_resize_renderable_output_egl,
      .destroy_renderable_output = renderer_destroy_renderable_output_egl,
      .import_buffer = renderer_import_buffer_egl,
      .destroy_surface_texture = renderer_destroy_surface_texture_egl,
      .drop_context = renderer_drop_context_egl, 
      .set_vsync = renderer_set_vsync_egl,
      .composite_pass = renderer_composite_pass_egl,
      .stencil_damage_pass = renderer_stencil_damage_pass_egl,
      .set_clear_color = renderer_set_clear_color_egl,
      .begin_frame = renderer_begin_frame_egl,
      .begin_scene = renderer_begin_scene_egl,
      .draw_surface = renderer_draw_surface_egl,
      .draw_rect = renderer_draw_rect_egl,
      .end_frame = renderer_end_frame_egl,
      .end_scene = renderer_end_scene_egl,
      .destroy = renderer_destroy_egl
    };
  }
}

