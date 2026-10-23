#pragma once

#include "core/core_types.h"
#include "core/surface.h"

bool vt_proto_vt_background_init(struct vt_compositor_t *comp);

struct vt_background_t {
  struct wl_resource *resource;
  struct wl_listener surface_destroy;

  struct vt_surface_t *surface;
  struct vt_output_t  *output;
};
