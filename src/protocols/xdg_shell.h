#pragma once

#include "../core/backend.h"

typedef enum {
  VT_XDG_SURFACE_TOPLEVEL = 0,
  VT_XDG_SURFACE_POPUP,
} vt_xdg_surface_role_t;

typedef struct vt_xdg_surface_t vt_xdg_surface_t;

typedef struct {
  struct wl_resource *xdg_toplevel_res; 
  char* app_id, *title;

  vt_xdg_surface_t* xdg_surf;
} vt_xdg_toplevel_t;

struct vt_xdg_surface_t {
  struct wl_resource *xdg_surf_res;
  uint32_t last_configure_serial;

  struct vt_surface_t* surf;

  union {
    vt_xdg_toplevel_t toplevel;
  };
};

bool vt_xdg_shell_init(struct vt_compositor_t* c);

