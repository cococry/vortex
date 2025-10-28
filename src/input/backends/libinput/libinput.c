#include "libinput.h"

#include "src/core/core_types.h"
#include "src/core/session.h"
#include "src/core/util.h"
#include "src/input/input.h"
#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <xkbcommon/xkbcommon.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

#define _VT_XKB_GET_MOD_MASK(keymap, name) ({ \
    int idx = xkb_keymap_mod_get_index((keymap), (name)); \
    idx == XKB_MOD_INVALID ? 0 : (1u << idx); \
})

struct vt_input_backend_li_t {
  struct libinput* input;
  struct vt_compositor_t* comp;
  
};

static int32_t    _vt_li_open_restricted(const char* path, int32_t flags, void* user_data);
static void       _vt_li_close_restricted(int32_t fd, void* user_data);
static int32_t    _vt_li_input_dispatch(int fd, uint32_t mask, void *data);
static bool       _vt_li_input_handle_key_event(struct vt_input_backend_li_t* li, struct libinput_event_keyboard* kbev);
static bool       _vt_li_init_xkb(struct vt_input_backend_li_t* li);
static enum vt_input_key_state_t _vt_li_key_state_to_vt_key_state(enum libinput_key_state state);

static const struct libinput_interface input_interface = {
  .close_restricted = _vt_li_close_restricted,
  .open_restricted = _vt_li_open_restricted 
};


int32_t 
_vt_li_open_restricted(const char* path, int32_t flags, void* user_data) {
  struct vt_session_t* session = (struct vt_session_t*)user_data;

  struct vt_device_t* dev = VT_ALLOC(session->comp, sizeof(*dev));
  if (!session->impl.open_device(session, dev, path)) {
    VT_ERROR(session->comp->log, "INPUT: Failed to open input device on path '%s'.", path);
    return -errno;
  }

  session->impl.manage_device(session, dev);
  
  VT_TRACE(session->comp->log, "INPUT: Opening device via seatd: %s", path);

  return dev->fd;
}

void 
_vt_li_close_restricted(int32_t fd, void* user_data) {
  struct vt_session_t* session = (struct vt_session_t*)user_data;
  struct vt_device_t* dev = session->impl.device_from_fd(session, fd);
  if(dev) {
    session->impl.close_device(session, dev); 
    session->impl.unmanage_device(session, dev);
  }
  VT_TRACE(session->comp->log, "INPUT: Closing device FD %d (path=%s)", fd, dev->path);
}

int32_t 
_vt_li_input_dispatch(int fd, uint32_t mask, void *data) {
  struct vt_input_backend_li_t* li = (struct vt_input_backend_li_t*)data;
  libinput_dispatch(li->input);


  struct libinput_event* event;
  while ((event = libinput_get_event(li->input)) != NULL) {
    enum libinput_event_type type = libinput_event_get_type(event);
    if(type == LIBINPUT_EVENT_KEYBOARD_KEY) {
      if(!_vt_li_input_handle_key_event(li, libinput_event_get_keyboard_event(event))) {
        VT_ERROR(li->comp->log, "INPUT: Failed to handle key event.\n");
      }
    }
    libinput_event_destroy(event);
  }
}

bool
_vt_li_input_handle_key_event(struct vt_input_backend_li_t* li, struct libinput_event_keyboard* kbev) {
  if(!li || !kbev) return false;

  const enum libinput_key_state state = libinput_event_keyboard_get_key_state(kbev);
  const uint32_t time = libinput_event_keyboard_get_time(kbev);
  const uint32_t key = libinput_event_keyboard_get_key(kbev);
  const uint32_t key_xkb = key + 8;

  vt_seat_handle_key(li->comp->seat, key_xkb, _vt_li_key_state_to_vt_key_state(state), time);

  return true;
}

bool
_vt_li_init_xkb(struct vt_input_backend_li_t* li) {
  struct xkb_rule_names rules = {
    .rules = "evdev",
    .model = "pc105",
    .layout = "us",
    .variant = NULL,
    .options = NULL
};

  struct vt_input_backend_t* backend = li->comp->input_backend;

  backend->kb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!backend->kb_context) {
    VT_ERROR(li->comp->log, "XKB: failed to create context");
    return false;
  }

  backend->keymap = xkb_keymap_new_from_names(
    backend->kb_context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!backend->keymap) {
    VT_ERROR(li->comp->log, "XKB: failed to create keymap");
    return false;
  }

  backend->kb_state = xkb_state_new(backend->keymap);
  if (!backend->kb_state) {
    VT_ERROR(li->comp->log, "XKB: failed to create state");
    return false;
  }

  li->comp->input_backend->mods.shift = _VT_XKB_GET_MOD_MASK(backend->keymap, XKB_MOD_NAME_SHIFT);
  li->comp->input_backend->mods.ctrl  = _VT_XKB_GET_MOD_MASK(backend->keymap, XKB_MOD_NAME_CTRL);
  li->comp->input_backend->mods.alt   = _VT_XKB_GET_MOD_MASK(backend->keymap, XKB_MOD_NAME_ALT);
  li->comp->input_backend->mods.super = _VT_XKB_GET_MOD_MASK(backend->keymap, XKB_MOD_NAME_LOGO);
  li->comp->input_backend->mods.caps  = _VT_XKB_GET_MOD_MASK(backend->keymap, XKB_MOD_NAME_CAPS);

  return true;
}

