/*
 * Copyright (c) 2026 Luca Machiedo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "renderer.h"
#include "config.h"

#if VT_HAS_OPENGL
#include "gl/egl_gl.h"
#endif

#if VT_HAS_VULKAN
#include "vk/vulkan.h"
#endif

void vt_renderer_implement(struct vt_renderer_t       *renderer,
                           enum vt_rendering_backend_t backend) {
#if VT_HAS_OPENGL
  if (backend == VT_RENDERING_BACKEND_EGL_OPENGL) {
    renderer->impl = (struct vt_renderer_interface_t){
        .init = renderer_init_egl,
        .is_handle_renderable = renderer_is_handle_renderable_egl,
        .query_dmabuf_formats = renderer_query_dmabuf_formats_egl,
        .query_dmabuf_formats_with_renderer =
            renderer_query_dmabuf_formats_with_renderer_egl,
        .setup_renderable_output = renderer_setup_renderable_output_egl,
        .resize_renderable_output = renderer_resize_renderable_output_egl,
        .destroy_renderable_output = renderer_destroy_renderable_output_egl,
        .import_buffer = renderer_import_buffer_egl,
        .drop_context = renderer_drop_context_egl,
        .supports_explicit_sync = renderer_supports_explicit_sync_egl,
        .set_vsync = renderer_set_vsync_egl,
        .composite_pass = renderer_composite_pass_egl,
        .stencil_damage_pass = renderer_stencil_damage_pass_egl,
        .set_clear_color = renderer_set_clear_color_egl,
        .clear_color_buffer = renderer_clear_color_buffer_egl,
        .begin_frame = renderer_begin_frame_egl,
        .begin_scene = renderer_begin_scene_egl,
        .draw_buffer = renderer_draw_buffer_egl,
        .draw_surface = renderer_draw_surface_egl,
        .draw_surface_simple = renderer_draw_surface_simple_egl,
        .draw_rect = renderer_draw_rect_egl,
        .end_frame = renderer_end_frame_egl,
        .end_scene = renderer_end_scene_egl,
        .destroy = renderer_destroy_egl};
  }
#endif
#if VT_HAS_VULKAN
  if (backend == VT_RENDERING_BACKEND_VULKAN) {
    renderer->impl = (struct vt_renderer_interface_t){
        .init = renderer_init_vk,
        .is_handle_renderable = renderer_is_handle_renderable_vk,
        .query_dmabuf_formats = renderer_query_dmabuf_formats_vk,
        .query_dmabuf_formats_with_renderer =
            renderer_query_dmabuf_formats_with_renderer_vk,
        .setup_renderable_output = renderer_setup_renderable_output_vk,
        .resize_renderable_output = renderer_resize_renderable_output_vk,
        .destroy_renderable_output = renderer_destroy_renderable_output_vk,
        .import_buffer = renderer_import_buffer_vk,
        .is_dmabuf_importable = renderer_is_dmabuf_importable_vk,
        .drop_context = renderer_drop_context_vk,
        .supports_explicit_sync = renderer_supports_explicit_sync_vk,
        .set_vsync = renderer_set_vsync_vk,
        .composite_pass = renderer_composite_pass_vk,
        .stencil_damage_pass = renderer_stencil_damage_pass_vk,
        .set_clear_color = renderer_set_clear_color_vk,
        .clear_color_buffer = renderer_clear_color_buffer_vk,
        .begin_frame = renderer_begin_frame_vk,
        .begin_scene = renderer_begin_scene_vk,
        .draw_buffer = renderer_draw_buffer_vk,
        .draw_surface = renderer_draw_surface_vk,
        .draw_surface_simple = renderer_draw_surface_simple_vk,
        .draw_rect = renderer_draw_rect_vk,
        .end_frame = renderer_end_frame_vk,
        .end_scene = renderer_end_scene_vk,
        .destroy = renderer_destroy_vk};
  }
#elif !VT_HAS_OPENGL
#error                                                                         \
    "No renderer is enabled."
#endif
}
