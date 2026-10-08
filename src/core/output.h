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

#include <pixman.h>
#include "util.h"
#include <stdbool.h>

#define VT_OUTPUT_MAX_DAMAGE_RECTS 64

struct vt_buffer_use_t;
struct vt_backend_t;
struct vt_surface_t;
struct vt_output_t;

enum vt_cursor_mode_t {
  VT_CURSOR_MODE_SOFTWARE,
  VT_CURSOR_MODE_HARDWARE,
};

struct vt_output_implementation_t {
  bool (*update_cursor_image)(struct vt_output_t     *output,
                              struct vt_buffer_use_t *use);
  void (*move_cursor)(struct vt_output_t *output, int32_t x, int32_t y);
};

struct vt_output_t {
  struct vt_output_implementation_t *impl;

  struct wl_list       presented_surfaces;
  struct wl_list       link_local, link_global;
  struct vt_backend_t *backend;
  void                *native_window;
  void                *render_surface;

  uint32_t width, height;
  int32_t  x, y;
  float    refresh_rate;
  uint32_t format, id;

  bool needs_repaint; 
  bool cursor_dirty;
  bool resize_pending;

  void *user_data, *user_data_render;

  struct wl_event_source *commit_source;

  pixman_region32_t damage;

  pixman_box32_t cached_damage[VT_OUTPUT_MAX_DAMAGE_RECTS];
  int32_t        n_damage_boxes;
  bool           needs_damage_rebuild;

  struct {
    struct wl_global *global;
    struct wl_list    resources;
  } proto;

  uint32_t transform;
  int32_t  native_scale;
  int32_t  current_scale;
  int32_t  original_scale;

  struct {
    int32_t mm_width;
    int32_t mm_height;

    // WL_OUTPUT_TRANSFORM
    uint32_t transform;

    char *make;
    char *model;
    char *name;

    char    *serial_number;
    uint32_t subpixel;

    struct wl_list modes;
  } physical;

  enum vt_cursor_mode_t cursor_mode;
};

struct vt_output_layer_state_t {
  struct vt_surface_t *surface;

  struct vt_box_t src;
  struct vt_box_t dst;

  bool accepted;
};

struct vt_output_t *vt_output_init(struct vt_backend_t               *backend,
                                   struct vt_output_implementation_t *impl);

bool vt_output_track_presented_surface(struct vt_output_t  *output,
                                       struct vt_surface_t *surface);
