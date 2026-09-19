#include "surface.h"
#include "../input/wl_seat.h"
#include "src/core/buffer.h"
#include "src/core/compositor.h"
#include "src/core/content_update.h"
#include "src/core/core_types.h"
#include "src/core/scene.h"
#include "src/core/surface_addon.h"
#include "src/core/util.h"
#include "src/protocols/wl_subcompositor.h"
#include "src/protocols/xdg_shell.h"
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#define _SUBSYS_NAME "SURFACE"

bool vt_surface_init(struct vt_surface_t *surf) {
  if (!surf)
    return false;

  vt_surface_pending_state_init(&surf->pending);
  vt_surface_pending_state_defaults(&surf->pending);
  vt_surface_applied_state_init(&surf->applied);
  vt_surface_applied_state_defaults(&surf->applied);

  wl_list_init(&surf->content_updates);
  wl_list_init(&surf->subsurface.childs);
  wl_list_init(&surf->addons);

  wl_list_init(&surf->link);
  wl_list_init(&surf->link_focus);
  wl_list_init(&surf->frame_callbacks);

  surf->role.impl = NULL;

  surf->scene_node = NULL;

  surf->proto_state.linux_dmabuf_v1 = NULL;

  surf->damaged = false;
  surf->mapped = false;

  return true;
}

void vt_surface_mapped(struct vt_surface_t *surf) {
  if (!surf)
    return;

  struct vt_seat_t *seat = surf->comp->seat;

  if (surf->mapped)
    return;

  if (!surf->role.impl)
    return;

  surf->mapped = true;

  if (vt_surface_has_role(surf, VT_SURFACE_ROLE_XDG_TOPLEVEL)) {
    vt_seat_set_keyboard_focus(seat, surf);

    if (wl_list_empty(&surf->link_focus)) {
      wl_list_insert(&seat->focus_stack, &surf->link_focus);
    }
  }

  struct vt_surface_t *under_cursor =
      vt_comp_pick_surface(surf->comp, seat->pointer_x, seat->pointer_y);

  if (under_cursor) {
    struct vt_rect_t *global_bounds =
        vt_scene_node_get_global_bounds(under_cursor->scene_node);
    if (!global_bounds)
      return;

    double sx = seat->pointer_x - global_bounds->x;
    double sy = seat->pointer_y - global_bounds->y;

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

  if (!surf->role.impl)
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

      struct vt_rect_t *global_bounds =
          vt_scene_node_get_global_bounds(under_cursor->scene_node);
      if (!global_bounds)
        return;

      vt_seat_set_pointer_focus(seat, under_cursor,
                                seat->pointer_x - global_bounds->x,
                                seat->pointer_y - global_bounds->y);
    } else {
      vt_seat_set_pointer_focus(seat, NULL, 0.0, 0.0);
    }
  }

  if (!had_keyboard_focus)
    return;

  struct vt_surface_t *new_focus = NULL;

  if (vt_surface_has_role(surf, VT_SURFACE_ROLE_XDG_TOPLEVEL)) {
    struct vt_xdg_surface_t *xdg_surf = surf->role.data;

    if (xdg_surf->toplevel->parent) {
      struct vt_xdg_toplevel_t *toplevel_parent = xdg_surf->toplevel->parent;

      bool valid_mapped_parent = toplevel_parent && toplevel_parent->xdg_surf &&
                                 toplevel_parent->xdg_surf->surf &&
                                 toplevel_parent->xdg_surf->surf->mapped;
      if (valid_mapped_parent) {
        new_focus = toplevel_parent->xdg_surf->surf;
      }
    }

  }

  else if (vt_surface_has_role(surf, VT_SURFACE_ROLE_XDG_POPUP)) {
    struct vt_xdg_surface_t *xdg_surf = surf->role.data;

    if (xdg_surf->popup->parent_xdg_surf) {
      struct vt_xdg_surface_t *parent = xdg_surf->popup->parent_xdg_surf;

      if (parent && parent->surf && parent->surf->mapped) {

        new_focus = parent->surf;
      }
    }
  }

  if (!new_focus) {
    new_focus = focus_stack_pop(surf->comp);
  }

  vt_seat_set_keyboard_focus(seat, new_focus);
}

