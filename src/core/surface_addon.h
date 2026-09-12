#pragma once

#include <stdbool.h>
#include <wayland-util.h>

struct vt_surface_addon_t;

struct vt_surface_addon_impl_t {
  const char *name;

  void (*destroy)(struct vt_surface_addon_t *addon);

  bool (*validate_commit)(
      struct vt_surface_addon_t *addon);

  bool (*commit)(
      struct vt_surface_addon_t *addon,
      struct vt_content_update_t *cu);
};

struct vt_surface_addon_t {
  struct wl_list link;

  struct vt_surface_addon_impl_t impl;
};

void vt_surface_addon_destroy(struct vt_surface_addon_t* addon);
