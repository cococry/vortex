#include "content_update.h"
#include "src/core/surface.h"
#include <wayland-util.h>

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

  vt_surface_pending_state_move(&update->state, state);

  wl_list_init(&update->constraints);
  wl_list_init(&update->dependencies);
  wl_list_init(&update->dependants);
  wl_list_init(&update->queue_link);
  wl_list_init(&update->retire_link);

  return update;
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

bool vt_content_update_prepare(struct vt_content_update_t *cu) {
  if(!cu || !cu->surf) return false;

  if(!cu->state.buffer_attached) return true;

  if (!vt_buffer_import(cu->state.buf, &cu->surf->applied.damage)) {
    return false;
  }

  return true;
}

bool vt_content_update_apply(struct vt_content_update_t *cu) {
  if (!cu || !cu->surf)
    return false;

  struct vt_surface_t               *surf = cu->surf;
  struct vt_surface_state_pending_t *s = &cu->state;

  if (s->buffer_attached) {
    vt_surface_apply_buffer(surf, s->buf);
  }

  if (s->buffer_scale_changed)
    surf->applied.buffer_scale = s->buffer_scale;

  if (s->buffer_transform_changed)
    surf->applied.buffer_transform = s->buffer_transform;

  if (s->input_region_changed) {
    pixman_region32_copy(&surf->applied.input_region, &s->input_region);

    surf->applied.input_region_infinite = s->input_region_infinite;
  }

  if (s->opaque_region_changed) {
    pixman_region32_copy(&surf->applied.opaque_region, &s->opaque_region);
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

  if (!vt_content_update_prepare(cu))
    return false;

  cu->prepared = true;

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

  if (!vt_content_update_apply(cu))
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

  if (!vt_content_update_is_candidate(root))
    return false;

  if (!vt_content_update_is_ready(root))
    return false;
  
  if (!_vt_content_update_prepare_dag_recursive(root))
    return false;

  if (!_vt_content_update_apply_dag_recursive(root))
    return false;

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
