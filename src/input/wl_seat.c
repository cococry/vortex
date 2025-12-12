#define _GNU_SOURCE
#include <sys/mman.h>

#include "wl_seat.h"
#include "input.h"

#include <wayland-server-core.h>
#include <wayland-client-core.h>
#include <wayland-server-protocol.h>
#include <unistd.h>
#include <string.h>

#include "src/core/compositor.h"
#include "src/core/util.h"
#include "src/protocols/xdg_shell.h"

#define _SUBSYS_NAME "SEAT"

static void _wl_seat_bind(struct wl_client* client, void* data, uint32_t version, uint32_t id);
static int  _wl_create_keymap_fd(struct vt_keyboard_t* kbd);
static void _wl_keyboard_send_initial_state(struct vt_keyboard_t *kbd);
static bool _wl_handle_global_keybind(struct vt_seat_t* seat, uint32_t keycode, uint32_t state, uint32_t mods_mask);

static void _wl_handle_keybind_exit(struct vt_compositor_t* comp, void* user_data);
static void _wl_handle_keybind_term(struct vt_compositor_t* comp, void* user_data);

static void _wl_seat_get_pointer(struct wl_client* client, struct wl_resource* seat_res, uint32_t id);
static void _wl_seat_get_keyboard(struct wl_client* client, struct wl_resource* seat_res, uint32_t id);
static void _wl_seat_get_touch(struct wl_client* client, struct wl_resource* seat_res, uint32_t id);

static void _wl_seat_pointer_set_cursor(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t serial,
  struct wl_resource *surface,
  int32_t hotspot_x,
  int32_t hotspot_y);

static void _wl_seat_release_kb(struct wl_client *client, struct wl_resource *resource);
static void _wl_seat_release_pointer(struct wl_client *client, struct wl_resource *resource);

static void _wl_keyboard_handle_resource_destroy(struct wl_resource *res);
static void _wl_pointer_handle_resource_destroy(struct wl_resource *res);

static struct vt_kb_modifier_states_t _wl_kb_get_mod_states(struct xkb_state* state);

static const struct wl_seat_interface seat_impl = {
    .get_pointer  = _wl_seat_get_pointer,
    .get_keyboard = _wl_seat_get_keyboard,
    .get_touch    = _wl_seat_get_touch,
};

static const struct wl_keyboard_interface keyboard_impl = {
  .release = _wl_seat_release_kb 
};

static const struct wl_pointer_interface pointer_impl = {
  .release = _wl_seat_release_pointer,
  .set_cursor = _wl_seat_pointer_set_cursor,
};

void 
_wl_seat_bind(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
  struct vt_seat_t *seat = data;

  struct wl_resource *resource = wl_resource_create(
    client, &wl_seat_interface, version, id);

  wl_resource_set_implementation(resource, &seat_impl, seat, NULL);

  uint32_t caps = WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER;
  wl_seat_send_capabilities(resource, caps);

  if (version >= WL_SEAT_NAME_SINCE_VERSION)
    wl_seat_send_name(resource, seat->comp->session->name);
}

int 
_wl_create_keymap_fd(struct vt_keyboard_t* kbd) {
  struct xkb_keymap *keymap = kbd->seat->comp->input_backend->keymap;
  xkb_keymap_ref(keymap);

  char *keymap_str = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
  size_t size = strlen(keymap_str) + 1;

  int fd = memfd_create("keymap", MFD_CLOEXEC);
  ftruncate(fd, size);
  void *ptr = mmap(NULL, size, PROT_WRITE, MAP_SHARED, fd, 0);
  memcpy(ptr, keymap_str, size);
  munmap(ptr, size);
  free(keymap_str);

  kbd->_keymap_size = size;

  return fd;
}

