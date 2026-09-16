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

  return vt_scene_node_add_child(c, new_parent, node);
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

static bool _box_intersect_box(float x1, float y1, float w1, float h1, float x2,
                               float y2, float w2, float h2) {
  return x1 + w1 >= x2 && x1 <= x2 + w2 && y1 + h1 >= y2 && y1 <= y2 + h2;
}

static void sceneprintindent(int indent) {
  for (int i = 0; i < indent; i++)
    printf("  ");
  for (int i = 0; i < indent; i++)
    printf("━");
}

static void _scene_node_render_at(struct vt_renderer_t   *renderer,
                                  struct vt_output_t     *output,
                                  struct vt_scene_node_t *node, float parent_x,
                                  float                       parent_y,
                                  vt_scene_node_filter_func_t filter) {
  if (!renderer || !node)
    return;

  float x = parent_x + node->x;
  float y = parent_y + node->y;

  bool filter_out = filter ? filter(node) : false;

  if (!filter_out) {
    if (node->surf) {
      renderer->impl.draw_surface(renderer, output, node->surf, x, y);
    } else {
      renderer->impl.draw_rect(renderer, x, y, node->cached_bounds.width,
                               node->cached_bounds.height, node->color);
    }
  }

  for (uint32_t i = 0; i < node->child_count; i++) {
    _scene_node_render_at(renderer, output, node->childs[i], x, y, filter);
  }
}

void vt_scene_node_render(struct vt_renderer_t   *renderer,
                          struct vt_output_t     *output,
                          struct vt_scene_node_t *node, bool care_for_damage,
                          vt_scene_node_filter_func_t filter) {
  _scene_node_render_at(renderer, output, node, 0, 0, filter);
}

static bool _composite_scene_node_filter(struct vt_scene_node_t *node) {
  if (node->type == VT_SCENE_NODE_INVISIBLE_GEOMETRY)
    return true;

  if (!node->surf)
    return true;

  if (!vt_surface_effectively_mapped(node->surf))
    return true;

  if (node->surf && node->surf->role.impl &&
      node->surf->role.impl->type == VT_SURFACE_ROLE_CURSOR)
    return true;

  return false;
}

static void  _composite_pass(struct vt_renderer_t   *renderer,
                             struct vt_output_t     *output,
                             struct vt_scene_node_t *root,
                             bool                    care_for_damage) {
  struct vt_renderer_t *r = renderer;

  r->impl.composite_pass(r, output);

  r->impl.begin_scene(r, output);

  if (care_for_damage) {
    r->impl.draw_rect(r, output->x, output->y, output->width, output->height,
                       0xffffff);
  } else {
    r->impl.set_clear_color(r, output, 0x000000);
  }

  vt_scene_node_render(renderer, output, root, true,
                        _composite_scene_node_filter);

  struct vt_seat_t    *seat = renderer->comp->seat;
  struct vt_surface_t *cursor = seat->cursor.surf;

  if (cursor && cursor->mapped) {
    renderer->impl.draw_surface(renderer, output, cursor,
                                 seat->pointer_x - seat->cursor.hotspot_x,
                                 seat->pointer_y - seat->cursor.hotspot_y);
  }

  r->impl.end_scene(r, output);
}

void vt_scene_render(struct vt_renderer_t *renderer, struct vt_output_t *output,
                     struct vt_scene_node_t *root) {
  if (!renderer || !output)
    return;

  renderer->impl.begin_frame(renderer, output);

  //_damage_pass(renderer, output);
  _composite_pass(renderer, output, root, false);

  renderer->impl.end_frame(renderer, output, output->cached_damage,
                           output->n_damage_boxes);

  pixman_region32_clear(&output->damage);
  output->needs_repaint = false;
}

void vt_scene_node_set_position(struct vt_scene_node_t *node, int32_t x,
                                int32_t y) {
  if (!node)
    return;

  if(node->x == x && node->y == y) return;

  node->x = x;
  node->y = y;

  vt_scene_node_mark_geometry_dirty(node);
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
    *o_w = node->rect_w;
    *o_h = node->rect_h;
    return;
  default:
    *o_w = 0;
    *o_h = 0;
    break;
  }
}

void vt_scene_node_update_global_bounds(struct vt_scene_node_t *node) {
  if(!node || !node->geom_dirty) return;

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

void vt_scene_node_mark_geometry_dirty(struct vt_scene_node_t *node) {
  if(!node) return;

  node->geom_dirty = true;
}

struct vt_rect_t* vt_scene_node_get_global_bounds(struct vt_scene_node_t *node) {
  if(!node) return NULL;

  vt_scene_node_update_global_bounds(node);

  return &node->cached_bounds;
}

struct vt_output_t *vt_scene_node_primary_output(struct vt_compositor_t *comp,
                                                 struct vt_scene_node_t *node) {
  if (!node)
    return NULL;

  const struct vt_rect_t *rect = vt_scene_node_get_global_bounds(node);

  if (!rect)
    return NULL;

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

  return best;
}
