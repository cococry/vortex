#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <wayland-util.h>

struct vt_drm_format_modifier_t {
  uint64_t mod;
  bool     _egl_ext_only;
};

struct vt_drm_format_t {
  uint32_t        format;
  struct wl_array mods; /* struct vt_drm_format_modifier_t */
};

void vt_drm_format_init(struct vt_drm_format_t* fmt, uint32_t format); 

struct vt_drm_format_modifier_t* vt_drm_format_get_mod(struct vt_drm_format_t *fmt, uint64_t mod);

struct vt_drm_format_modifier_t* vt_drm_format_add_mod(struct vt_drm_format_t *fmt, uint64_t mod);

void vt_drm_format_fini(struct vt_drm_format_t *fmt); 

bool vt_drm_format_add_mods(struct vt_drm_format_t *fmt, struct wl_array *mods);

size_t vt_drm_format_mod_count(const struct vt_drm_format_t *fmt);

size_t vt_drm_format_array_count(const struct wl_array *formats);

struct vt_drm_format_t *vt_drm_format_array_push(struct wl_array *formats,
                                                 struct vt_drm_format_t* fmt);

struct vt_drm_format_t *vt_drm_format_array_push_pair(struct wl_array *formats,
                                                      uint32_t         format,
                                                      uint64_t         mod);

struct vt_drm_format_t *
vt_drm_format_array_get_format(struct wl_array              *formats,
                               const struct vt_drm_format_t *find);

void vt_drm_format_array_free(struct wl_array *formats);
