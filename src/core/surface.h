#pragma once 

#define VT_MAX_FRAME_CBS 8 

#include <stdint.h>
#include <runara/runara.h>
#include "core_types.h"

struct vt_frame_cb_pool {
  struct wl_resource* cbs[VT_MAX_FRAME_CBS];
  uint32_t n_cbs;
};

struct vt_surface_t {
  struct wl_resource* surf_res;
  struct wl_resource* buf_res;

  struct vt_xdg_surface_t* xdg_surf;

  RnTexture tex; 

  struct wl_list link;
  
  struct vt_compositor_t* comp;

  uint32_t width, height;
  int32_t x, y;

  bool needs_frame_done;

  bool has_buffer, mapped;

  uint32_t _mask_outputs_visible_on;
  uint32_t _mask_outputs_presented_on;

  void* user_data;

  pixman_region32_t current_damage, pending_damage;
  bool damaged;

  struct vt_frame_cb_pool cb_pool;
};

void vt_surface_mapped(struct vt_surface_t* surf);

void vt_surface_unmapped(struct vt_surface_t* surf);
