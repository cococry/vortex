#include "scene.h"
#include "src/core/compositor.h"
#include "src/core/core_types.h"
#include "src/core/util.h"
#include "src/render/renderer.h"

#define _SCENE_CHILD_CAP_INIT 4

#define _SUBSYS_NAME "SCENE"

struct vt_scene_node_t*
vt_scene_node_create(struct vt_compositor_t* c, float x, float y, float w, float h,
                     enum vt_scene_node_type_t type, struct vt_surface_t* surf
                     ) {
  struct vt_scene_node_t* n = VT_ALLOC(c, sizeof(*n));
  if(!n) {
    VT_ERROR(c->log, "Failed to allocate scene node.");
    return NULL;
  }

  n->x = x; n->y = y;
  n->w = x; n->h = h;
  n->surf = surf;
  n->type = type;
  surf->scene_node = n;

  return n;
}

bool 
vt_scene_node_add_child(struct vt_compositor_t* c, struct vt_scene_node_t* node, struct vt_scene_node_t* child) {
  if(!c || !node || !child) {
    VT_ERROR(c->log, "One or more parameters of vt_scene_node_add_child() are invalid, cannot add child.");
    return false;
  }
  if(node->child_count >= node->_child_cap) {
    node->_child_cap = !node->_child_cap ? _SCENE_CHILD_CAP_INIT : node->_child_cap * 2;
    node->childs = realloc(
      node->childs,
      sizeof(struct vt_scene_node_t*) * node->_child_cap);
  }

  node->childs[node->child_count++] = child;
  child->parent = node;
  child->x += node->x;
  child->y += node->y;

  return true;
}

static bool _box_intersect_box(
  float x1, float y1, float w1, float h1,
  float x2, float y2, float w2, float h2) {
  return 
    x1 + w1 >= x2 && x1 <= x2 + w2 && 
    y1 + h1 >= y2 && y1 <= y2 + h2; 
}

static bool _node_intersects_damage(struct vt_scene_node_t* node, const pixman_box32_t* boxes, uint32_t n_boxes) {
  for(uint32_t i = 0; i < n_boxes; i++) {
    pixman_box32_t box = boxes[i];
    if (_box_intersect_box(
      node->x, node->y, node->w, node->h, 
      box.x1, box.y1, box.x2 - box.x1, box.y2 - box.y1 
    )) {
      return true;
    } 
  }
  return false;
}

void 
vt_scene_node_render(
  struct vt_renderer_t* renderer, struct vt_output_t* output, 
  struct vt_scene_node_t* node, bool care_for_damage, vt_scene_node_filter_func_t filter) {
  if(!renderer || !node) return;
 
  bool skip = filter ? !filter(node) : false;
  
  if(care_for_damage && !_node_intersects_damage(node, output->cached_damage, output->n_damage_boxes)) {
    skip = true;
  }

  if(!skip) {
    if(!node->surf) {
      renderer->impl.draw_rect(renderer, node->x, node->y, node->w, node->h, node->color);
    } else {
      renderer->impl.draw_surface(renderer, output, node->surf, node->x, node->y);
    }
  }

  for(uint32_t i = 0; i < node->child_count; i++) {
    vt_scene_node_render(renderer, output, node->childs[i], care_for_damage, filter);
  }
}

struct vt_surface_t *_get_focused_cursor_surface(struct vt_seat_t *seat) {
    if (!seat->ptr_focus.res)
        return NULL;

    struct wl_client *focused_client = wl_resource_get_client(seat->ptr_focus.res);
    struct vt_pointer_t *ptr;
    wl_list_for_each(ptr, &seat->pointers, link) {
        if (wl_resource_get_client(ptr->res) == focused_client)
            return ptr->cursor.surf;
    }
    return NULL;
}

static bool _composite_scene_node_filter(struct vt_scene_node_t* node) {
  if(!node->surf) return false;
  if(node->surf->type != VT_SURFACE_TYPE_NORMAL) return false;
  return true;
}

static void _composite_pass(struct vt_renderer_t* renderer, struct vt_output_t *output, struct vt_scene_node_t* root, bool care_for_damage) {
  struct vt_renderer_t* r = renderer;
  r->impl.begin_scene(r, output);

  if(care_for_damage) {
  r->impl.draw_rect(r, 0, 0, output->width, output->height, 0xffffff); 
  } else {
    r->impl.set_clear_color(r, output, 0xffffff);
  }

  struct vt_surface_t* surf;
  vt_scene_node_render(renderer, output, root, care_for_damage, _composite_scene_node_filter);

  struct vt_surface_t* cursor_surf = _get_focused_cursor_surface(renderer->comp->seat);
  if(cursor_surf) {
    vt_scene_node_render(renderer, output, cursor_surf->scene_node, care_for_damage, NULL);
  }

  r->impl.end_scene(r, output);
}

static void _damage_pass(struct vt_compositor_t *c, struct vt_output_t *output) {
  struct vt_renderer_t* r = c->renderer;
  output->n_damage_boxes = vt_comp_merge_damaged_regions(output->cached_damage, &output->damage);
  output->needs_damage_rebuild = false;

  r->impl.stencil_damage_pass(r, output);

  // 1. drawcall 
  r->impl.begin_scene(r, output);

  if(output->resize_pending) {
    r->impl.draw_rect(r, 0, 0, output->width, output->height, 0xffffff); 
    output->resize_pending = false;
  }
  for(uint32_t i = 0; i < output->n_damage_boxes; i++) {
    pixman_box32_t box = output->cached_damage[i];
    r->impl.draw_rect(r, box.x1, box.y1, box.x2 - box.x1, box.y2 - box.y1, 0xffffff); 
  }

  r->impl.end_scene(r, output);

}

void vt_scene_render(struct vt_renderer_t* renderer, struct vt_output_t *output, struct vt_scene_node_t* root) {
  if (!renderer || !output) return;

  renderer->impl.begin_frame(renderer, output);

  if(output->repaint_pending) {
    output->repaint_pending = false;

    _composite_pass(renderer, output, root, false);
  } else {

    if(output->needs_damage_rebuild) {
      _damage_pass(renderer, output);
    } 

    // 2. drawcall 
    renderer->impl.composite_pass(renderer, output);

    _composite_pass(renderer, output, root, true);
  }

  renderer->impl.end_scene(renderer, output);

  renderer->impl.end_frame(renderer, output, output->cached_damage, output->n_damage_boxes);

  pixman_region32_clear(&output->damage);
  output->needs_repaint = false;

}
