#pragma once

#include "../core/core_types.h"

#include <wayland-util.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

struct vt_keybind_t {
  xkb_keysym_t sym;
  uint32_t mods;
  void (*callback)(struct vt_compositor_t *comp, void* user_data);
  struct wl_list link;
  void* user_data;
};

struct vt_kb_modifier_states_t {
  uint32_t depressed, latched, locked, group;
};

struct vt_seat_t {
  struct wl_global* global;
  struct wl_list keyboards; 
  struct wl_list pointers;
  struct vt_surface* focused;
  struct vt_compositor_t* comp;

  struct wl_list keybinds;
  uint32_t serial;
  struct vt_kb_modifier_states_t _last_mods;
};

struct vt_keyboard_t {
  struct vt_seat_t* seat;
  struct wl_resource* resource;  
  struct xkb_state* xkb_state;
  struct xkb_keymap* xkb_keymap;
  struct wl_list link;

  uint32_t _keymap_size;
};

bool vt_seat_init(struct vt_seat_t* seat);

void vt_seat_handle_key(struct vt_seat_t* seat, uint32_t keycode, uint32_t state, uint32_t time);

struct vt_keybind_t* vt_seat_add_global_keybind(struct vt_seat_t* seat, 
    xkb_keysym_t sym,
    uint32_t mods,
    void (*callback)(struct vt_compositor_t *comp, void* user_data),
    void* user_data);

bool vt_seat_terminate(struct vt_seat_t* seat);


