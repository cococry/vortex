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

bool vt_surface_apply_buffer(struct vt_surface_t *surf,
                             struct vt_buffer_t  *buf) {
  if (!surf || !surf->comp || !buf)
    return false;

  if (!buf) {
    if (surf->applied.buf != NULL) {
      vt_comp_surf_mark_damaged(surf->comp, surf);
    }

    vt_buffer_unref(&surf->applied.buf);

    vt_surface_unmapped(surf);

    return true;
  }

  /* import from the new buffer */
  if (!vt_buffer_import(buf, &surf->applied.damage)) {
    return false;
  }

  vt_buffer_unref(&surf->applied.buf);

  surf->applied.buf = buf;

  return true;
}
void vt_surface_pending_state_init(struct vt_surface_state_pending_t *state) {
  memset(state, 0, sizeof(*state));

  pixman_region32_init(&state->input_region);
  pixman_region32_init(&state->opaque_region);

  pixman_region32_init(&state->damage_surface);
  pixman_region32_init(&state->damage_buffer);

  wl_list_init(&state->frame_callbacks);
  wl_list_init(&state->release_callbacks);
}

void vt_surface_pending_state_move(struct vt_surface_state_pending_t *dst,
                                   struct vt_surface_state_pending_t *src) {
  if (!dst || !src || dst == src)
    return;

  vt_surface_pending_state_init(dst);

  dst->input_region_changed = src->input_region_changed;
  dst->input_region_infinite = src->input_region_infinite;
  pixman_region32_copy(&dst->input_region, &src->input_region);

  dst->opaque_region_changed = src->opaque_region_changed;
  pixman_region32_copy(&dst->opaque_region, &src->opaque_region);

  dst->buffer_transform = src->buffer_transform;
  dst->buffer_transform_changed = src->buffer_transform_changed;

  dst->buffer_scale = src->buffer_scale;
  dst->buffer_scale_changed = src->buffer_scale_changed;

  dst->buffer_attached = src->buffer_attached;

  dst->buf = src->buf;
  src->buf = NULL;

  pixman_region32_copy(&dst->damage_surface, &src->damage_surface);
  pixman_region32_copy(&dst->damage_buffer, &src->damage_buffer);

  dst->offset_set = src->offset_set;
  dst->offset_x = src->offset_x;
  dst->offset_y = src->offset_y;

  wl_list_insert_list(&dst->frame_callbacks, &src->frame_callbacks);
  wl_list_init(&src->frame_callbacks);

  wl_list_insert_list(&dst->release_callbacks, &src->release_callbacks);
  wl_list_init(&src->release_callbacks);

  src->input_region_changed = false;
  src->input_region_infinite = false;
  pixman_region32_clear(&src->input_region);

  src->opaque_region_changed = false;
  pixman_region32_clear(&src->opaque_region);

  src->buffer_transform_changed = false;
  src->buffer_scale_changed = false;

  src->buffer_attached = false;

  pixman_region32_clear(&src->damage_surface);
  pixman_region32_clear(&src->damage_buffer);

  src->offset_set = false;
  src->offset_x = 0;
  src->offset_y = 0;
}

void vt_surface_pending_state_fini(struct vt_surface_state_pending_t *state) {
  if (!state)
    return;

  vt_buffer_unref(&state->buf);

  struct vt_surface_frame_callback_t *frame_cb, *frame_tmp;
  wl_list_for_each_safe(frame_cb, frame_tmp, &state->frame_callbacks, link) {

    wl_list_remove(&frame_cb->link);

    if (frame_cb->res)
      wl_resource_destroy(frame_cb->res);
  }

  struct vt_surface_release_t *release, *release_tmp;
  wl_list_for_each_safe(release, release_tmp, &state->release_callbacks, link) {

    wl_list_remove(&release->link);

    if (release->res)
      wl_resource_destroy(release->res);
  }

  pixman_region32_fini(&state->input_region);
  pixman_region32_fini(&state->opaque_region);

  pixman_region32_fini(&state->damage_surface);
  pixman_region32_fini(&state->damage_buffer);
}
