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

#include "../core/surface.h"

#include <stdint.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>

struct vt_keybind_t {
  xkb_keysym_t sym;
  uint32_t     mods;
  void (*callback)(struct vt_compositor_t *comp, void *user_data);
  struct wl_list link;
  void          *user_data;
};

struct vt_seat_focus_t {
  struct vt_surface_t *surf;
  struct wl_client    *client;
};

struct vt_kb_modifier_states_t {
  uint32_t depressed, latched, locked, group;
};

struct vt_seat_cursor_t {
  struct vt_surface_t *surf;

  struct vt_pointer_t *owner;

  int32_t hotspot_x;
  int32_t hotspot_y;
};

struct vt_seat_t {
  struct wl_global *global;
  struct wl_list    keyboards;
  struct wl_list    pointers;
  struct wl_list    drag_resources;

  struct vt_seat_focus_t kb_focus, ptr_focus;

  struct vt_compositor_t *comp;

  struct wl_list                 keybinds;
  uint32_t                       serial;
  struct vt_kb_modifier_states_t _last_mods;

  double pointer_x, pointer_y;

  struct vt_seat_cursor_t cursor;
};

struct vt_keyboard_t {
  struct vt_seat_t   *seat;
  struct wl_resource *res;
  struct wl_list      link;

  uint32_t _keymap_size;
};

struct vt_pointer_t {
  struct vt_seat_t   *seat;
  struct wl_resource *res;
  struct wl_list      link;

  uint32_t enter_serial;
};

bool vt_seat_init(struct vt_seat_t *seat);

void vt_seat_handle_key(struct vt_seat_t *seat, uint32_t keycode,
                        uint32_t state, uint32_t time);

void vt_seat_handle_pointer_motion(struct vt_seat_t *seat, double x, double y,
                                   uint32_t time);

void vt_seat_handle_pointer_button(struct vt_seat_t *seat, uint32_t button,
                                   bool pressed, uint32_t time);

struct vt_keybind_t *vt_seat_add_global_keybind(
    struct vt_seat_t *seat, xkb_keysym_t sym, uint32_t mods,
    void (*callback)(struct vt_compositor_t *comp, void *user_data),
    void *user_data);

void vt_seat_send_keyboard_leave(struct vt_seat_t *seat);

void vt_seat_send_pointer_leave(struct vt_seat_t *seat);

void vt_seat_set_keyboard_focus(struct vt_seat_t    *seat,
                                struct vt_surface_t *surface);

void vt_seat_set_pointer_focus(struct vt_seat_t    *seat,
                               struct vt_surface_t *surf, double sx, double sy);

void vt_seat_bind_global_keybinds(struct vt_seat_t *seat);

void vt_seat_handle_surface_unmapped(struct vt_seat_t* seat, struct vt_surface_t* surf);

void vt_seat_repick_pointer_focus(struct vt_seat_t *seat);

bool vt_seat_terminate(struct vt_seat_t *seat);
