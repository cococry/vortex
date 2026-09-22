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

#include "../core/core_types.h"
#include <stdbool.h>
#include <wayland-server.h>
#include <wayland-util.h>

enum vt_xdg_surface_role_t {
  VT_XDG_SURFACE_TOPLEVEL = 0,
  VT_XDG_SURFACE_POPUP,
};

struct vt_xdg_window_geom_t {
  int32_t  x, y;
  uint32_t w, h;
};

struct vt_xdg_toplevel_t {
  struct wl_resource *xdg_toplevel_res;
  char               *app_id, *title;

  struct vt_xdg_toplevel_t *parent;
  struct wl_list            childs;
  struct wl_list            link;

  struct vt_xdg_surface_t *xdg_surf;
};

struct vt_xdg_popup_t {
  struct wl_resource *xdg_popup_res;
  struct wl_resource *parent_xdg_surface_res;

  struct vt_xdg_surface_t *xdg_surf, *parent_xdg_surf;

  struct vt_xdg_window_geom_t configured_geom;

  struct vt_xdg_window_geom_t acked_geom;
  bool                        have_acked_geom;

  struct vt_xdg_window_geom_t pending_ack_geom;
  uint32_t                    pending_ack_serial;
  bool                        have_pending_ack_geom;

  bool                mapped, reactive;
  bool                has_grab;
  struct wl_resource *grab_seat;
  uint32_t            grab_serial;

  struct vt_xdg_popup_t *child_popup;
  struct vt_xdg_popup_t *parent_popup;
};

struct vt_xdg_surface_t {
  struct wl_resource *xdg_surf_res;
  uint32_t            last_configure_serial;

  struct vt_surface_t *surf;

  struct vt_xdg_toplevel_t *toplevel;
  struct vt_xdg_popup_t    *popup;

  struct vt_xdg_window_geom_t geom;
  struct vt_scene_node_t     *geom_node;

  struct vt_scene_node_t *popup_layer;
  struct vt_scene_node_t *subsurface_layer;

  bool                        have_pending_geom;
  struct vt_xdg_window_geom_t pending_geom;
};

bool vt_proto_xdg_shell_init(struct vt_compositor_t *c, uint32_t version);

bool vt_proto_xdg_toplevel_set_state_maximized(struct vt_xdg_toplevel_t *top,
                                               bool activated);

bool vt_proto_xdg_toplevel_set_state_fullscreen(struct vt_xdg_toplevel_t *top,
                                                bool activated);

bool vt_proto_xdg_toplevel_set_state_resizing(struct vt_xdg_toplevel_t *top,
                                              bool activated);

bool vt_proto_xdg_toplevel_set_state_activated(struct vt_xdg_toplevel_t *top,
                                               bool activated);
