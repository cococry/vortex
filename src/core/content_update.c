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

#include "content_update.h"
#include "src/core/compositor.h"
#include "src/core/core_types.h"
#include "src/core/surface.h"
#include "src/core/util.h"
#include "src/protocols/wl_subcompositor.h"
#include "src/render/renderer.h"
#include <wayland-util.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define _SUBSYS_NAME "CONTENT-UPDATE"

static void
_content_update_dependency_destroy(struct vt_content_update_dependency_t *edge);

static bool _content_update_prepare_buffer(struct vt_content_update_t *cu);

static bool _content_update_is_ready(const struct vt_content_update_t *cu);

static bool _content_update_is_candidate(const struct vt_content_update_t *cu);

static bool
_content_update_prepare_dag_recursive(struct vt_content_update_t *cu);

static bool _content_update_apply_surface_state(struct vt_content_update_t *cu);

static bool _content_update_apply_one(struct vt_content_update_t *cu);

static bool _content_update_apply_dag_recursive(struct vt_content_update_t *cu);

static void _content_update_collect_applied_dag(struct vt_content_update_t *cu,
                                                struct wl_list *list);

static void
_content_update_retire_applied_dag(struct vt_content_update_t *root);

static void _content_update_dependency_destroy(
    struct vt_content_update_dependency_t *edge) {
  if (!edge)
    return;

  wl_list_remove(&edge->dependency_link);
  wl_list_remove(&edge->dependant_link);

  free(edge);
}

static bool _content_update_prepare_buffer(struct vt_content_update_t *cu) {
  assert(cu && cu->surf && cu->surf->comp && cu->surf->comp->renderer);

  if (!cu->state.buffer_attached)
    return true;

  /* wl_surface.attach(NULL) */
  if (!cu->buffer_use)
    return true;

  assert(cu->buffer_use->buf);

  /* TODO: For correctness, we should make sure that that this buffer is
   * importable. If the renderer import fails after DAG applying, the atomic
   * apply path would fail, which should not happen. */
  /*
   * if (r->impl.import_buffer &&
      !r->impl.import_buffer(r, cu->buffer_use->buf, &cu->state.damage_surface))
    return false;
    */

  return true;
}

static bool _content_update_is_ready(const struct vt_content_update_t *cu) {
  if (!cu)
    return false;

  if (!wl_list_empty(&cu->constraints))
    return false;

  const struct vt_content_update_dependency_t *edge;

  wl_list_for_each(edge, &cu->dependencies, dependency_link) {
    if (!_content_update_is_ready(edge->dependency))
      return false;
  }

  return true;
}

static bool _content_update_is_candidate(const struct vt_content_update_t *cu) {
  if (!cu)
    return false;

  if (cu->type != VT_CU_DESYNC || wl_list_empty(&cu->surf->content_updates))
    return false;

  // anything queued ahead is unapplied (sync, or held by a constraint), and
  // applying past it would show frames out of order
  const struct vt_content_update_t *head =
      wl_container_of(cu->surf->content_updates.next, head, queue_link);
  return head == cu;
}

static bool
_content_update_prepare_dag_recursive(struct vt_content_update_t *cu) {
  if (!cu || !cu->surf)
    return false;

  if (cu->prepared)
    return true;

  struct vt_content_update_dependency_t *edge;
  wl_list_for_each(edge, &cu->dependencies, dependency_link) {
    if (!_content_update_prepare_dag_recursive(edge->dependency))
      return false;
  }

  /* If a content update fails any part of preperation, it must not be applied.
   */
  {
    int32_t _w, _h;
    if (!vt_surface_compute_final_size(cu->surf, cu->state.buffer_scale,
                                       cu->state.buffer_transform, &_w, &_h)) {
      return false;
    }
  }

  if (!_content_update_prepare_buffer(cu))
    return false;

  cu->prepared = true;

  return true;
}

