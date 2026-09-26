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
#include "core/session.h"
#include "props.h"
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <wayland-util.h>
#include <wayland-server-core.h>

struct drm_backend_master_state_t;

struct drm_framebuffer_t {
  struct vt_buffer_t *buf;
  uint32_t            id;
};

struct drm_plane_t {
  uint32_t id, type;

  struct drm_framebuffer_t *buf_pending;
  struct drm_framebuffer_t *buf_current;

  struct wl_array formats;

  /*uint32_t props[VT_DRM_PLANE__COUNT];*/
};

struct drm_crtc_t {
  uint32_t id;

  uint32_t props[VT_DRM_CRTC__COUNT];
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

  bool have_atomic_modeset;

	uint64_t cursor_w, cursor_h;

  struct wl_array crtcs;
};

struct drm_backend_master_state_t {
  struct wl_list          backends;
  uint32_t                x_ptr;
  int32_t                 vt_fd;
  struct vt_compositor_t *comp;

  struct wl_listener session_terminate_listener, seat_disable_listener,
      seat_enable_listener;

  struct drm_backend_state_t *main_drm;
  uint32_t                    n_drm;
};

struct drm_output_state_t {
  struct gbm_bo *current_bo;
  struct gbm_bo *pending_bo;
  struct gbm_bo *prev_bo;
  struct gbm_bo *older_bo;
  uint32_t       older_fb;
  uint32_t       current_fb;
  uint32_t       pending_fb;
  uint32_t       prev_fb;

  struct drm_backend_state_t *drm_backend;

  bool needs_modeset;
  bool flip_inflight;
  bool modeset_bootstrapped;
  bool renderable_setup;

  struct gbm_surface *gbm_surf;

  drmModeModeInfo mode;
  uint32_t        conn_id;
  uint32_t        crtc_id;
  uint32_t        primary_plane_id;
};

