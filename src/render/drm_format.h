#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-util.h>

struct vt_drm_format_modifier_t {
  uint64_t mod;
  bool     _egl_ext_only;
};

struct vt_drm_format_t {
  uint32_t                         format;
  size_t                           len;
  struct vt_drm_format_modifier_t *mods;
};

bool vt_drm_format_has_mod(struct vt_drm_format_t *fmt, uint64_t mod);


void vt_drm_format_free_array(struct wl_array *formats); 