static bool
_content_update_apply_surface_state(struct vt_content_update_t *cu) {
  /* Content update application must assert a valid prepared content update. All
   * failures here must not be permitted. */
  assert(cu && cu->surf);

  struct vt_surface_t               *surf = cu->surf;
  struct vt_surface_state_pending_t *pending = &cu->state;

  if (pending->buffer_attached) {
    bool applied = vt_surface_apply_buffer_use(surf, cu->buffer_use);
    assert(applied);

    if (!applied)
      return false;

    cu->buffer_use = NULL;
  }

  vt_surface_apply_pending_frame_callbacks(surf, pending);

  if (pending->buffer_scale_changed)
    surf->applied.buffer_scale = pending->buffer_scale;

  if (pending->buffer_transform_changed)
    surf->applied.buffer_transform = pending->buffer_transform;

  bool compute_success = vt_surface_compute_final_size(
      surf, surf->applied.buffer_scale, surf->applied.buffer_transform,
      &surf->applied.width, &surf->applied.height);

  assert(compute_success);
  if (!compute_success) {
    return false;
  }

  if (pending->input_region_changed) {
    pixman_region32_copy(&surf->applied.input_region, &pending->input_region);

    surf->applied.input_region_infinite = pending->input_region_infinite;
  }

  if (pending->opaque_region_changed) {
    pixman_region32_copy(&surf->applied.opaque_region, &pending->opaque_region);
  }

  /* we need to accumulate both buffer-damage and surface-damage
   * independently. */
  struct vt_buffer_t *buf = vt_surface_get_buffer(surf);
  if (buf) {
    /* buffer-damage is already in buffer coordinates*/
    pixman_region32_union(&surf->applied.damage, &surf->applied.damage,
                          &pending->damage_buffer);

    /* clip surface-damage to buffer size and convert surface coordinates to
     * buffer coordinates by applying scale and reversing the transform.*/
    pixman_region32_t clipped;
    pixman_region32_init(&clipped);
    pixman_region32_intersect_rect(&clipped, &pending->damage_surface, 0, 0,
                                   surf->applied.width, surf->applied.height);
    int             count;
    pixman_box32_t *rects = pixman_region32_rectangles(&clipped, &count);
    int32_t         scale = surf->applied.buffer_scale;

    uint32_t transform = surf->applied.buffer_transform;
    /* inverses the transform */
    if (!(transform & WL_OUTPUT_TRANSFORM_FLIPPED))
      transform = (4 - transform) & 3;

    for (int i = 0; i < count; i++) {
      struct vt_box_t box = {rects[i].x1 * scale, rects[i].y1 * scale,
                             (rects[i].x2 - rects[i].x1) * scale,
                             (rects[i].y2 - rects[i].y1) * scale};

      box = vt_util_transform_box(box, transform, surf->applied.width * scale,
                                  surf->applied.height * scale);

      pixman_region32_union_rect(&surf->applied.damage, &surf->applied.damage,
                                 box.x, box.y, box.width, box.height);
    }
    pixman_region32_fini(&clipped);
    pixman_region32_intersect_rect(&surf->applied.damage, &surf->applied.damage,
                                   0, 0, buf->width, buf->height);
  } else {
    pixman_region32_clear(&surf->applied.damage);
  }

  return true;
}

static bool _content_update_apply_one(struct vt_content_update_t *cu) {
  bool valid_params = cu && cu->surf;
  assert(valid_params);

  if (!valid_params)
    return false;

  struct vt_surface_t *surf = cu->surf;

  struct vt_box_t old_bounds = {0};
  bool            was_mapped = vt_surface_effectively_mapped(surf);
  if (surf->scene_node)
    old_bounds = *vt_scene_node_get_global_bounds(surf->scene_node);

  bool applied = _content_update_apply_surface_state(cu);
  assert(applied);

  if (!applied)
    return false;

  if (surf->role.impl && surf->role.impl->apply) {
    bool role_applied = surf->role.impl->apply(surf, cu);
    assert(role_applied);

    if (!role_applied)
      return false;
  }

  vt_subsurface_apply_stacking(cu);

  vt_scene_node_mark_geometry_dirty(surf->scene_node);

  vt_surface_set_mapped(surf, surf->current_buf_use != NULL);

  if (surf->proto_state.linux_dmabuf_v1)
    vt_proto_linux_dmabuf_v1_update_surface_feedback(surf);

  if (surf->scene_node && !vt_surface_has_role(surf, VT_SURFACE_ROLE_CURSOR)) {
    struct vt_box_t bounds = *vt_scene_node_get_global_bounds(surf->scene_node);
    bool            mapped = vt_surface_effectively_mapped(surf);
    bool geometry_changed = memcmp(&old_bounds, &bounds, sizeof(bounds)) != 0;
    if (was_mapped != mapped || geometry_changed ||
        cu->state.buffer_transform_changed || cu->state.buffer_scale_changed) {
      if (was_mapped)
        vt_scene_damage_box(surf->comp, &old_bounds);
      if (mapped)
        vt_scene_node_damage_whole(surf->comp, surf->scene_node);
    } else if (mapped) {
      vt_scene_node_damage_buffer(surf->comp, surf->scene_node,
                                  &surf->applied.damage);
    }
    /* A callback-only commit still needs presentation pacing, without
     * inventing pixel damage on unrelated outputs. */
    if (mapped && !pixman_region32_not_empty(&surf->applied.damage) &&
        !wl_list_empty(&surf->frame_callbacks)) {
      struct vt_output_t *output =
          vt_scene_node_primary_output(surf->comp, surf->scene_node);
      if (output)
        vt_comp_schedule_repaint(surf->comp, output);
    }
  }

  return true;
}

