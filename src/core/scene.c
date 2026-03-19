#include "scene.h"
#include "pixman.h"
#include "src/core/core_types.h"
#include "src/core/surface.h"
#include "src/core/util.h"
#include "src/render/renderer.h"
#include <wayland-util.h>

#define _SCENE_CHILD_CAP_INIT 4

#define _SUBSYS_NAME "SCENE"

struct vt_scene_node_t*
vt_scene_node_create(struct vt_compositor_t* c, struct vt_surface_t* surf) {
  struct vt_scene_node_t* n = VT_ALLOC(c, sizeof(*n));
  if(!n) {
    VT_ERROR(c->log, "Failed to allocate scene node.");
    return NULL;
  }

  n->surf = surf;
  n->type = VT_SCENE_NODE_SURFACE;

  if(surf)
    surf->scene_node = n;

  return n;
}

struct vt_scene_node_t* vt_scene_node_destroy(struct vt_compositor_t* c, struct vt_scene_node_t* node) {
  if(node->parent && node->parent->child_count != 0) {
    size_t idx = 0;
    bool found = false;
    while (true) {
      if(node->parent->childs[idx] == node) {
        found = true;
        break;
      }
      else if(idx < node->parent->child_count) {
        idx++;
      } else {
        found = false;
        break;
      }
    }

    if(found) {
      for(size_t i = idx; i < node->parent->child_count - 1; i++) {
        node->parent->childs[i] = node->parent->childs[i + 1];
      }

      node->parent->child_count--;
      if(node->parent->child_count == 0) {
        free(node->parent->childs);
        node->parent->_child_cap = 0;
        node->parent->childs = NULL;
      }
    }
  }
}
struct vt_scene_node_t* vt_scene_node_create_rect(struct vt_compositor_t* c, float x, float y, float w, float h,
    uint32_t color) {
  struct vt_scene_node_t* n = VT_ALLOC(c, sizeof(*n));
  if(!n) {
    VT_ERROR(c->log, "Failed to allocate scene node.");
    return NULL;
  }

  n->surf         = NULL;
  n->type         = VT_SCENE_NODE_RECT;

  n->rect.x       = x;
  n->rect.y       = y;
  n->rect.width   = w;
  n->rect.height  = h;
  n->rect.color   = color;

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
  float x1 = node->surf ? node->surf->x : node->rect.x;
  float y1 = node->surf ? node->surf->y : node->rect.y;
  float w1 = node->surf ? node->surf->width : node->rect.width;
  float h1 = node->surf ? node->surf->height : node->rect.height;

  for(uint32_t i = 0; i < n_boxes; i++) {
    pixman_box32_t box = boxes[i];
    if (_box_intersect_box(
      x1, y1, w1, h1,
      box.x1, box.y1, box.x2 - box.x1, box.y2 - box.y1 
    )) {
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

void sceneprint(struct vt_scene_node_t* node, int indent) {
  if (!node) return;

  sceneprintindent(indent);

  switch (node->type) {
    case VT_SCENE_NODE_ROOT: {
      printf("Root: [%f, %f, %f, %f]\n",
             node->rect.x,
             node->rect.y,
             node->rect.width,
             node->rect.height);
      break;
    }
    case VT_SCENE_NODE_SURFACE: {
      printf("━> Surface: [%i, %i, %i, %i]\n",
             node->surf->x,
             node->surf->y,
             node->surf->width,
             node->surf->height);
      break;
    } 
    case VT_SCENE_NODE_RECT: {
      printf("━> Rect: [%f, %f, %f, %f]\n",
             node->rect.x,
             node->rect.y,
             node->rect.width,
             node->rect.height);
      break;
    }

  }

  for (size_t i = 0; i < node->child_count; i++) {
    sceneprint(node->childs[i], indent + 1);
  }
}

void 
vt_scene_node_render(
  struct vt_renderer_t* renderer, struct vt_output_t* output, 
  struct vt_scene_node_t* node, bool care_for_damage, vt_scene_node_filter_func_t filter) {
  if(!renderer || !node) return;
 
  bool skip = filter ? !filter(node) : false;
  


  if(!skip) {
    if(node->surf) {
      renderer->impl.draw_surface(renderer, output, node->surf, node->surf->x, node->surf->y);
    } else {
      renderer->impl.draw_rect(
        renderer, 
        node->rect.x, node->rect.y, 
        node->rect.width, node->rect.height,
        node->rect.color);
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
    if(!ptr || !ptr->res) continue;
        if (wl_resource_get_client(ptr->res) == focused_client)
            return ptr->cursor.surf;
    }
    return NULL;
}

static bool _composite_scene_node_filter(struct vt_scene_node_t* node) {
  if(node->surf && !node->surf->mapped) return false;
  if(node->surf && node->surf->type != VT_SURFACE_TYPE_NORMAL) return false;
  return true;
}

static void _get_cursor_hotspot(struct vt_surface_t *surf, int32_t *hx, int32_t *hy)
{
    struct vt_seat_t *seat = surf->comp->seat;

    struct vt_pointer_t *ptr;
    wl_list_for_each(ptr, &seat->pointers, link) {

        // Is this the pointer whose cursor surface is `surf`?
        if (ptr->cursor.surf == surf) {
            *hx = (float)ptr->cursor.hotspot_x / ptr->cursor.surf->buffer_scale;
            *hy = (float)ptr->cursor.hotspot_y / ptr->cursor.surf->buffer_scale;
            return;
        }
    }

    // No pointer uses this as a cursor surface
    *hx = 0;
    *hy = 0;
}



static float prev_cur_x = 0, prev_cur_y = 0, prev_cur_w = 0, prev_cur_h = 0;
static void _composite_pass(struct vt_renderer_t* renderer, struct vt_output_t *output, struct vt_scene_node_t* root, bool care_for_damage) {
  struct vt_renderer_t* r = renderer;
  
  r->impl.composite_pass(r, output);

  r->impl.begin_scene(r, output);

  if(care_for_damage) {
  r->impl.draw_rect(r, 0, 0, output->width, output->height, 0xffffff); 
  } else {
    r->impl.set_clear_color(r, output, 0xffffff);
  }

  sceneprint(root, 0);
  vt_scene_node_render(renderer, output, root, true, _composite_scene_node_filter);

  if(!renderer->comp->seat->ptr_focus.surf) {
    /*if(!renderer->comp->seat->ptr_focus.surf &&  _node_intersects_damage(renderer->comp->root_cursor, output->cached_damage, output->n_damage_boxes)) {
    renderer->impl.draw_rect(
      renderer, renderer->comp->root_cursor->x,
      renderer->comp->root_cursor->y, renderer->comp->root_cursor->width, renderer->comp->root_cursor->height, 0xff0000);
    }*/
  } else {
    struct vt_surface_t* cursor_focus = _get_focused_cursor_surface(renderer->comp->seat);
    if(cursor_focus && cursor_focus->mapped && cursor_focus->comp->seat->ptr_focus.surf) { 
      struct vt_seat_t* seat = cursor_focus->comp->seat;

      int32_t hx, hy;
      _get_cursor_hotspot(cursor_focus, &hx, &hy);
      float x = (seat->pointer_x - hx);
      float y = (seat->pointer_y - hy);
      float w = cursor_focus->width * cursor_focus->buffer_scale; 
      float h = cursor_focus->height * cursor_focus->buffer_scale;

      renderer->impl.draw_surface(
        renderer, output, cursor_focus, x, y);

      prev_cur_x = x; 
      prev_cur_y = y; 
      prev_cur_w = w; 
      prev_cur_h = h; 
    }
  }
  
  //renderer->impl.draw_rect(renderer, renderer->comp->root_cursor->x, renderer->comp->root_cursor->y, 50, 50, 0xff00000); 

  r->impl.end_scene(r, output);
}

static void _damage_pass(struct vt_renderer_t* r, struct vt_output_t *output) {

  struct vt_surface_t* surf;
  wl_list_for_each(surf, &r->comp->surfaces, link) {
    if(surf && surf->damaged && surf->type == VT_SURFACE_TYPE_CURSOR) {
      struct vt_seat_t* seat = surf->comp->seat;
      if(prev_cur_w != 0) {
        pixman_region32_union_rect(
        &output->damage, &output->damage,
        prev_cur_x, prev_cur_y, prev_cur_w, prev_cur_h);
      }
      struct vt_surface_t* under_cursor = r->comp->seat->ptr_focus.surf;
      if(under_cursor) {
        int32_t hx, hy;
        _get_cursor_hotspot(under_cursor, &hx, &hy);
        pixman_region32_union_rect(
          &output->damage, &output->damage,
          seat->pointer_x - hx, seat->pointer_y - hy, surf->width * surf->buffer_scale, surf->height * surf->buffer_scale); 
        surf->damaged = false;
      }
    }
  }

  pixman_region32_union_rect(
    &output->damage, &output->damage,
    output->x, output->y, output->width, output->height); 
  pixman_box32_t* boxes = pixman_region32_rectangles(&output->damage, &output->n_damage_boxes);
  if(output->n_damage_boxes) {
    memset(output->cached_damage, 0, sizeof(pixman_box32_t) * VT_MAX_DAMAGE_RECTS);
    memcpy(output->cached_damage, boxes, sizeof(pixman_box32_t) * output->n_damage_boxes);
  }
  output->needs_damage_rebuild = false;

  r->impl.stencil_damage_pass(r, output);

  // 1. drawcall 
  r->impl.begin_scene(r, output);

  if(output->resize_pending) {
    r->impl.draw_rect(r, 0, 0, output->width, output->height, 0xffffff); 
    output->resize_pending = false;
    printf("Resizing.\n");
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

  if(output->needs_damage_rebuild)
    _damage_pass(renderer, output);
  _composite_pass(renderer, output, root, true);

  for(size_t i = 0; i < output->n_damage_boxes; i++) {
    VT_TRACE(renderer->comp->log, "  => REDRAWING SECTION: %i, %i, %i, %i\n",
             output->cached_damage[i].x1,
             output->cached_damage[i].x2,
             output->cached_damage[i].y1,
             output->cached_damage[i].y2
             );
  }
  renderer->impl.end_frame(renderer, output, output->cached_damage, output->n_damage_boxes); 


  pixman_region32_clear(&output->damage);
  output->needs_repaint = false;

}
