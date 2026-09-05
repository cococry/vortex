#pragma once

#include "../protocols/linux_dmabuf.h"
#include "../protocols/wl_subcompositor.h"
#include "../protocols/xdg_shell.h"
#include "scene.h"
#include "../core/buffer.h"
#include <wayland-server.h>
#define VT_MAX_FRAME_CBS 8

#include "core_types.h"
#include <runara/runara.h>
#include <stdint.h>

struct vt_frame_cb_pool {
  struct wl_resource *cbs[VT_MAX_FRAME_CBS];
  uint32_t            n_cbs;
};

struct vt_surface_release_t {
  struct wl_resource  *res;
  struct vt_surface_t *pending_surface;
};

struct vt_surface_pending_state_t {
  int32_t                      acquire_fence_fd;
  struct vt_surface_release_t *release;

  pixman_region32_t            input_region;
  bool                         input_region_changed;
  bool                         input_region_set;
  bool                         buffer_attached;

  pixman_region32_t            opaque_region;
  bool                         opaque_region_changed;

  pixman_region32_t            damage;

  struct vt_buffer_t *buf;

  int32_t dx;
  int32_t dy;
};

enum vt_surface_type_t {
  VT_SURFACE_TYPE_NORMAL = 0,
  VT_SURFACE_TYPE_CURSOR = 1,
};

struct vt_linux_dmabuf_v1_surface_t;

struct vt_surface_role_impl_t {
  void (*commit)(struct vt_surface_t *surf);
};

struct vt_surface_t {
  struct wl_resource *surf_res;

  struct vt_buffer_t* buf;

  struct vt_xdg_surface_t *xdg_surf;

  struct wl_list link, link_focus;

  struct vt_compositor_t *comp;

  int32_t  x, y, dx, dy;

  struct vt_surface_role_impl_t role_impl;

  bool needs_frame_done;

  bool mapped;

  uint32_t _mask_outputs_visible_on;
  uint32_t _mask_outputs_presented_on;

  void *user_data;

  pixman_region32_t damage;

  pixman_region32_t opaque_region;

  pixman_region32_t input_region;
  bool              input_region_set;

  int32_t buffer_transform;
  int32_t buffer_scale;

  bool damaged;

  struct vt_frame_cb_pool cb_pool;

  struct vt_surface_pending_state_t pending;

  struct {
    struct wl_resource          *res;
    int                          acquire_fence_fd;
    struct vt_surface_release_t *release;
  } sync;

  struct vt_linux_dmabuf_v1_surface_t *dmabuf_surf;

  enum vt_surface_type_t type;

  struct vt_scene_node_t *scene_node;

  struct wl_list subsurfaces;

  struct vt_subsurface_t *subsurface;
};

void vt_surface_mapped(struct vt_surface_t *surf);

void vt_surface_unmapped(struct vt_surface_t *surf);
