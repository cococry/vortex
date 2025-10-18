#pragma once

#include "../backend.h"


bool renderer_init_egl(vt_backend_t* backend, vt_renderer_t *r, void* native_handle);

bool renderer_setup_renderable_output_egl(vt_renderer_t *r, vt_output_t* output);
  
bool renderer_resize_renderable_output_egl(vt_renderer_t* r, vt_output_t* output, int32_t w, int32_t h);
    
bool renderer_destroy_renderable_output_egl(vt_renderer_t *r, vt_output_t* output);
    
bool renderer_import_buffer_egl(vt_renderer_t *r, vt_surface_t *surf,
    struct wl_resource *buffer_resource);

bool renderer_drop_context_egl(vt_renderer_t* r);
  
void renderer_set_vsync(vt_renderer_t* r, bool vsync);
  
void renderer_set_scissor(vt_renderer_t* r, vt_output_t* output, int32_t x, int32_t y, int32_t w, int32_t h);

void renderer_begin_frame_egl(vt_renderer_t *r, vt_output_t *output);

void renderer_begin_scene_egl(vt_renderer_t *r, vt_output_t *output);
  
void renderer_init_surface_egl(vt_renderer_t* r, vt_surface_t *surface);

void renderer_draw_surface_egl(vt_renderer_t *r, vt_surface_t *surface, int32_t x, int32_t y);

void renderer_end_scene_egl(vt_renderer_t *r, vt_output_t *output);

void renderer_end_frame_egl(vt_renderer_t *r, vt_output_t *output,  const pixman_box32_t* damaged, int32_t n_damaged);

bool renderer_destroy_egl(vt_renderer_t *r);
