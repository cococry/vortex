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

#include "scene.h"
#include "pixman.h"
#include "src/core/compositor.h"
#include "src/core/core_types.h"
#include "src/core/surface.h"
#include "src/core/util.h"
#include "src/render/renderer.h"
#include <wayland-util.h>

#define _SCENE_CHILD_CAP_INIT 4

#define _SUBSYS_NAME "SCENE"

static bool _box_intersect_box(float x1, float y1, float w1, float h1, float x2,
                               float y2, float w2, float h2);

static void sceneprintindent(int indent);

static struct vt_output_layer_state_t *
_scene_push_layer(struct vt_scene_t *scene);

static bool _scene_node_is_layer_candidate(struct vt_scene_node_t *node);
static void _scene_node_accumulate_layers(struct vt_scene_t      *scene,
                                          struct vt_output_t     *output,
                                          struct vt_scene_node_t *node,
                                          int32_t parent_x, int32_t parent_y);

static void _scene_accumulate_layers(struct vt_scene_t  *scene,
                                     struct vt_output_t *output);
static void _scene_render_layers(struct vt_scene_t  *scene,
                                 struct vt_output_t *output);
static void _composite_pass(struct vt_scene_t  *scene,
                            struct vt_output_t *output);
static void _scene_node_get_size(struct vt_scene_node_t *node, uint32_t *o_w,
                                 uint32_t *o_h);

static void _scene_node_update_surface_feedback(struct vt_scene_node_t *node);

static struct vt_output_layer_state_t *
_scene_push_layer(struct vt_scene_t *scene) {
  assert(scene);

  if (scene->n_layers == scene->layers_cap) {
    size_t new_cap = scene->layers_cap ? scene->layers_cap * 2 : 16;

    struct vt_output_layer_state_t *layers =
        realloc(scene->layers, new_cap * sizeof(*layers));
    if (!layers)
      return NULL;

    scene->layers = layers;
    scene->layers_cap = new_cap;
  }

  struct vt_output_layer_state_t *layer = &scene->layers[scene->n_layers++];

  memset(layer, 0, sizeof(*layer));
  return layer;
}

static bool _scene_node_is_layer_candidate(struct vt_scene_node_t *node) {
  if (node->type == VT_SCENE_NODE_INVISIBLE_GEOMETRY)
    return false;

  if (!node->surf)
    return false;

  if (!vt_surface_effectively_mapped(node->surf))
    return false;

  if (node->surf->role.impl &&
      node->surf->role.impl->type == VT_SURFACE_ROLE_CURSOR)
    return false;

  return true;
}

static bool _scene_node_on_output(struct vt_scene_node_t *node,
                                  struct vt_output_t     *output) {
  assert(node && output);

  struct vt_box_t *box = vt_scene_node_get_global_bounds(node);
  if (!box) {
    VT_TRACE(output->backend->comp->log,
             "SCENE: node_on_output: node=%p has no global bounds", node);
    return false;
  }

  int32_t node_right = box->x + box->width;
  int32_t node_bottom = box->y + box->height;

  int32_t output_right = output->x + output->width;
  int32_t output_bottom = output->y + output->height;

  if (node_right <= output->x) {
    return false;
  }

  if (box->x >= output_right) {
    return false;
  }

  if (node_bottom <= output->y) {
    return false;
  }

  if (box->y >= output_bottom) {
    return false;
  }

  return true;
}

static void _scene_node_accumulate_layers(struct vt_scene_t      *scene,
                                          struct vt_output_t     *output,
                                          struct vt_scene_node_t *node,
                                          int32_t parent_x, int32_t parent_y) {
  if (!scene || !output || !node)
    return;

  int32_t x = parent_x + node->x;
  int32_t y = parent_y + node->y;

  if (_scene_node_is_layer_candidate(node)) {
    if (!_scene_node_on_output(node, output))
      goto children;

    struct vt_box_t *rect = vt_scene_node_get_global_bounds(node);
    if (!rect)
      goto children;

    int32_t left = rect->x;
    int32_t top = rect->y;
    int32_t right = rect->x + rect->width;
    int32_t bottom = rect->y + rect->height;

    int32_t output_left = output->x;
    int32_t output_top = output->y;
    int32_t output_right = output->x + output->width;
    int32_t output_bottom = output->y + output->height;

    int32_t clipped_left = VT_MAX(left, output_left);
    int32_t clipped_top = VT_MAX(top, output_top);
    int32_t clipped_right = VT_MIN(right, output_right);
    int32_t clipped_bottom = VT_MIN(bottom, output_bottom);

    if (clipped_right <= clipped_left || clipped_bottom <= clipped_top)
      goto children;

    struct vt_output_layer_state_t *layer = _scene_push_layer(scene);
    if (!layer)
      return;

    layer->surface = node->surf;

    layer->src = (struct vt_box_t){
        .x = clipped_left - left,
        .y = clipped_top - top,
        .width = clipped_right - clipped_left,
        .height = clipped_bottom - clipped_top,
    };

    layer->dst = (struct vt_box_t){
        .x = clipped_left - output->x,
        .y = clipped_top - output->y,
        .width = clipped_right - clipped_left,
        .height = clipped_bottom - clipped_top,
    };
  }

children:
  for (uint32_t i = 0; i < node->child_count; i++) {
    _scene_node_accumulate_layers(scene, output, node->childs[i], x, y);
  }
}

