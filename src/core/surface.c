#include "surface.h"
#include "../input/wl_seat.h"
#include "src/core/compositor.h"
#include "src/core/core_types.h"
#include "src/core/util.h"
#include "src/protocols/xdg_shell.h"
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#define _SUBSYS_NAME "SURFACE"

void vt_surface_mapped(struct vt_surface_t *surf) {
  if (!surf)
    return;

  struct vt_seat_t *seat = surf->comp->seat;

  if (surf->mapped)
    return;

  surf->mapped = true;

  if (surf->xdg_surf && surf->xdg_surf->toplevel) {
    vt_seat_set_keyboard_focus(seat, surf);

    if (wl_list_empty(&surf->link_focus)) {
      wl_list_insert(&seat->focus_stack, &surf->link_focus);
    }
  }

  struct vt_surface_t *under_cursor =
      vt_comp_pick_surface(surf->comp, seat->pointer_x, seat->pointer_y);

  if (under_cursor) {
    double gx, gy;

    vt_scene_node_get_global_position(under_cursor->scene_node, &gx, &gy);

    double sx = seat->pointer_x - gx;
    double sy = seat->pointer_y - gy;

    vt_seat_set_pointer_focus(seat, under_cursor, sx, sy);
  } else {
    vt_seat_set_pointer_focus(seat, NULL, 0.0, 0.0);
  }
}

struct vt_surface_t *focus_stack_pop(struct vt_compositor_t *comp) {
  if (!comp || !comp->seat)
    return NULL;

  struct wl_list *stack = &comp->seat->focus_stack;

  if (wl_list_empty(stack))
    return NULL;

  struct vt_surface_t *surf = wl_container_of(stack->next, surf, link_focus);

  return surf;
}

void vt_surface_unmapped(struct vt_surface_t *surf) {
  if (!surf || !surf->comp || !surf->comp->seat)
    return;

  if (!surf->mapped)
    return;

  struct vt_seat_t *seat = surf->comp->seat;

  bool had_keyboard_focus = seat->kb_focus.surf == surf;

  bool had_pointer_focus = seat->ptr_focus.surf == surf;

  surf->mapped = false;

  if (!wl_list_empty(&surf->link_focus)) {
    wl_list_remove(&surf->link_focus);
    wl_list_init(&surf->link_focus);
  }

  if (had_pointer_focus) {
    struct vt_surface_t *under_cursor =
        vt_comp_pick_surface(surf->comp, seat->pointer_x, seat->pointer_y);

    if (under_cursor) {
      double gx, gy;

      vt_scene_node_get_global_position(under_cursor->scene_node, &gx, &gy);

      vt_seat_set_pointer_focus(seat, under_cursor, seat->pointer_x - gx,
                                seat->pointer_y - gy);
    } else {
      vt_seat_set_pointer_focus(seat, NULL, 0.0, 0.0);
    }
  }

  if (!had_keyboard_focus)
    return;

  struct vt_surface_t *new_focus = NULL;

  if (surf->xdg_surf && surf->xdg_surf->toplevel &&
      surf->xdg_surf->toplevel->parent) {

    struct vt_xdg_toplevel_t *parent = surf->xdg_surf->toplevel->parent;

    if (parent->xdg_surf && parent->xdg_surf->surf &&
        parent->xdg_surf->surf->mapped) {

      new_focus = parent->xdg_surf->surf;
    }
  }

  if (!new_focus && surf->xdg_surf && surf->xdg_surf->popup) {

    struct vt_xdg_surface_t *parent = surf->xdg_surf->popup->parent_xdg_surf;

    if (parent && parent->surf && parent->surf->mapped) {

      new_focus = parent->surf;
    }
  }

  if (!new_focus) {
    new_focus = focus_stack_pop(surf->comp);
  }

  vt_seat_set_keyboard_focus(seat, new_focus);
}
