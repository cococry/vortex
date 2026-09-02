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

  n->rect.x = surf->x;
  n->rect.y = surf->y;
  n->rect.width = surf->width;
  n->rect.height = surf->height;

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

  n->rect.x = x;
  n->rect.y = y;
  n->rect.width = w;
  n->rect.height = h;
  n->rect.color = color;

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
static bool _node_intersects_damage(struct vt_scene_node_t *node,
                                    const pixman_box32_t   *boxes,
                                    uint32_t                n_boxes) {
  float x1 = node->rect.x;
  float y1 = node->rect.y;
  float w1 = node->rect.width;
  float h1 = node->rect.height;

  for (uint32_t i = 0; i < n_boxes; i++) {
    pixman_box32_t box = boxes[i];
    if (_box_intersect_box(x1, y1, w1, h1, box.x1, box.y1, box.x2 - box.x1,
                           box.y2 - box.y1)) {
      return true;
    }
  }
  return false;
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

  float x = parent_x + node->rect.x;
  float y = parent_y + node->rect.y;

  bool skip = filter ? !filter(node) : false;

  VT_TRACE(renderer->comp->log,
           "scene node=%p surf=%p skip=%d "
           "mapped=%d effective_mapped=%d type=%d "
           "pos=(%.2f, %.2f) children=%u parent=%p",
           (void *)node, (void *)node->surf, skip,
           node->surf ? node->surf->mapped : -1,
           node->surf ? vt_comp_surface_is_effectively_mapped(node->surf) : -1,
           node->surf ? node->surf->type : -1, x, y, node->child_count,
           (void *)node->parent);

  if (!skip) {
    if (node->surf) {
      renderer->impl.draw_surface(renderer, output, node->surf, x, y);
    } else {
      renderer->impl.draw_rect(renderer, x, y, node->rect.width,
                               node->rect.height, node->rect.color);
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
    return false;

  if (!node->surf)
    return true;

  if (!vt_comp_surface_is_effectively_mapped(node->surf))
    return false;

  if (node->surf && node->surf->type != VT_SURFACE_TYPE_NORMAL)
    return false;

  return true;
}

static void _get_cursor_hotspot(struct vt_surface_t *surf, int32_t *hx,
                                int32_t *hy) {
  struct vt_seat_t *seat = surf->comp->seat;

  if (seat->cursor.surf == surf) {
    *hx = (float)seat->cursor.hotspot_x / seat->cursor.surf->buffer_scale;
    *hy = (float)seat->cursor.hotspot_y / seat->cursor.surf->buffer_scale;
    return;
  }

  // No pointer uses this as a cursor surface
  *hx = 0;
  *hy = 0;
}

static float prev_cur_x = 0, prev_cur_y = 0, prev_cur_w = 0, prev_cur_h = 0;
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

static void _damage_pass(struct vt_renderer_t *r, struct vt_output_t *output) {

  struct vt_surface_t *surf;
  wl_list_for_each(surf, &r->comp->surfaces, link) {
    if (surf && surf->damaged && surf->type == VT_SURFACE_TYPE_CURSOR) {
      struct vt_seat_t *seat = surf->comp->seat;
      if (prev_cur_w != 0) {
        pixman_region32_union_rect(&output->damage, &output->damage, prev_cur_x,
                                   prev_cur_y, prev_cur_w, prev_cur_h);
      }
      struct vt_surface_t *under_cursor = r->comp->seat->ptr_focus.surf;
      if (under_cursor) {
        int32_t hx, hy;
        _get_cursor_hotspot(under_cursor, &hx, &hy);
        pixman_region32_union_rect(&output->damage, &output->damage,
                                   seat->pointer_x - hx, seat->pointer_y - hy,
                                   surf->width * surf->buffer_scale,
                                   surf->height * surf->buffer_scale);
        surf->damaged = false;
      }
    }
  }

  pixman_region32_union_rect(&output->damage, &output->damage, output->x,
                             output->y, output->width, output->height);
  pixman_box32_t *boxes =
      pixman_region32_rectangles(&output->damage, &output->n_damage_boxes);
  if (output->n_damage_boxes) {
    memset(output->cached_damage, 0,
           sizeof(pixman_box32_t) * VT_MAX_DAMAGE_RECTS);
    memcpy(output->cached_damage, boxes,
           sizeof(pixman_box32_t) * output->n_damage_boxes);
  }
  output->needs_damage_rebuild = false;

  r->impl.stencil_damage_pass(r, output);

  // 1. drawcall
  r->impl.begin_scene(r, output);

  if (output->resize_pending) {
    r->impl.draw_rect(r, 0, 0, output->width, output->height, 0xffffff);
    output->resize_pending = false;
  }
  for (uint32_t i = 0; i < output->n_damage_boxes; i++) {
    pixman_box32_t box = output->cached_damage[i];
    r->impl.draw_rect(r, box.x1, box.y1, box.x2 - box.x1, box.y2 - box.y1,
                      0xffffff);
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
  node->rect.x = x;
  node->rect.y = y;
}

void vt_scene_node_get_global_position(struct vt_scene_node_t *node, double *x,
                                       double *y) {
  *x = 0;
  *y = 0;

  while (node) {
    *x += node->rect.x;
    *y += node->rect.y;
    node = node->parent;
  }
}
