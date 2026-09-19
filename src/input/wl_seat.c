#define _GNU_SOURCE
#include <sys/mman.h>

#include "input.h"
#include "wl_seat.h"

#include <string.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include "src/core/compositor.h"
#include "src/core/content_update.h"
#include "src/core/scene.h"
#include "src/core/surface.h"
#include "src/core/util.h"
#include "src/protocols/wl_subcompositor.h"
#include "src/protocols/xdg_shell.h"

#define _SUBSYS_NAME "SEAT"

static void _wl_seat_bind(struct wl_client *client, void *data,
                          uint32_t version, uint32_t id);
static int  _wl_create_keymap_fd(struct vt_keyboard_t *kbd);
static void _wl_keyboard_send_initial_state(struct vt_keyboard_t *kbd);
static bool _wl_handle_global_keybind(struct vt_seat_t *seat, uint32_t keycode,
                                      uint32_t state, uint32_t mods_mask);

static void _wl_handle_keybind_exit(struct vt_compositor_t *comp,
                                    void                   *user_data);
static void _wl_handle_keybind_term(struct vt_compositor_t *comp,
                                    void                   *user_data);

static void _wl_seat_get_pointer(struct wl_client   *client,
                                 struct wl_resource *seat_res, uint32_t id);
static void _wl_seat_get_keyboard(struct wl_client   *client,
                                  struct wl_resource *seat_res, uint32_t id);
static void _wl_seat_get_touch(struct wl_client   *client,
                               struct wl_resource *seat_res, uint32_t id);

static void _wl_seat_pointer_set_cursor(struct wl_client   *client,
                                        struct wl_resource *resource,
                                        uint32_t            serial,
                                        struct wl_resource *surface,
                                        int32_t hotspot_x, int32_t hotspot_y);

static void _wl_seat_release_kb(struct wl_client   *client,
                                struct wl_resource *resource);
static void _wl_seat_release_pointer(struct wl_client   *client,
                                     struct wl_resource *resource);

static void _wl_keyboard_handle_resource_destroy(struct wl_resource *res);
static void _wl_pointer_handle_resource_destroy(struct wl_resource *res);

static bool _surface_role_cursor_apply(struct vt_surface_t        *surf,
                                       struct vt_content_update_t *cu);

static void _send_pointer_enter(struct vt_pointer_t *ptr,
                                struct vt_surface_t *surf, double sx,
                                double sy);

static struct vt_kb_modifier_states_t
_wl_kb_get_mod_states(struct xkb_state *state);

static void _wl_seat_release(struct wl_client   *client,
                             struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

static const struct wl_seat_interface seat_impl = {
    .get_pointer = _wl_seat_get_pointer,
    .get_keyboard = _wl_seat_get_keyboard,
    .get_touch = _wl_seat_get_touch,
    .release = _wl_seat_release,
};

static const struct wl_keyboard_interface keyboard_impl = {
    .release = _wl_seat_release_kb};

static const struct wl_pointer_interface pointer_impl = {
    .release = _wl_seat_release_pointer,
    .set_cursor = _wl_seat_pointer_set_cursor,
};

static const struct vt_surface_role_impl_t surface_role_cursor_impl = {
    .type = VT_SURFACE_ROLE_CURSOR,
    .validate_commit = NULL,
    .commit = NULL,
    .apply = _surface_role_cursor_apply};

void _wl_seat_bind(struct wl_client *client, void *data, uint32_t version,
                   uint32_t id) {
  struct vt_seat_t *seat = data;

  struct wl_resource *resource =
      wl_resource_create(client, &wl_seat_interface, version, id);

  wl_resource_set_implementation(resource, &seat_impl, seat, NULL);

  if (version >= WL_SEAT_NAME_SINCE_VERSION)
    wl_seat_send_name(resource, seat->comp->session->name);

  uint32_t caps = WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER;
  wl_seat_send_capabilities(resource, caps);
}

int _wl_create_keymap_fd(struct vt_keyboard_t *kbd) {
  struct xkb_keymap *keymap = kbd->seat->comp->input_backend->keymap;
  xkb_keymap_ref(keymap);

  char *keymap_str =
      xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
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

void _wl_keyboard_send_initial_state(struct vt_keyboard_t *kbd) {
  struct xkb_state *state = kbd->seat->comp->input_backend->kb_state;

  uint32_t depressed =
      xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED);
  uint32_t latched = xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED);
  uint32_t locked = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
  uint32_t group =
      xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);

  wl_keyboard_send_modifiers(kbd->res, 0, depressed, latched, locked, group);
}

