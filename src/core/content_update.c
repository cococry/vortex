#include "content_update.h"
#include "src/core/surface.h"
#include "src/core/util.h"
#include <wayland-util.h>

#define _SUBSYS_NAME "CONTENT-UPDATE"

struct vt_content_update_t *
vt_content_update_create(struct vt_surface_t               *surf,
                         struct vt_surface_state_pending_t *state,
                         enum vt_content_update_type_t      type) {
  if (!surf || !state)
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
           surf, state->buf, state->buffer_release,
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
  if (!cu)
    return false;

  if (cu->state.buffer_attached && cu->state.buf) {
    cu->buffer_use = vt_buffer_use_create_take(
        &cu->state.buf, &cu->state.buffer_release, &cu->acquire_fence_fd);

    if (!cu->buffer_use)
      return false;
  }

  if (cu->surf) {
    VT_TRACE(cu->surf->comp->log,
             "Finished creating content update cu=%p for "
             "surface=%p with "
             "buffer use=%p",
             cu, cu->surf, cu->buffer_use);
  }

  return true;
}

void vt_content_update_destroy(struct vt_content_update_t *cu) {
  if (!cu)
    return;

  struct vt_content_update_dependency_t *it, *tmp;
  wl_list_for_each_safe(it, tmp, &cu->dependencies, dependency_link) {
    vt_content_update_dependency_destroy(it);
  }

  wl_list_for_each_safe(it, tmp, &cu->dependants, dependant_link) {
    vt_content_update_dependency_destroy(it);
  }

  vt_surface_pending_state_fini(&cu->state);

  if (cu->queued) {
    wl_list_remove(&cu->queue_link);
  }

  free(cu);
}

void vt_content_update_dependency_destroy(
    struct vt_content_update_dependency_t *edge) {
  if (!edge)
    return;

  wl_list_remove(&edge->dependency_link);
  wl_list_remove(&edge->dependant_link);

  free(edge);
}

static bool _content_update_prepare_buffer(struct vt_content_update_t *cu) {
  if (!cu || !cu->surf)
    return false;

  if (!cu->state.buffer_attached)
    return true;

  /* wl_surface.attach(NULL) */
  if (!cu->buffer_use)
    return true;

  if (!vt_buffer_import(cu->buffer_use->buf, &cu->state.damage_surface)) {
    return false;
  }

  return true;
}

static void _apply_frame_callbacks(struct vt_surface_t               *surf,
                                   struct vt_surface_state_pending_t *state) {
  if (wl_list_empty(&state->frame_callbacks))
    return;

  wl_list_insert_list(surf->frame_callbacks.prev, &state->frame_callbacks);

  wl_list_init(&state->frame_callbacks);
}

static bool _apply_surface_state(struct vt_content_update_t *cu) {
  if (!cu || !cu->surf)
    return false;

  struct vt_surface_t               *surf = cu->surf;
  struct vt_surface_state_pending_t *pending = &cu->state;

  if (pending->buffer_attached) {
    vt_surface_apply_buffer_use(surf, cu->buffer_use);
    cu->buffer_use = NULL;
  }

  _apply_frame_callbacks(surf, pending);

  if (pending->buffer_scale_changed)
    surf->applied.buffer_scale = pending->buffer_scale;

  if (pending->buffer_transform_changed)
    surf->applied.buffer_transform = pending->buffer_transform;

  vt_surface_compute_applied_size(surf, &surf->applied.width,
                                  &surf->applied.height);

  if (pending->input_region_changed) {
    pixman_region32_copy(&surf->applied.input_region, &pending->input_region);

    surf->applied.input_region_infinite = pending->input_region_infinite;
  }

  if (pending->opaque_region_changed) {
    pixman_region32_copy(&surf->applied.opaque_region, &pending->opaque_region);
  }

  return true;
}

