#pragma once

#include "surface.h"

enum vt_content_update_type_t {
  VT_CU_SYNC = 0,
  VT_CU_DESYNC,
};

struct vt_content_update_t {
  struct vt_surface_t *surf;

  struct vt_surface_state_pending_t state;

  enum vt_content_update_type_t type;

  struct wl_list queue_link;
  struct wl_list retire_link;
  struct wl_list constraints;
  struct wl_list dependants;
  struct wl_list dependencies;

  bool applied, queued, prepared;

  struct vt_buffer_use_t *buffer_use;
  int                     acquire_fence_fd;
};

struct vt_content_update_dependency_t {
  struct vt_content_update_t *cu;
  struct vt_content_update_t *dependency;

  struct wl_list dependant_link;
  struct wl_list dependency_link;
};

struct vt_content_update_t *
vt_content_update_create(struct vt_surface_t                     *surf,
                         struct vt_surface_state_pending_t *state,
                         enum vt_content_update_type_t            type);

void vt_content_update_destroy(struct vt_content_update_t *cu);

void vt_content_update_dependency_destroy(
    struct vt_content_update_dependency_t *dependency);

bool vt_content_update_apply(struct vt_content_update_t *cu);

bool vt_content_update_add_dependency(struct vt_content_update_t *cu,
                                      struct vt_content_update_t *dependency);

bool vt_content_update_is_candidate(const struct vt_content_update_t *cu);

bool vt_content_update_is_ready(const struct vt_content_update_t *cu);

bool vt_content_update_apply_dag(struct vt_content_update_t *root);

bool vt_content_update_reaches(struct vt_content_update_t *from, struct vt_content_update_t* target);