bool _wl_handle_global_keybind(struct vt_seat_t *seat, uint32_t keycode,
                               uint32_t state, uint32_t mods_mask) {
  if (state != VT_KEY_STATE_PRESSED)
    return false;
  xkb_keysym_t sym =
      xkb_state_key_get_one_sym(seat->comp->input_backend->kb_state, keycode);

  struct vt_keybind_t *keybind;
  wl_list_for_each(keybind, &seat->keybinds, link) {
    if (!keybind->callback)
      continue;
    if (keybind->sym == sym && (mods_mask & keybind->mods) == keybind->mods) {
      keybind->callback(seat->comp, keybind->user_data);
      return true;
    }
  }
  return false;
}

void _wl_handle_keybind_exit(struct vt_compositor_t *comp, void *user_data) {
  (void)user_data;
  vt_comp_terminate(comp);
}
void _wl_handle_keybind_term(struct vt_compositor_t *comp, void *user_data) {
  char buf[64];
  snprintf(buf, sizeof(buf), "weston-terminal &");
  VT_TRACE(comp->log, "Doing: '%s'", buf);
  system(buf);
}

void _wl_seat_get_pointer(struct wl_client   *client,
                          struct wl_resource *seat_res, uint32_t id) {
  struct vt_seat_t *seat = wl_resource_get_user_data(seat_res);
  if (!seat)
    return;

  struct wl_resource *res = wl_resource_create(
      client, &wl_pointer_interface, wl_resource_get_version(seat_res), id);
  if (!res) {
    wl_client_post_no_memory(client);
    return;
  }

  struct vt_pointer_t *ptr = calloc(1, sizeof(*ptr));
  if (!ptr) {
    wl_client_post_no_memory(client);
    return;
  }
  ptr->seat = seat;
  ptr->res = res;

  wl_list_insert(&seat->pointers, &ptr->link);

  wl_resource_set_implementation(res, &pointer_impl, ptr,
                                 _wl_pointer_handle_resource_destroy);

  if (seat->ptr_focus.client == client &&
    seat->ptr_focus.surf) {

    struct vt_surface_t *surf =
        seat->ptr_focus.surf;

    struct vt_rect_t *bounds =
        vt_scene_node_get_global_bounds(surf->scene_node);

    if (bounds) {
        _send_pointer_enter(
            ptr,
            surf,
            seat->pointer_x - bounds->x,
            seat->pointer_y - bounds->y);
    }
}
}

void _wl_seat_get_touch(struct wl_client *client, struct wl_resource *seat_res,
                        uint32_t id) {
  struct vt_seat_t *seat = wl_resource_get_user_data(seat_res);
  if (!seat)
    return;

  struct wl_resource *res = wl_resource_create(
      client, &wl_touch_interface, wl_resource_get_version(seat_res), id);
  if (!res) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(res, NULL, NULL, NULL);
}