static bool
_content_update_apply_dag_recursive(struct vt_content_update_t *cu) {
  bool valid_params = cu != NULL;
  assert(valid_params);

  if (!valid_params)
    return false;

  if (cu->applied)
    return true;

  struct vt_content_update_dependency_t *edge;
  wl_list_for_each(edge, &cu->dependencies, dependency_link) {
    if (!_content_update_apply_dag_recursive(edge->dependency))
      return false;
  }

  bool applied = _content_update_apply_one(cu);
  assert(applied);

  if (!applied)
    return false;

  cu->applied = true;

  return true;
}

static void _content_update_collect_applied_dag(struct vt_content_update_t *cu,
                                                struct wl_list *list) {
  if (!cu || !cu->applied)
    return;

  if (!wl_list_empty(&cu->retire_link))
    return;

  wl_list_insert(list, &cu->retire_link);

  struct vt_content_update_dependency_t *edge;
  wl_list_for_each(edge, &cu->dependencies, dependency_link) {
    _content_update_collect_applied_dag(edge->dependency, list);
  }
}

static void
_content_update_retire_applied_dag(struct vt_content_update_t *root) {
  struct wl_list applied;
  wl_list_init(&applied);
  _content_update_collect_applied_dag(root, &applied);

  struct vt_content_update_t *retired, *tmp;
  wl_list_for_each_safe(retired, tmp, &applied, retire_link) {
    wl_list_remove(&retired->retire_link);
    wl_list_init(&retired->retire_link);

    vt_content_update_destroy(retired);
  }
}

struct vt_content_update_t *
vt_content_update_create(struct vt_surface_t               *surf,
                         struct vt_surface_state_pending_t *state,
                         enum vt_content_update_type_t      type) {
  if (!surf || !state || !surf->comp)
    return NULL;

  struct vt_content_update_t *update = calloc(1, sizeof(*update));
  if (!update) {
    return NULL;
  }

  update->surf = surf;
  update->type = type;

  VT_TRACE(surf->comp->log,
           "Creating content update for surface %p with pending buffer=%p, "
           "buffer_release=%p, buffer_attached=%s",
           (void *)surf, (void *)state->buf, (void *)state->buffer_release,
           state->buffer_attached ? "true" : "false");

  vt_surface_pending_state_move(&update->state, state);

  update->acquire_fence_fd = -1;

  wl_list_init(&update->constraints);
  wl_list_init(&update->dependencies);
  wl_list_init(&update->dependants);
  wl_list_init(&update->queue_link);
  wl_list_init(&update->retire_link);

  return update;
}

bool vt_content_update_finish_create(struct vt_content_update_t *cu) {
  assert(cu && cu->surf && cu->surf->comp && cu->surf->comp->renderer);

  struct vt_renderer_t *r = cu->surf->comp->renderer;

  if (cu->state.buffer_attached && cu->state.buf) {
    /* Transfers ownership of a potentially pending acquire_fence_fd to the
     * buffer use */
    cu->buffer_use = vt_buffer_use_create_take(
        cu->surf->comp, r, &cu->state.buf, &cu->state.buffer_release,
        &cu->acquire_fence_fd);

    if (!cu->buffer_use)
      return false;

    cu->buffer_use->acquire_source = cu->acquire_source;
    cu->acquire_source = NULL;
  }

  VT_TRACE(cu->surf->comp->log,
           "Finished creating content update cu=%p for "
           "surface=%p with "
           "buffer use=%p",
           (void *)cu, (void *)cu->surf, (void *)cu->buffer_use);

  return true;
}

void vt_content_update_destroy(struct vt_content_update_t *cu) {
  if (!cu)
    return;

  struct vt_content_update_dependency_t *it, *tmp;
  wl_list_for_each_safe(it, tmp, &cu->dependencies, dependency_link) {
    _content_update_dependency_destroy(it);
  }

  wl_list_for_each_safe(it, tmp, &cu->dependants, dependant_link) {
    _content_update_dependency_destroy(it);
  }

  struct vt_content_update_constraint_t *c, *c_tmp;
  wl_list_for_each_safe(c, c_tmp, &cu->constraints, link) {
    wl_list_remove(&c->link);
    wl_list_init(&c->link);
    c->destroy(c);
  }

  vt_surface_pending_state_fini(&cu->state);

  if (cu->queued) {
    wl_list_remove(&cu->queue_link);
  }

  if (cu->acquire_fence_fd >= 0) {
    close(cu->acquire_fence_fd);
    cu->acquire_fence_fd = -1;
  }

  if (cu->acquire_source)
    cu->acquire_source->destroy(cu->acquire_source);

  if (cu->buffer_use)
    vt_buffer_use_unref(&cu->buffer_use);

  free(cu->subsurface_stack);
  free(cu);
}

