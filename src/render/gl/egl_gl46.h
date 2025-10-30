#pragma once


#include "../renderer.h"
#include "../../core/core_types.h"

bool renderer_init_egl(struct vt_backend_t* backend, struct vt_renderer_t* r, void* native_handle);

bool renderer_is_handle_renderable_egl(struct vt_renderer_t* renderer, void* native_handle);

bool renderer_query_dmabuf_formats_egl(struct vt_compositor_t* comp, void* native_handle, struct wl_array* formats);

bool renderer_query_dmabuf_formats_with_renderer_egl(struct vt_renderer_t* renderer, struct wl_array* formats);

bool renderer_setup_renderable_output_egl(struct vt_renderer_t* r, struct vt_output_t* output);
  
bool renderer_resize_renderable_output_egl(struct vt_renderer_t* r, struct vt_output_t* output, int32_t w, int32_t h);
    
bool renderer_destroy_renderable_output_egl(struct vt_renderer_t* r, struct vt_output_t* output);
    
bool renderer_import_buffer_egl(struct vt_renderer_t* r, struct vt_surface_t* surf,
    struct wl_resource *buffer_resource);

bool renderer_destroy_surface_texture_egl(struct vt_renderer_t* r, struct vt_surface_t* surf);

bool renderer_drop_context_egl(struct vt_renderer_t* r);
  
void renderer_set_vsync_egl(struct vt_renderer_t* r, bool vsync);
  
void renderer_set_clear_color_egl(struct vt_renderer_t* r, struct vt_output_t* output, uint32_t col);
  
void renderer_stencil_damage_pass_egl(struct vt_renderer_t* r, struct vt_output_t* output); 

void renderer_composite_pass_egl(struct vt_renderer_t* r, struct vt_output_t* output); 

void renderer_begin_frame_egl(struct vt_renderer_t* r, struct vt_output_t* output);

void renderer_begin_scene_egl(struct vt_renderer_t* r, struct vt_output_t* output);
  
void renderer_init_surface_egl(struct vt_renderer_t* r, struct vt_surface_t* surf);

void renderer_draw_surface_egl(struct vt_renderer_t* r, struct vt_surface_t* surface, float x, float y);
  
void renderer_draw_rect_egl(struct vt_renderer_t* r, float x, float y, float w, float h, uint32_t col);

void renderer_end_scene_egl(struct vt_renderer_t* r, struct vt_output_t* output);

void renderer_end_frame_egl(struct vt_renderer_t* r, struct vt_output_t* output,  const pixman_box32_t* damaged, int32_t n_damaged);

bool renderer_destroy_egl(struct vt_renderer_t* r);