bool vt_content_update_add_dependency(struct vt_content_update_t *cu,
                                      struct vt_content_update_t *dependency) {
  if (!cu || !dependency || cu == dependency)
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

bool vt_content_update_is_candidate(const struct vt_content_update_t *cu) {
  if (!cu)
    return false;

  if (cu->type != VT_CU_DESYNC)
    return false;

  const struct vt_content_update_t *it;
  wl_list_for_each(it, &cu->surf->content_updates, queue_link) {
    if (it == cu)
      return true;

    if (it->type == VT_CU_SYNC)
      return false;
  }

  return false;
}

bool vt_content_update_is_ready(const struct vt_content_update_t *cu) {
  if (!cu)
    return false;

  if (!wl_list_empty(&cu->constraints))
    return false;

  const struct vt_content_update_dependency_t *edge;

  wl_list_for_each(edge, &cu->dependencies, dependency_link) {
    if (!vt_content_update_is_ready(edge->dependency))
      return false;
  }

  return true;
}

static bool
_vt_content_update_prepare_dag_recursive(struct vt_content_update_t *cu) {
  if (!cu)
    return false;

  if (cu->prepared)
    return true;

  struct vt_content_update_dependency_t *edge;
  wl_list_for_each(edge, &cu->dependencies, dependency_link) {
    if (!_vt_content_update_prepare_dag_recursive(edge->dependency))
      return false;
  }

  if (!_content_update_prepare_buffer(cu))
    return false;

  cu->prepared = true;

  return true;
}

static bool
_content_update_apply_one(struct vt_content_update_t *cu)
{
    struct vt_surface_t *surf = cu->surf;

    _apply_surface_state(cu);

    if (surf->role.impl && surf->role.impl->apply) {
        if (!surf->role.impl->apply(surf, cu))
            return false;
    }

    vt_scene_node_mark_geometry_dirty(surf->scene_node);

    vt_surface_set_mapped(
        surf,
        surf->current_buf_use != NULL);

    vt_scene_node_damage_whole(
        surf->comp,
        surf->scene_node);

    return true;
}


static bool
_vt_content_update_apply_dag_recursive(struct vt_content_update_t *cu) {
  if (!cu)
    return false;

  if (cu->applied)
    return true;

  struct vt_content_update_dependency_t *edge;
  wl_list_for_each(edge, &cu->dependencies, dependency_link) {
    if (!_vt_content_update_apply_dag_recursive(edge->dependency))
      return false;
  }

  if (!_content_update_apply_one(cu))
    return false;

  cu->applied = true;

  return true;
}

static void _collect_applied_dag(struct vt_content_update_t *cu,
                                 struct wl_list             *list) {
  if (!cu || !cu->applied)
    return;

  if (!wl_list_empty(&cu->retire_link))
    return;

  wl_list_insert(list, &cu->retire_link);

  struct vt_content_update_dependency_t *edge;
  wl_list_for_each(edge, &cu->dependencies, dependency_link) {
    _collect_applied_dag(edge->dependency, list);
  }
}

static void _retire_applied_dag(struct vt_content_update_t *root) {
  struct wl_list applied;
  wl_list_init(&applied);
  _collect_applied_dag(root, &applied);

  struct vt_content_update_t *retired, *tmp;
  wl_list_for_each_safe(retired, tmp, &applied, retire_link) {
    wl_list_remove(&retired->retire_link);
    wl_list_init(&retired->retire_link);

    vt_content_update_destroy(retired);
  }
}
bool vt_content_update_apply_dag(struct vt_content_update_t *root) {
  if (!root)
    return false;

  VT_TRACE(root->surf->comp->log,
           "APPLY DAG: root=%p type=%d", root, root->type);

  if (!vt_content_update_is_candidate(root)) {
    VT_TRACE(root->surf->comp->log,
             "APPLY DAG: root=%p NOT CANDIDATE", root);
    return false;
  }

  VT_TRACE(root->surf->comp->log,
           "APPLY DAG: root=%p is candidate", root);

  if (!vt_content_update_is_ready(root)) {
    VT_TRACE(root->surf->comp->log,
             "APPLY DAG: root=%p NOT READY", root);
    return false;
  }

  VT_TRACE(root->surf->comp->log,
           "APPLY DAG: root=%p is ready", root);

  if (!_vt_content_update_prepare_dag_recursive(root)) {
    VT_TRACE(root->surf->comp->log,
             "APPLY DAG: root=%p PREPARE FAILED", root);
    return false;
  }

  VT_TRACE(root->surf->comp->log,
           "APPLY DAG: root=%p prepared", root);

  if (!_vt_content_update_apply_dag_recursive(root)) {
    VT_TRACE(root->surf->comp->log,
             "APPLY DAG: root=%p APPLY FAILED", root);
    return false;
  }

  VT_TRACE(root->surf->comp->log,
           "APPLY DAG: root=%p applied, retiring", root);

  _retire_applied_dag(root);

  return true;
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
