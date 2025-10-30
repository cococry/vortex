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

static void _wl_seat_bind(struct wl_client* client, void* data, uint32_t version, uint32_t id);
static int  _wl_create_keymap_fd(struct vt_keyboard_t* kbd);
static void _wl_keyboard_send_initial_state(struct vt_keyboard_t *kbd);
static bool _wl_handle_global_keybind(struct vt_seat_t* seat, uint32_t keycode, uint32_t state, uint32_t mods_mask);

static void _wl_handle_keybind_exit(struct vt_compositor_t* comp, void* user_data);
static void _wl_handle_keybind_term(struct vt_compositor_t* comp, void* user_data);

static void _wl_seat_get_pointer(struct wl_client* client, struct wl_resource* seat_res, uint32_t id);
static void _wl_seat_get_keyboard(struct wl_client* client, struct wl_resource* seat_res, uint32_t id);
static void _wl_seat_get_touch(struct wl_client* client, struct wl_resource* seat_res, uint32_t id);

static void _wl_seat_release_kb(struct wl_client *client, struct wl_resource *resource);
static void _wl_keyboard_destroy(struct wl_resource *res);

static struct vt_kb_modifier_states_t _wl_kb_get_mod_states(struct xkb_state* state);

static const struct wl_seat_interface seat_impl = {
    .get_pointer  = _wl_seat_get_pointer,
    .get_keyboard = _wl_seat_get_keyboard,
    .get_touch    = _wl_seat_get_touch,
};

static const struct wl_keyboard_interface keyboard_impl = {
  .release = _wl_seat_release_kb 
};

void 
_wl_seat_bind(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
  struct vt_seat_t *seat = data;

  struct wl_resource *resource = wl_resource_create(
    client, &wl_seat_interface, version, id);

  wl_resource_set_implementation(resource, &seat_impl, seat, NULL);

  uint32_t caps = WL_SEAT_CAPABILITY_KEYBOARD;
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
    char buf[64];
    xkb_keysym_get_name(sym, buf, sizeof(buf));
    VT_TRACE(seat->comp->log, "Checking keybind: %s\n", buf);
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
_wl_seat_get_pointer(struct wl_client* client, struct wl_resource* seat_res, uint32_t id) {
    struct wl_resource* res =
        wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(seat_res), id);
    wl_resource_set_implementation(res, NULL, NULL, NULL);
}

void 
_wl_seat_get_touch(struct wl_client* client, struct wl_resource* seat_res, uint32_t id) {
    struct wl_resource* res =
        wl_resource_create(client, &wl_touch_interface, wl_resource_get_version(seat_res), id);
    wl_resource_set_implementation(res, NULL, NULL, NULL);
}

void 
_wl_seat_get_keyboard(struct wl_client* client, struct wl_resource* seat_res, uint32_t id) {
  struct vt_seat_t* seat = wl_resource_get_user_data(seat_res);

  struct wl_resource* kbd_res =
    wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(seat_res), id);

  struct vt_keyboard_t* kbd = calloc(1, sizeof(*kbd));
  kbd->seat = seat;
  kbd->res = kbd_res;
  wl_resource_set_implementation(kbd_res, &keyboard_impl, kbd, _wl_keyboard_destroy);

  wl_list_insert(&seat->keyboards, &kbd->link);

  int fd = _wl_create_keymap_fd(kbd);
  wl_keyboard_send_keymap(kbd_res,
                          WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                          fd,
                          kbd->_keymap_size);
  close(fd);
  _wl_keyboard_send_initial_state(kbd);
  if (wl_resource_get_version(kbd_res) >= 4)
    wl_keyboard_send_repeat_info(kbd_res, 25, 600);
}

void 
_wl_seat_release_kb(struct wl_client* client, struct wl_resource* resource) {
  wl_resource_destroy(resource);
}

void 
_wl_keyboard_destroy(struct wl_resource *res) {
  struct vt_keyboard_t *kbd = wl_resource_get_user_data(res);
    if (!kbd || !kbd->seat) return;

  // when a client destroys it's keyboard, we need to update our list to 
  // reflect that aswell as free resources associated with the keyboard.
  wl_list_remove(&kbd->link);
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
  seat->serial = 1;


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
    printf("Doign th at.\n");
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

  printf(
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
        seat->serial++, 
        mod_states.depressed, mod_states.latched, 
        mod_states.locked, mod_states.group);
    }

    wl_keyboard_send_key(
      kbd->res,
      seat->serial++,
      time,
      keycode - 8, // wayland expects evdev codes
      state == VT_KEY_STATE_PRESSED 
      ? WL_KEYBOARD_KEY_STATE_PRESSED
      : WL_KEYBOARD_KEY_STATE_RELEASED);
  }
  
  seat->_last_mods = mod_states;

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
  printf("Mods: %i\n", mods);
  keybind->sym = sym;
  keybind->callback = callback;
  keybind->user_data = user_data;

  wl_list_insert(&seat->keybinds, &keybind->link);

  return keybind;
}

void 
vt_seat_set_keyboard_focus(
    struct vt_seat_t* seat, 
    struct vt_surface_t* surface
    ) {
  if(seat->focused.surf == surface)  {
    return; 
  }

  struct wl_client* client = wl_resource_get_client(surface->surf_res);

  if(seat->focused.surf && seat->focused.keyboard) {
    // send leave to previously focused client
    if (wl_resource_get_client(seat->focused.keyboard) ==
      wl_resource_get_client(seat->focused.surf->surf_res)) {
      wl_keyboard_send_leave(seat->focused.keyboard, seat->serial++,
                             seat->focused.surf->surf_res);
    }
  }

  seat->focused.surf = surface;
  seat->focused.keyboard = NULL;

  struct vt_keyboard_t* kbd, *tmp; 
  wl_list_for_each_safe(kbd, tmp, &seat->keyboards, link) {
    if(wl_resource_get_client(kbd->res) != client) continue;
    seat->focused.keyboard = kbd->res;
    break;
  }
  if(!seat->focused.keyboard) return;

  struct vt_input_backend_t* backend = seat->comp->input_backend;
  struct vt_kb_modifier_states_t mod_states = _wl_kb_get_mod_states(backend->kb_state);

  // send enter event to the new focused client 
  struct wl_array keys;
  wl_array_init(&keys);
  wl_keyboard_send_enter(
    seat->focused.keyboard, seat->serial++,
    surface->surf_res, &keys);
  wl_array_release(&keys);

  wl_keyboard_send_modifiers(
    seat->focused.keyboard, seat->serial++,
    mod_states.depressed, mod_states.latched, 
    mod_states.locked, mod_states.group);
}

void 
vt_seat_bind_global_keybinds(struct vt_seat_t* seat) {
  if(!seat) return;
  struct vt_kb_modifiers_t mods = seat->comp->input_backend->mods; 
  vt_seat_add_global_keybind(seat, XKB_KEY_Escape, mods.alt, _wl_handle_keybind_exit, NULL);
  vt_seat_add_global_keybind(seat, XKB_KEY_e, mods.alt, _wl_handle_keybind_term, NULL);
}

bool
vt_seat_terminate(struct vt_seat_t* seat) {
  if (!seat) return false;
  if (seat->global)
    wl_global_destroy(seat->global);

  return true;
}


