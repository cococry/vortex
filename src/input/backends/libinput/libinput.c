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
#include "libinput.h"

#include "src/core/core_types.h"
#include "src/core/session.h"
#include "src/core/util.h"
#include "src/input/input.h"
#include "src/input/wl_seat.h"
#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#define _SUBSYS_NAME "INPUT/LIBINPUT"

struct vt_input_backend_li_t {
  struct libinput        *input;
  struct vt_compositor_t *comp;
};

static int32_t _vt_li_open_restricted(const char *path, int32_t flags,
                                      void *user_data);
static void    _vt_li_close_restricted(int32_t fd, void *user_data);
static int32_t _vt_li_input_dispatch(int fd, uint32_t mask, void *data);
static bool    _vt_li_input_handle_key_event(struct vt_input_backend_li_t   *li,
                                             struct libinput_event_keyboard *kbev);
static bool
_vt_li_input_handle_pointer_motion_event(struct vt_input_backend_li_t  *li,
                                         struct libinput_event_pointer *ptrev);
static bool
_vt_li_input_handle_pointer_button_event(struct vt_input_backend_li_t  *li,
                                         struct libinput_event_pointer *ptrev);
static bool
_vt_li_input_handle_pointer_scroll_event(struct vt_input_backend_li_t  *li,
                                         struct libinput_event_pointer *ptrev);
static bool _vt_li_init_xkb(struct vt_input_backend_li_t *li);
static enum vt_input_key_state_t
_vt_li_key_state_to_vt_key_state(enum libinput_key_state state);

static const struct libinput_interface input_interface = {
    .close_restricted = _vt_li_close_restricted,
    .open_restricted = _vt_li_open_restricted};

int32_t _vt_li_open_restricted(const char *path, int32_t flags,
                               void *user_data) {
  (void)flags;
  struct vt_session_t *session = (struct vt_session_t *)user_data;

  struct vt_device_t *dev = VT_ALLOC(session->comp, sizeof(*dev));
  if (!session->impl.open_device(session, dev, path)) {
    VT_ERROR(session->comp->log, "Failed to open input device on path '%s'.",
             path);
    return -errno;
  }

  session->impl.manage_device(session, dev);

  VT_TRACE(session->comp->log, "Opening device via seatd: %s", path);

  return dev->fd;
}

void _vt_li_close_restricted(int32_t fd, void *user_data) {
  struct vt_session_t *session = (struct vt_session_t *)user_data;
  struct vt_device_t  *dev = session->impl.device_from_fd(session, fd);
  if (dev) {
    session->impl.close_device(session, dev);
    session->impl.unmanage_device(session, dev);
  }
  VT_TRACE(session->comp->log, "Closing device FD %d (path=%s)", fd, dev->path);
}

int32_t _vt_li_input_dispatch(int fd, uint32_t mask, void *data) {
  VT_PROFILE_SCOPE();
  (void)fd;
  (void)mask;
  struct vt_input_backend_li_t *li = (struct vt_input_backend_li_t *)data;
  libinput_dispatch(li->input);

  struct libinput_event *event;
  while ((event = libinput_get_event(li->input)) != NULL) {
    enum libinput_event_type type = libinput_event_get_type(event);
    if (type == LIBINPUT_EVENT_KEYBOARD_KEY) {
      if (!_vt_li_input_handle_key_event(
              li, libinput_event_get_keyboard_event(event))) {
        VT_ERROR(li->comp->log, "Failed to handle key event.\n");
      }
    } else if (type == LIBINPUT_EVENT_POINTER_MOTION) {
      if (!_vt_li_input_handle_pointer_motion_event(
              li, libinput_event_get_pointer_event(event))) {
        VT_ERROR(li->comp->log, "Failed to handle pointer motion event.\n");
      }
    } else if (type == LIBINPUT_EVENT_POINTER_BUTTON) {
      if (!_vt_li_input_handle_pointer_button_event(
              li, libinput_event_get_pointer_event(event))) {
        VT_ERROR(li->comp->log, "Failed to handle pointer button.\n");
      }
    } else if (type == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE) {
      if (!_vt_li_input_handle_pointer_motion_event(
              li, libinput_event_get_pointer_event(event))) {
        VT_ERROR(li->comp->log, "Failed to handle absolute pointer motion.\n");
      }
    } else if (type == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL ||
               type == LIBINPUT_EVENT_POINTER_SCROLL_FINGER ||
               type == LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS) {
      if (!_vt_li_input_handle_pointer_scroll_event(
              li, libinput_event_get_pointer_event(event))) {
        VT_ERROR(li->comp->log, "Failed to handle pointer scroll.\n");
      }
    }
    libinput_event_destroy(event);
  }
  return 0;
}

