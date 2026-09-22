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

#include "core_types.h"

enum vt_scene_node_type_t {
  VT_SCENE_NODE_ROOT = 0,
  VT_SCENE_NODE_SURFACE,
  VT_SCENE_NODE_RECT,
  VT_SCENE_NODE_INVISIBLE_GEOMETRY
};

struct vt_rect_t {
    uint32_t width, height;
    int32_t x, y;
};

struct vt_scene_node_t {

  struct vt_scene_node_t  *parent;
  struct vt_scene_node_t **childs;
  uint32_t                 child_count;
  uint32_t                 _child_cap;

  uint32_t color;

  /* position relative to parent */
  int32_t x, y;
  uint32_t rect_w, rect_h;

  /* resolved global bounds */
  struct vt_rect_t cached_bounds;

  bool geom_dirty;

  struct vt_surface_t *surf;

  enum vt_scene_node_type_t type;
};

typedef bool (*vt_scene_node_filter_func_t)(struct vt_scene_node_t *node);

struct vt_scene_node_t *vt_scene_node_create(struct vt_compositor_t *c,
                                             struct vt_surface_t    *surf);

bool vt_scene_node_destroy(struct vt_compositor_t *c,
                           struct vt_scene_node_t *node);

bool vt_scene_node_damage_whole(struct vt_compositor_t *comp,
                                struct vt_scene_node_t *node);

struct vt_scene_node_t *vt_scene_node_create_rect(struct vt_compositor_t *c,
                                                  float x, float y, float w,
                                                  float h, uint32_t color);

struct vt_scene_node_t *
vt_scene_node_create_rect_invisible(struct vt_compositor_t *c, float x, float y,
                                    float w, float h);
struct vt_scene_node_t *
vt_scene_node_create_container(struct vt_compositor_t *c);

bool vt_scene_node_reparent(struct vt_compositor_t *c,
                            struct vt_scene_node_t *node,
                            struct vt_scene_node_t *new_parent);

bool vt_scene_node_add_child(struct vt_compositor_t *c,
                             struct vt_scene_node_t *node,
                             struct vt_scene_node_t *child);
bool vt_scene_node_remove_child(struct vt_scene_node_t *parent,
                                struct vt_scene_node_t *child);
struct vt_renderer_t;
struct vt_output_t;
void vt_scene_node_render(struct vt_renderer_t   *renderer,
                          struct vt_output_t     *output,
                          struct vt_scene_node_t *node, bool care_for_damage,
                          vt_scene_node_filter_func_t filter);

void vt_scene_render(struct vt_renderer_t *renderer, struct vt_output_t *output,
                     struct vt_scene_node_t *root);

void vt_scene_node_set_position(struct vt_scene_node_t *node, int32_t x,
                                int32_t y);

void vt_scene_node_mark_geometry_dirty(struct vt_scene_node_t *node); 

void vt_scene_node_update_global_bounds(struct vt_scene_node_t *node); 

struct vt_rect_t* vt_scene_node_get_global_bounds(struct vt_scene_node_t *node);

struct vt_output_t * vt_scene_node_primary_output(struct vt_compositor_t* comp, struct vt_scene_node_t *node);
