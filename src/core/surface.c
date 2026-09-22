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

#include "surface.h"
#include "../input/wl_seat.h"
#include "src/core/buffer.h"
#include "src/core/content_update.h"
#include "src/core/core_types.h"
#include "src/core/focus_policy.h"
#include "src/core/scene.h"
#include "src/core/surface_addon.h"
#include "src/core/util.h"
#include "src/protocols/wl_subcompositor.h"
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#define _SUBSYS_NAME "SURFACE"

static void _surface_update_mapping(struct vt_surface_t *surf);

static void _surface_update_mapping(struct vt_surface_t *surf) {
  bool mapped = surf->current_buf_use != NULL;

  vt_surface_set_mapped(surf, mapped);
}

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

void vt_surface_set_mapped(struct vt_surface_t *surf, bool mapped) {
  if (!surf || !surf->comp || surf->mapped == mapped)
    return;

  struct vt_seat_t *seat = surf->comp->seat;

  surf->mapped = mapped;

  if (surf->role.impl && surf->role.impl->mapping_changed) {
    surf->role.impl->mapping_changed(surf, mapped);
  }

  if (seat) {
    if (mapped) {
      vt_focus_policy_mapped(surf->comp, surf);
    } else {
      vt_focus_policy_unmapped(surf->comp, surf);
      vt_seat_handle_surface_unmapped(seat, surf);
    }

    vt_seat_repick_pointer_focus(seat);
  }

}

struct vt_surface_t *focus_stack_pop(struct vt_compositor_t *comp) {
  if (!comp || !comp->seat)
    return NULL;

  struct wl_list *stack = &comp->focus_stack;

  if (wl_list_empty(stack))
    return NULL;

  struct vt_surface_t *surf = wl_container_of(stack->next, surf, link_focus);

  return surf;
}

bool vt_surface_apply_buffer_use(struct vt_surface_t    *surf,
                                 struct vt_buffer_use_t *new_use) {
  if (!surf)
    return false;

  struct vt_buffer_use_t *old = surf->current_buf_use;

  surf->current_buf_use = new_use;
  if (old) {
    vt_buffer_use_unref(&old);
  }

  if (!vt_scene_node_damage_whole(surf->comp, surf->scene_node))
    return false;

  return true;
}

void vt_surface_apply_pending_frame_callbacks(
    struct vt_surface_t *surf, struct vt_surface_state_pending_t *state) {
  if (wl_list_empty(&state->frame_callbacks))
    return;

  wl_list_insert_list(surf->frame_callbacks.prev, &state->frame_callbacks);

  wl_list_init(&state->frame_callbacks);
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
  vt_surface_pending_state_defaults(dst);

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
    if (!surf->role.impl ||
        !vt_surface_has_role(surf, VT_SURFACE_ROLE_SUBSURFACE))
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

bool vt_surface_emit_content_update(struct vt_surface_t *surf)
{
    if (!surf)
        return false;

    bool effectively_sync =
        vt_surface_effectively_synchronized(surf);

    struct vt_content_update_t *cu =
        vt_content_update_create(
            surf,
            &surf->pending,
            effectively_sync
                ? VT_CU_SYNC
                : VT_CU_DESYNC);

    if (!cu)
        return false;

    /* Addons capture */
    struct vt_surface_addon_t *it;
    wl_list_for_each(it, &surf->addons, link) {
        if (it->impl.commit &&
            !it->impl.commit(surf, cu))
            goto fail;
    }

    /* Role capture */
    if (surf->role.impl &&
        surf->role.impl->commit &&
        !surf->role.impl->commit(surf, cu))
        goto fail;

    if (!vt_content_update_finish_create(cu))
        goto fail;

    if (!_content_update_enqueue(cu))
        goto fail;

    if (!_content_update_add_child_dependencies(cu))
        goto fail;

    if (cu->type == VT_CU_SYNC)
        return true;

    if (!vt_content_update_apply_dag(cu))
        goto fail;

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
    struct vt_renderer_t *renderer, struct vt_surface_state_pending_t *state) {
  if (!renderer || !state)
    return NULL;

  if (state->buffer_release)
    return state->buffer_release;

  struct vt_buffer_release_t *release = calloc(1, sizeof(*release));

  if (!release)
    return NULL;
  
  release->renderer = renderer;

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

bool vt_surface_compute_final_size(const struct vt_surface_t *surf,
                                   uint32_t                   buffer_scale,
                                   uint32_t buffer_transform, uint32_t *o_w,
                                   uint32_t *o_h) {
  if (!surf || !o_w || !o_h)
    return false;

  struct vt_buffer_t *buf = vt_surface_get_buffer(surf);

  if (!buf) {
    *o_w = 0;
    *o_h = 0;
    return true;
  }

  uint32_t buffer_w = buf->tex.width;
  uint32_t buffer_h = buf->tex.height;

  switch (buffer_transform) {
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
                         void                                *data) {
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
  if (!surf)
    return false;
  return surf->role.impl && surf->role.impl->type == type;
}

struct vt_buffer_t *vt_surface_get_buffer(struct vt_surface_t *surf) {
  if (!surf)
    return NULL;
  return surf->current_buf_use ? surf->current_buf_use->buf : NULL;
}
