#pragma once
#include "src/input/input.h"
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

struct vt_input_backend_wl_t {
  struct wl_seat* wl_seat;
  struct wl_keyboard* wl_keyboard;
  struct wl_pointer* wl_pointer;
  struct wl_touch* wl_touch;
  struct xkb_context* kb_context;
  struct vt_compositor_t* comp;
};

static void _wl_keyboard_keymap(void* data, struct wl_keyboard* wl_keyboard,
                               uint32_t format, int fd, uint32_t size);
static void _wl_keyboard_enter(void* data, struct wl_keyboard* wl_keyboard,
                              uint32_t serial, struct wl_surface* surface,
                              struct wl_array* keys);
static void _wl_keyboard_leave(void* data, struct wl_keyboard* wl_keyboard,
                              uint32_t serial, struct wl_surface* surface);
static void _wl_keyboard_key(void* data, struct wl_keyboard* wl_keyboard,
                            uint32_t serial, uint32_t time, uint32_t key,
                            uint32_t state);
static void _wl_keyboard_modifiers(void* data, struct wl_keyboard* wl_keyboard,
                                  uint32_t serial, uint32_t mods_depressed,
                                  uint32_t mods_latched, uint32_t mods_locked,
                                  uint32_t group);

static void _wl_keyboard_repeat_info(void* data, struct wl_keyboard* wl_keyboard,
                                    int32_t rate, int32_t delay);

void 
_wl_keyboard_keymap(void* data, struct wl_keyboard* wl_keyboard,
                               uint32_t format, int fd, uint32_t size) {
  struct vt_input_backend_wl_t* wl = data;
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
    return;

  char* map_str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
  if (!map_str)
    return;

  struct xkb_keymap* keymap = xkb_keymap_new_from_string(
      wl->kb_context, map_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map_str, size);
  close(fd);

  if (!keymap)
    return;

  struct xkb_state* state = xkb_state_new(keymap);
  if (!state) {
    xkb_keymap_unref(keymap);
    return;
  }
 
  struct vt_input_backend_t* ibackend = wl->comp->input_backend;
  if (ibackend->keymap)
    xkb_keymap_unref(ibackend->keymap);
  if (ibackend->kb_state)
    xkb_state_unref(ibackend->kb_state);

  ibackend->keymap = keymap;
  ibackend->kb_state = state;
}

void 
_wl_keyboard_enter(void* data, struct wl_keyboard* wl_keyboard,
                              uint32_t serial, struct wl_surface* surface,
                              struct wl_array* keys) {
  (void)data; (void)wl_keyboard; (void)serial; (void)surface; (void)keys;
}

void 
_wl_keyboard_leave(void* data, struct wl_keyboard* wl_keyboard,
                              uint32_t serial, struct wl_surface* surface) {
  (void)data; (void)wl_keyboard; (void)serial; (void)surface;
}

void
_wl_keyboard_key(void* data, struct wl_keyboard* wl_keyboard,
                            uint32_t serial, uint32_t time, uint32_t key,
                            uint32_t state) {
  struct vt_input_backend_wl_t* wl = data;

  uint32_t key_xkb = key + 8;
  enum vt_input_key_state_t vts =
      (state == WL_KEYBOARD_KEY_STATE_PRESSED)
          ? VT_KEY_STATE_PRESSED
          : VT_KEY_STATE_RELEASED;

  vt_seat_handle_key(wl->comp->seat, key_xkb, vts, time);
}


void 
_wl_keyboard_modifiers(void* data, struct wl_keyboard* wl_keyboard,
                                  uint32_t serial, uint32_t mods_depressed,
                                  uint32_t mods_latched, uint32_t mods_locked,
                                  uint32_t group) {
  struct vt_input_backend_wl_t* wl = data;
  struct vt_input_backend_t* ibackend = wl->comp->input_backend;
  if (!ibackend->kb_state) return;
  xkb_state_update_mask(ibackend->kb_state,
                        mods_depressed, mods_latched,
                        mods_locked, 0, 0, group);
}

void 
_wl_keyboard_repeat_info(void* data, struct wl_keyboard* wl_keyboard,
                                    int32_t rate, int32_t delay) {
  (void)data; (void)wl_keyboard; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
  .keymap = _wl_keyboard_keymap,
  .enter = _wl_keyboard_enter,
  .leave = _wl_keyboard_leave,
  .key = _wl_keyboard_key,
  .modifiers = _wl_keyboard_modifiers,
  .repeat_info = _wl_keyboard_repeat_info,
};


bool input_backend_init_wl(struct vt_input_backend_t* backend, void* native_handle) {
  if (!backend) return false;

  backend->user_data = VT_ALLOC(backend->comp, sizeof(struct vt_input_backend_wl_t));
  struct vt_input_backend_wl_t* wl = backend->user_data;
  wl->comp = backend->comp;
  wl->kb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  wl->comp->input_backend->kb_context = wl->kb_context;

  wl->wl_seat = native_handle;
  wl->wl_keyboard = wl_seat_get_keyboard(wl->wl_seat);
  wl_keyboard_add_listener(wl->wl_keyboard, &keyboard_listener, wl);

  VT_TRACE(backend->comp->log, "INPUT: Initialized Wayland nested keyboard backend.");
  return true;
}

bool input_backend_terminate_wl(struct vt_input_backend_t* backend) {
  if (!backend || !backend->user_data) return false;
  struct vt_input_backend_wl_t* wl = backend->user_data;
  VT_TRACE(backend->comp->log, "INPUT: Terminated Wayland nested backend.");
  return true;
}

bool input_backend_suspend_wl(struct vt_input_backend_t* backend) {
  (void)backend;
  return true;
}

bool input_backend_resume_wl(struct vt_input_backend_t* backend) {
  (void)backend;
  return true;
}
