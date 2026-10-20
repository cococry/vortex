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

#include "core_types.h"
#include "src/core/buffer.h"

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

bool _init_output_bg(struct vt_compositor_t* c, struct vt_output_background_t* bg, uint32_t width, uint32_t height) {
  assert(bg);
  if(!bg) return false;

  struct vt_scene_node_t* bg_node = vt_scene_node_create(c, NULL);

  if (!bg_node)
    return false;

  bg_node->type = VT_SCENE_NODE_BACKGROUND;

  bg->node = bg_node;

  struct vt_buffer_t* bg_buf = vt_buffer_create(c, width, height, NULL);

  if(!bg_buf) return false;

  bg->buf = bg_buf;

  return true;
}

bool vt_output_init_geom(struct vt_output_t* output, uint32_t width, uint32_t height) {
  assert(output && output->backend && output->backend->comp);

  output->width = width;
  output->height = height;

  output->x = output_x_ptr;
  output->y = 0;

  output_x_ptr += width;

  if (!_init_output_bg(output->backend->comp, output->bg, width, height))
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
