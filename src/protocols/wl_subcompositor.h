#pragma once

#include "../core/core_types.h"
#include "../core/scene.h"

struct vt_subsurface_t {
  struct wl_resource *resource;

  struct vt_surface_t *surf;
  struct vt_surface_t *parent;

  bool synchronized;

  struct wl_list parent_link;

  struct vt_scene_node_t *scene_node;
};

bool vt_proto_wl_subcompositor_init(struct vt_compositor_t *comp);
