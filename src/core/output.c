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

#include "src/core/profiling.h"
#include "output.h"

#include "core/compositor.h"
#include "core/scene.h"
#include "core_types.h"
#include "src/core/buffer.h"
#include <drm/drm_fourcc.h>

static int32_t output_x_ptr = 0;

struct vt_output_t *vt_output_init(struct vt_backend_t               *backend,
                                   struct vt_output_implementation_t *impl) {
  VT_PROFILE_SCOPE();
  assert(backend && backend->comp && impl);

  struct vt_output_t *output = VT_ALLOC(backend->comp, sizeof(*output));
  if (!output)
    return NULL;

  wl_list_init(&output->link_global);
  wl_list_init(&output->link_local);

  output->backend = backend;
  output->impl = impl;

  return output;
}

void vt_output_fini(struct vt_output_t *output) {
  VT_PROFILE_SCOPE();
  assert(output && output->backend && output->backend->comp);

  if (output->background.owner) {
    output->background.owner->output = NULL;
    wl_resource_destroy(output->background.owner->resource);
    output->background.owner = NULL;
  }
  vt_buffer_use_unref(&output->background.use_fallback);
  if (output->background.node) {
    vt_scene_node_destroy(output->backend->comp, output->background.node);
    output->background.node = NULL;
  }

  output->user_data = NULL;

  free(output->physical.make);
  free(output->physical.model);
  free(output->physical.name);
  free(output->physical.serial_number);

  struct vt_output_mode_t *mode, *tmp_mode;
  wl_list_for_each_safe(mode, tmp_mode, &output->physical.modes, link) {
    wl_list_remove(&mode->link);
    free(mode);
  }

  pixman_region32_fini(&output->damage);

  wl_list_remove(&output->link_global);
  wl_list_remove(&output->link_local);
}

bool _init_output_bg(struct vt_output_t *output) {
  VT_PROFILE_SCOPE();
  assert(output && output->backend && output->backend->comp &&
         output->backend->impl.allocate_buffer &&
         output->backend->impl.clear_buffer);

  struct vt_scene_node_t *bg_node = NULL;
  struct vt_buffer_t     *fallback_buf = NULL;
  struct vt_buffer_use_t *fallback_use = NULL;

  /* initial reference owned by the allocation */
  fallback_buf = output->backend->impl.allocate_buffer(
      output->backend, output->width, output->height, DRM_FORMAT_XRGB8888);

  if (!fallback_buf)
    goto fail;

  if (!output->backend->impl.clear_buffer(output->backend, fallback_buf,
                                          0x181818))
    goto fail;

  fallback_use = vt_buffer_use_create_take(output->backend->comp, output,
                                           &fallback_buf, NULL, NULL);
  if (!fallback_use)
    goto fail;

  bg_node = vt_scene_node_create_buffer(output->backend->comp, fallback_use);

  if (!bg_node)
    goto fail;

  output->background.node = bg_node;
  output->background.use_fallback = fallback_use;

  return true;
fail:
  if (bg_node)
    vt_scene_node_destroy(output->backend->comp, bg_node);
  if (fallback_buf) {
    /* destroys the buffer */
    vt_buffer_unref(&fallback_buf);
  }

  return false;
}

bool vt_output_init_geom(struct vt_output_t *output, uint32_t width,
                         uint32_t height) {
  VT_PROFILE_SCOPE();
  assert(output && output->backend && output->backend->comp);

  output->width = width;
  output->height = height;

  output->x = output_x_ptr;
  output->y = 0;

  output_x_ptr += width;

  if (!_init_output_bg(output))
    return false;

  return true;
}

bool vt_output_track_presented_surface(struct vt_output_t  *output,
                                       struct vt_surface_t *surface) {
  VT_PROFILE_SCOPE();
  assert(output && surface);

  struct vt_presented_surface_t *entry = calloc(1, sizeof(*entry));
  if (!entry)
    return false;

  entry->surf = surface;

  wl_list_insert(output->presented_surfaces.prev, &entry->link);

  return true;
}

void vt_output_set_background(struct vt_output_t     *output,
                              struct vt_buffer_use_t *use) {
  VT_PROFILE_SCOPE();
  assert(output && output->background.node && output->background.use_fallback);

  struct vt_buffer_use_t *next =
      vt_buffer_use_ref(use ? use : output->background.use_fallback);
  vt_buffer_use_unref(&output->background.node->use);
  output->background.node->use = next;
  vt_scene_node_mark_geometry_dirty(output->background.node);
  vt_output_damage_whole(output);
}

bool vt_output_resize_background(struct vt_output_t *output) {
  VT_PROFILE_SCOPE();
  struct vt_buffer_use_t *old = output->background.use_fallback;
  if (!old ||
      (old->buf->width == output->width && old->buf->height == output->height))
    return true;

  struct vt_backend_t *backend = output->backend;
  struct vt_buffer_t  *buf = backend->impl.allocate_buffer(
      backend, output->width, output->height, DRM_FORMAT_XRGB8888);
  if (!buf)
    return false;
  if (!backend->impl.clear_buffer(backend, buf, 0x181818)) {
    vt_buffer_unref(&buf);
    return false;
  }
  struct vt_buffer_use_t *use =
      vt_buffer_use_create_take(backend->comp, output, &buf, NULL, NULL);
  if (!use) {
    vt_buffer_unref(&buf);
    return false;
  }
  output->background.use_fallback = use;
  if (output->background.node->use == old)
    vt_output_set_background(output, NULL);
  vt_buffer_use_unref(&old);
  output->needs_damage_rebuild = true;
  return true;
}

void vt_output_damage_whole(struct vt_output_t *output) {
  VT_PROFILE_SCOPE();
  assert(output && output->backend && output->backend->comp);

  pixman_region32_union_rect(&output->damage, &output->damage, 0, 0,
                             output->width, output->height);
  output->needs_damage_rebuild = true;

  vt_comp_schedule_repaint(output->backend->comp, output);
}
