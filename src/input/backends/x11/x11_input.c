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

#include "src/core/profiling.h"
#include "x11_input.h"
#include "src/core/compositor.h"
#include "src/input/wl_seat.h"
#include <X11/XKBlib.h>
#include <X11/Xlib-xcb.h>
#include <xkbcommon/xkbcommon-x11.h>

struct x11_input {
  Display *display;
  int      event_base;
  int32_t  device;
};

bool input_backend_terminate_x11(struct vt_input_backend_t *backend) {
  VT_PROFILE_SCOPE();
  assert(backend);

  xkb_state_unref(backend->kb_state);
  xkb_keymap_unref(backend->keymap);
  xkb_context_unref(backend->kb_context);
  backend->kb_state = NULL;
  backend->keymap = NULL;
  backend->kb_context = NULL;

  backend->user_data = NULL;
  return true;
}

bool input_backend_init_x11(struct vt_input_backend_t *backend,
                            void                      *native_handle) {
  VT_PROFILE_SCOPE();
  if (!native_handle)
    return false;

  /* make xcb connection */
  xcb_connection_t *connection = XGetXCBConnection(native_handle);

  if (!xkb_x11_setup_xkb_extension(connection, XKB_X11_MIN_MAJOR_XKB_VERSION,
                                   XKB_X11_MIN_MINOR_XKB_VERSION,
                                   XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, NULL,
                                   NULL, NULL, NULL)) {
    return false;
  }

  int32_t device = xkb_x11_get_core_keyboard_device_id(connection);
  if (device < 0)
    return false;

  struct x11_input *input = VT_ALLOC(backend->comp, sizeof(*input));
  if (!input)
    return false;

  backend->user_data = input;
  input->display = native_handle;
  input->device = device;
  int opcode, error, major = XkbMajorVersion, minor = XkbMinorVersion;

  if (!XkbQueryExtension(input->display, &opcode, &input->event_base, &error,
                         &major, &minor)) {
    return false;
  }

  unsigned events =
      XkbNewKeyboardNotifyMask | XkbMapNotifyMask | XkbStateNotifyMask;
  XkbSelectEvents(input->display, XkbUseCoreKbd, events, events);

  backend->kb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!backend->kb_context)
    return false;
  backend->keymap = xkb_x11_keymap_new_from_device(
      backend->kb_context, connection, device, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (backend->keymap)
    backend->kb_state =
        xkb_x11_state_new_from_device(backend->keymap, connection, device);
  if (!backend->kb_state) {
    input_backend_terminate_x11(backend);
    return false;
  }

  /* get modifiers */
  backend->mods.shift =
      vt_input_xkb_mod_mask(backend->keymap, XKB_MOD_NAME_SHIFT);
  backend->mods.ctrl =
      vt_input_xkb_mod_mask(backend->keymap, XKB_MOD_NAME_CTRL);
  backend->mods.alt = vt_input_xkb_mod_mask(backend->keymap, XKB_MOD_NAME_ALT);
  backend->mods.super =
      vt_input_xkb_mod_mask(backend->keymap, XKB_MOD_NAME_LOGO);
  backend->mods.caps =
      vt_input_xkb_mod_mask(backend->keymap, XKB_MOD_NAME_CAPS);
  return true;
}

void input_backend_sync_x11(struct vt_input_backend_t *backend) {
  VT_PROFILE_SCOPE();
  struct x11_input *input = backend->user_data;
  if (!input || !backend->keymap)
    return;

  struct xkb_state *state = xkb_x11_state_new_from_device(
      backend->keymap, XGetXCBConnection(input->display), input->device);
  if (state) {
    xkb_state_unref(backend->kb_state);
    backend->kb_state = state;
  }
}

bool input_backend_handle_event_x11(struct vt_input_backend_t *backend,
                                    void                      *event) {
  VT_PROFILE_SCOPE();
  struct x11_input *input = backend->user_data;
  XkbEvent         *xkb = event;

  /* event might not be part of the keyboard event set */
  if (!input || xkb->type != input->event_base)
    return false;

  if (xkb->any.xkb_type == XkbStateNotify && backend->kb_state) {
    xkb_state_update_mask(backend->kb_state, xkb->state.base_mods,
                          xkb->state.latched_mods, xkb->state.locked_mods,
                          xkb->state.base_group, xkb->state.latched_group,
                          xkb->state.locked_group);
  } else if (xkb->any.xkb_type == XkbMapNotify ||
             xkb->any.xkb_type == XkbNewKeyboardNotify) {
    /* recreate both map and state after host layout or keyboard changes. */
    Display *display = input->display;
    input_backend_terminate_x11(backend);

    if (!input_backend_init_x11(backend, display))
      backend->comp->running = false;
    else
      vt_seat_refresh_keymap(backend->comp->seat);
  }
  return true;
}
