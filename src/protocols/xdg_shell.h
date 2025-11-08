#pragma once

#include "../core/core_types.h"
#include <wayland-server.h>
#include <stdbool.h>
#include <wayland-util.h>

enum vt_xdg_surface_role_t {
  VT_XDG_SURFACE_TOPLEVEL = 0,
  VT_XDG_SURFACE_POPUP,
};

typedef struct vt_xdg_surface_t vt_xdg_surface_t;

struct vt_xdg_window_geom_t {
  int32_t x, y;
  uint32_t w, h;
};

struct vt_xdg_toplevel_t {
  struct wl_resource *xdg_toplevel_res; 
  char* app_id, *title;

  struct vt_xdg_toplevel_t* parent;
  struct wl_list childs;
  struct wl_list link;

  vt_xdg_surface_t* xdg_surf;
};

struct vt_xdg_popup_t {
  struct wl_resource* xdg_popup_res;
  struct wl_resource* parent_xdg_surface_res;
  struct wl_resource* positioner_res;

  struct vt_xdg_surface_t* xdg_surf, *parent_xdg_surf;

  struct vt_xdg_window_geom_t geom;
  struct vt_xdg_window_geom_t pending_geom;

  uint32_t last_configure_serial;

  bool mapped, reactive;
  bool has_grab;
  struct wl_resource* grab_seat;
  uint32_t grab_serial;

  struct vt_xdg_popup_t *child_popup;
  struct vt_xdg_popup_t *parent_popup;
};


struct vt_xdg_surface_t {
  struct wl_resource *xdg_surf_res; 
  uint32_t last_configure_serial;

  struct vt_surface_t* surf;

  struct vt_xdg_toplevel_t* toplevel;
  struct vt_xdg_popup_t* popup;

  struct vt_xdg_window_geom_t pending_geom; 
};

bool vt_proto_xdg_shell_init(struct vt_compositor_t* c, uint32_t version);

bool vt_proto_xdg_toplevel_set_state_maximized(struct vt_xdg_toplevel_t* top, bool activated);

bool vt_proto_xdg_toplevel_set_state_fullscreen(struct vt_xdg_toplevel_t* top, bool activated);

bool vt_proto_xdg_toplevel_set_state_resizing(struct vt_xdg_toplevel_t* top, bool activated);

bool vt_proto_xdg_toplevel_set_state_activated(struct vt_xdg_toplevel_t* top, bool activated);
