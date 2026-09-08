#pragma once

#include <wayland-util.h>

struct vt_surface_addon_t {
  struct wl_list link;

  const struct vt_surface_addon_impl *impl;
};

struct vt_surface_addon_impl_t {
  const char *name;

  void (*destroy)(struct vt_surface_addon_t *addon);
};

