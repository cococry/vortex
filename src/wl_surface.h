#pragma once 

#define VT_MAX_FRAME_CBS 8 

#include <stdint.h>
#include "core/backend.h"

typedef struct {
  struct wl_resource* cbs[VT_MAX_FRAME_CBS];
  uint32_t n_cbs;
} vt_frame_cb_pool;

typedef struct vt_surface_t vt_surface_t;
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

  uint32_t _mask_outputs_visible_on;
  uint32_t _mask_outputs_presented_on;

  void* user_data;

  pixman_region32_t current_damage, pending_damage;
  bool damaged;

  vt_frame_cb_pool cb_pool;
};
