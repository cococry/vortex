#pragma once 

#include "../protocols/linux_dmabuf.h"
#include "../protocols/xdg_shell.h"
#define VT_MAX_FRAME_CBS 8 

#include <stdint.h>
#include <runara/runara.h>
#include "core_types.h"

struct vt_frame_cb_pool {
  struct wl_resource* cbs[VT_MAX_FRAME_CBS];
  uint32_t n_cbs;
};

struct vt_surface_sync_state_t {
  int32_t acquire_fence_fd, release_fence_fd;
  struct wl_resource* res_release;
  struct wl_resource* res;
};

enum vt_surface_type_t {
  VT_SURFACE_TYPE_NORMAL = 0,
  VT_SURFACE_TYPE_CURSOR = 1,
};

struct vt_linux_dmabuf_v1_surface_t;

struct vt_surface_t {
  struct wl_resource* surf_res;
  struct wl_resource* buf_res;

  struct vt_xdg_surface_t* xdg_surf;

  RnTexture tex; 
  void* render_tex_handle;

  struct wl_list link;
  
  struct vt_compositor_t* comp;

  uint32_t width, height;
  int32_t x, y;

  bool needs_frame_done;

  bool has_buffer, mapped;

  uint32_t _mask_outputs_visible_on;
  uint32_t _mask_outputs_presented_on;

  void* user_data;

  pixman_region32_t current_damage;
  pixman_region32_t pending_damage;
  pixman_region32_t opaque_region;
  pixman_region32_t input_region;

  int32_t buffer_transform;
  int32_t buffer_scale;

  bool damaged;

  struct vt_frame_cb_pool cb_pool;

  struct vt_surface_sync_state_t sync;

  struct vt_linux_dmabuf_v1_surface_t* dmabuf_surf;

  enum vt_surface_type_t type;

};

void vt_surface_mapped(struct vt_surface_t* surf);

void vt_surface_unmapped(struct vt_surface_t* surf);
