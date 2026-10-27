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

#pragma once

#include "../../core/core_types.h"
#include "../renderer.h"
#include "src/core/surface.h"
#include "subprojects/runara/include/runara/runara.h"

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

struct vt_vk_texture_t {
  uint32_t width, height;
  VkImage image;
  VkFormat fmt;
};

struct vt_vk_buffer_t {
  struct vt_vk_texture_t tex;
  struct rn_image_t       rn_img;
  bool                    rn_img_registered;
  struct vt_vk_texture_t  vk_img;
  struct vt_renderer_t   *renderer;
};

bool renderer_init_vk(struct vt_backend_t *backend, struct vt_renderer_t *r,
                       void *native_handle);

bool renderer_is_handle_renderable_vk(struct vt_renderer_t *renderer,
                                       void                 *native_handle);

bool renderer_query_dmabuf_formats_vk(struct vt_compositor_t *comp,
                                       void                   *native_handle,
                                       struct wl_array        *formats);

bool renderer_query_dmabuf_formats_with_renderer_vk(
    struct vt_renderer_t *renderer, struct wl_array *formats);

bool renderer_setup_renderable_output_vk(struct vt_renderer_t *r,
                                          struct vt_output_t   *output);

bool renderer_resize_renderable_output_vk(struct vt_renderer_t *r,
                                           struct vt_output_t   *output,
                                           int32_t w, int32_t h);

bool renderer_destroy_renderable_output_vk(struct vt_renderer_t *r,
                                            struct vt_output_t   *output);

bool renderer_import_buffer_vk(struct vt_renderer_t    *r,
                                struct vt_buffer_t      *buf,
                                const pixman_region32_t *damage);

bool renderer_drop_context_vk(struct vt_renderer_t *r);

bool renderer_supports_explicit_sync_vk(struct vt_renderer_t *r);

void renderer_set_vsync_vk(struct vt_renderer_t *r, bool vsync);

void renderer_set_clear_color_vk(struct vt_renderer_t *r,
                                  struct vt_output_t *output, uint32_t col);

void renderer_clear_color_buffer_vk(struct vt_renderer_t *r,
                                     struct vt_output_t   *output);

void renderer_stencil_damage_pass_vk(struct vt_renderer_t *r,
                                      struct vt_output_t   *output);

void renderer_composite_pass_vk(struct vt_renderer_t *r,
                                 struct vt_output_t   *output);

void renderer_begin_frame_vk(struct vt_renderer_t *r,
                              struct vt_output_t   *output);

void renderer_begin_scene_vk(struct vt_renderer_t *r,
                              struct vt_output_t   *output);

void renderer_draw_buffer_vk(struct vt_renderer_t   *r,
                              struct vt_output_t     *output,
                              struct vt_buffer_use_t *use,
                              struct vt_box_t        *src_box,
                              struct vt_box_t        *dst_box);

void renderer_draw_surface_vk(struct vt_renderer_t *r,
                               struct vt_output_t   *output,
                               struct vt_surface_t  *surface,
                               struct vt_box_t      *src_box,
                               struct vt_box_t      *dst_box);

void renderer_draw_surface_simple_vk(struct vt_renderer_t *r,
                                      struct vt_output_t   *output,
                                      struct vt_surface_t *surface, float x,
                                      float y);

void renderer_draw_rect_vk(struct vt_renderer_t *r, float x, float y, float w,
                            float h, uint32_t col);

void renderer_end_scene_vk(struct vt_renderer_t *r,
                            struct vt_output_t   *output);

void renderer_end_frame_vk(struct vt_renderer_t *r, struct vt_output_t *output,
                            const pixman_box32_t *damaged, int32_t n_damaged);

bool renderer_destroy_vk(struct vt_renderer_t *r);


