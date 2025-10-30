#pragma once

#include "../core/core_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <wayland-util.h>

#define VT_DMABUF_PLANES_CAP 4

enum vt_dmabuf_tranche_flags_t {
  VT_DMABUF_TRANCHE_FLAG_DIRECT_SCANOUT = 1 // equivalent to  ZWP_LINUX_DMABUF_FEEDBACK_V1_TRANCHE_FLAGS_SCANOUT
}; 

struct vt_dmabuf_feedback_t {
  struct vt_compositor_t* comp;
  dev_t dev_main;
  struct wl_array tranches; // array of vt_dmabuf_tranche_t 
};

struct vt_dmabuf_tranche_t {
	dev_t target_device;
	enum vt_dmabuf_tranche_flags_t flags;
	struct wl_array formats; // array of vt_dmabuf_drm_format_t
};

struct vt_dmabuf_format_modifier_t {
  uint64_t mod;
  bool _egl_ext_only;
};

struct vt_dmabuf_drm_format_t {
  uint32_t format;
  size_t len;
  struct vt_dmabuf_format_modifier_t* mods;
};

struct vt_dmbuf_attr_t {
	int32_t num_planes;
	uint32_t offsets[VT_DMABUF_PLANES_CAP];
	uint32_t strides[VT_DMABUF_PLANES_CAP];
	int32_t fds[VT_DMABUF_PLANES_CAP];
	int32_t width, height;
	uint32_t format; 
	uint64_t modifier; 
};