bool vt_content_update_add_dependency(struct vt_content_update_t *cu,
                                      struct vt_content_update_t *dependency) {
  if (!cu || !dependency || cu == dependency ||
      vt_content_update_reaches(dependency, cu))
    return false;

  struct vt_content_update_dependency_t *edge = calloc(1, sizeof(*edge));

  if (!edge) {
    return false;
  }

  edge->cu = cu;
  edge->dependency = dependency;

  wl_list_insert(&cu->dependencies, &edge->dependency_link);
  wl_list_insert(&dependency->dependants, &edge->dependant_link);

  return true;
}

bool vt_content_update_apply_dag(struct vt_content_update_t *root) {
  if (!root || !root->surf || !root->surf->comp)
    return false;

  struct vt_compositor_t *comp = root->surf->comp;

  VT_TRACE(comp->log, "Content update apply DAG: root=%p type=%d", (void *)root,
           root->type);

  if (!_content_update_is_candidate(root)) {
    VT_TRACE(comp->log, "Content update apply DAG: root=%p NOT CANDIDATE",
             (void *)root);
    return false;
  }

  VT_TRACE(comp->log, "Content update apply DAG: root=%p is candidate",
           (void *)root);

  if (!_content_update_is_ready(root)) {
    VT_TRACE(comp->log, "Content update apply DAG: root=%p not ready",
             (void *)root);
    return false;
  }

  VT_TRACE(comp->log, "Content update DAG root=%p is ready", (void *)root);

  if (!_content_update_prepare_dag_recursive(root)) {
    VT_WARN(comp->log, "Content update apply DAG: root=%p prepare failed",
            (void *)root);
    return false;
  }

  VT_TRACE(comp->log, "Content update DAG: root=%p prepared", (void *)root);

  if (!_content_update_apply_dag_recursive(root)) {
    log_fatal(comp->log,
              "Content update DAG: root=%p apply failed with valid prepare",
              (void *)root);
    return false;
  }

  VT_TRACE(comp->log, "Content update apply DAG: root=%p applied, retiring",
           (void *)root);

  if (comp->seat)
    vt_seat_repick_pointer_focus(comp->seat);

  _content_update_retire_applied_dag(root);

  return true;
}

bool vt_content_update_waits(const struct vt_content_update_t *cu) {
  assert(cu && cu->surf);

  const struct vt_content_update_t *it;
  wl_list_for_each(it, &cu->surf->content_updates, queue_link) {
    if (!_content_update_is_ready(it))
      return true;
    if (it == cu)
      break;
  }
  return false;
}

void vt_content_update_add_constraint(
    struct vt_content_update_t *cu, struct vt_content_update_constraint_t *c) {
  assert(cu && c && c->destroy);
  wl_list_insert(&cu->constraints, &c->link);
}

static void
_content_update_collect_dependant_surfaces(struct vt_content_update_t *cu,
                                           struct wl_array            *surfs) {
  struct vt_content_update_dependency_t *edge;
  wl_list_for_each(edge, &cu->dependants, dependant_link) {
    struct vt_surface_t **slot = wl_array_add(surfs, sizeof(*slot));
    if (slot)
      *slot = edge->cu->surf;
    _content_update_collect_dependant_surfaces(edge->cu, surfs);
  }
}

// applies queue heads in order until one is not ready yet
static void _content_update_flush_surface(struct vt_surface_t *surf) {
  while (!wl_list_empty(&surf->content_updates)) {
    struct vt_content_update_t *head =
        wl_container_of(surf->content_updates.next, head, queue_link);
    if (!vt_content_update_apply_dag(head))
      return;
  }
}

void vt_content_update_resolve_constraint(
    struct vt_content_update_t *cu, struct vt_content_update_constraint_t *c) {
  assert(cu && c);

  wl_list_remove(&c->link);
  wl_list_init(&c->link);

  // applying retires updates, so gather the affected surfaces up front
  struct wl_array surfs;
  wl_array_init(&surfs);
  struct vt_surface_t **slot = wl_array_add(&surfs, sizeof(*slot));
  if (slot)
    *slot = cu->surf;
  _content_update_collect_dependant_surfaces(cu, &surfs);

  wl_array_for_each(slot, &surfs) { _content_update_flush_surface(*slot); }

  wl_array_release(&surfs);
}

bool vt_content_update_reaches(struct vt_content_update_t *from,
                               struct vt_content_update_t *target) {
  if (!from || !target)
    return false;

  if (from == target)
    return true;

  struct vt_content_update_dependency_t *it;
  wl_list_for_each(it, &from->dependencies, dependency_link) {
    if (vt_content_update_reaches(it->dependency, target))
      return true;
  }

  return false;
}