static void _wl_seat_pointer_set_cursor(struct wl_client   *client,
                                        struct wl_resource *resource,
                                        uint32_t            serial,
                                        struct wl_resource *surface,
                                        int32_t hotspot_x, int32_t hotspot_y) {
  struct vt_pointer_t *ptr = wl_resource_get_user_data(resource);

  if (!ptr || !ptr->seat)
    return;

  struct vt_seat_t *seat = ptr->seat;

  if (seat->ptr_focus.client != client) {
    return;
  }

  struct vt_surface_t *surf = NULL;

  if (surface) {
    surf = wl_resource_get_user_data(surface);
    if (!surf)
      return;

    if (!vt_surface_set_role(surf, &surface_role_cursor_impl, NULL)) {
      wl_resource_post_error(resource, WL_POINTER_ERROR_ROLE,
                             "wl_surface already has another role");
      VT_ERROR(surf->comp->log, "Failed to set cursor role for surface %p",
               surf);
      return;
    }
  }
  
  seat->cursor.surf = surf;
  seat->cursor.owner = ptr;
  seat->cursor.hotspot_x = hotspot_x;
  seat->cursor.hotspot_y = hotspot_y;

  if (surf) {
    vt_scene_node_damage_whole(surf->comp, surf->scene_node);
  }
}

