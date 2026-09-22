/*
 * Copyright (c) 2026 Luca Machiedo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include "../core/buffer.h"
#include "../protocols/linux_dmabuf.h"
#include "../protocols/linux_explicit_sync.h"
#include "scene.h"
#include <wayland-server.h>
#define VT_MAX_FRAME_CBS 8

#include "core_types.h"
#include <runara/runara.h>
#include <stdint.h>

struct vt_frame_cb_pool {
  struct wl_resource *cbs[VT_MAX_FRAME_CBS];
  uint32_t            n_cbs;
};

struct vt_surface_state_applied_t {
  pixman_region32_t input_region;
  bool              input_region_infinite;

  pixman_region32_t opaque_region;

  int32_t buffer_transform;
  int32_t buffer_scale;

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

  struct vt_buffer_release_t *buffer_release;
};

enum vt_surface_type_t {
  VT_SURFACE_TYPE_NORMAL = 0,
  VT_SURFACE_TYPE_CURSOR = 1,
};

struct vt_linux_dmabuf_v1_surface_t;

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

struct vt_content_update_t;

struct vt_surface_role_impl_t {
  enum vt_surface_role_type_t type;

  bool (*validate_commit)(struct vt_surface_t *surface);

  bool (*commit)(struct vt_surface_t *surface, struct vt_content_update_t *cu);
  bool (*apply)(struct vt_surface_t *surface, struct vt_content_update_t *cu);

  void (*mapping_changed)(struct vt_surface_t *surface, bool mapped);
};

struct vt_surface_role_t {
  struct vt_surface_role_impl_t *impl;
  void                          *data;
};

struct vt_surface_frame_callback_t {
  struct wl_resource *res;
  struct wl_list      link;
};

struct vt_surface_t {
  struct wl_resource     *res;
  struct vt_compositor_t *comp;

  struct vt_surface_role_t role;

  struct vt_surface_state_pending_t pending;
  struct vt_surface_state_applied_t applied;

  struct vt_buffer_use_t *current_buf_use;

  struct wl_list content_updates;

  struct vt_scene_node_t *scene_node;

  struct wl_list addons;

  struct {
    struct vt_linux_dmabuf_v1_surface_state_t        *linux_dmabuf_v1;
    struct vt_linux_explicit_sync_v1_surface_state_t *linux_explicit_sync_v1;
  } proto_state;

  struct wl_list link, link_focus; 

  bool damaged;
  bool mapped;

  struct {
    struct wl_list childs;
  } subsurface;

  struct wl_list frame_callbacks;
};

bool vt_surface_init(struct vt_surface_t *surf);

void vt_surface_set_mapped(struct vt_surface_t *surf, bool mapped);

void vt_surface_apply_buffer_use(struct vt_surface_t    *surf,
                                 struct vt_buffer_use_t *new_use);

void vt_surface_pending_state_init(struct vt_surface_state_pending_t *state);

void vt_surface_pending_state_defaults(
    struct vt_surface_state_pending_t *state);

void vt_surface_applied_state_init(struct vt_surface_state_applied_t *state);

void vt_surface_applied_state_defaults(
    struct vt_surface_state_applied_t *state);

void vt_surface_pending_state_move(struct vt_surface_state_pending_t *dst,
                                   struct vt_surface_state_pending_t *src);

void vt_surface_pending_state_fini(struct vt_surface_state_pending_t *state);
void vt_surface_applied_state_fini(struct vt_surface_state_applied_t *state);

bool vt_surface_validate_commit(struct vt_surface_t *surf);

bool vt_surface_effectively_synchronized(struct vt_surface_t *surf);

bool vt_surface_effectively_mapped(struct vt_surface_t *surf);

bool vt_surface_emit_content_update(struct vt_surface_t *surf);

struct vt_content_update_t *vt_surface_last_scu(struct vt_surface_t *surf);

struct vt_buffer_release_t *vt_surface_state_get_or_create_buffer_release(
    struct vt_renderer_t *renderer, struct vt_surface_state_pending_t *state); 

void vt_surface_frame_done(struct vt_surface_t *surf, uint32_t frame_time_msec);

bool vt_surface_compute_applied_size(const struct vt_surface_t *surf,
                                     uint32_t *o_w, uint32_t *o_h);

bool vt_surface_set_role(struct vt_surface_t                 *surf,
                         const struct vt_surface_role_impl_t *impl, void *data);

bool vt_surface_has_role(struct vt_surface_t        *surf,
                         enum vt_surface_role_type_t type);

struct vt_buffer_t *vt_surface_get_buffer(struct vt_surface_t *surf);
