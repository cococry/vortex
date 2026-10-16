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

#include "src/core/output.h"
#include "src/input/input.h"
#include "src/input/wl_seat.h"
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include <wayland-server-core.h>
#include <xkbcommon/xkbcommon.h>

#define _SUBSYS_NAME "INPUT/WL"

struct vt_input_backend_wl_t {
  struct wl_seat         *wl_seat;
  struct wl_keyboard     *wl_keyboard;
  struct wl_pointer      *wl_pointer;
  struct wl_touch        *wl_touch;
  struct xkb_context     *kb_context;
  struct vt_compositor_t *comp;

  struct vt_pointer_axis_event_t pending_axis;
  bool                           axis_pending;
  int32_t                        pointer_offset_x, pointer_offset_y;
};

static void _wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
                                uint32_t format, int fd, uint32_t size);
static void _wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
                               uint32_t serial, struct wl_surface *surface,
                               struct wl_array *keys);
static void _wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
                               uint32_t serial, struct wl_surface *surface);
static void _wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
                             uint32_t serial, uint32_t time, uint32_t key,
                             uint32_t state);
static void _wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
                                   uint32_t serial, uint32_t mods_depressed,
                                   uint32_t mods_latched, uint32_t mods_locked,
                                   uint32_t group);

static void _wl_keyboard_repeat_info(void               *data,
                                     struct wl_keyboard *wl_keyboard,
                                     int32_t rate, int32_t delay);

static void _wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
                               uint32_t time, wl_fixed_t surface_x,
                               wl_fixed_t surface_y);

static void _wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
                               uint32_t serial, uint32_t time, uint32_t button,
                               uint32_t state);

static void _wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
                              uint32_t serial, struct wl_surface *surface,
                              wl_fixed_t surface_x, wl_fixed_t surface_y);

static void _wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
                              uint32_t serial, struct wl_surface *surface);

static void _wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
                             uint32_t time, uint32_t axis, wl_fixed_t value);

static void _wl_pointer_frame(void *data, struct wl_pointer *wl_pointer);

static void _wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
                                    uint32_t axis_source);

static void _wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t time, uint32_t axis);

static void _wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                                      uint32_t axis, int32_t discrete);

static void _wl_pointer_axis_value120(void *data, struct wl_pointer *wl_pointer,
                                      uint32_t axis, int32_t value120);

static void _wl_pointer_axis_relative_direction(void              *data,
                                                struct wl_pointer *wl_pointer,
                                                uint32_t           axis,
                                                uint32_t           direction);

void _wl_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
                         uint32_t format, int fd, uint32_t size) {
  (void)wl_keyboard;
  struct vt_input_backend_wl_t *wl = data;
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
    return;

  char *map_str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
  if (!map_str)
    return;

  struct xkb_keymap *keymap = xkb_keymap_new_from_string(
      wl->kb_context, map_str, XKB_KEYMAP_FORMAT_TEXT_V1,
      XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map_str, size);
  close(fd);

  if (!keymap)
    return;

  struct xkb_state *state = xkb_state_new(keymap);
  if (!state) {
    xkb_keymap_unref(keymap);
    return;
  }

  struct vt_input_backend_t *ibackend = wl->comp->input_backend;
  if (ibackend->keymap)
    xkb_keymap_unref(ibackend->keymap);
  if (ibackend->kb_state)
    xkb_state_unref(ibackend->kb_state);

  ibackend->keymap = keymap;
  ibackend->kb_state = state;

  ibackend->mods.shift =
      vt_input_xkb_mod_mask(ibackend->keymap, XKB_MOD_NAME_SHIFT);
  ibackend->mods.ctrl =
      vt_input_xkb_mod_mask(ibackend->keymap, XKB_MOD_NAME_CTRL);
  ibackend->mods.alt =
      vt_input_xkb_mod_mask(ibackend->keymap, XKB_MOD_NAME_ALT);
  ibackend->mods.super =
      vt_input_xkb_mod_mask(ibackend->keymap, XKB_MOD_NAME_LOGO);
  ibackend->mods.caps =
      vt_input_xkb_mod_mask(ibackend->keymap, XKB_MOD_NAME_CAPS);
}

void _wl_keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
                        uint32_t serial, struct wl_surface *surface,
                        struct wl_array *keys) {
  (void)data;
  (void)wl_keyboard;
  (void)serial;
  (void)surface;
  (void)keys;
}

