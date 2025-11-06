#define _GNU_SOURCE

#include "xdg_shell.h"

#include "src/core/core_types.h"
#include "src/core/surface.h"
#include "xdg-shell-protocol.h" 

#include <string.h>
#include <wayland-server-core.h>

void _xdg_wm_base_bind(
  struct wl_client *client, void *data,
  uint32_t version, uint32_t id);

void _xdg_wm_base_destroy(struct wl_client *client, struct wl_resource *resource);

void _xdg_wm_base_create_positioner(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

static void _xdg_wm_base_positioner_handle_resource_destroy(struct wl_resource* resource);

static void _xdg_toplevel_handle_resource_destroy(struct wl_resource* resource);

static void _xdg_popup_handle_resource_destroy(struct wl_resource* resource);

static void _xdg_wm_base_get_xdg_surface(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id,
  struct wl_resource *surface_res);

static void _xdg_wm_base_pong(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t serial);

static void _xdg_positioner_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

static void _xdg_positioner_set_size(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t width,
  int32_t height);

static void _xdg_positioner_set_anchor_rect(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

static void _xdg_positioner_set_anchor(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t anchor);

static void _xdg_positioner_set_gravity(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t gravity);

static void _xdg_positioner_set_constraint_adjustment(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t constraint_adjustment);

static void _xdg_positioner_set_offset(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y);

static void _xdg_surface_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

static void _xdg_surface_get_toplevel(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

static void _xdg_surface_get_popup(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id,
  struct wl_resource *parent_surface,
  struct wl_resource *positioner);

static void _xdg_surface_ack_configure(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t serial);

static void _xdg_surface_set_window_geometry(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

static void _xdg_toplevel_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

static void _xdg_toplevel_set_parent(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *parent_resource);

static void _xdg_toplevel_set_title(
  struct wl_client *client,
  struct wl_resource *resource,
  const char *title);

static void _xdg_toplevel_set_app_id(
  struct wl_client *client,
  struct wl_resource *resource,
  const char *app_id);

static void _xdg_toplevel_show_window_menu(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial,
  int32_t x,
  int32_t y);

static void _xdg_toplevel_move(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial);

static void _xdg_toplevel_resize(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial,
  uint32_t edges);

static void _xdg_toplevel_set_max_size(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t width,
  int32_t height);

static void _xdg_toplevel_set_min_size(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t width,
  int32_t height);

static void _xdg_toplevel_set_maximized(
  struct wl_client *client,
  struct wl_resource *resource);

static void _xdg_toplevel_unset_maximized(
  struct wl_client *client,
  struct wl_resource *resource);

static void _xdg_toplevel_set_fullscreen(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *output);

static void _xdg_toplevel_unset_fullscreen(struct wl_client *client, struct wl_resource *resource);

static void _xdg_toplevel_set_minimized(struct wl_client *client, struct wl_resource *resource);

static void _xdg_popup_destroy(struct wl_client *client, struct wl_resource *resource);

static void _xdg_popup_grab(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial);

static void _xdg_popup_reposition(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *positioner,
  uint32_t token);

struct vt_xdg_positioner_t {
    struct wl_resource *res;
    int32_t width;
    int32_t height;
    struct {
        int32_t x;
        int32_t y;
    } anchor_rect_pos;
    struct {
        int32_t width;
        int32_t height;
    } anchor_rect_size;
    int32_t anchor;
    int32_t gravity;
    int32_t constraint_adjustment;
    int32_t offset_x;
    int32_t offset_y;

  struct vt_compositor_t* comp;
};

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

static const struct xdg_popup_interface xdg_popup_impl = { 
  .grab = _xdg_popup_grab,
  .destroy = _xdg_popup_destroy,
  .reposition = _xdg_popup_reposition
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
                                       struct wl_resource *resource, uint32_t id) {
  struct vt_compositor_t* comp = wl_resource_get_user_data(resource);
  if(!comp) return;

  struct wl_resource* pos = wl_resource_create(client, &xdg_positioner_interface,
                                               wl_resource_get_version(resource), id);
  if(!pos) {
    wl_client_post_no_memory(client);
    return;
  }

  struct vt_xdg_positioner_t* pos_data = calloc(1, sizeof(*pos_data));
  pos_data->comp = comp;
  pos_data->res = pos;
  if(!pos_data) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(pos, &xdg_positioner_impl, pos_data, _xdg_wm_base_positioner_handle_resource_destroy);
}

void 
_xdg_wm_base_positioner_handle_resource_destroy(struct wl_resource* resource) {
  struct vt_xdg_positioner_t* pos = wl_resource_get_user_data(resource);
  if(!pos) return;
  free(pos);
  pos->res = NULL;
  pos = NULL;
}

void 
_xdg_toplevel_handle_resource_destroy(struct wl_resource* resource) {
  struct vt_xdg_toplevel_t* top = wl_resource_get_user_data(resource);
  if(!top) return;
  free(top);
  top->xdg_toplevel_res = NULL;
  top = NULL;
}

void 
_xdg_popup_handle_resource_destroy(struct wl_resource* resource) {
  struct vt_xdg_popup_t* popup = wl_resource_get_user_data(resource);
  if(!popup) return;
  free(popup);
  popup->xdg_popup_res = NULL;
  popup = NULL;
}

void 
_xdg_wm_base_get_xdg_surface(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id, struct wl_resource *surface_res)
{
  struct wl_resource* xdg_surf_res = wl_resource_create(client, &xdg_surface_interface,
                                                    wl_resource_get_version(resource), id);
  if(!xdg_surf_res) {
    wl_client_post_no_memory(client);
    return;
  }

  struct vt_surface_t* surf =  wl_resource_get_user_data(surface_res);
  vt_xdg_surface_t* xdg_surf = VT_ALLOC(surf->comp, sizeof(*xdg_surf));
  xdg_surf->surf = surf;
  xdg_surf->xdg_surf_res = xdg_surf_res;
  surf->xdg_surf = xdg_surf;

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
                         int32_t width, int32_t height) {
  struct vt_xdg_positioner_t* pos = wl_resource_get_user_data(resource);
  if (!pos)
    return;

  // according to spec, both must be > 0
  if (width <= 0 || height <= 0) {
    wl_resource_post_error(
      resource,
      XDG_POSITIONER_ERROR_INVALID_INPUT,
      "width and height must be greater than zero (got %i×%i)",
      width, height);
    return;
  }

  pos->width = width;
  pos->height = height;

  VT_TRACE(
    pos->comp->log,
    "VT_PROTO_XDG_SHELL: xdg_positioner.set_size: Size %ix%i for positioner %p.",
    width, height, pos);
}

void 
_xdg_positioner_set_anchor_rect(struct wl_client *client,
                                        struct wl_resource *resource,
                                        int32_t x, int32_t y,
                                        int32_t width, int32_t height) {
  struct vt_xdg_positioner_t* pos = wl_resource_get_user_data(resource);
  if (!pos)
    return;

  // according to spec, both must be positive 
  if (width < 0 || height < 0) {
    wl_resource_post_error(
      resource,
      XDG_POSITIONER_ERROR_INVALID_INPUT,
      "width and height must be greater than zero (got %i×%i)",
      width, height);
    return;
  }

  pos->anchor_rect_pos.x = x;
  pos->anchor_rect_pos.y = y;
  pos->anchor_rect_size.width = width;
  pos->anchor_rect_size.height = height;

  VT_TRACE(
    pos->comp->log,
    "VT_PROTO_XDG_SHELL: xdg_positioner.set_anchor_rect: Pos %ix%i, size %ix%i for positioner %p.",
    x, y, width, height, pos);

}

void 
_xdg_positioner_set_anchor(struct wl_client *client,
                                   struct wl_resource *resource,
                                   uint32_t anchor) { 
  struct vt_xdg_positioner_t* pos = wl_resource_get_user_data(resource);
  if (!pos)
    return;

  if(!xdg_positioner_anchor_is_valid(anchor, wl_resource_get_version(resource))) {
    VT_WARN(
      pos->comp->log,
      "VT_PROTO_XDG_SHELL: xdg_positioner.set_anchor: Trying to set invalid anchor %i for positioner %p.", anchor, pos); 
  }

  pos->anchor = anchor; 

  VT_TRACE(
    pos->comp->log,
    "VT_PROTO_XDG_SHELL: xdg_positioner.set_anchor: Set anchor of positioner %p to %i.", pos, anchor); 
}

void 
_xdg_positioner_set_gravity(struct wl_client *client,
                                    struct wl_resource *resource,
                                    uint32_t gravity) {
  struct vt_xdg_positioner_t* pos = wl_resource_get_user_data(resource);
  if (!pos)
    return;

  if(!xdg_positioner_gravity_is_valid(gravity, wl_resource_get_version(resource))) {
    VT_WARN(
      pos->comp->log,
      "VT_PROTO_XDG_SHELL: xdg_positioner.set_gravity: Trying to set invalid gravity %i for positioner %p.", gravity, pos); 
  }

  pos->gravity = gravity; 

  VT_TRACE(
    pos->comp->log,
    "VT_PROTO_XDG_SHELL: xdg_positioner.set_gravity: Set gravity of positioner %p to %i.", pos, gravity); 
}

void 
_xdg_positioner_set_constraint_adjustment(struct wl_client *client,
                                                  struct wl_resource *resource,
                                                  uint32_t constraint_adjustment){
  struct vt_xdg_positioner_t* pos = wl_resource_get_user_data(resource);
  if (!pos)
    return;

  if(!xdg_positioner_constraint_adjustment_is_valid(constraint_adjustment, wl_resource_get_version(resource))) {
    VT_WARN(
      pos->comp->log,
      "VT_PROTO_XDG_SHELL: xdg_positioner.set_constraint_adjustment: Trying to set invalid constraint adjustment %i for positioner %p.", constraint_adjustment, pos); 
  }

  pos->constraint_adjustment = constraint_adjustment; 

  VT_TRACE(
    pos->comp->log,
    "VT_PROTO_XDG_SHELL: xdg_positioner.set_constraint_adjustment: Set constraint adjustment of positioner %p to %i.", pos, constraint_adjustment); 
}

void 
_xdg_positioner_set_offset(struct wl_client *client,
                                   struct wl_resource *resource,
                                   int32_t x, int32_t y) {
  struct vt_xdg_positioner_t* pos = wl_resource_get_user_data(resource);
  if (!pos)
    return;

  pos->offset_x = x; 
  pos->offset_y = y;

  VT_TRACE(
    pos->comp->log,
    "VT_PROTO_XDG_SHELL: xdg_positioner.set_offset: Set offset of positioner %p to %ix%i.", pos, x, y); 
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
  xdg_toplevel_send_configure(surf->toplevel->xdg_toplevel_res, 0, 0, &states);

  xdg_surface_send_configure(surf->xdg_surf_res,
                             wl_display_next_serial(surf->surf->comp->wl.dsp));

  wl_array_release(&states);
}


void 
_xdg_surface_get_toplevel(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t id) {

  vt_xdg_surface_t* xdg_surf = wl_resource_get_user_data(resource);
  if(!xdg_surf) return;

  struct wl_resource* top = wl_resource_create(
    client, &xdg_toplevel_interface,
    wl_resource_get_version(resource), id);
  if(!top) {
    wl_client_post_no_memory(client);
    return;
  }

  xdg_surf->toplevel = calloc(1, sizeof(*xdg_surf->toplevel));
  xdg_surf->toplevel->xdg_toplevel_res = top;

  wl_resource_set_implementation(top, &xdg_toplevel_impl, xdg_surf->toplevel, _xdg_toplevel_handle_resource_destroy);

  // send an initial configure
  send_initial_configure(xdg_surf);
}

void 
_xdg_surface_get_popup(struct wl_client *client,
                               struct wl_resource *resource,
                               uint32_t id,
                               struct wl_resource *parent_surface,
                               struct wl_resource *positioner) {
  vt_xdg_surface_t* xdg_surf = wl_resource_get_user_data(resource);
  if(!xdg_surf) return;

  struct wl_resource* popup = wl_resource_create(
    client, &xdg_popup_interface,
    wl_resource_get_version(resource), id);
  if(!popup) {
    wl_client_post_no_memory(client);
    return;
  }
  
  xdg_surf->popup = calloc(1, sizeof(*xdg_surf->popup));
  xdg_surf->popup->xdg_popup_res = popup;
  xdg_surf->popup->parent_xdg_surface_res = parent_surface;
  xdg_surf->popup->positioner_res = positioner;
  xdg_surf->popup->parent_xdg_surf = xdg_surf;
  
  wl_resource_set_implementation(popup, &xdg_popup_impl, xdg_surf->popup, _xdg_popup_handle_resource_destroy);

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
                                         int32_t width, int32_t height) {
  vt_xdg_surface_t* xdg_surf = wl_resource_get_user_data(resource);
  if(!xdg_surf) return;

  // according to spec, both must be > 0
  if (width <= 0 || height <= 0) {
    wl_resource_post_error(
      resource,
      XDG_SURFACE_ERROR_INVALID_SIZE,
      "width and height must be greater than zero (got %i×%i)",
      width, height);
    return;
  }

  xdg_surf->pending_geom.x = x;
  xdg_surf->pending_geom.y = y;
  xdg_surf->pending_geom.x = (uint32_t)width;
  xdg_surf->pending_geom.h = (uint32_t)height;

  VT_TRACE(
    xdg_surf->surf->comp->log,
    "VT_PROTO_XDG_SHELL: xdg_surface.set_window_geometry: Set window window geometry of surface %p to (%ix%, %ix%i).",
    xdg_surf->surf,
    x, y, width, height);
}

void
_xdg_toplevel_destroy(struct wl_client *client, struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void
_xdg_toplevel_set_parent(struct wl_client *client,
                                 struct wl_resource *resource,
                                 struct wl_resource *parent_resource)
{
  struct vt_xdg_toplevel_t* xdg_toplevel = wl_resource_get_user_data(resource);
  if(!xdg_toplevel) return;

  xdg_toplevel->xdg_parent_toplevel_res = parent_resource;

  VT_TRACE(
    xdg_toplevel->xdg_surf->surf->comp->log,
    "VT_PROTO_XDG_SHELL: xdg_toplevel.set_parent: Set parent of toplevel %p to resource %p.",
    xdg_toplevel,
    parent_resource);
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
}

void
_xdg_toplevel_move(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *seat,
                           uint32_t serial)
{
}

void
_xdg_toplevel_resize(struct wl_client *client,
                             struct wl_resource *resource,
                             struct wl_resource *seat,
                             uint32_t serial,
                             uint32_t edges)
{
}

void
_xdg_toplevel_set_max_size(struct wl_client *client,
                                   struct wl_resource *resource,
                                   int32_t width, int32_t height)
{
}

void
_xdg_toplevel_set_min_size(struct wl_client *client,
                                   struct wl_resource *resource,
                                   int32_t width, int32_t height)
{
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

void 
_xdg_popup_destroy(
  struct wl_client *client, 
  struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void 
_xdg_popup_grab(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial) {

  struct vt_xdg_popup_t* popup = wl_resource_get_user_data(resource);
  if (!popup)
    return;

  if (popup->mapped) {
    wl_resource_post_error(
      resource,
      XDG_POPUP_ERROR_INVALID_GRAB,
      "xdg_popup.grab requested after popup was mapped");
    return;
  }

  // TODO: SET FOCUSED WITH SEAT
  popup->mapped = true;
}

void 
_xdg_popup_reposition(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *positioner,
  uint32_t token) {
  struct vt_xdg_popup_t *popup = wl_resource_get_user_data(resource);
  if (!popup)
    return;

  popup->positioner_res = positioner;

  VT_TRACE(popup->parent_xdg_surf->surf->comp->log, "VT_PROTO_XDG_SHELL: xdg_popup_reposition: token=%i.", token);

  // TOD: replace with real geom 
  xdg_popup_send_configure(resource, 100, 100, 200, 150);
  xdg_popup_send_repositioned(resource, token);
}


// ===================================================
// =================== PUBLIC API ====================
// ===================================================
bool
vt_proto_xdg_shell_init(struct vt_compositor_t* c, uint32_t version) {
  if(!(xdg_wm_base = wl_global_create(c->wl.dsp, &xdg_wm_base_interface, version, c, _xdg_wm_base_bind))) {
    VT_ERROR(c->log, "VT_PROTO_XDG_SHELL: Cannot implement XDG base interface.");
    return false;
  }
  VT_TRACE(c->log, "VT_PROTO_XDG_SHELL: Initialized XDG shell protocol.");
  return true;
}