void 
_wl_keyboard_send_initial_state(struct vt_keyboard_t *kbd) {
  struct xkb_state *state = kbd->seat->comp->input_backend->kb_state;

  uint32_t depressed = xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED);
  uint32_t latched   = xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED);
  uint32_t locked    = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
  uint32_t group     = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);

  wl_keyboard_send_modifiers(kbd->res,
                             0, depressed, latched, locked, group);
}

bool 
_wl_handle_global_keybind(struct vt_seat_t* seat, uint32_t keycode, uint32_t state, uint32_t mods_mask) {
  if(state != VT_KEY_STATE_PRESSED) return false;
  xkb_keysym_t sym = xkb_state_key_get_one_sym(seat->comp->input_backend->kb_state, keycode);

  struct vt_keybind_t* keybind;
  wl_list_for_each(keybind, &seat->keybinds, link) {
    if(!keybind->callback) continue;
    if(keybind->sym == sym && (mods_mask & keybind->mods) == keybind->mods) {
      keybind->callback(seat->comp, keybind->user_data);
      return true;
    }
  } 
  return false;
}

void
_wl_handle_keybind_exit(struct vt_compositor_t* comp, void* user_data) {
  (void)user_data;
  vt_comp_terminate(comp);
}
void 
_wl_handle_keybind_term(struct vt_compositor_t* comp, void* user_data) {
  char buf[64];
  snprintf(buf, sizeof(buf), "WAYLAND_DISPLAY=wayland-1 weston-terminal &");
  VT_TRACE(comp->log, "Doing: '%s'", buf);
  system(buf);
}
void 
_wl_handle_keybind_nogger(struct vt_compositor_t* comp, void* user_data) {
  comp->nogger = !comp->nogger;
  struct vt_output_t* output;
  wl_list_for_each(output, &comp->outputs, link_global) {
  vt_comp_schedule_repaint(comp, output);
  }
}

void 
_wl_seat_get_pointer(struct wl_client* client, struct wl_resource* seat_res, uint32_t id) {
  struct vt_seat_t* seat = wl_resource_get_user_data(seat_res);
  if(!seat) return;

  struct wl_resource* res =
    wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(seat_res), id);
  if(!res) {
    wl_client_post_no_memory(client);
    return;
  }

  struct vt_pointer_t* pointer = calloc(1, sizeof(*pointer));
  if(!pointer) {
    wl_client_post_no_memory(client);
    return;
  }
  pointer->seat = seat;
  pointer->res = res;
  pointer->cursor.hotspot_x = 0;
  pointer->cursor.hotspot_y = 0;
  pointer->cursor.surf = NULL;
 
  wl_list_insert(&seat->pointers, &pointer->link);

  wl_resource_set_implementation(res, &pointer_impl, pointer, _wl_pointer_handle_resource_destroy);
}

void 
_wl_seat_get_touch(struct wl_client* client, struct wl_resource* seat_res, uint32_t id) {
  struct vt_seat_t* seat = wl_resource_get_user_data(seat_res);
  if(!seat) return;

  struct wl_resource* res =
    wl_resource_create(client, &wl_touch_interface, wl_resource_get_version(seat_res), id);
  if(!res) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(res, NULL, NULL, NULL); 

}

void 
_wl_seat_pointer_set_cursor(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t serial,
  struct wl_resource *surface,
  int32_t hotspot_x,
  int32_t hotspot_y) {
  if(!resource) return;
  struct vt_surface_t* surf = NULL; 
  if(surface) {
    surf = wl_resource_get_user_data(surface);
    surf->type = VT_SURFACE_TYPE_CURSOR;
    surf->mapped = true;
    vt_comp_surf_mark_damaged(surf->comp, surf); 
  } 

  struct vt_pointer_t* ptr = wl_resource_get_user_data(resource);
  if(!ptr) return;

  ptr->cursor.surf = surf; 
  ptr->cursor.hotspot_x = hotspot_x;
  ptr->cursor.hotspot_y = hotspot_y;
}