void vt_surface_apply_buffer_use(struct vt_surface_t    *surf,
                                 struct vt_buffer_use_t *new_use) {
  if (!surf)
    return;
  struct vt_buffer_use_t *old = surf->current_buf_use;

  surf->current_buf_use = new_use;
  if (old) {
    vt_buffer_use_unref(&old);
  }

  surf->mapped = surf->current_buf_use != NULL;

  // TODO: Temporary
  vt_scene_node_damage_whole(surf->comp, surf->scene_node);
}

void vt_surface_pending_state_init(struct vt_surface_state_pending_t *state) {
  if (!state)
    return;

  memset(state, 0, sizeof(*state));

  pixman_region32_init(&state->input_region);
  pixman_region32_init(&state->opaque_region);

  pixman_region32_init(&state->damage_surface);
  pixman_region32_init(&state->damage_buffer);

  wl_list_init(&state->frame_callbacks);

  state->buffer_scale = 1;
  state->buffer_transform = WL_OUTPUT_TRANSFORM_NORMAL;

  state->input_region_infinite = true;
}

void vt_surface_pending_state_defaults(
    struct vt_surface_state_pending_t *state) {
  if (!state)
    return;

  state->buffer_scale = 1;
  state->buffer_transform = WL_OUTPUT_TRANSFORM_NORMAL;

  state->input_region_infinite = true;
}

void vt_surface_applied_state_init(struct vt_surface_state_applied_t *state) {
  if (!state)
    return;

  memset(state, 0, sizeof(*state));

  pixman_region32_init(&state->input_region);
  pixman_region32_init(&state->opaque_region);

  pixman_region32_init(&state->damage);
}

void vt_surface_applied_state_defaults(
    struct vt_surface_state_applied_t *state) {
  if (!state)
    return;

  state->buffer_scale = 1;
  state->buffer_transform = WL_OUTPUT_TRANSFORM_NORMAL;

  state->input_region_infinite = true;
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

  dst->buffer_release = src->buffer_release;
  src->buffer_release = NULL;

  pixman_region32_copy(&dst->damage_surface, &src->damage_surface);
  pixman_region32_copy(&dst->damage_buffer, &src->damage_buffer);

  dst->offset_set = src->offset_set;
  dst->offset_x = src->offset_x;
  dst->offset_y = src->offset_y;

  wl_list_insert_list(&dst->frame_callbacks, &src->frame_callbacks);
  wl_list_init(&src->frame_callbacks);

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

  src->buffer_release = NULL;
}

void vt_surface_pending_state_fini(struct vt_surface_state_pending_t *state) {
  if (!state)
    return;

  vt_buffer_unref(&state->buf);
  vt_buffer_release_unref(&state->buffer_release);

  struct vt_surface_frame_callback_t *frame_cb, *frame_tmp;
  wl_list_for_each_safe(frame_cb, frame_tmp, &state->frame_callbacks, link) {

    if (frame_cb->res)
      wl_resource_destroy(frame_cb->res);
  }

  pixman_region32_fini(&state->input_region);
  pixman_region32_fini(&state->opaque_region);

  pixman_region32_fini(&state->damage_surface);
  pixman_region32_fini(&state->damage_buffer);
}

void vt_surface_applied_state_fini(struct vt_surface_state_applied_t *state) {
  if (!state)
    return;

  pixman_region32_fini(&state->input_region);
  pixman_region32_fini(&state->opaque_region);

  pixman_region32_fini(&state->damage);
}

bool vt_surface_validate_commit(struct vt_surface_t *surf) {
  if (!surf)
    return false;

  if (surf->role.impl && surf->role.impl->validate_commit) {
    if (!surf->role.impl->validate_commit(surf))
      return false;
  }

  const struct vt_surface_addon_t *it;
  wl_list_for_each(it, &surf->addons, link) {
    if (it->impl.validate_commit) {
      if (!it->impl.validate_commit(surf)) {
        return false;
      }
    }
  }

  return true;
}

