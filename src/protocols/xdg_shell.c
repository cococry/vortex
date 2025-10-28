#define _GNU_SOURCE

#include "xdg_shell.h"

#include "src/core/core_types.h"
#include "src/core/surface.h"
#include "xdg-shell-protocol.h" 

#include <string.h>

void _xdg_wm_base_bind(
  struct wl_client *client, void *data,
  uint32_t version, uint32_t id);

void _xdg_wm_base_destroy(struct wl_client *client, struct wl_resource *resource);

void _xdg_wm_base_create_positioner(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

void _xdg_wm_base_get_xdg_surface(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id,
  struct wl_resource *surface_res);

void _xdg_wm_base_pong(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t serial);

void _xdg_positioner_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

void _xdg_positioner_set_size(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t width,
  int32_t height);

void _xdg_positioner_set_anchor_rect(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

void _xdg_positioner_set_anchor(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t anchor);

void _xdg_positioner_set_gravity(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t gravity);

void _xdg_positioner_set_constraint_adjustment(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t constraint_adjustment);

void _xdg_positioner_set_offset(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y);

void _xdg_surface_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

void _xdg_surface_get_toplevel(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

void _xdg_surface_get_popup(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id,
  struct wl_resource *parent_surface,
  struct wl_resource *positioner);

void _xdg_surface_ack_configure(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t serial);

void _xdg_surface_set_window_geometry(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

void _xdg_toplevel_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

void _xdg_toplevel_set_parent(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *parent_resource);

void _xdg_toplevel_set_title(
  struct wl_client *client,
  struct wl_resource *resource,
  const char *title);

void _xdg_toplevel_set_app_id(
  struct wl_client *client,
  struct wl_resource *resource,
  const char *app_id);

void _xdg_toplevel_show_window_menu(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial,
  int32_t x,
  int32_t y);

void _xdg_toplevel_move(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial);

void _xdg_toplevel_resize(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial,
  uint32_t edges);

void _xdg_toplevel_set_max_size(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t width,
  int32_t height);

void _xdg_toplevel_set_min_size(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t width,
  int32_t height);

void _xdg_toplevel_set_maximized(
  struct wl_client *client,
  struct wl_resource *resource);

void _xdg_toplevel_unset_maximized(
  struct wl_client *client,
  struct wl_resource *resource);

void _xdg_toplevel_set_fullscreen(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *output);

void _xdg_toplevel_unset_fullscreen(struct wl_client *client, struct wl_resource *resource);

void _xdg_toplevel_set_minimized(struct wl_client *client, struct wl_resource *resource);

static const struct xdg_positioner_interface xdg_positioner_impl = {
  .destroy = _xdg_positioner_destroy,
  .set_size = _xdg_positioner_set_size,
  .set_anchor_rect = _xdg_positioner_set_anchor_rect,
  .set_anchor = _xdg_positioner_set_anchor,
  .set_gravity = _xdg_positioner_set_gravity,
  .set_constraint_adjustment = _xdg_positioner_set_constraint_adjustment,
  .set_offset = _xdg_positioner_set_offset,
};


static const struct xdg_toplevel_interface xdg_toplevel_impl = {
  .destroy = _xdg_toplevel_destroy,
  .set_parent = _xdg_toplevel_set_parent,
  .set_title = _xdg_toplevel_set_title,
  .set_app_id = _xdg_toplevel_set_app_id,
  .show_window_menu = _xdg_toplevel_show_window_menu,
  .move = _xdg_toplevel_move,
  .resize = _xdg_toplevel_resize,
  .set_max_size = _xdg_toplevel_set_max_size,
  .set_min_size = _xdg_toplevel_set_min_size,
  .set_maximized = _xdg_toplevel_set_maximized,
  .unset_maximized = _xdg_toplevel_unset_maximized,
  .set_fullscreen = _xdg_toplevel_set_fullscreen,
  .unset_fullscreen = _xdg_toplevel_unset_fullscreen,
  .set_minimized = _xdg_toplevel_set_minimized,
};


static const struct xdg_surface_interface xdg_surface_impl = {
  .destroy = _xdg_surface_destroy,
  .get_toplevel = _xdg_surface_get_toplevel,
  .get_popup = _xdg_surface_get_popup,
  .ack_configure = _xdg_surface_ack_configure,
  .set_window_geometry = _xdg_surface_set_window_geometry,
};

static const struct xdg_wm_base_interface xdg_wm_base_impl = {
  .destroy = _xdg_wm_base_destroy,
  .create_positioner = _xdg_wm_base_create_positioner,
  .get_xdg_surface = _xdg_wm_base_get_xdg_surface,
  .pong = _xdg_wm_base_pong,
};

static const struct wl_global* xdg_wm_base;

void
_xdg_wm_base_bind(
  struct wl_client *client, void *data,
  uint32_t version, uint32_t id) {
  struct wl_resource* res = wl_resource_create(client, &xdg_wm_base_interface, version, id);
  wl_resource_set_implementation(res, &xdg_wm_base_impl, data, NULL);
}


void 
_xdg_wm_base_destroy(struct wl_client *client, struct wl_resource *resource)
{
  wl_resource_destroy(resource);
}

void 
_xdg_wm_base_create_positioner(struct wl_client *client,
                                       struct wl_resource *resource, uint32_t id)
{
  struct wl_resource* pos = wl_resource_create(client, &xdg_positioner_interface,
                                               wl_resource_get_version(resource), id);
  wl_resource_set_implementation(pos, &xdg_positioner_impl, NULL, NULL);
}

void 
_xdg_wm_base_get_xdg_surface(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id, struct wl_resource *surface_res)
{
  struct wl_resource* xdg_surf_res = wl_resource_create(client, &xdg_surface_interface,
                                                    wl_resource_get_version(resource), id);

  struct vt_surface_t* surf =  wl_resource_get_user_data(surface_res);
  vt_xdg_surface_t* xdg_surf = VT_ALLOC(surf->comp, sizeof(*xdg_surf));
  xdg_surf->surf = surf;
  xdg_surf->xdg_surf_res = xdg_surf_res;

  wl_resource_set_implementation(xdg_surf_res, &xdg_surface_impl, xdg_surf, NULL);
}

void 
_xdg_wm_base_pong(struct wl_client *client,
                          struct wl_resource *resource, uint32_t serial)
{
}

void 
_xdg_positioner_destroy(struct wl_client *client,
                                struct wl_resource *resource)
{
  wl_resource_destroy(resource);
}

void 
_xdg_positioner_set_size(struct wl_client *client,
                                 struct wl_resource *resource,
                                 int32_t width, int32_t height)
{
}

void 
_xdg_positioner_set_anchor_rect(struct wl_client *client,
                                        struct wl_resource *resource,
                                        int32_t x, int32_t y,
                                        int32_t width, int32_t height)
{
}

void 
_xdg_positioner_set_anchor(struct wl_client *client,
                                   struct wl_resource *resource,
                                   uint32_t anchor)
{
}

void 
_xdg_positioner_set_gravity(struct wl_client *client,
                                    struct wl_resource *resource,
                                    uint32_t gravity)
{
}

void 
_xdg_positioner_set_constraint_adjustment(struct wl_client *client,
                                                  struct wl_resource *resource,
                                                  uint32_t constraint_adjustment)
{
}

void 
_xdg_positioner_set_offset(struct wl_client *client,
                                   struct wl_resource *resource,
                                   int32_t x, int32_t y)
{
}

void 
_xdg_surface_destroy(struct wl_client *client,
                             struct wl_resource *resource)
{
  wl_resource_destroy(resource);
}

void
send_initial_configure(vt_xdg_surface_t* surf)
{
  struct wl_array states;
  wl_array_init(&states);

  // 0,0 => let client decide initial size
  xdg_toplevel_send_configure(surf->toplevel.xdg_toplevel_res, 0, 0, &states);

  xdg_surface_send_configure(surf->xdg_surf_res,
                             wl_display_next_serial(surf->surf->comp->wl.dsp));

  wl_array_release(&states);
}


void 
_xdg_surface_get_toplevel(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t id) {

  vt_xdg_surface_t* xdg_surf = wl_resource_get_user_data(resource);

  wl_resource_get_user_data(resource);

  struct wl_resource* top = wl_resource_create(
    client, &xdg_toplevel_interface,
    wl_resource_get_version(resource), id);

  xdg_surf->toplevel.xdg_toplevel_res = top;

  wl_resource_set_implementation(top, &xdg_toplevel_impl, &xdg_surf->toplevel, NULL);

  // send an initial configure
  send_initial_configure(xdg_surf);
}

void 
_xdg_surface_get_popup(struct wl_client *client,
                               struct wl_resource *resource,
                               uint32_t id,
                               struct wl_resource *parent_surface,
                               struct wl_resource *positioner)
{
}

void 
_xdg_surface_ack_configure(struct wl_client *client,
                                   struct wl_resource *resource,
                                   uint32_t serial)
{
  vt_xdg_surface_t* surf = wl_resource_get_user_data(resource);
  surf->last_configure_serial = serial;
}

void 
_xdg_surface_set_window_geometry(struct wl_client *client,
                                         struct wl_resource *resource,
                                         int32_t x, int32_t y,
                                         int32_t width, int32_t height)
{
}



void
_xdg_toplevel_destroy(struct wl_client *client, struct wl_resource *resource)
{
  wl_resource_destroy(resource);
}

void
_xdg_toplevel_set_parent(struct wl_client *client,
                                 struct wl_resource *resource,
                                 struct wl_resource *parent_resource)
{
}

void
_xdg_toplevel_set_title(struct wl_client *client,
                                struct wl_resource *resource,
                                const char *title)
{
  struct vt_xdg_toplevel_t* top = wl_resource_get_user_data(resource);
  if (top->title)
    free(top->title);
  top->title = strdup(title ? title : "");
}

void
_xdg_toplevel_set_app_id(struct wl_client *client,
                                 struct wl_resource *resource,
                                 const char *app_id)
{
  struct vt_xdg_toplevel_t* top = wl_resource_get_user_data(resource);
  if (top->app_id)
    free(top->app_id);
  top->app_id = strdup(app_id ? app_id : "");
}

void
_xdg_toplevel_show_window_menu(struct wl_client *client,
                                       struct wl_resource *resource,
                                       struct wl_resource *seat,
                                       uint32_t serial,
                                       int32_t x, int32_t y)
{
  // optional: ignore
}

void
_xdg_toplevel_move(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *seat,
                           uint32_t serial)
{
  // optional: ignore
}

void
_xdg_toplevel_resize(struct wl_client *client,
                             struct wl_resource *resource,
                             struct wl_resource *seat,
                             uint32_t serial,
                             uint32_t edges)
{
  // optional: ignore
}

void
_xdg_toplevel_set_max_size(struct wl_client *client,
                                   struct wl_resource *resource,
                                   int32_t width, int32_t height)
{
  // optional: ignore
}

void
_xdg_toplevel_set_min_size(struct wl_client *client,
                                   struct wl_resource *resource,
                                   int32_t width, int32_t height)
{
  // optional: ignore
}

void
_xdg_toplevel_set_maximized(struct wl_client *client,
                                    struct wl_resource *resource)
{
  // optional: ignore
}

void
_xdg_toplevel_unset_maximized(struct wl_client *client,
                                      struct wl_resource *resource)
{
}

void
_xdg_toplevel_set_fullscreen(struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *output)
{
}

void
_xdg_toplevel_unset_fullscreen(struct wl_client *client,
                                       struct wl_resource *resource)
{
}

void
_xdg_toplevel_set_minimized(struct wl_client *client,
                                    struct wl_resource *resource)
{
}

// ===================================================
// =================== PUBLIC API ====================
// ===================================================
bool
vt_xdg_shell_init(struct vt_compositor_t* c) {
  if(!(xdg_wm_base = wl_global_create(c->wl.dsp, &xdg_wm_base_interface, 1, NULL, _xdg_wm_base_bind))) {
    VT_ERROR(c->log, "XDG: Cannot implement XDG base interface.");
    return false;
  }
  return true;
}