void 
_wl_seat_get_keyboard(struct wl_client* client, struct wl_resource* seat_res, uint32_t id) {
  struct vt_seat_t* seat = wl_resource_get_user_data(seat_res);
  if(!seat) return;

  struct wl_resource* res =
    wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(seat_res), id);
  if(!res) {
    wl_client_post_no_memory(client);
    return;
  }

  struct vt_keyboard_t* kbd = calloc(1, sizeof(*kbd));
  kbd->seat = seat;
  kbd->res = res;
  wl_resource_set_implementation(res, &keyboard_impl, kbd, _wl_keyboard_handle_resource_destroy);

  wl_list_insert(&seat->keyboards, &kbd->link);

  int fd = _wl_create_keymap_fd(kbd);
  wl_keyboard_send_keymap(res,
                          WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                          fd,
                          kbd->_keymap_size);
  close(fd);
  _wl_keyboard_send_initial_state(kbd);
  if (wl_resource_get_version(res) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
    wl_keyboard_send_repeat_info(res, 25, 600);
}

void 
_wl_seat_release_kb(struct wl_client* client, struct wl_resource* resource) {
  wl_resource_destroy(resource);
}

void 
_wl_seat_release_pointer(struct wl_client *client, struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void 
_wl_keyboard_handle_resource_destroy(struct wl_resource *res) {
  struct vt_keyboard_t *kbd = wl_resource_get_user_data(res);
    if (!kbd || !kbd->seat) return;

  // If this keyboard had focus, clear it
  if (kbd->seat->kb_focus.res == res) {
    kbd->seat->kb_focus.res = NULL;
    kbd->seat->kb_focus.surf = NULL;
  }

  // when a client destroys it's keyboard, we need to update our list to 
  // reflect that 
  wl_list_remove(&kbd->link);
  free(kbd);
  wl_resource_set_user_data(res, NULL);
  kbd = NULL;

}

void 
_wl_pointer_handle_resource_destroy(struct wl_resource *res) {
  struct vt_pointer_t* ptr = wl_resource_get_user_data(res);
    if (!ptr || !ptr->seat) return;

    // If this pointer had focus, clear it
    if (ptr->seat->ptr_focus.res == res) {
        ptr->seat->ptr_focus.res = NULL;
        ptr->seat->ptr_focus.surf = NULL;
    }

  // when a client destroys it's pointer, we need to update our list to 
  // reflect that 
  wl_list_remove(&ptr->link);
  free(ptr);
  wl_resource_set_user_data(res, NULL);
  ptr = NULL;
}

struct vt_kb_modifier_states_t 
_wl_kb_get_mod_states(struct xkb_state* state) {
  uint32_t depressed = xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED);
  uint32_t latched   = xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED);
  uint32_t locked    = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
  uint32_t group     = xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);

  return (struct vt_kb_modifier_states_t){
    .depressed = depressed,
    .latched = latched,
    .locked = locked,
    .group = group,
  };
}

bool 
vt_seat_init(struct vt_seat_t* seat) {
  if(!seat || !seat->comp) return false;

  wl_list_init(&seat->keyboards);
  wl_list_init(&seat->pointers);
  wl_list_init(&seat->keybinds);

  wl_list_init(&seat->focus_stack);

  seat->serial = 1;

  seat->pointer_x = 0;
  seat->pointer_y = 0;

  seat->ptr_focus.surf = NULL;

  // advertise chair 
  seat->global = wl_global_create(
    seat->comp->wl.dsp,
    &wl_seat_interface,
    7, 
    seat,
    _wl_seat_bind
    );

  if(seat->comp->input_backend->platform == VT_INPUT_LIBINPUT) {
    vt_seat_bind_global_keybinds(seat);
  }
  
  return true;
}