bool _vt_li_input_handle_key_event(struct vt_input_backend_li_t   *li,
                                   struct libinput_event_keyboard *kbev) {
  VT_PROFILE_SCOPE();
  if (!li || !kbev) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return false;
  }

  const enum libinput_key_state state =
      libinput_event_keyboard_get_key_state(kbev);
  const uint32_t time = libinput_event_keyboard_get_time(kbev);
  const uint32_t key = libinput_event_keyboard_get_key(kbev);
  const uint32_t key_xkb = key + 8;

  vt_seat_handle_key(li->comp->seat, key_xkb,
                     _vt_li_key_state_to_vt_key_state(state), time);

  return true;
}
bool _vt_li_input_handle_pointer_motion_event(
    struct vt_input_backend_li_t *li, struct libinput_event_pointer *ptrev) {
  VT_PROFILE_SCOPE();
  if (!li || !li->comp || !ptrev) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return false;
  }

  const uint32_t           time = libinput_event_pointer_get_time(ptrev);
  double                   posx, posy;
  enum libinput_event_type type =
      libinput_event_get_type(libinput_event_pointer_get_base_event(ptrev));
  if (type == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE) {
    struct vt_output_t  *target = NULL;
    struct vt_surface_t *ptr_focus = li->comp->seat->ptr_focus.surf;
    if (ptr_focus)
      target = vt_scene_node_primary_output(li->comp, ptr_focus->scene_node);

    if (!target) {
      struct vt_output_t *output;
      wl_list_for_each(output, &li->comp->outputs, link_global) {
        if (!target)
          target = output;
        if (li->comp->seat->pointer_x >= output->x &&
            li->comp->seat->pointer_x < output->x + output->width &&
            li->comp->seat->pointer_y >= output->y &&
            li->comp->seat->pointer_y < output->y + output->height) {
          target = output;
          break;
        }
      }
    }
    if (!target || !target->width || !target->height) {
      VT_WARN(li->comp->log, "Cannot retrieve output dimensions.");
      return false;
    }
    posx = target->x + libinput_event_pointer_get_absolute_x_transformed(
                           ptrev, target->width);
    posy = target->y + libinput_event_pointer_get_absolute_y_transformed(
                           ptrev, target->height);
  } else { // relative motion
    double dx = libinput_event_pointer_get_dx(ptrev);
    double dy = libinput_event_pointer_get_dy(ptrev);
    posx = li->comp->seat->pointer_x + dx;
    posy = li->comp->seat->pointer_y + dy;
  }

  VT_TRACE(li->comp->log, "Handled pointer motion event: %lfx%lf", posx, posy)
  vt_seat_handle_pointer_motion(li->comp->seat, posx, posy, time);

  return true;
}

bool _vt_li_input_handle_pointer_button_event(
    struct vt_input_backend_li_t *li, struct libinput_event_pointer *ptrev) {
  VT_PROFILE_SCOPE();
  if (!li || !li->comp || !ptrev) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return false;
  }

  const uint32_t time = libinput_event_pointer_get_time(ptrev);
  const uint32_t button = libinput_event_pointer_get_button(ptrev);
  const enum libinput_button_state state =
      libinput_event_pointer_get_button_state(ptrev);

  vt_seat_handle_pointer_button(li->comp->seat, button,
                                state == LIBINPUT_BUTTON_STATE_PRESSED, time);

  return true;
}

