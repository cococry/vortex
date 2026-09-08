#include "content_update.h"
#include "src/core/surface.h"
#include <wayland-util.h>

struct vt_content_update_t *
vt_content_update_create(struct vt_surface_t                     *surf,
                         const struct vt_surface_state_pending_t *state,
                         enum vt_content_update_type_t            type) {
  if (!surf || !state)
    return NULL;

  struct vt_content_update_t *update = calloc(1, sizeof(*update));
  if (!update) {
    return NULL;
  }

  update->surf = surf;
  update->state = *state;
  update->type = type;

  wl_list_init(&update->constraints);
  wl_list_init(&update->dependencies);
  wl_list_init(&update->dependants);

  return update;
}

void vt_content_update_destroy(struct vt_content_update_t *cu) {
  if (!cu)
    return;

  free(cu);
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
  if (!cu || !dependency)
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

bool vt_content_update_apply_dag(struct vt_content_update_t *root) {
  if (!root)
    return false;

  if (!vt_content_update_is_candidate(root))
    return false;

  if (!vt_content_update_is_ready(root))
    return false;

  return _vt_content_update_apply_dag_recursive(root);
}