enum vt_input_key_state_t 
_vt_li_key_state_to_vt_key_state(enum libinput_key_state state) {
  switch(state) {
    case LIBINPUT_KEY_STATE_PRESSED: return VT_KEY_STATE_PRESSED;
    case LIBINPUT_KEY_STATE_RELEASED: return VT_KEY_STATE_RELEASED;
  }
  
  return VT_KEY_STATE_INVALID;
}

static void
_vt_li_log_devices(struct vt_input_backend_li_t *li) {
  struct libinput_event *event;

  bool printed = false;
  while ((event = libinput_get_event(li->input)) != NULL) {
    enum libinput_event_type type = libinput_event_get_type(event);

    if (type == LIBINPUT_EVENT_DEVICE_ADDED) {
      struct libinput_device *dev = libinput_event_get_device(event);
      const char *name = libinput_device_get_name(dev);
      const char *sysname = libinput_device_get_sysname(dev);

      char msg[512];
      snprintf(msg, sizeof(msg), "INPUT: Discovered device: name='%s', sysname='%s', caps: %s %s %s",
               name ? name : "(null)",
               sysname ? sysname : "(null)", 
               libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_KEYBOARD) ? "keyboard" : "",
               libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER) ? "pointer" : "",
               libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TOUCH) ? "touch" : ""
               );
      VT_TRACE(li->comp->log, msg);
      printed = true;

    }

    libinput_event_destroy(event);
  }
  if(!printed) {
    VT_TRACE(li->comp->log, "INPUT: No devices were discovered.\n");
  }
}

bool 
input_backend_init_li(struct vt_input_backend_t* backend, void* native_handle) {
  if(!backend || !native_handle) return false;
  if(backend->comp->backend->platform != VT_BACKEND_DRM_GBM) {
    VT_ERROR(backend->comp->log, "INPUT: libinput is currently only supported by the DRM sink backend.");
    return false;
  }
  if(!(backend->user_data = VT_ALLOC(backend->comp, sizeof(struct vt_input_backend_li_t)))) return false;

  struct vt_input_backend_li_t* li = BACKEND_DATA(backend, struct vt_input_backend_li_t);
  li->comp = backend->comp;

  // context creation with the udev handle we got from the session subsystem
  struct udev* udev = (struct udev*)native_handle; 
  if(!(li->input = libinput_udev_create_context(&input_interface, backend->comp->session, udev))) {
    VT_ERROR(backend->comp->log, "INPUT: Failed to create libinput context.");
    return false;
  }

  VT_TRACE(backend->comp->log, "INPUT: Successfully created libinput context.");

  // initial dispatch 
  // libinput only emits DEVICE_ADDED events after the first dispatch
  if(libinput_udev_assign_seat(li->input, "seat0") < 0) {
    VT_ERROR(backend->comp->log, "INPUT: Cannot assign seat '%s': %s", 
             backend->comp->session->name, strerror(errno));
    return false;
  } 
  
  VT_TRACE(backend->comp->log, "INPUT: Successfully assigned libinput seat '%s'.", backend->comp->session->name);

  if(libinput_dispatch(li->input) < 0) {
    VT_ERROR(backend->comp->log, "INPUT: Cannot perform initial dispatch: %s", 
             strerror(errno));
    return false;
  }

  if(li->comp->log.verbose)
    _vt_li_log_devices(li);
  
  VT_TRACE(backend->comp->log, "INPUT: Successfully performed libinput displatch."); 

  if(!_vt_li_init_xkb(li)) {
    VT_ERROR(backend->comp->log, "INPUT: Failed to initialize libinput backend XKB state."); 
    return false;
  }
  
  VT_TRACE(backend->comp->log, "INPUT: Successfully initialized XKB keyboard backend-global state."); 
 
  // add the fd
  int li_fd = libinput_get_fd(li->input);
  wl_event_loop_add_fd(backend->comp->wl.evloop, li_fd,
                       WL_EVENT_READABLE, _vt_li_input_dispatch, li);
  
  VT_TRACE(backend->comp->log, "INPUT: Successfully added the libinput FD to the wayland event loop."); 

  return true;
}


bool 
input_backend_terminate_li(struct vt_input_backend_t* backend) {
  if(!backend || !backend->user_data) return false;
  struct vt_input_backend_li_t* li = BACKEND_DATA(backend, struct vt_input_backend_li_t);
  if(libinput_unref(li->input) != NULL) {
    VT_ERROR(backend->comp->log, "INPUT: Failed to unref libinput context: %s", strerror(errno));
  }

  return true;
}

bool 
input_backend_suspend_li(struct vt_input_backend_t* backend) {
  if(!backend || !backend->user_data) return false;
  struct vt_input_backend_li_t* li = BACKEND_DATA(backend, struct vt_input_backend_li_t);
  libinput_suspend(li->input);
  VT_TRACE(backend->comp->log, "INPUT: Suspended libinput."); 
}

bool 
input_backend_resume_li(struct vt_input_backend_t* backend) {
  if(!backend || !backend->user_data) return false;
  struct vt_input_backend_li_t* li = BACKEND_DATA(backend, struct vt_input_backend_li_t);
  libinput_resume(li->input);
  VT_TRACE(backend->comp->log, "INPUT: Resumed libinput."); 
}