static void _scene_accumulate_layers(struct vt_scene_t  *scene,
                                     struct vt_output_t *output) {
  assert(scene && scene->root);

  scene->n_layers = 0;

  _scene_node_accumulate_layers(scene, output, scene->root, 0, 0);
}

static void _scene_render_layers(struct vt_scene_t  *scene,
                                 struct vt_output_t *output) {
  assert(scene);
  assert(scene->renderer);

  struct vt_renderer_t *renderer = scene->renderer;

  for (size_t i = 0; i < scene->n_layers; i++) {
    struct vt_output_layer_state_t *layer = &scene->layers[i];

    /*if (layer->accepted) {
      VT_TRACE(
          renderer->comp->log,
          "Surface=%p accepted as direct-scanout layer; Will not be rendered.",
          layer->surface);
      continue;
    }*/

    renderer->impl.draw_surface(renderer, output, layer->surface, &layer->src,
                                &layer->dst);
  }
}

static void _scene_render_cursor(struct vt_scene_t  *scene,
                                 struct vt_output_t *output) {
  assert(scene && scene->renderer && output);

  struct vt_renderer_t *r = scene->renderer;

  struct vt_seat_t    *seat = r->comp->seat;
  struct vt_surface_t *cursor = seat->cursor.surf;

  if (cursor && cursor->mapped) {

    int32_t cursor_x = seat->pointer_x - seat->cursor.hotspot_x;
    int32_t cursor_y = seat->pointer_y - seat->cursor.hotspot_y;

    int32_t cursor_right = cursor_x + cursor->applied.width;
    int32_t cursor_bottom = cursor_y + cursor->applied.height;

    int32_t output_right = output->x + output->width;
    int32_t output_bottom = output->y + output->height;

    if (cursor && cursor->mapped && cursor_right > output->x &&
        cursor_x < output_right && cursor_bottom > output->y &&
        cursor_y < output_bottom) {
      r->impl.draw_surface_simple(r, output, cursor, cursor_x - output->x,
                                  cursor_y - output->y);
    }
  }
}

static void _composite_pass(struct vt_scene_t  *scene,
                            struct vt_output_t *output) {
  assert(scene && scene->renderer && output);

  struct vt_renderer_t *r = scene->renderer;

  r->impl.composite_pass(r, output);

  r->impl.begin_scene(r, output);

  r->impl.set_clear_color(r, output, 0x000000);

  _scene_render_layers(scene, output);

  /*_scene_render_cursor(scene, output);*/

  r->impl.end_scene(r, output);
}

static void _scene_node_get_size(struct vt_scene_node_t *node, uint32_t *o_w,
                                 uint32_t *o_h) {
  if (!node || !o_w || !o_h)
    return;

  switch (node->type) {
  case VT_SCENE_NODE_SURFACE:
    *o_w = node->surf->applied.width;
    *o_h = node->surf->applied.height;
    return;
  case VT_SCENE_NODE_RECT:
  case VT_SCENE_NODE_INVISIBLE_GEOMETRY:
  case VT_SCENE_NODE_ROOT:
    *o_w = node->rect_w;
    *o_h = node->rect_h;
    return;
  default:
    *o_w = 0;
    *o_h = 0;
    break;
  }
}

static void _scene_node_update_surface_feedback(struct vt_scene_node_t *node) {
  if (!node)
    return;

  if (node->surf && node->surf->proto_state.linux_dmabuf_v1)
    vt_proto_linux_dmabuf_v1_update_surface_feedback(node->surf);

  for (uint32_t i = 0; i < node->child_count; i++) {
    _scene_node_update_surface_feedback(node->childs[i]);
  }
}

