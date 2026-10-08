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

struct vt_renderer_t;
struct vt_surface_t;
struct vt_backend_t;
struct vt_output_t;
struct vt_output_layer_state_t;
struct vt_scene_t;
struct vt_dmabuf_feedback_t;

#include <stdbool.h>

#include <pixman.h>
#include <wayland-util.h>

#include "../input/input.h"
#include "../input/wl_seat.h"
#include "../render/dmabuf_attr.h"

#include "session.h"
#include "util.h"

#define BACKEND_DATA(b, type) ((type *)((b)->user_data))

#define VT_ALLOC(c, size)       vt_util_alloc(&(c)->arena, (size))
#define VT_ALLOC_FRAME(c, size) vt_util_alloc(&(c)->frame_arena, (size))

struct log_state_t {
  FILE *stream;
  bool  verbose, quiet;
};

struct wl_state_t {
  struct wl_display    *dsp;
  struct wl_event_loop *evloop;
  struct wl_compositor *compositor;
  const char           *socket_name; // owned by dsp
};

enum vt_backend_platform_t {
  VT_BACKEND_DRM_GBM = 0,
  VT_BACKEND_WAYLAND,
  VT_BACKEND_SURFACELESS,
};

struct vt_region_t {
  pixman_region32_t       region;
  struct vt_compositor_t *comp;
};

struct vt_backend_interface_t {
  bool (*init)(struct vt_backend_t *backend);
  bool (*is_dmabuf_importable)(struct vt_backend_t     *backend,
                               struct vt_dmabuf_attr_t *attr,
                               int32_t                  device_fd);
  bool (*handle_frame)(struct vt_backend_t *backend,
                       struct vt_output_t  *output);
  bool (*prepare_output_frame)(struct vt_backend_t *backend,
                               struct vt_output_t  *output);
  bool (*commit_cursor_only)(struct vt_backend_t *backend,
                             struct vt_output_t  *output);

  bool (*test_output_layers)(struct vt_backend_t            *backend,
                             struct vt_output_t             *output,
                             struct vt_output_layer_state_t *layers,
                             size_t                          layer_count);

  bool (*build_surface_feedback)(struct vt_backend_t         *backend,
                                 struct vt_surface_t         *surface,
                                 struct vt_output_t          *output,
                                 struct vt_dmabuf_feedback_t *feedback);

  bool (*terminate)(struct vt_backend_t *backend);
};

struct vt_backend_t {
  void (*on_output_change)(struct vt_backend_t *backend,
                           struct vt_output_t  *changed);
  void                         *user_data;
  struct vt_backend_interface_t impl;

  struct vt_compositor_t *comp;

  enum vt_backend_platform_t platform;
};

enum vt_output_mode_aspect_ratio_t {
  VT_MODE_PIC_AR_NONE = 0,    /* DRM_MODE_PICTURE_ASPECT_NONE */
  VT_MODE_PIC_AR_4_3 = 1,     /* DRM_MODE_PICTURE_ASPECT_4_3 */
  VT_MODE_PIC_AR_16_9 = 2,    /* DRM_MODE_PICTURE_ASPECT_16_9 */
  VT_MODE_PIC_AR_64_27 = 3,   /* DRM_MODE_PICTURE_ASPECT_64_27 */
  VT_MODE_PIC_AR_256_135 = 4, /* DRM_MODE_PICTURE_ASPECT_256_135*/
};

struct vt_output_mode_t {
  uint32_t flags;
  /** Picture aspect ratio.*/
  enum vt_output_mode_aspect_ratio_t aspect_ratio;
  int32_t                            width;   /**< Width in pixels. */
  int32_t                            height;  /**< Height in pixels. */
  uint32_t                           refresh; /**< Refresh rate in mHz. */
  struct wl_list                     link;
};

struct vt_presented_surface_t {
  struct wl_list       link;
  struct vt_surface_t *surf;
};

struct vt_output_cursor_size_t {
  uint16_t width, height;
};

struct vt_compositor_t {
  struct vt_arena_t arena, frame_arena;

  struct wl_state_t     wl;
  struct vt_backend_t  *backend;
  struct vt_renderer_t *renderer;
  struct log_state_t    log;

  struct vt_scene_t *scene;

  struct wl_list surfaces;
  struct wl_list focus_stack;

  bool running, suspended;
  bool sent_frame_cbs, any_frame_cb_pending;

  uint32_t    n_virtual_outputs;
  const char *_cmd_line_backend_path;

  struct wl_list outputs;

  struct vt_session_t       *session;
  struct vt_seat_t          *seat;
  struct vt_input_backend_t *input_backend;

  bool have_proto_dmabuf, have_proto_dmabuf_explicit_sync;

  struct vt_surface_t *root_cursor;
};

typedef bool (*backend_implement_func_t)(struct vt_compositor_t *comp);