bool _vt_li_input_handle_pointer_scroll_event(
    struct vt_input_backend_li_t *li, struct libinput_event_pointer *ptrev) {
  VT_PROFILE_SCOPE();
  if (!li || !li->comp || !ptrev) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return false;
  }

  enum libinput_event_type type =
      libinput_event_get_type(libinput_event_pointer_get_base_event(ptrev));
  struct vt_pointer_axis_event_t event = {0};
  event.has_source = true;
  event.source = type == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL
                     ? WL_POINTER_AXIS_SOURCE_WHEEL
                 : type == LIBINPUT_EVENT_POINTER_SCROLL_FINGER
                     ? WL_POINTER_AXIS_SOURCE_FINGER
                     : WL_POINTER_AXIS_SOURCE_CONTINUOUS;

  uint32_t                time = libinput_event_pointer_get_time(ptrev);
  struct libinput_device *device =
      libinput_event_get_device(libinput_event_pointer_get_base_event(ptrev));
  uint32_t direction =
      libinput_device_config_scroll_get_natural_scroll_enabled(device)
          ? WL_POINTER_AXIS_RELATIVE_DIRECTION_INVERTED
          : WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL;

  for (uint32_t axis = WL_POINTER_AXIS_VERTICAL_SCROLL;
       axis <= WL_POINTER_AXIS_HORIZONTAL_SCROLL; axis++) {
    enum libinput_pointer_axis li_axis =
        axis == WL_POINTER_AXIS_VERTICAL_SCROLL
            ? LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL
            : LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL;
    if (!libinput_event_pointer_has_axis(ptrev, li_axis))
      continue;

    struct vt_pointer_axis_t *a = &event.axes[axis];
    a->time = time;
    a->value = libinput_event_pointer_get_scroll_value(ptrev, li_axis);
    if (a->value == 0.0 && type != LIBINPUT_EVENT_POINTER_SCROLL_WHEEL) {
      /* Finger and continuous sources end their sequence with a zero delta. */
      a->stopped = true;
    } else {
      a->has_value = true;
      a->direction = direction;
      a->has_direction = true;
    }

    if (type == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL) {
      a->value120 =
          (int32_t)libinput_event_pointer_get_scroll_value_v120(ptrev, li_axis);
      a->has_value120 = a->value120 != 0;
    }
  }

  vt_seat_handle_pointer_axis(li->comp->seat, &event);
  return true;
}

bool _vt_li_init_xkb(struct vt_input_backend_li_t *li) {
  // NULL fields make xkbcommon read XKB_DEFAULT_*, then evdev/pc105/us
  struct xkb_rule_names rules = {0};

  struct vt_input_backend_t *backend = li->comp->input_backend;

  backend->kb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!backend->kb_context) {
    VT_ERROR(li->comp->log, "XKB: failed to create context");
    return false;
  }

  backend->keymap = xkb_keymap_new_from_names(backend->kb_context, &rules,
                                              XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!backend->keymap) {
    VT_ERROR(li->comp->log, "XKB: failed to create keymap");
    return false;
  }

  backend->kb_state = xkb_state_new(backend->keymap);
  if (!backend->kb_state) {
    VT_ERROR(li->comp->log, "XKB: failed to create state");
    return false;
  }

  li->comp->input_backend->mods.shift =
      vt_input_xkb_mod_mask(backend->keymap, XKB_MOD_NAME_SHIFT);
  li->comp->input_backend->mods.ctrl =
      vt_input_xkb_mod_mask(backend->keymap, XKB_MOD_NAME_CTRL);
  li->comp->input_backend->mods.alt =
      vt_input_xkb_mod_mask(backend->keymap, XKB_MOD_NAME_ALT);
  li->comp->input_backend->mods.super =
      vt_input_xkb_mod_mask(backend->keymap, XKB_MOD_NAME_LOGO);
  li->comp->input_backend->mods.caps =
      vt_input_xkb_mod_mask(backend->keymap, XKB_MOD_NAME_CAPS);

  return true;
}

enum vt_input_key_state_t
_vt_li_key_state_to_vt_key_state(enum libinput_key_state state) {
  VT_PROFILE_SCOPE();
  switch (state) {
  case LIBINPUT_KEY_STATE_PRESSED:
    return VT_KEY_STATE_PRESSED;
  case LIBINPUT_KEY_STATE_RELEASED:
    return VT_KEY_STATE_RELEASED;
  }

  return VT_KEY_STATE_INVALID;
}