void _wl_keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
                        uint32_t serial, struct wl_surface *surface) {
  (void)data;
  (void)wl_keyboard;
  (void)serial;
  (void)surface;
}

void _wl_keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
                      uint32_t serial, uint32_t time, uint32_t key,
                      uint32_t state) {
  (void)wl_keyboard;
  (void)serial;
  struct vt_input_backend_wl_t *wl = data;

  uint32_t                  key_xkb = key + 8;
  enum vt_input_key_state_t vts = (state == WL_KEYBOARD_KEY_STATE_PRESSED)
                                      ? VT_KEY_STATE_PRESSED
                                      : VT_KEY_STATE_RELEASED;

  vt_seat_handle_key(wl->comp->seat, key_xkb, vts, time);
}

void _wl_keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
                            uint32_t serial, uint32_t mods_depressed,
                            uint32_t mods_latched, uint32_t mods_locked,
                            uint32_t group) {
  (void)wl_keyboard;
  (void)serial;
  struct vt_input_backend_wl_t *wl = data;
  struct vt_input_backend_t    *ibackend = wl->comp->input_backend;
  if (!ibackend->kb_state)
    return;
  xkb_state_update_mask(ibackend->kb_state, mods_depressed, mods_latched,
                        mods_locked, 0, 0, group);
}

void _wl_keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
                              int32_t rate, int32_t delay) {
  (void)data;
  (void)wl_keyboard;
  (void)rate;
  (void)delay;
}

void _wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
                        uint32_t time, wl_fixed_t surface_x,
                        wl_fixed_t surface_y) {
  struct vt_input_backend_wl_t *wl = data;
  (void)wl_pointer;

  vt_seat_handle_pointer_motion(
      wl->comp->seat, wl->pointer_offset_x + wl_fixed_to_double(surface_x),
      wl->pointer_offset_y + wl_fixed_to_double(surface_y), time);
}

void _wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
                        uint32_t serial, uint32_t time, uint32_t button,
                        uint32_t state) {
  struct vt_input_backend_wl_t *wl = data;
  (void)wl_pointer;
  (void)serial;

  vt_seat_handle_pointer_button(
      wl->comp->seat, button, (state == WL_POINTER_BUTTON_STATE_PRESSED), time);
}

void _wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
                       uint32_t serial, struct wl_surface *surface,
                       wl_fixed_t surface_x, wl_fixed_t surface_y) {
  struct vt_input_backend_wl_t *wl = data;
  wl->pointer_offset_x = 0;
  wl->pointer_offset_y = 0;
  struct vt_output_t *output;
  wl_list_for_each(output, &wl->comp->outputs, link_global) {
    if (output->native_window == surface) {
      wl->pointer_offset_x = output->x;
      wl->pointer_offset_y = output->y;
      break;
    }
  }

  vt_seat_handle_pointer_motion(
      wl->comp->seat, wl->pointer_offset_x + wl_fixed_to_double(surface_x),
      wl->pointer_offset_y + wl_fixed_to_double(surface_y), 0);
  // The parent compositor's enter serial is required for set_cursor.
  wl_pointer_set_cursor(wl_pointer, serial, NULL, 0, 0);
}

static void _wl_pointer_flush_axis(struct vt_input_backend_wl_t *wl) {
  if (!wl->axis_pending)
    return;
  vt_seat_handle_pointer_axis(wl->comp->seat, &wl->pending_axis);
  memset(&wl->pending_axis, 0, sizeof(wl->pending_axis));
  wl->axis_pending = false;
}

void _wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
                       uint32_t serial, struct wl_surface *surface) {
  struct vt_input_backend_wl_t *wl = data;
  (void)wl_pointer;
  (void)serial;
  (void)surface;

  _wl_pointer_flush_axis(wl);
  vt_seat_set_pointer_focus(wl->comp->seat, NULL, 0, 0);
}

void _wl_pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
                      uint32_t axis, wl_fixed_t value) {
  struct vt_input_backend_wl_t *wl = data;
  if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL)
    return;

  struct vt_pointer_axis_t *a = &wl->pending_axis.axes[axis];
  a->time = time;
  a->value = wl_fixed_to_double(value);
  a->has_value = true;
  wl->axis_pending = true;

  // wl_pointer versions before 5 have no frame event.
  if (wl_pointer_get_version(wl_pointer) < WL_POINTER_FRAME_SINCE_VERSION)
    _wl_pointer_flush_axis(wl);
}

void _wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
  struct vt_input_backend_wl_t *wl = data;
  (void)wl_pointer;
  _wl_pointer_flush_axis(wl);
}

