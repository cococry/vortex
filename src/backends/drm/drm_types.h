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

#include "core/buffer.h"
#include "core/scene.h"
#include "core/core_types.h"
#include "core/session.h"
#include "props.h"
#include <gbm.h>
#include <libliftoff.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <wayland-util.h>
#include <wayland-server-core.h>

struct drm_backend_master_state_t;
struct drm_backend_state_t;
struct drm_output_state_t;

struct drm_framebuffer_t {
  uint32_t id;

  struct vt_buffer_t *buf;

  uint32_t handles[4];
  bool     owns_handles;
};

struct drm_scanout_buffer_t {
  struct drm_framebuffer_t fb;

  struct gbm_bo      *bo;
  struct gbm_surface *surface;

  struct vt_buffer_use_t *use;
};

struct drm_scanout_layer_t {
  struct vt_surface_t        *surface;
  struct drm_plane_t *plane;
  struct drm_scanout_buffer_t scanout;

  struct vt_box_t src;
  struct vt_box_t dst;
};

struct drm_plane_t {
  uint32_t id, type;
  uint32_t id_crtc_init;
  uint32_t possible_crtcs;

  uint32_t props[VT_DRM_PLANE__COUNT];

  struct wl_array formats;

  struct vt_output_cursor_size_t *cursor_sizes;
  size_t                           n_cursor_sizes;
};

struct drm_crtc_t {
  uint32_t id;
  uint32_t index;

  uint32_t props[VT_DRM_CRTC__COUNT];

  struct drm_plane_t *plane_cursor;
  struct drm_plane_t *plane_primary;

  bool in_use;
};

enum {
  VT_DRM_CAP_ATOMIC_MODESET = 0,
  VT_DRM_CAP_ADDFB2_MODIFIERS,
  VT_DRM_CAP_TEARING_PAGE_FLIPS,
  VT_DRM_CAP__COUNT,
};

struct drm_kms_plane_state_t {
  struct drm_plane_t       *plane;
  struct drm_framebuffer_t *fb;

  struct vt_box_t src;
  struct vt_box_t dst;

  int acquire_fence_fd;
};

struct drm_kms_commit_t {
  struct drm_output_state_t *output;

  struct drm_kms_plane_state_t *planes;
  size_t                         plane_count;

  bool active;
  bool modeset;
  bool test_only;
  bool async;
  bool event_pending;

  int out_fence_fd;
};

struct drm_kms_impl_t {
  const char *name;
  bool        atomic;

  bool (*commit)(struct drm_backend_state_t *drm,
                 struct drm_kms_commit_t    *commit);

  bool (*disable)(struct drm_backend_state_t *drm,
                  struct drm_output_state_t  *output);
};

struct drm_backend_state_t {
  int                drm_fd;
  drmEventContext    evctx;
  struct gbm_device *gbm_dev;

  struct wl_list outputs;

  struct vt_compositor_t *comp;
  struct gbm_device      *native_handle;

  struct wl_list link;

  struct vt_backend_t *backend;

  struct drm_backend_state_t *main_drm;

  struct vt_device_t *dev;

  struct wl_event_source *event_source;

  const struct drm_kms_impl_t *impl;

  uint64_t cursor_w, cursor_h;

  struct wl_array crtcs;
  struct wl_array planes;

  bool caps[VT_DRM_CAP__COUNT];

  struct wl_array sampling_formats;

  drmModeRes *res;

  struct liftoff_device* liftoff_dev;
};

struct drm_backend_master_state_t {
  struct wl_list          backends;
  uint32_t                x_ptr;
  int32_t                 vt_fd;
  struct vt_compositor_t *comp;

  struct wl_listener session_terminate_listener, seat_disable_listener,
      seat_enable_listener, drm_change_listener;

  struct drm_backend_state_t *main_drm;
  uint32_t                    n_drm;
};

struct drm_output_state_t {
  struct vt_output_t          *base;
  struct drm_backend_state_t *drm_backend;
  struct drm_crtc_t          *crtc;

  struct wl_array current_layers;
  struct wl_array pending_layers;
  struct wl_array layer_plan;

  bool current_valid;
  bool pending_valid;

  int pending_out_fence_fd;

  bool needs_modeset;
  bool flip_inflight;
  bool modeset_bootstrapped;
  bool renderable_setup;
  bool connector_seen;
  bool allow_tearing;

  struct gbm_surface *gbm_surf;

  drmModeModeInfo mode;
  uint32_t        conn_id;
  uint32_t        conn_props[VT_DRM_CONNECTOR__COUNT];

  struct liftoff_output *liftoff_output;
  
  struct liftoff_layer **liftoff_layers;
  size_t liftoff_layers_len;
  size_t liftoff_layers_cap;
};
