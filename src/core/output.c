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

#include "output.h"

#include "core/scene.h"
#include "core_types.h"
#include "src/core/buffer.h"
#include <drm/drm_fourcc.h>

static int32_t output_x_ptr = 0;

struct vt_output_t *vt_output_init(struct vt_backend_t               *backend,
                                   struct vt_output_implementation_t *impl) {
  assert(backend && backend->comp && impl);

  struct vt_output_t *output = VT_ALLOC(backend->comp, sizeof(*output));
  if (!output)
    return NULL;

  output->backend = backend;
  output->impl = impl;

  return output;
}

void vt_output_fini(struct vt_output_t *output) {
  assert(output && output->backend && output->backend->comp);

  output->background.buf_fallback = NULL;
  vt_scene_node_destroy(output->backend->comp, output->background.node);

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

bool _init_output_bg(struct vt_output_t* output) {
  assert(output && output->backend && output->backend->comp && 
      output->backend->impl.allocate_buffer && output->backend->impl.clear_buffer);

  struct vt_scene_node_t *bg_node = NULL;
  struct vt_buffer_t     *fallback_buf = NULL;

  /* initial reference owned by the allocation */
  fallback_buf = output->backend->impl.allocate_buffer(
      output->backend, output->width, output->height, DRM_FORMAT_XRGB8888);

  if(!fallback_buf)
    goto fail;

  if (!output->backend->impl.clear_buffer(output->backend, fallback_buf, 0x0))
    goto fail;

  bg_node = vt_scene_node_create_buffer(output->backend->comp, fallback_buf);

  if (!bg_node)
    goto fail;

  output->background.node = bg_node;

  return true;
fail:
  if(bg_node)
    vt_scene_node_destroy(output->backend->comp, bg_node);
  if(fallback_buf) {
    /* destroys the buffer */
    vt_buffer_unref(&fallback_buf);
  }

  return false;
}

bool vt_output_init_geom(struct vt_output_t* output, uint32_t width, uint32_t height) {
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
  assert(output && surface);

  struct vt_presented_surface_t *entry = calloc(1, sizeof(*entry));
  if (!entry)
    return false;

  entry->surf = surface;

  wl_list_insert(output->presented_surfaces.prev, &entry->link);

  return true;
}