void _wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
                             uint32_t axis_source) {
  struct vt_input_backend_wl_t *wl = data;
  (void)wl_pointer;
  wl->pending_axis.has_source = true;
  wl->pending_axis.source = axis_source;
  wl->axis_pending = true;
}

void _wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
                           uint32_t time, uint32_t axis) {
  struct vt_input_backend_wl_t *wl = data;
  (void)wl_pointer;
  if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL)
    return;
  wl->pending_axis.axes[axis].time = time;
  wl->pending_axis.axes[axis].stopped = true;
  wl->axis_pending = true;
}

void _wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                               uint32_t axis, int32_t discrete) {
  struct vt_input_backend_wl_t *wl = data;
  (void)wl_pointer;
  if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL)
    return;
  wl->pending_axis.axes[axis].discrete = discrete;
  wl->pending_axis.axes[axis].has_discrete = true;
  wl->axis_pending = true;
}

void _wl_pointer_axis_value120(void *data, struct wl_pointer *wl_pointer,
                               uint32_t axis, int32_t value120) {
  struct vt_input_backend_wl_t *wl = data;
  (void)wl_pointer;
  if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL)
    return;
  wl->pending_axis.axes[axis].value120 = value120;
  wl->pending_axis.axes[axis].has_value120 = true;
  wl->axis_pending = true;
}

void _wl_pointer_axis_relative_direction(void              *data,
                                         struct wl_pointer *wl_pointer,
                                         uint32_t axis, uint32_t direction) {
  struct vt_input_backend_wl_t *wl = data;
  (void)wl_pointer;
  if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL)
    return;
  wl->pending_axis.axes[axis].direction = direction;
  wl->pending_axis.axes[axis].has_direction = true;
  wl->axis_pending = true;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = _wl_keyboard_keymap,
    .enter = _wl_keyboard_enter,
    .leave = _wl_keyboard_leave,
    .key = _wl_keyboard_key,
    .modifiers = _wl_keyboard_modifiers,
    .repeat_info = _wl_keyboard_repeat_info,
};

static const struct wl_pointer_listener pointer_listener = {
    .enter = _wl_pointer_enter,
    .leave = _wl_pointer_leave,
    .motion = _wl_pointer_motion,
    .button = _wl_pointer_button,
    .axis = _wl_pointer_axis,
    .frame = _wl_pointer_frame,
    .axis_source = _wl_pointer_axis_source,
    .axis_stop = _wl_pointer_axis_stop,
    .axis_discrete = _wl_pointer_axis_discrete,
    .axis_value120 = _wl_pointer_axis_value120,
    .axis_relative_direction = _wl_pointer_axis_relative_direction,
};

bool input_backend_init_wl(struct vt_input_backend_t *backend,
                           void                      *native_handle) {
  if (!backend)
    return false;

  backend->user_data =
      VT_ALLOC(backend->comp, sizeof(struct vt_input_backend_wl_t));
  struct vt_input_backend_wl_t *wl = backend->user_data;
  wl->comp = backend->comp;
  wl->kb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  wl->comp->input_backend->kb_context = wl->kb_context;

  wl->wl_seat = native_handle;
  wl->wl_keyboard = wl_seat_get_keyboard(wl->wl_seat);
  wl->wl_pointer = wl_seat_get_pointer(wl->wl_seat);
  wl_keyboard_add_listener(wl->wl_keyboard, &keyboard_listener, wl);
  wl_pointer_add_listener(wl->wl_pointer, &pointer_listener, wl);

  // keymap arrives async from the parent, clients binding wl_keyboard
  // before it lands would get a NULL keymap
  struct wl_display *parent =
      wl_proxy_get_display((struct wl_proxy *)wl->wl_seat);
  if (wl_display_roundtrip(parent) < 0 || !backend->keymap) {
    VT_ERROR(backend->comp->log, "No keymap received from parent seat.");
    return false;
  }

  VT_TRACE(backend->comp->log, "Initialized Wayland nested keyboard backend.");
  return true;
}

bool input_backend_terminate_wl(struct vt_input_backend_t *backend) {
  if (!backend || !backend->user_data)
    return false;
  VT_TRACE(backend->comp->log, "Terminated Wayland nested backend.");
  return true;
}

bool input_backend_suspend_wl(struct vt_input_backend_t *backend) {
  (void)backend;
  return true;
}

bool input_backend_resume_wl(struct vt_input_backend_t *backend) {
  (void)backend;
  return true;
}
