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

#include "../core/core_types.h"
#include "../core/scene.h"

struct vt_subsurface_t {
  struct wl_resource *resource;

  struct vt_surface_t *surf;
  struct vt_surface_t *parent;

  bool synchronized;

  struct wl_list link;          /* pending bottom to top */
  struct wl_list stacking_link; /* applied bottom to top */
  uint64_t       stacking_id;
  bool           pending_below_parent, below_parent;

  struct vt_scene_node_t *scene_node;
};

bool vt_proto_wl_subcompositor_init(struct vt_compositor_t *comp);

struct vt_subsurface_stack_entry_t {
  uint64_t id;
  bool     below_parent;
};

struct vt_content_update_t;
bool vt_subsurface_capture_stacking(struct vt_content_update_t *cu);
void vt_subsurface_apply_stacking(struct vt_content_update_t *cu);
void vt_subsurface_surface_destroy(struct vt_surface_t *surf);