bool vt_surface_effectively_synchronized(struct vt_surface_t *surf) {
  if (!surf)
    return false;

  while (surf) {
    if (!surf->role.impl || !vt_surface_has_role(surf, VT_SURFACE_ROLE_SUBSURFACE)) 
      return false;

    const struct vt_subsurface_t *sub = surf->role.data;

    if (!sub || !sub->parent)
      return false;

    if (sub->synchronized)
      return true;

    surf = sub->parent;
  }
  return false;
}

bool vt_surface_effectively_mapped(struct vt_surface_t *surf) {
  if (!surf || !surf->mapped)
    return false;

  while (vt_surface_has_role(surf, VT_SURFACE_ROLE_SUBSURFACE)) {
    struct vt_subsurface_t *sub = surf->role.data;

    surf = sub->parent;

    if (!surf || !surf->mapped)
      return false;
  }

  return true;
}

static bool _content_update_enqueue(struct vt_content_update_t *cu) {
  if (!cu || !cu->surf || cu->queued)
    return false;

  struct vt_surface_t *surf = cu->surf;
  if (!wl_list_empty(&surf->content_updates)) {
    struct vt_content_update_t *prev =
        wl_container_of(surf->content_updates.prev, prev, queue_link);

    if (!vt_content_update_add_dependency(cu, prev)) {
      return false;
    }
  }

  wl_list_insert(cu->surf->content_updates.prev, &cu->queue_link);
  cu->queued = true;

  return true;
}

static bool
_content_update_add_child_dependencies(struct vt_content_update_t *cu) {
  if (!cu || !cu->surf)
    return false;

  struct vt_surface_t *surf = cu->surf;

  struct vt_subsurface_t *sub;

  wl_list_for_each(sub, &surf->subsurface.childs, link) {
    struct vt_surface_t *child = sub->surf;

    if (!child)
      continue;

    struct vt_content_update_t *last_scu = vt_surface_last_scu(child);

    if (!last_scu)
      continue;

    if (vt_content_update_reaches(cu, last_scu))
      continue;

    vt_content_update_add_dependency(cu, last_scu);
  }

  return true;
}

bool vt_surface_emit_content_update(struct vt_surface_t *surf) {
  if (!surf)
    return false;

  bool effectively_sync = vt_surface_effectively_synchronized(surf);

  struct vt_content_update_t *cu = vt_content_update_create(
      surf, &surf->pending, effectively_sync ? VT_CU_SYNC : VT_CU_DESYNC);

  if (!cu) {
    VT_ERROR(surf->comp->log, "Failed to create content update for surface %p",
             surf);
    return false;
  }

  /* --- CU injection --- */

  /* Addons commit */
  struct vt_surface_addon_t *it;
  wl_list_for_each(it, &surf->addons, link) {
    if (it->impl.commit) {
      if (!it->impl.commit(surf, cu)) {
        VT_ERROR(surf->comp->log,
                 "Failed to run commit for addon %p of surface %p for CU %p",
                 it, surf, cu);
      }
    }
  }

  /* Role commit */
  if (surf->role.impl && surf->role.impl->commit) {
    if (!surf->role.impl->commit(surf, cu)) {
      VT_ERROR(
          surf->comp->log,
          "Failed to run role-specific commit for CU %p of surface %p",
          cu, surf);
      return false;
    }
  }
  
  /* --- End of CU injection --- */

  if (!vt_content_update_finish_create(cu)) {
    VT_ERROR(surf->comp->log,
             "Failed to finish creation of content update %p for surface %p",
             cu, surf);
    goto fail;
  }

  if (!_content_update_enqueue(cu)) {
    VT_ERROR(surf->comp->log,
             "Failed to enqueue content update %p for surface %p", cu, surf);
    goto fail;
  }

  if (!_content_update_add_child_dependencies(cu)) {
    VT_ERROR(surf->comp->log,
             "Failed to add child dependencies for content update %p", cu);
    goto fail;
  }

  if (cu->type == VT_CU_SYNC) {
    VT_TRACE(surf->comp->log,
             "Queued synchronized content update=%p for surface=%p; "
             "waiting for parent commit",
             cu, surf);

    return true;
  }

  bool applied = vt_content_update_apply_dag(cu);

  if (applied) {
    VT_TRACE(surf->comp->log,
             "Successfully applied DAG of %s content update=%p",
             cu->type == VT_CU_SYNC ? "synchronized" : "desynchronized", cu);

    if (surf->role.impl && surf->role.impl->apply) {
      if (!surf->role.impl->apply(surf, cu))
        return false;
    }
  } else {
    VT_TRACE(surf->comp->log, "Failed to apply DAG of %s content update=%p",
             cu->type == VT_CU_SYNC ? "synchronized" : "desynchronized", cu);
    goto fail;
  }

  return true;
fail:
  vt_content_update_destroy(cu);
  return false;
}

