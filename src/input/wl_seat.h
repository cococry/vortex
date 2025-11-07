#pragma once

#include "../core/core_types.h"
#include "../core/surface.h"

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

struct vt_seat_focus_t {
  struct vt_surface_t* surf;
  struct wl_resource*  res; 
};

struct vt_kb_modifier_states_t {
  uint32_t depressed, latched, locked, group;
};

struct vt_seat_t {
  struct wl_global* global;
  struct wl_list keyboards; 
  struct wl_list pointers;

  struct  vt_seat_focus_t kb_focus, ptr_focus;

  struct vt_compositor_t* comp;

  struct wl_list keybinds;
  uint32_t serial;
  struct vt_kb_modifier_states_t _last_mods;

  double pointer_x, pointer_y;
};

struct vt_keyboard_t {
  struct vt_seat_t* seat;
  struct wl_resource* res;  
  struct wl_list link;

  uint32_t _keymap_size;
};

struct vt_cursor_t {
  struct vt_surface_t* surf;
  bool visible;
  int32_t hotspot_x, hotspot_y;
};

struct vt_pointer_t {
  struct vt_seat_t* seat;
  struct wl_resource* res;  
  struct wl_list link;

  struct vt_cursor_t cursor;
  wl_fixed_t px, py;
};

bool vt_seat_init(struct vt_seat_t* seat);

void vt_seat_handle_key(struct vt_seat_t* seat, uint32_t keycode, uint32_t state, uint32_t time);

void vt_seat_handle_pointer_motion(struct vt_seat_t* seat, double x, double y, uint32_t time);

void vt_seat_handle_pointer_button(
    struct vt_seat_t *seat,
    uint32_t button, bool pressed,
    uint32_t time);

struct vt_keybind_t* vt_seat_add_global_keybind(
    struct vt_seat_t* seat, 
    xkb_keysym_t sym,
    uint32_t mods,
    void (*callback)(struct vt_compositor_t *comp, void* user_data),
    void* user_data);

void vt_seat_send_keyboard_leave(struct vt_seat_t *seat);

void vt_seat_send_pointer_leave(struct vt_seat_t* seat);

void vt_seat_set_keyboard_focus(
    struct vt_seat_t* seat, 
    struct vt_surface_t* surface);

void vt_seat_set_pointer_focus(
    struct vt_seat_t *seat,
    struct vt_surface_t *surf,
    double sx, double sy);

void vt_seat_bind_global_keybinds(struct vt_seat_t* seat);

bool vt_seat_terminate(struct vt_seat_t* seat);