struct vt_scene_node_t *
_scene_node_create_rect(struct vt_compositor_t *c, float x, float y, float w,
                        float h, uint32_t color,
                        enum vt_scene_node_type_t type) {
  struct vt_scene_node_t *n = VT_ALLOC(c, sizeof(*n));
  if (!n) {
    VT_ERROR(c->log, "Failed to allocate scene node.");
    return NULL;
  }

  n->surf = NULL;
  n->type = type;

  n->x = x;
  n->y = y;
  n->rect_w = w;
  n->rect_h = h;
  n->color = color;

  return n;
}

struct vt_scene_node_t *vt_scene_node_create(struct vt_compositor_t *c,
                                             struct vt_surface_t    *surf) {
  struct vt_scene_node_t *n = VT_ALLOC(c, sizeof(*n));
  if (!n) {
    VT_ERROR(c->log, "Failed to allocate scene node.");
    return NULL;
  }

  n->surf = surf;
  n->type = VT_SCENE_NODE_SURFACE;

  n->x = 0;
  n->y = 0;

  n->geom_dirty = true;

  if (surf)
    surf->scene_node = n;

  VT_TRACE(c->log,
           "Created scene node %p for surface %p at (%d,%d), geom_dirty=%d", n,
           surf, n->x, n->y, n->geom_dirty);

  return n;
}

bool vt_scene_node_destroy(struct vt_compositor_t *c,
                           struct vt_scene_node_t *node) {
  if (node->parent) {
    return vt_scene_node_remove_child(node->parent, node);
  }
  return true;
}

bool vt_scene_node_damage_whole(struct vt_compositor_t *comp,
                                struct vt_scene_node_t *node) {
  if (!node || !comp)
    return false;
  VT_TRACE(comp->log, "DEBUG: node %p damaged; repainting all outputs", node);

  struct vt_output_t *it;
  wl_list_for_each(it, &comp->outputs, link_global) {
    VT_TRACE(comp->log, "DEBUG: scheduling output %p", it);

    vt_comp_schedule_repaint(comp, it);
  }

  return true;
}

struct vt_scene_node_t *vt_scene_node_create_rect(struct vt_compositor_t *c,
                                                  float x, float y, float w,
                                                  float h, uint32_t color) {
  return _scene_node_create_rect(c, x, y, w, h, color, VT_SCENE_NODE_RECT);
}

struct vt_scene_node_t *
vt_scene_node_create_rect_invisible(struct vt_compositor_t *c, float x, float y,
                                    float w, float h) {
  return _scene_node_create_rect(c, x, y, w, h, 0x0,
                                 VT_SCENE_NODE_INVISIBLE_GEOMETRY);
}

struct vt_scene_node_t *
vt_scene_node_create_container(struct vt_compositor_t *c) {
  return vt_scene_node_create_rect_invisible(c, 0, 0, 0, 0);
}

bool vt_scene_node_reparent(struct vt_compositor_t *c,
                            struct vt_scene_node_t *node,
                            struct vt_scene_node_t *new_parent) {
  if (!c || !node) {
    VT_ERROR(c->log, "One or more parameters of vt_scene_node_reparent() are "
                     "invalid, cannot add child.");
    return false;
  }

  if (node->parent == new_parent)
    return true;

  if (node->parent) {
    vt_scene_node_remove_child(node->parent, node);
  }

  if (!vt_scene_node_add_child(c, new_parent, node))
    return false;

  vt_scene_node_mark_geometry_dirty(node);
  _scene_node_update_surface_feedback(node);

  return true;
}

bool vt_scene_node_add_child(struct vt_compositor_t *c,
                             struct vt_scene_node_t *node,
                             struct vt_scene_node_t *child) {
  if (!c || !node || !child) {
    VT_ERROR(c->log, "One or more parameters of vt_scene_node_add_child() are "
                     "invalid, cannot add child.");
    return false;
  }
  if (node->child_count >= node->_child_cap) {
    node->_child_cap =
        !node->_child_cap ? _SCENE_CHILD_CAP_INIT : node->_child_cap * 2;
    node->childs = realloc(node->childs,
                           sizeof(struct vt_scene_node_t *) * node->_child_cap);
  }

  node->childs[node->child_count++] = child;
  child->parent = node;

  return true;
}

bool vt_scene_node_remove_child(struct vt_scene_node_t *parent,
                                struct vt_scene_node_t *child)

{
  if (!parent || !child)
    return false;

  for (size_t i = 0; i < parent->child_count; i++) {
    if (parent->childs[i] != child)
      continue;

    for (size_t j = i; j + 1 < parent->child_count; j++)
      parent->childs[j] = parent->childs[j + 1];

    parent->child_count--;

    if (parent->child_count == 0) {
      free(parent->childs);
      parent->_child_cap = 0;
      parent->childs = NULL;
    }

    if (child->parent == parent)
      child->parent = NULL;

    return true;
  }