static void _vt_li_log_devices(struct vt_input_backend_li_t *li) {
  struct libinput_event *event;

  bool printed = false;
  while ((event = libinput_get_event(li->input)) != NULL) {
    enum libinput_event_type type = libinput_event_get_type(event);

    if (type == LIBINPUT_EVENT_DEVICE_ADDED) {
      struct libinput_device *dev = libinput_event_get_device(event);
      const char             *name = libinput_device_get_name(dev);
      const char             *sysname = libinput_device_get_sysname(dev);

      char msg[512];
      snprintf(msg, sizeof(msg),
               "Discovered device: name='%s', sysname='%s', caps: %s %s %s",
               name ? name : "(null)", sysname ? sysname : "(null)",
               libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_KEYBOARD)
                   ? "keyboard"
                   : "",
               libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER)
                   ? "pointer"
                   : "",
               libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TOUCH)
                   ? "touch"
                   : "");
      VT_TRACE(li->comp->log, msg);
      printed = true;
    }

    libinput_event_destroy(event);
  }
  if (!printed) {
    VT_TRACE(li->comp->log, "No devices were discovered.\n");
  }
}

bool input_backend_init_li(struct vt_input_backend_t *backend,
                           void                      *native_handle) {
  if (!backend || !native_handle)
    return false;
  if (backend->comp->backend->platform != VT_BACKEND_DRM_GBM) {
    VT_ERROR(backend->comp->log,
             "libinput is currently only supported by the DRM sink backend.");
    return false;
  }
  if (!(backend->user_data =
            VT_ALLOC(backend->comp, sizeof(struct vt_input_backend_li_t))))
    return false;

  struct vt_input_backend_li_t *li =
      BACKEND_DATA(backend, struct vt_input_backend_li_t);
  li->comp = backend->comp;

  // context creation with the udev handle we got from the session subsystem
  struct udev *udev = (struct udev *)native_handle;
  if (!(li->input = libinput_udev_create_context(
            &input_interface, backend->comp->session, udev))) {
    VT_ERROR(backend->comp->log, "Failed to create libinput context.");
    return false;
  }

  VT_TRACE(backend->comp->log, "Successfully created libinput context.");

  // initial dispatch
  // libinput only emits DEVICE_ADDED events after the first dispatch
  if (libinput_udev_assign_seat(li->input, "seat0") < 0) {
    VT_ERROR(backend->comp->log, "Cannot assign seat '%s': %s",
             backend->comp->session->name, strerror(errno));
    return false;
  }

  VT_TRACE(backend->comp->log, "Successfully assigned libinput seat '%s'.",
           backend->comp->session->name);

  if (libinput_dispatch(li->input) < 0) {
    VT_ERROR(backend->comp->log, "Cannot perform initial dispatch: %s",
             strerror(errno));
    return false;
  }

  if (li->comp->log.verbose)
    _vt_li_log_devices(li);

  VT_TRACE(backend->comp->log, "Successfully performed libinput displatch.");

  if (!_vt_li_init_xkb(li)) {
    VT_ERROR(backend->comp->log,
             "Failed to initialize libinput backend XKB state.");
    return false;
  }

  VT_TRACE(backend->comp->log,
           "Successfully initialized XKB keyboard backend-global state.");

  // add the fd
  int li_fd = libinput_get_fd(li->input);
  wl_event_loop_add_fd(backend->comp->wl.evloop, li_fd, WL_EVENT_READABLE,
                       _vt_li_input_dispatch, li);

  VT_TRACE(backend->comp->log,
           "Successfully added the libinput FD to the wayland event loop.");

  return true;
}

bool input_backend_terminate_li(struct vt_input_backend_t *backend) {
  if (!backend || !backend->user_data)
    return false;
  struct vt_input_backend_li_t *li =
      BACKEND_DATA(backend, struct vt_input_backend_li_t);
  if (libinput_unref(li->input) != NULL) {
    VT_ERROR(backend->comp->log, "Failed to unref libinput context: %s",
             strerror(errno));
  }

  return true;
}

bool input_backend_suspend_li(struct vt_input_backend_t *backend) {
  if (!backend || !backend->user_data)
    return false;
  struct vt_input_backend_li_t *li =
      BACKEND_DATA(backend, struct vt_input_backend_li_t);
  libinput_suspend(li->input);
  VT_TRACE(backend->comp->log, "Suspended libinput.");
  return true;
}

bool input_backend_resume_li(struct vt_input_backend_t *backend) {
  if (!backend || !backend->user_data)
    return false;
  struct vt_input_backend_li_t *li =
      BACKEND_DATA(backend, struct vt_input_backend_li_t);
  if (libinput_resume(li->input) != 0) {
    VT_ERROR(backend->comp->log, "Failed to resume libinput.");
    return false;
  }
  VT_TRACE(backend->comp->log, "Resumed libinput.");
  return true;
}
