#pragma once

#include "../core/core_types.h"

struct vt_linux_explicit_sync_v1_surface_state_t {
  struct vt_surface_addon_t addon;
  struct vt_surface_t      *surf;
  struct wl_list            link;

  struct wl_resource *res;

  int acquire_fence_fd;
};

bool vt_proto_linux_explicit_sync_v1_init(struct vt_compositor_t *comp,
                                          uint32_t                version);

void vt_proto_linux_explicit_sync_v1_err(struct wl_resource *resource,
                                         const char         *msg);