  return false;
}

struct vt_scene_t *vt_scene_create(struct vt_renderer_t   *renderer,
                                   struct vt_scene_node_t *root) {
  assert(renderer && renderer->comp && root);

  struct vt_scene_t *scene = VT_ALLOC(renderer->comp, sizeof(*scene));
  if (!scene)
    return NULL;

  scene->renderer = renderer;
  scene->root = root;

  return scene;
}

void vt_scene_render(struct vt_scene_t *scene, struct vt_output_t *output) {
  assert(scene && scene->renderer && output);
  scene->n_layers = 0;

  _scene_accumulate_layers(scene, output);

  if (scene->n_layers > 0) {
    if (output->backend->impl.test_output_layers)
      output->backend->impl.test_output_layers(output->backend, output,
                                               scene->layers, scene->n_layers);
  }

  struct vt_renderer_t *r = scene->renderer;
  bool                  need_compositing = true;

  /*
  for (size_t i = 0; i < scene->n_layers; i++) {
    if (!scene->layers[i].accepted) {
      need_compositing = true;
      break;
    }
  }
  if (!need_compositing && scene->n_layers == 0)
    need_compositing = true;*/

  if (need_compositing) {
    r->impl.begin_frame(r, output);

    // TODO: Damage pass
    _composite_pass(scene, output);

    r->impl.end_frame(r, output, output->cached_damage, output->n_damage_boxes);
  }

  pixman_region32_clear(&output->damage);
  output->needs_repaint = false;
}

void vt_scene_node_set_position(struct vt_scene_node_t *node, int32_t x,
                                int32_t y) {
  if (!node)
    return;

  if (node->x == x && node->y == y)
    return;

  node->x = x;
  node->y = y;

  vt_scene_node_mark_geometry_dirty(node);
  _scene_node_update_surface_feedback(node);
}

void vt_scene_node_mark_geometry_dirty(struct vt_scene_node_t *node) {
  if (!node)
    return;

  node->geom_dirty = true;

  for (uint32_t i = 0; i < node->child_count; i++) {
    vt_scene_node_mark_geometry_dirty(node->childs[i]);
  }
}

void vt_scene_node_update_global_bounds(struct vt_scene_node_t *node) {
  if (!node || !node->geom_dirty)
    return;

  float global_x = node->x;
  float global_y = node->y;

  if (node->parent) {
    vt_scene_node_update_global_bounds(node->parent);

    global_x += node->parent->cached_bounds.x;
    global_y += node->parent->cached_bounds.y;
  }

  node->cached_bounds.x = global_x;
  node->cached_bounds.y = global_y;
  _scene_node_get_size(node, &node->cached_bounds.width,
                       &node->cached_bounds.height);

  node->geom_dirty = false;
}

struct vt_box_t *vt_scene_node_get_global_bounds(struct vt_scene_node_t *node) {
  if (!node)
    return NULL;

  vt_scene_node_update_global_bounds(node);

  return &node->cached_bounds;
}

struct vt_output_t *vt_scene_node_primary_output(struct vt_compositor_t *comp,
                                                 struct vt_scene_node_t *node) {
  if (!node) {
    VT_PARAM_CHECK_FAIL(comp);
    VT_ERROR(comp->log, "Cannot pick primary output for NULL node");
    return NULL;
  }

  const struct vt_box_t *rect = vt_scene_node_get_global_bounds(node);

  if (!rect) {
    VT_PARAM_CHECK_FAIL(comp);
    VT_ERROR(comp->log,
             "Failed to pick primary output for scene node %p. Node has "
             "invalid global bounds",
             node);
    return NULL;
  }

  struct vt_output_t *best = NULL;
  uint64_t            best_area = 0;

  struct vt_output_t *output;

  wl_list_for_each(output, &comp->outputs, link_global) {

    int32_t x1 = VT_MAX(rect->x, output->x);
    int32_t y1 = VT_MAX(rect->y, output->y);

    int32_t x2 = VT_MIN(rect->x + rect->width, output->x + output->width);

    int32_t y2 = VT_MIN(rect->y + rect->height, output->y + output->height);

    if (x2 <= x1 || y2 <= y1)
      continue;

    uint64_t area = (uint64_t)(x2 - x1) * (uint64_t)(y2 - y1);

    if (area > best_area) {
      best_area = area;
      best = output;
    }
  }

  if (!best) {
    VT_ERROR(comp->log,
             "Failed to pick primary output for scene node %p. [x: %i, y: %i, "
             "w: %u, h: %u]",
             node, rect->x, rect->y, rect->width, rect->height);
  }
  return best;
}