void 
vt_seat_handle_key(struct vt_seat_t* seat, uint32_t keycode, uint32_t state, uint32_t time) {
  if (!seat) return;


  // safety to not use any keyboards of clients that don't exist anymore
  struct vt_keyboard_t* kbd, *tmp; 
  wl_list_for_each_safe(kbd, tmp, &seat->keyboards, link) {
    if (!kbd->res) continue;
    if (!wl_resource_get_client(kbd->res)) {
      wl_list_remove(&kbd->link);
      continue;
    }
  }

  struct vt_input_backend_t* backend = seat->comp->input_backend;
  xkb_state_update_key(backend->kb_state, keycode,
                       state == VT_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);
  struct vt_kb_modifier_states_t mod_states = _wl_kb_get_mod_states(backend->kb_state);

  VT_TRACE(seat->comp->log,
           "INPUT: Got key event: keycode: %i, state: %i, mods: %08x\n",
           keycode, state, mod_states.depressed);

  if(_wl_handle_global_keybind(seat, keycode, state, mod_states.depressed)) return;

  wl_list_for_each(kbd, &seat->keyboards, link) {
    if(
      seat->_last_mods.depressed  != mod_states.depressed ||
      seat->_last_mods.latched    != mod_states.latched ||
      seat->_last_mods.locked     != mod_states.locked ||
      seat->_last_mods.group      != mod_states.group) {
      wl_keyboard_send_modifiers(
        kbd->res,
        wl_display_next_serial(seat->comp->wl.dsp), 
        mod_states.depressed, mod_states.latched, 
        mod_states.locked, mod_states.group);
    }

    wl_keyboard_send_key(
      kbd->res,
      wl_display_next_serial(seat->comp->wl.dsp), 
      time,
      keycode - 8, // wayland expects evdev codes
      state == VT_KEY_STATE_PRESSED 
      ? WL_KEYBOARD_KEY_STATE_PRESSED
      : WL_KEYBOARD_KEY_STATE_RELEASED);
  }

  seat->_last_mods = mod_states;

}

void 
vt_seat_handle_pointer_motion(struct vt_seat_t* seat, double x, double y, uint32_t time) {
  if(!seat) return;

  struct vt_surface_t* surf = vt_comp_pick_surface(seat->comp, x, y);


  if (surf != seat->ptr_focus.surf || surf != seat->kb_focus.surf) {
    if (surf) {
      if(surf != seat->ptr_focus.surf)
        vt_seat_send_pointer_leave(seat);
      if(surf != seat->kb_focus.surf)
        vt_seat_send_keyboard_leave(seat);

      vt_seat_set_pointer_focus(seat, surf, x - surf->x, y - surf->y);
      vt_seat_set_keyboard_focus(seat, surf);
    } else {
      if(surf != seat->ptr_focus.surf)
        vt_seat_send_keyboard_leave(seat);
      if(surf != seat->kb_focus.surf)
        vt_seat_send_pointer_leave(seat);
      vt_seat_set_pointer_focus(seat, NULL, 0, 0);
      vt_seat_set_keyboard_focus(seat, NULL); 
    }
  }
  seat->pointer_x = x;
  seat->pointer_y = y;

  if(!seat->ptr_focus.surf) {
    vt_comp_damage_entire_surface(
      seat->comp, seat->comp->root_cursor, x, y); 
  }
  struct vt_pointer_t *ptr;
  wl_list_for_each(ptr, &seat->pointers, link) {
    if (ptr->cursor.surf) {
      vt_comp_surf_mark_damaged(seat->comp, ptr->cursor.surf);  
    }
  }
  if (!surf || !seat->ptr_focus.res)
    return;

  wl_pointer_send_motion(
    seat->ptr_focus.res, time,
    wl_fixed_from_double(x),
    wl_fixed_from_double(y));
  wl_pointer_send_frame(seat->ptr_focus.res);
}


void vt_seat_handle_pointer_button(
  struct vt_seat_t *seat,
  uint32_t button, 
  bool pressed, uint32_t time
) {
  if(!seat) return;
  if(!seat->ptr_focus.res) return;

  wl_pointer_send_button(
    seat->ptr_focus.res,
    wl_display_next_serial(seat->comp->wl.dsp),
    time, button,
    pressed ? WL_POINTER_BUTTON_STATE_PRESSED
    : WL_POINTER_BUTTON_STATE_RELEASED);
  wl_pointer_send_frame(seat->ptr_focus.res);
}