void _wl_seat_get_keyboard(struct wl_client   *client,
                           struct wl_resource *seat_res, uint32_t id) {
  struct vt_seat_t *seat = wl_resource_get_user_data(seat_res);
  if (!seat)
    return;

  struct wl_resource *res = wl_resource_create(
      client, &wl_keyboard_interface, wl_resource_get_version(seat_res), id);
  if (!res) {
    wl_client_post_no_memory(client);
    return;
  }

  struct vt_keyboard_t *kbd = calloc(1, sizeof(*kbd));
  kbd->seat = seat;
  kbd->res = res;
  wl_resource_set_implementation(res, &keyboard_impl, kbd,
                                 _wl_keyboard_handle_resource_destroy);

  wl_list_insert(&seat->keyboards, &kbd->link);

  int fd = _wl_create_keymap_fd(kbd);
  wl_keyboard_send_keymap(res, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd,
                          kbd->_keymap_size);
  close(fd);
  if (wl_resource_get_version(res) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
    wl_keyboard_send_repeat_info(res, 25, 600);

  if (seat->kb_focus.client == client && seat->kb_focus.surf) {

    struct vt_surface_t *surf = seat->kb_focus.surf;

    uint32_t serial = wl_display_next_serial(seat->comp->wl.dsp);

    struct wl_array keys;
    wl_array_init(&keys);

    wl_keyboard_send_enter(kbd->res, serial, surf->res, &keys);

    wl_array_release(&keys);
  }
}

void _wl_seat_release_kb(struct wl_client   *client,
                         struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void _wl_seat_release_pointer(struct wl_client   *client,
                              struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void _wl_keyboard_handle_resource_destroy(struct wl_resource *res) {
  struct vt_keyboard_t *kbd = wl_resource_get_user_data(res);
  if (!kbd || !kbd->seat)
    return;

  // when a client destroys it's keyboard, we need to update our list to
  // reflect that
  wl_list_remove(&kbd->link);
  free(kbd);
  wl_resource_set_user_data(res, NULL);
  kbd = NULL;
}

void _wl_pointer_handle_resource_destroy(struct wl_resource *res) {
  struct vt_pointer_t *ptr = wl_resource_get_user_data(res);
  if (!ptr || !ptr->seat)
    return;

  wl_list_remove(&ptr->link);
  free(ptr);

  wl_resource_set_user_data(res, NULL);
}

static bool _surface_role_cursor_apply(struct vt_surface_t        *surf,
                                       struct vt_content_update_t *cu) {
  if (!surf || !cu || !surf->comp || !surf->comp->seat)
    return false;

  struct vt_seat_t *seat = surf->comp->seat;

  if (surf != seat->cursor.surf)
    return true;

  if (cu->state.offset_set) {
    seat->cursor.hotspot_x -= cu->state.offset_x;
    seat->cursor.hotspot_y -= cu->state.offset_y;

    VT_TRACE(surf->comp->log,
             "Cursor role commit of seat cursor, offsetting pointer by [x: %i, "
             "y: %i]",
             cu->state.offset_x, cu->state.offset_y);
  }

  return true;
}

struct vt_kb_modifier_states_t _wl_kb_get_mod_states(struct xkb_state *state) {
  uint32_t depressed =
      xkb_state_serialize_mods(state, XKB_STATE_MODS_DEPRESSED);
  uint32_t latched = xkb_state_serialize_mods(state, XKB_STATE_MODS_LATCHED);
  uint32_t locked = xkb_state_serialize_mods(state, XKB_STATE_MODS_LOCKED);
  uint32_t group =
      xkb_state_serialize_layout(state, XKB_STATE_LAYOUT_EFFECTIVE);

  return (struct vt_kb_modifier_states_t){
      .depressed = depressed,
      .latched = latched,
      .locked = locked,
      .group = group,
  };
}

bool vt_seat_init(struct vt_seat_t *seat) {
  if (!seat || !seat->comp)
    return false;

  wl_list_init(&seat->keyboards);
  wl_list_init(&seat->pointers);
  wl_list_init(&seat->keybinds);

  wl_list_init(&seat->drag_resources);

  seat->serial = 1;

  seat->pointer_x = 0;
  seat->pointer_y = 0;

  seat->ptr_focus.surf = NULL;

  // advertise chair
  seat->global = wl_global_create(seat->comp->wl.dsp, &wl_seat_interface, 7,
                                  seat, _wl_seat_bind);

  if (seat->comp->input_backend->platform == VT_INPUT_LIBINPUT) {
    vt_seat_bind_global_keybinds(seat);
  }

  return true;
}

void vt_seat_handle_key(struct vt_seat_t *seat, uint32_t keycode,
                        uint32_t state, uint32_t time) {
  if (!seat)
    return;

  struct vt_input_backend_t *backend = seat->comp->input_backend;
  xkb_state_update_key(backend->kb_state, keycode,
                       state == VT_KEY_STATE_PRESSED ? XKB_KEY_DOWN
                                                     : XKB_KEY_UP);
  struct vt_kb_modifier_states_t mod_states =
      _wl_kb_get_mod_states(backend->kb_state);

  VT_TRACE(seat->comp->log,
           "INPUT: Got key event: keycode: %i, state: %i, mods: %08x", keycode,
           state, mod_states.depressed);

  if (_wl_handle_global_keybind(seat, keycode, state, mod_states.depressed))
    return;

  VT_TRACE(seat->comp->log,
           "INPUT: Handled global keybind: keycode: %i, state: %i, mods: %08x",
           keycode, state, mod_states.depressed);

  struct wl_client *client = seat->kb_focus.client;
  if (!client)
    return;

  bool mods_changed = seat->_last_mods.depressed != mod_states.depressed ||
                      seat->_last_mods.latched != mod_states.latched ||
                      seat->_last_mods.locked != mod_states.locked ||
                      seat->_last_mods.group != mod_states.group;

  struct vt_keyboard_t *kbd;
  wl_list_for_each(kbd, &seat->keyboards, link) {
    if (!kbd->res)
      continue;

    if (wl_resource_get_client(kbd->res) != client)
      continue;

    if (mods_changed) {
      wl_keyboard_send_modifiers(kbd->res,
                                 wl_display_next_serial(seat->comp->wl.dsp),
                                 mod_states.depressed, mod_states.latched,
                                 mod_states.locked, mod_states.group);
    }

    wl_keyboard_send_key(
        kbd->res, wl_display_next_serial(seat->comp->wl.dsp), time, keycode - 8,
        state == VT_KEY_STATE_PRESSED ? WL_KEYBOARD_KEY_STATE_PRESSED
                                      : WL_KEYBOARD_KEY_STATE_RELEASED);
  }
  VT_TRACE(seat->comp->log,
           "INPUT: Sent key data to keyboard clients: keycode: %i, state: %i, "
           "mods: %08x",
           keycode, state, mod_states.depressed);

  seat->_last_mods = mod_states;
}

static void _send_pointer_enter(struct vt_pointer_t *ptr,
                                struct vt_surface_t *surf, double sx,
                                double sy) {
  if (!ptr || !ptr->res || !surf || !surf->res)
    return;

  uint32_t serial = wl_display_next_serial(ptr->seat->comp->wl.dsp);

  wl_pointer_send_enter(ptr->res, serial, surf->res, wl_fixed_from_double(sx),
                        wl_fixed_from_double(sy));

  ptr->enter_serial = serial;

  if (wl_resource_get_version(ptr->res) >= WL_POINTER_FRAME_SINCE_VERSION)
    wl_pointer_send_frame(ptr->res);

  printf("SENT ENTER TO: %p\n", surf);
}

static void _send_pointer_motion(struct vt_seat_t *seat, uint32_t time,
                                 double sx, double sy) {
  struct wl_client    *client = seat->ptr_focus.client;
  struct vt_surface_t *surf = seat->ptr_focus.surf;

  if (!client || !surf || !surf->res)
    return;

  struct vt_pointer_t *ptr;
  wl_list_for_each(ptr, &seat->pointers, link) {
    if (!ptr->res)
      continue;

    if (wl_resource_get_client(ptr->res) != client)
      continue;

    wl_pointer_send_motion(ptr->res, time, wl_fixed_from_double(sx),
                           wl_fixed_from_double(sy));

    if (wl_resource_get_version(ptr->res) >= WL_POINTER_FRAME_SINCE_VERSION) {
      wl_pointer_send_frame(ptr->res);
    }
  }

}


void vt_seat_handle_pointer_motion(struct vt_seat_t *seat,
                                   double x, double y,
                                   uint32_t time) {
  if (!seat)
    return;

  seat->pointer_x = x;
  seat->pointer_y = y;

  if (seat->cursor.surf && seat->cursor.surf->mapped) {
    vt_scene_node_damage_whole(
        seat->comp,
        seat->cursor.surf->scene_node);
  }

  struct vt_surface_t *surf =
      vt_comp_pick_surface(seat->comp, x, y);

  if (!surf) {
    if (seat->ptr_focus.surf)
      vt_seat_set_pointer_focus(seat, NULL, 0.0, 0.0);

    return;
  }

  struct vt_rect_t *global_bounds =
      vt_scene_node_get_global_bounds(surf->scene_node);

  if (!global_bounds)
    return;

  double sx = x - global_bounds->x;
  double sy = y - global_bounds->y;

  if (surf != seat->ptr_focus.surf)
    vt_seat_set_pointer_focus(seat, surf, sx, sy);

  _send_pointer_motion(seat, time, sx, sy);
}

static struct vt_surface_t *
_get_first_focusable_surface(struct vt_surface_t *surf) {
  while (surf) {
    if (!surf->role.impl)
      return NULL;

    switch (surf->role.impl->type) {
    case VT_SURFACE_ROLE_SUBSURFACE: {
      struct vt_subsurface_t *sub = surf->role.data;

      if (!sub || !sub->parent)
        return NULL;

      surf = sub->parent;
      continue;
    }

    case VT_SURFACE_ROLE_XDG_POPUP: {
      struct vt_xdg_surface_t *xdg = surf->role.data;

      if (!xdg || !xdg->popup || !xdg->popup->parent_xdg_surf ||
          !xdg->popup->parent_xdg_surf->surf)
        return NULL;

      surf = xdg->popup->parent_xdg_surf->surf;
      continue;
    }

    case VT_SURFACE_ROLE_XDG_TOPLEVEL:
      return surf;

    case VT_SURFACE_ROLE_CURSOR:
    case VT_SURFACE_ROLE_DRAG_ICON:
      return NULL;

    default:
      return NULL;
    }
  }

  return NULL;
}

void vt_seat_handle_pointer_button(struct vt_seat_t *seat, uint32_t button,
                                   bool pressed, uint32_t time) {
  if (!seat)
    return;

  struct wl_client    *client = seat->ptr_focus.client;
  struct vt_surface_t *surf = seat->ptr_focus.surf;

  if (!client || !surf || !surf->res)
    return;

  uint32_t serial = wl_display_next_serial(seat->comp->wl.dsp);

  VT_TRACE(seat->comp->log, "BUTTON: ptr_focus=%p picked=%p button=%u",
           seat->ptr_focus.surf,
           vt_comp_pick_surface(seat->comp, seat->pointer_x, seat->pointer_y),
           button);

  if (pressed && surf) {
    struct vt_surface_t *focus =
        _get_first_focusable_surface(seat->ptr_focus.surf);

    if (focus && focus != seat->kb_focus.surf) {
      vt_seat_set_keyboard_focus(seat, focus);
    }
  }

  struct vt_pointer_t *ptr;
  wl_list_for_each(ptr, &seat->pointers, link) {
    if (!ptr->res)
      continue;

    if (wl_resource_get_client(ptr->res) != client)
      continue;

    wl_pointer_send_button(ptr->res, serial, time, button,
                           pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                                   : WL_POINTER_BUTTON_STATE_RELEASED);

    if (wl_resource_get_version(ptr->res) >= WL_POINTER_FRAME_SINCE_VERSION) {
      wl_pointer_send_frame(ptr->res);
    }
  }
}

struct vt_keybind_t *vt_seat_add_global_keybind(
    struct vt_seat_t *seat, xkb_keysym_t sym, uint32_t mods,
    void (*callback)(struct vt_compositor_t *comp, void *user_data),
    void *user_data) {
  if (!seat || !seat->comp)
    return NULL;
  struct vt_keybind_t *keybind = VT_ALLOC(seat->comp, sizeof(*keybind));
  keybind->mods = mods;
  keybind->sym = sym;
  keybind->callback = callback;
  keybind->user_data = user_data;

  wl_list_insert(&seat->keybinds, &keybind->link);

  return keybind;
}

void vt_seat_send_keyboard_leave(struct vt_seat_t *seat) {
  if (!seat)
    return;

  struct wl_client    *client = seat->kb_focus.client;
  struct vt_surface_t *surf = seat->kb_focus.surf;

  if (!client || !surf || !surf->res)
    return;

  uint32_t serial = wl_display_next_serial(seat->comp->wl.dsp);

  struct vt_keyboard_t *kbd;
  wl_list_for_each(kbd, &seat->keyboards, link) {
    if (!kbd->res)
      continue;

    if (wl_resource_get_client(kbd->res) != client)
      continue;

    wl_keyboard_send_leave(kbd->res, serial, surf->res);
  }
}

static void _send_kb_enter(struct vt_seat_t *seat, struct vt_surface_t *surf) {
  if (!seat || !surf || !surf->res)
    return;

  struct wl_client *client = seat->kb_focus.client;
  if (!client)
    return;

  uint32_t enter_serial = wl_display_next_serial(seat->comp->wl.dsp);

  struct vt_kb_modifier_states_t mods =
      _wl_kb_get_mod_states(seat->comp->input_backend->kb_state);

  struct vt_keyboard_t *kbd;
  wl_list_for_each(kbd, &seat->keyboards, link) {
    if (!kbd->res)
      continue;

    if (wl_resource_get_client(kbd->res) != client)
      continue;

    struct wl_array keys;
    wl_array_init(&keys);

    wl_keyboard_send_enter(kbd->res, enter_serial, surf->res, &keys);

    wl_array_release(&keys);

    wl_keyboard_send_modifiers(
        kbd->res, wl_display_next_serial(seat->comp->wl.dsp), mods.depressed,
        mods.latched, mods.locked, mods.group);
  }
}

static struct vt_xdg_toplevel_t *
_surface_get_xdg_toplevel(struct vt_surface_t *surf) {
  if (!surf || !vt_surface_has_role(surf, VT_SURFACE_ROLE_XDG_TOPLEVEL))
    return NULL;

  struct vt_xdg_surface_t *xdg = surf->role.data;
  if (!xdg)
    return NULL;

  return xdg->toplevel;
}

void vt_seat_set_keyboard_focus(struct vt_seat_t    *seat,
                                struct vt_surface_t *surf) {

  if (!seat)
    return;

  if (seat->kb_focus.surf == surf)
    return;

  struct vt_surface_t *old = seat->kb_focus.surf;

  /*
   * Tell the old client it lost focus while the old focus
   * information is still intact.
   */
  if (old)
    vt_seat_send_keyboard_leave(seat);

  /* Deactivate old toplevel. */
  struct vt_xdg_toplevel_t *old_toplevel = _surface_get_xdg_toplevel(old);

  if (old_toplevel)
    vt_proto_xdg_toplevel_set_state_activated(old_toplevel, false);

  seat->kb_focus.surf = surf;
  seat->kb_focus.client = surf ? wl_resource_get_client(surf->res) : NULL;

  if (!surf)
    return;

  /* Activate new toplevel. */
  struct vt_xdg_toplevel_t *new_toplevel = _surface_get_xdg_toplevel(surf);

  if (new_toplevel)
    vt_proto_xdg_toplevel_set_state_activated(new_toplevel, true);

  _send_kb_enter(seat, surf);
}

void vt_seat_set_pointer_focus(struct vt_seat_t    *seat,
                               struct vt_surface_t *surf, double sx,
                               double sy) {
  if (!seat)
    return;
  struct vt_surface_t *old = seat->ptr_focus.surf;

  if (old == surf)
    return;

  struct wl_client *old_client = seat->ptr_focus.client;

  struct wl_client *new_client =
      surf && surf->res ? wl_resource_get_client(surf->res) : NULL;

  if (old && old->res && old_client) {
    uint32_t serial = wl_display_next_serial(seat->comp->wl.dsp);

    struct vt_pointer_t *ptr;
    wl_list_for_each(ptr, &seat->pointers, link) {
      if (!ptr->res)
        continue;

      if (wl_resource_get_client(ptr->res) != old_client)
        continue;

      wl_pointer_send_leave(ptr->res, serial, old->res);

      if (wl_resource_get_version(ptr->res) >= WL_POINTER_FRAME_SINCE_VERSION)
        wl_pointer_send_frame(ptr->res);
    }
  }

  seat->ptr_focus.surf = surf;
  seat->ptr_focus.client = new_client;

  if (surf && surf->res && new_client) {
    uint32_t serial = wl_display_next_serial(seat->comp->wl.dsp);


    struct vt_pointer_t *ptr;
wl_list_for_each(ptr, &seat->pointers, link) {
    if (!ptr->res)
        continue;

    if (wl_resource_get_client(ptr->res) != new_client)
        continue;

    _send_pointer_enter(ptr, surf, sx, sy);
}
  }
}

void vt_seat_bind_global_keybinds(struct vt_seat_t *seat) {
  if (!seat)
    return;
  struct vt_kb_modifiers_t mods = seat->comp->input_backend->mods;
  vt_seat_add_global_keybind(seat, XKB_KEY_Escape, mods.alt,
                             _wl_handle_keybind_exit, NULL);
  vt_seat_add_global_keybind(seat, XKB_KEY_e, mods.alt, _wl_handle_keybind_term,
                             NULL);
}

void vt_seat_handle_surface_unmapped(struct vt_seat_t    *seat,
                                     struct vt_surface_t *surf) {
  if (!seat || !surf)
    return;

  if (seat->kb_focus.surf == surf)
    vt_seat_set_keyboard_focus(seat, NULL);

}
void vt_seat_repick_pointer_focus(struct vt_seat_t *seat) {
  struct vt_surface_t *surf =
      vt_comp_pick_surface(seat->comp, seat->pointer_x, seat->pointer_y);

  if (!surf) {
    vt_seat_set_pointer_focus(seat, NULL, 0, 0);
    return;
  }

  struct vt_rect_t *bounds = vt_scene_node_get_global_bounds(surf->scene_node);

  if (!bounds) {
    vt_seat_set_pointer_focus(seat, NULL, 0, 0);
    return;
  }

  vt_seat_set_pointer_focus(seat, surf, seat->pointer_x - bounds->x,
                            seat->pointer_y - bounds->y);
}

bool vt_seat_terminate(struct vt_seat_t *seat) {
  if (!seat)
    return false;
  if (seat->global)
    wl_global_destroy(seat->global);

  return true;
}