struct vt_content_update_t *vt_surface_last_scu(struct vt_surface_t *surf) {
  if (!surf)
    return NULL;

  struct vt_content_update_t *cu;
  wl_list_for_each_reverse(cu, &surf->content_updates, queue_link) {
    if (cu->type == VT_CU_SYNC) {
      return cu;
    }
  }
  return NULL;
}

struct vt_buffer_release_t *vt_surface_state_get_or_create_buffer_release(
    struct vt_surface_state_pending_t *state) {
  if (!state)
    return NULL;

  if (state->buffer_release)
    return state->buffer_release;

  struct vt_buffer_release_t *release = calloc(1, sizeof(*release));

  if (!release)
    return NULL;

  release->refcount = 1;
  release->fence_fd = -1;
  wl_list_init(&release->callbacks);

  state->buffer_release = release;

  return release;
}

void vt_surface_frame_done(struct vt_surface_t *surf,
                           uint32_t             frame_time_msec) {
  if (!surf)
    return;

  struct vt_surface_frame_callback_t *cb, *tmp;
  wl_list_for_each_safe(cb, tmp, &surf->frame_callbacks, link) {
    wl_callback_send_done(cb->res, frame_time_msec);

    VT_TRACE(surf->comp->log, "Sent wl_callback done for surf=%p callback=%p",
             surf, cb);

    wl_resource_destroy(cb->res);
  }
}

bool vt_surface_compute_applied_size(
    const struct vt_surface_t*surf, uint32_t *o_w,
    uint32_t *o_h) {
  if (!surf || !o_w || !o_h)
    return false;

  struct vt_buffer_t* buf = vt_surface_get_buffer(surf);
  struct vt_surface_state_applied_t *state = &surf->applied;

  if (!buf) {
    *o_w = 0;
    *o_h = 0;
    return true;
  }

  uint32_t buffer_scale = (uint32_t)state->buffer_scale;
  uint32_t buffer_w = buf->tex.width; 
  uint32_t buffer_h = buf->tex.height; 

  switch (state->buffer_transform) {
  case WL_OUTPUT_TRANSFORM_90:
  case WL_OUTPUT_TRANSFORM_270:
  case WL_OUTPUT_TRANSFORM_FLIPPED_90:
  case WL_OUTPUT_TRANSFORM_FLIPPED_270: {
    int32_t tmp = buffer_w;
    buffer_w = buffer_h;
    buffer_h = tmp;
    break;
  }

  default:
    break;
  }

  if (buffer_w % buffer_scale != 0 || buffer_h % buffer_scale != 0) {
    return false;
  }

  buffer_w /= buffer_scale;
  buffer_h /= buffer_scale;

  *o_w = buffer_w;
  *o_h = buffer_h;

  return true;
}

bool vt_surface_set_role(struct vt_surface_t                 *surf,
                         const struct vt_surface_role_impl_t *impl,
                         void *data) {
  if (!surf || !impl)
    return false;

  /* Surface has never had a role. */
  if (!surf->role.impl) {
    surf->role.impl = impl;
    surf->role.data = data;
    return true;
  }

  if (surf->role.impl->type == impl->type) {
    surf->role.data = data;
    return true;
  }

  /* A wl_surface can never change role. */
  return false;
}

bool vt_surface_has_role(struct vt_surface_t        *surf,
                         enum vt_surface_role_type_t type) {
  if(!surf) return false;
  return surf->role.impl && surf->role.impl->type == type;
}

struct vt_buffer_t *vt_surface_get_buffer(struct vt_surface_t *surf) {
  if (!surf)
    return NULL;
  return surf->current_buf_use ? surf->current_buf_use->buf : NULL;
}
