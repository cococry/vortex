#pragma once

#include "../protocols/linux_dmabuf.h"
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
  struct wl_list       link;
};

struct vt_surface_state_applied_t {
  pixman_region32_t input_region;
  bool              input_region_infinite;

  pixman_region32_t opaque_region;

  int32_t buffer_transform;
  int32_t buffer_scale;

  struct vt_buffer_t *buf;

  int32_t width;
  int32_t height;

  pixman_region32_t damage;
};

struct vt_surface_state_pending_t {
  bool              input_region_changed;
  pixman_region32_t input_region;
  bool              input_region_infinite;

  bool              opaque_region_changed;
  pixman_region32_t opaque_region;

  bool    buffer_transform_changed;
  int32_t buffer_transform;

  bool    buffer_scale_changed;
  int32_t buffer_scale;

  bool                buffer_attached;
  struct vt_buffer_t *buf;

  pixman_region32_t damage_surface;
  pixman_region32_t damage_buffer;

  bool    offset_set;
  int32_t offset_x;
  int32_t offset_y;

  struct wl_list frame_callbacks;
  struct wl_list release_callbacks;
};

enum vt_surface_type_t {
  VT_SURFACE_TYPE_NORMAL = 0,
  VT_SURFACE_TYPE_CURSOR = 1,
};

struct vt_linux_dmabuf_v1_surface_t;

struct vt_surface_role_impl_t {
  bool (*validate_commit)(struct vt_surface_addon_t *addon);

  bool (*commit)(struct vt_surface_addon_t  *addon,
                 struct vt_content_update_t *cu);
};

enum vt_surface_role_type_t {
  VT_SURFACE_ROLE_NONE = 0,

  VT_SURFACE_ROLE_CURSOR,
  VT_SURFACE_ROLE_DRAG_ICON,

  VT_SURFACE_ROLE_SUBSURFACE,

  VT_SURFACE_ROLE_XDG_TOPLEVEL,
  VT_SURFACE_ROLE_XDG_POPUP,

  VT_SURFACE_ROLE_LAYER_SURFACE,
  VT_SURFACE_ROLE_SESSION_LOCK,

  VT_SURFACE_ROLE_XWAYLAND,
};

struct vt_surface_role_t {
  enum vt_surface_role_type_t   type;
  struct vt_surface_role_impl_t impl;
  void                         *data;
};

struct vt_surface_frame_callback_t {
  struct wl_resource *res;
  struct wl_list      link;
};

struct vt_surface_t {
  struct wl_resource     *res;
  struct vt_compositor_t *comp;

  struct vt_surface_role_t* role;

  struct vt_surface_state_pending_t pending;
  struct vt_surface_state_applied_t applied;

  struct wl_list content_updates;

  struct vt_scene_node_t *scene_node;

  struct wl_list addons;

  struct {
    struct vt_linux_dmabuf_v1_surface_state_t *linux_dmabuf_v1;
  } proto_state;

  struct wl_list link, link_focus;

  bool damaged;
  bool mapped;

  uint32_t outputs_visible_on;
  uint32_t outputs_presented_on;

  struct {
    struct wl_list childs;
    struct wl_list link_parent;
  } subsurface;
};

bool vt_surface_init(struct vt_surface_t* surf);

void vt_surface_mapped(struct vt_surface_t *surf);

void vt_surface_unmapped(struct vt_surface_t *surf);

void vt_surface_apply_buffer(struct vt_surface_t *surf,
                             struct vt_buffer_t  *buf);

void vt_surface_pending_state_init(struct vt_surface_state_pending_t *state);

void vt_surface_pending_state_defaults(
    struct vt_surface_state_pending_t *state);

void vt_surface_applied_state_init(struct vt_surface_state_applied_t *state);

void vt_surface_applied_state_defaults(
    struct vt_surface_state_applied_t *state);

void vt_surface_pending_state_move(struct vt_surface_state_pending_t *dst,
                                   struct vt_surface_state_pending_t *src);

void vt_surface_pending_state_fini(struct vt_surface_state_pending_t *state);

bool vt_surface_validate_commit(struct vt_surface_t *surf);

bool vt_surface_effictively_synchronized(struct vt_surface_t *surf);

bool vt_surface_emit_content_update(struct vt_surface_t *surf);

struct vt_content_update_t* vt_surface_last_scu(struct vt_surface_t *surf);