struct vt_keybind_t*
vt_seat_add_global_keybind(
  struct vt_seat_t* seat, 
  xkb_keysym_t sym,
  uint32_t mods,
  void (*callback)(struct vt_compositor_t *comp, void* user_data),
  void* user_data) {
  if(!seat || !seat->comp) return NULL;
  struct vt_keybind_t* keybind = VT_ALLOC(seat->comp, sizeof(*keybind));
  keybind->mods = mods;
  keybind->sym = sym;
  keybind->callback = callback;
  keybind->user_data = user_data;

  wl_list_insert(&seat->keybinds, &keybind->link);

  return keybind;
}

void vt_seat_send_keyboard_leave(struct vt_seat_t* seat) {
  if(!seat) return;
  if (seat->kb_focus.surf && seat->kb_focus.res) {
    if (wl_resource_get_client(seat->kb_focus.res) ==
      wl_resource_get_client(seat->kb_focus.surf->surf_res)) {
      wl_keyboard_send_leave(seat->kb_focus.res,
                            wl_display_next_serial(seat->comp->wl.dsp), 
                             seat->kb_focus.surf->surf_res);
    }
  }
}

void vt_seat_send_pointer_leave(struct vt_seat_t* seat) {
  if (seat->ptr_focus.surf && seat->ptr_focus.res) {
    if (wl_resource_get_client(seat->ptr_focus.res) ==
      wl_resource_get_client(seat->ptr_focus.surf->surf_res)) {
      wl_pointer_send_leave(seat->ptr_focus.res,
                            wl_display_next_serial(seat->comp->wl.dsp), 
                            seat->ptr_focus.surf->surf_res);
    }
  }
}
void
vt_seat_set_keyboard_focus(struct vt_seat_t *seat,
                           struct vt_surface_t *surf) {
  if (seat->kb_focus.surf == surf)
    return;

  if(surf) {
    struct vt_xdg_surface_t* new_focus_xdg_surf = surf->xdg_surf; 

    if(new_focus_xdg_surf && new_focus_xdg_surf->toplevel) {
      if(!vt_proto_xdg_toplevel_set_state_activated(new_focus_xdg_surf->toplevel, true)) {
        VT_ERROR(seat->comp->log, "vt_seat_set_pointer_focus: Cannot send XDG_TOPLEVEL_STATE_ACTIVATED to focused toplevel: %p.",
                 new_focus_xdg_surf->toplevel);
      }
    }
    
    struct vt_surface_t* old_focus_xdg_surf = seat->kb_focus.surf; 
    if(old_focus_xdg_surf && old_focus_xdg_surf->xdg_surf && old_focus_xdg_surf->xdg_surf->toplevel) {
      if(!vt_proto_xdg_toplevel_set_state_activated(old_focus_xdg_surf->xdg_surf->toplevel, false)) {
        VT_ERROR(seat->comp->log, "vt_seat_set_pointer_focus: Cannot send XDG_TOPLEVEL_STATE_ACTIVATED to focused toplevel: %p.",
                 old_focus_xdg_surf->xdg_surf->toplevel);
      }
    }

  }

  seat->kb_focus.surf = surf;
  seat->kb_focus.res  = NULL;

  if (!surf) return; // no new focus


  struct wl_client *client = wl_resource_get_client(surf->surf_res);
  struct vt_keyboard_t *kbd;
  wl_list_for_each(kbd, &seat->keyboards, link) {
    if (wl_resource_get_client(kbd->res) == client) {
      seat->kb_focus.res = kbd->res;
      break;
    }
  }
  if (!seat->kb_focus.res)
    return;

  struct wl_array keys;
  wl_array_init(&keys);
  wl_keyboard_send_enter(seat->kb_focus.res, wl_display_next_serial(seat->comp->wl.dsp), 
                         surf->surf_res, &keys);
  wl_array_release(&keys);

  struct vt_kb_modifier_states_t mods =
    _wl_kb_get_mod_states(seat->comp->input_backend->kb_state);

  wl_keyboard_send_modifiers(seat->kb_focus.res,  wl_display_next_serial(seat->comp->wl.dsp), 
                             mods.depressed, mods.latched,
                             mods.locked, mods.group);
}
void
vt_seat_set_pointer_focus(struct vt_seat_t *seat,
                          struct vt_surface_t *surf,
                          double sx, double sy) {
  struct vt_surface_t *old_surf = seat->ptr_focus.surf;
  struct wl_resource *old_res   = seat->ptr_focus.res;

  if (old_surf == surf)
    return;

  if(surf) {
    struct vt_xdg_surface_t* new_focus_xdg_surf = surf->xdg_surf; 

    if(new_focus_xdg_surf && new_focus_xdg_surf->toplevel) {
      if(!vt_proto_xdg_toplevel_set_state_activated(new_focus_xdg_surf->toplevel, true)) {
        VT_ERROR(seat->comp->log, "Cannot send XDG_TOPLEVEL_STATE_ACTIVATED to focused toplevel: %p.",
                 new_focus_xdg_surf->toplevel);
      }
    }
    
    struct vt_surface_t* old_focus_xdg_surf = seat->ptr_focus.surf; 
    if(old_focus_xdg_surf && old_focus_xdg_surf->xdg_surf && old_focus_xdg_surf->xdg_surf->toplevel) {
      if(!vt_proto_xdg_toplevel_set_state_activated(old_focus_xdg_surf->xdg_surf->toplevel, false)) {
        VT_ERROR(seat->comp->log, "Cannot send XDG_TOPLEVEL_STATE_ACTIVATED to focused toplevel: %p.",
                 old_focus_xdg_surf->xdg_surf->toplevel);
      }
    }

  }

  // Reset focus
  seat->ptr_focus.surf = surf;
  seat->ptr_focus.res  = NULL;
  // If no new surface, done
  if (!surf)
    return;

  // Find the pointer resource for this client
  struct wl_client* client = wl_resource_get_client(surf->surf_res);
  {
    struct vt_pointer_t* ptr;
    wl_list_for_each(ptr, &seat->pointers, link) {
      if (wl_resource_get_client(ptr->res) == client) {
        seat->ptr_focus.res = ptr->res;
        if(ptr->cursor.surf) {
          vt_comp_surf_mark_damaged(seat->comp, ptr->cursor.surf); 
        }
        break;
      }
    }
  }


  if (!seat->ptr_focus.res)
    return;

  if(seat->ptr_focus.surf) {
    // Send enter for new surface
    wl_pointer_send_enter(
      seat->ptr_focus.res,
      wl_display_next_serial(seat->comp->wl.dsp),
      surf->surf_res,
      wl_fixed_from_double(sx),
      wl_fixed_from_double(sy));
  }

}

void 
vt_seat_bind_global_keybinds(struct vt_seat_t* seat) {
  if(!seat) return;
  struct vt_kb_modifiers_t mods = seat->comp->input_backend->mods; 
  vt_seat_add_global_keybind(seat, XKB_KEY_Escape, mods.alt, _wl_handle_keybind_exit, NULL);
  vt_seat_add_global_keybind(seat, XKB_KEY_e, mods.alt, _wl_handle_keybind_term, NULL);
  vt_seat_add_global_keybind(seat, XKB_KEY_n, mods.alt, _wl_handle_keybind_nogger, NULL);
}

bool
vt_seat_terminate(struct vt_seat_t* seat) {
  if (!seat) return false;
  if (seat->global)
    wl_global_destroy(seat->global);

  return true;
}


