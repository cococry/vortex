#define _GNU_SOURCE

#include "src/core/util.h"
#include "xdg_shell.h"

#include "src/core/core_types.h"
#include "src/core/surface.h"
#include "xdg-shell-protocol.h" 

#include <string.h>
#include <wayland-server-core.h>

#define _SUBSYS_NAME "VT_PROTO_XDG_SHELL"

void _xdg_wm_base_bind(
  struct wl_client* client, void* data,
  uint32_t version, uint32_t id);

void _xdg_wm_base_destroy(
  struct wl_client* client, struct wl_resource* resource);

void _xdg_wm_base_create_positioner(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id);

static void _xdg_wm_base_positioner_handle_resource_destroy(struct wl_resource* resource);

static void _xdg_surface_handle_resource_destroy(struct wl_resource* resource);

static void _xdg_toplevel_handle_resource_destroy(struct wl_resource* resource);

static void _xdg_popup_handle_resource_destroy(struct wl_resource* resource);

static void _xdg_wm_base_get_xdg_surface(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id,
  struct wl_resource* surface_res);

static void _xdg_wm_base_pong(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t serial);

static void _xdg_positioner_destroy(
  struct wl_client* client,
  struct wl_resource* resource);

static void _xdg_positioner_set_size(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t width,
  int32_t height);

static void _xdg_positioner_set_anchor_rect(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

static void _xdg_positioner_set_anchor(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t anchor);

static void _xdg_positioner_set_gravity(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t gravity);

static void _xdg_positioner_set_constraint_adjustment(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t constraint_adjustment);

static void _xdg_positioner_set_offset(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t x,
  int32_t y);

static void _xdg_surface_destroy(
  struct wl_client* client,
  struct wl_resource* resource);

static void _xdg_surface_get_toplevel(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id);

static void _xdg_surface_get_popup(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id,
  struct wl_resource* parent_surface,
  struct wl_resource* positioner);

static void _xdg_surface_ack_configure(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t serial);

static void _xdg_surface_set_window_geometry(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

static void _xdg_toplevel_destroy(
  struct wl_client* client,
  struct wl_resource* resource);

static void _xdg_toplevel_set_parent(
  struct wl_client* client,
  struct wl_resource* resource,
  struct wl_resource* parent_resource);

static void _xdg_toplevel_set_title(
  struct wl_client* client,
  struct wl_resource* resource,
  const char* title);

static void _xdg_toplevel_set_app_id(
  struct wl_client* client,
  struct wl_resource* resource,
  const char* app_id);

static void _xdg_toplevel_show_window_menu(
  struct wl_client* client,
  struct wl_resource* resource,
  struct wl_resource* seat,
  uint32_t serial,
  int32_t x,
  int32_t y);

static void _xdg_toplevel_move(
  struct wl_client* client,
  struct wl_resource* resource,
  struct wl_resource* seat,
  uint32_t serial);

static void _xdg_toplevel_resize(
  struct wl_client* client,
  struct wl_resource* resource,
  struct wl_resource* seat,
  uint32_t serial,
  uint32_t edges);

static void _xdg_toplevel_set_max_size(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t width,
  int32_t height);

static void _xdg_toplevel_set_min_size(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t width,
  int32_t height);

static void _xdg_toplevel_set_maximized(
  struct wl_client* client,
  struct wl_resource* resource);

static void _xdg_toplevel_unset_maximized(
  struct wl_client* client,
  struct wl_resource* resource);

static void _xdg_toplevel_set_fullscreen(
  struct wl_client* client,
  struct wl_resource* resource,
  struct wl_resource* output);

static void _xdg_toplevel_unset_fullscreen(
  struct wl_client* client, struct wl_resource* resource);

static void _xdg_toplevel_set_minimized(
  struct wl_client* client, struct wl_resource* resource);

static void _xdg_popup_destroy(
  struct wl_client* client, struct wl_resource* resource);

static void _xdg_popup_grab(
  struct wl_client* client,
  struct wl_resource* resource,
  struct wl_resource* seat,
  uint32_t serial);

static void _xdg_popup_reposition(
  struct wl_client* client,
  struct wl_resource* resource,
  struct wl_resource* positioner,
  uint32_t token);

static bool 
_xdg_toplevel_send_state(struct vt_xdg_toplevel_t* top, uint32_t state, bool activated);

struct vt_xdg_positioner_t {
  struct wl_resource* res;
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

struct vt_proto_xdg_shell_t {
  struct wl_global* xdg_wm_base;
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
  .reposition = _xdg_popup_reposition, 
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

static struct vt_proto_xdg_shell_t _proto;

void
_xdg_wm_base_bind(
  struct wl_client* client, void* data,
  uint32_t version, uint32_t id) {
  /* 1. Allocate resource for the XDG WM-base interface */
  struct wl_resource* res = wl_resource_create(client, &xdg_wm_base_interface, version, id);
  if(!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  /* 2. Set handler functions via the implementation */
  wl_resource_set_implementation(res, &xdg_wm_base_impl, data, NULL);
}


void 
_xdg_wm_base_destroy(struct wl_client* client, struct wl_resource* resource) {
  /* Destroy the XDG WM-base interface resource */
  wl_resource_destroy(resource);
}

void 
_xdg_wm_base_create_positioner(
  struct wl_client* client,
  struct wl_resource* resource, uint32_t id) {
  /* 1. Allocate resource for the positioner interface */
  struct wl_resource* res = wl_resource_create(
    client, &xdg_positioner_interface,
    wl_resource_get_version(resource), id);

  if(!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  /* 2. Allocate internal data for the positioner handle.
   * We are not yet setting any positioning related data in 
   * this call. */
  struct vt_xdg_positioner_t* pos = calloc(1, sizeof(*pos));
  if(!pos) {
    wl_resource_destroy(res);
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  struct vt_compositor_t* comp = resource ? wl_resource_get_user_data(resource) : NULL;
  pos->comp = comp;
  pos->res = res;

  /* 3. Set handler functions via the implementation */
  wl_resource_set_implementation(
    res, &xdg_positioner_impl, pos, 
    _xdg_wm_base_positioner_handle_resource_destroy);

  VT_TRACE(pos->comp->log, "xdg_wm_base.create_positioner: created positioner with resource %p.", res);

}

void 
_xdg_wm_base_positioner_handle_resource_destroy(struct wl_resource* resource) {
  struct vt_xdg_positioner_t* pos = resource ?  wl_resource_get_user_data(resource) : NULL;
  if(!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(pos->comp->log, "xdg_positioner.resource_destroy: destroyed positioner with resource %p.", resource);

  /* 1. Unlink pointers within internal handle */
  pos->res = NULL;
  /* 2. Free Internal handle*/
  free(pos);
  /* 3. Clear resource user data */
  wl_resource_set_user_data(resource, NULL);
}

void 
_xdg_toplevel_handle_resource_destroy(struct wl_resource* resource) {
  struct vt_xdg_toplevel_t* top = resource ? wl_resource_get_user_data(resource) : NULL;
  if(!top) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 1. Unmap all children surfaces of the toplevel and remove 
   * them from this toplevel's children list. */
  struct vt_xdg_toplevel_t* child, *tmp;
  wl_list_for_each_safe(child, tmp, &top->childs, link) {
    /* Remove child from list first to avoid list corruption 
     * in unmap handle. */
    if (!wl_list_empty(&child->link))
      wl_list_remove(&child->link);

    /* Unmap child */
    child->parent = NULL;
    if (child->xdg_surf && child->xdg_surf->surf) {
      vt_surface_unmapped(child->xdg_surf->surf);
    }
  }

  /* 2. Unmap the toplevel surface itself. */
  if(top->xdg_surf && top->xdg_surf->surf) {
    vt_surface_unmapped(top->xdg_surf->surf);
  }

  /* 3. Unlink internal pointers and deallocate the toplevel 
   * handle associated with the resource. */
  if(top->xdg_surf) top->xdg_surf->toplevel = NULL;

  if (top->parent) {
    wl_list_remove(&top->link);
    top->parent = NULL;
  }

  wl_resource_set_user_data(resource, NULL);
  free(top);
}

void 
_xdg_surface_handle_resource_destroy(struct wl_resource* resource) {
  struct vt_xdg_surface_t* surf = resource ? wl_resource_get_user_data(resource) : NULL;
  if(!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp); 
    return;
  }

  /* Unlink internal pointers and deallocate the toplevel 
   * handle associated with the resource. */
  if (surf->surf) surf->surf->xdg_surf = NULL;
  wl_resource_set_user_data(resource, NULL);
  free(surf);
}

void 
_xdg_popup_handle_resource_destroy(struct wl_resource* resource) {
  struct vt_xdg_popup_t* popup = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!popup) {
    VT_PARAM_CHECK_FAIL(_proto.comp); 
    return;
  }

  /* 1. Unmap the popup's surface */ 
  if(popup->xdg_surf && popup->xdg_surf->surf) {
    vt_surface_unmapped(popup->xdg_surf->surf);
  }

  /* 2. Unlink internal pointers and deallocate the toplevel 
   * handle associated with the resource. */
  popup->xdg_popup_res = NULL;
  if (popup->parent_xdg_surf)  popup->parent_xdg_surf->popup = NULL;
  popup->parent_xdg_surf = NULL;
  popup->positioner_res = NULL;
  wl_resource_set_user_data(resource, NULL);
  free(popup);
}

void 
_xdg_wm_base_get_xdg_surface(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id, struct wl_resource* surface_res) {
  struct vt_surface_t* surf =  surface_res ? wl_resource_get_user_data(surface_res) : NULL;
  if(!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  } 

  /* 1. Allocate resource for the XDG-Surface interface */
  struct wl_resource* res = wl_resource_create(
    client, &xdg_surface_interface,
    wl_resource_get_version(resource), id);

  if(!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  /* 2. Allcoate internal surface handle and assign pointers. */
  struct vt_xdg_surface_t* xdg_surf = calloc(1, sizeof(*xdg_surf));
  if(!xdg_surf) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  xdg_surf->surf = surf;
  xdg_surf->xdg_surf_res = res;
  surf->xdg_surf = xdg_surf;

  /* 3. Set handler functions via the implementation */
  wl_resource_set_implementation(res, &xdg_surface_impl, xdg_surf, _xdg_surface_handle_resource_destroy);

  VT_TRACE(surf->comp->log, "xdg_wm_base.get_xdg_surface with resource %p.", res);
}

void 
_xdg_wm_base_pong(struct wl_client* client,
                  struct wl_resource* resource, uint32_t serial) {
}

void 
_xdg_positioner_destroy(struct wl_client* client,
                        struct wl_resource* resource)
{
  wl_resource_destroy(resource);
}

void 
_xdg_positioner_set_size(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t width, int32_t height) {
  /* 1. Retrieve internal positioner handle */
  struct vt_xdg_positioner_t* pos = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* according to spec, both must be > 0 */
  if (width <= 0 || height <= 0) {
    wl_resource_post_error(
      resource,
      XDG_POSITIONER_ERROR_INVALID_INPUT,
      "width and height must be greater than zero (got %i×%i)",
      width, height);

    VT_WARN(
      pos->comp->log,
      "xdg_positioner.set_size: Trying to set invalid width (%i) or height (%i) for positioner %p.", width, height, pos); 
    return;
  }

  /* 2. Update to reflect requested positioner data in internal handle */
  pos->width = width;
  pos->height = height;

  VT_TRACE(
    _proto.comp->log,
    "xdg_positioner.set_size: Size %ix%i for positioner %p.",
    width, height, pos);
}

void 
_xdg_positioner_set_anchor_rect(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t x, int32_t y,
  int32_t width, int32_t height) {
  /* 1. Retrieve internal positioner handle */
  struct vt_xdg_positioner_t* pos = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Validate input parameters
   * According to spec, width and height must be positive. */
  if (width < 0 || height < 0) {
    wl_resource_post_error(
      resource,
      XDG_POSITIONER_ERROR_INVALID_INPUT,
      "width and height must be greater than zero (got %i×%i)",
      width, height);

    VT_WARN(
      pos->comp->log,
      "xdg_positioner.set_anchor: Trying to set invalid width (%i) or height (%i) for positioner %p.", width, height, pos); 
    return;
  }

  /* 3. Update to reflect requested positioner data in internal handle */
  pos->anchor_rect_pos.x = x;
  pos->anchor_rect_pos.y = y;
  pos->anchor_rect_size.width = width;
  pos->anchor_rect_size.height = height;

  VT_TRACE(
    pos->comp->log,
    "xdg_positioner.set_anchor_rect: Pos %ix%i, size %ix%i for positioner %p.",
    x, y, width, height, pos);
}

void 
_xdg_positioner_set_anchor(struct wl_client* client,
                           struct wl_resource* resource,
                           uint32_t anchor) { 
  /* 1. Retrieve internal positioner handle */
  struct vt_xdg_positioner_t* pos = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Validate input parameters */
  if(!xdg_positioner_anchor_is_valid(anchor, wl_resource_get_version(resource))) {
    wl_resource_post_error(
      resource,
      XDG_POSITIONER_ERROR_INVALID_INPUT,
      "invalid anchor requested (got %i)",
      anchor);

    VT_WARN(
      pos->comp->log,
      "xdg_positioner.set_anchor: Trying to set invalid anchor %i for positioner %p.", anchor, pos); 

    return;
  }

  /* 3. Update to reflect requested positioner data in internal handle */
  pos->anchor = anchor; 

  VT_TRACE(
    pos->comp->log,
    "xdg_positioner.set_anchor: Set anchor of positioner %p to %i.", pos, anchor); 
}

void 
_xdg_positioner_set_gravity(struct wl_client* client,
                            struct wl_resource* resource,
                            uint32_t gravity) {
  /* 1. Retrieve internal positioner handle */
  struct vt_xdg_positioner_t* pos = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Validate input parameters */
  if(!xdg_positioner_gravity_is_valid(gravity, wl_resource_get_version(resource))) {
    wl_resource_post_error(
      resource,
      XDG_POSITIONER_ERROR_INVALID_INPUT,
      "invalid gravity requested (got %i)",
      gravity);

    VT_WARN(
      pos->comp->log,
      "xdg_positioner.set_gravity: Trying to set invalid gravity %i for positioner %p.", gravity, pos); 

    return;
  }

  /* 3. Update to reflect requested positioner data in internal handle */
  pos->gravity = gravity; 

  VT_TRACE(
    pos->comp->log,
    "xdg_positioner.set_gravity: Set gravity of positioner %p to %i.", pos, gravity); 
}

void 
_xdg_positioner_set_constraint_adjustment(struct wl_client* client,
                                          struct wl_resource* resource,
                                          uint32_t constraint_adjustment){
  /* 1. Retrieve internal positioner handle */
  struct vt_xdg_positioner_t* pos = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }


  /* 2. Validate input parameters */
  if(!xdg_positioner_constraint_adjustment_is_valid(constraint_adjustment, wl_resource_get_version(resource))) {
    wl_resource_post_error(
      resource,
      XDG_POSITIONER_ERROR_INVALID_INPUT,
      "invalid constraint adjustment requested (got %i)",
      constraint_adjustment);

    VT_WARN(
      pos->comp->log,
      "xdg_positioner.set_constraint_adjustment: Trying to set invalid constraint adjustment %i for positioner %p.", constraint_adjustment, pos); 

    return; 
  }

  /* 3. Update to reflect requested positioner data in internal handle */
  pos->constraint_adjustment = constraint_adjustment; 

  VT_TRACE(
    pos->comp->log,
    "xdg_positioner.set_constraint_adjustment: Set constraint adjustment of positioner %p to %i.", pos, constraint_adjustment); 
}

void 
_xdg_positioner_set_offset(struct wl_client* client,
                           struct wl_resource* resource,
                           int32_t x, int32_t y) {
  /* 1. Retrieve internal positioner handle */
  struct vt_xdg_positioner_t* pos = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Update to reflect requested positioner data in internal handle */
  pos->offset_x = x; 
  pos->offset_y = y;

  VT_TRACE(
    pos->comp->log,
    "xdg_positioner.set_offset: Set offset of positioner %p to %ix%i.", pos, x, y); 
}

void 
_xdg_surface_destroy(struct wl_client* client,
                     struct wl_resource* resource) {
  wl_resource_destroy(resource);
}

void
send_initial_configure(struct vt_xdg_surface_t* surf) {
  if(!surf || !surf->toplevel) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }
  /* Send empty state request to trigger initial configure. */
  _xdg_toplevel_send_state(surf->toplevel, 0, false);
}


void 
_xdg_surface_get_toplevel(struct wl_client* client,
                          struct wl_resource* resource,
                          uint32_t id) {

  /* 1. Retrieve internal XDG-Surface handle from the resource 
   * the request came from. */
  struct vt_xdg_surface_t* xdg_surf = resource ? wl_resource_get_user_data(resource) : NULL;
  if(!xdg_surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. According to spec, XDG-Surfaces can only ever be 
   * assigned one role.*/
  if (xdg_surf->toplevel || xdg_surf->popup) {
    wl_resource_post_error(
      resource,
      XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
      "xdg_surface already has a role");

    VT_WARN(_proto.comp->log, "XDG surface %p already has another role.", xdg_surf)
    return;
  }

  /* 3. Allocate resource for the XDG-Toplevel interface */
  struct wl_resource* res = wl_resource_create(
    client, &xdg_toplevel_interface,
    wl_resource_get_version(resource), id);

  if(!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }


  /* 4. Allcoate internal toplevel handle and assign pointers. */
  xdg_surf->toplevel = calloc(1, sizeof(*xdg_surf->toplevel));
  if(!xdg_surf->toplevel) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  xdg_surf->toplevel->xdg_surf = xdg_surf;
  xdg_surf->toplevel->xdg_toplevel_res = res;
  xdg_surf->toplevel->parent = NULL; 
  wl_list_init(&xdg_surf->toplevel->childs);

  /* 5. Set handler functions via the implementation */
  wl_resource_set_implementation(res, &xdg_toplevel_impl, xdg_surf->toplevel, _xdg_toplevel_handle_resource_destroy);

  /* 6. send an initial configure */
  send_initial_configure(xdg_surf);
}

void 
_xdg_surface_get_popup(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id,
  struct wl_resource* parent_surface,
  struct wl_resource* positioner) {

  /* 1. Retrieve internal XDG-Surface handle for both parent toplevel and popup 
   * from the resources the request came from. */
  struct vt_xdg_surface_t* popup_xdg_surf   = resource        ? wl_resource_get_user_data(resource)       : NULL;
  struct vt_xdg_surface_t* parent_xdg_surf  = parent_surface  ? wl_resource_get_user_data(parent_surface) : NULL;

  if(!popup_xdg_surf || !parent_xdg_surf) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  /* 2. According to spec, XDG-Surfaces can only ever be 
   * assigned one role.*/
  if (popup_xdg_surf->toplevel || popup_xdg_surf->popup) {
    wl_resource_post_error(
      resource,
      XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
      "xdg_surface already has a role");

    VT_WARN(_proto.comp->log, "XDG surface %p already has another role.", popup_xdg_surf)

    return;
  }

  /* 3. Allocate resource for the XDG-Popup interface */
  struct wl_resource* res = wl_resource_create(
    client, &xdg_popup_interface,
    wl_resource_get_version(resource), id);

  if(!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  /* 4. Allcoate internal popup handle and assign pointers. */
  popup_xdg_surf->popup = calloc(1, sizeof(*popup_xdg_surf->popup));
  if(!popup_xdg_surf->popup) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  popup_xdg_surf->popup->xdg_popup_res = res;
  popup_xdg_surf->popup->parent_xdg_surf = parent_xdg_surf;
  popup_xdg_surf->popup->xdg_surf = popup_xdg_surf;
  popup_xdg_surf->popup->positioner_res = positioner;


  /* 5. Set handler functions via the implementation */
  wl_resource_set_implementation(res, &xdg_popup_impl, popup_xdg_surf->popup, _xdg_popup_handle_resource_destroy);

  /* 6. Send popup configure with positioner data*/
  struct vt_xdg_positioner_t* pos = positioner ? wl_resource_get_user_data(positioner) : NULL;
  if (pos) {
    int32_t x = pos->anchor_rect_pos.x + pos->offset_x;
    int32_t y = pos->anchor_rect_pos.y + pos->offset_y;
    uint32_t w = pos->width;
    uint32_t h = pos->height;

    popup_xdg_surf->pending_geom.x = x;
    popup_xdg_surf->pending_geom.y = y;
    popup_xdg_surf->pending_geom.w = w;
    popup_xdg_surf->pending_geom.h = h;
    printf("SETTING ON POPUP: %i, %i, %i, %i\n", x, y, w, h);
    xdg_popup_send_configure(res, x, y, w, h);
  }

  /* 7. Send the corresponding xdg_surface.configure with a fresh serial
   * to the xdg surface associated with the popup*/
  uint32_t serial = wl_display_next_serial(popup_xdg_surf->surf->comp->wl.dsp);
  xdg_surface_send_configure(popup_xdg_surf->xdg_surf_res, serial);

}

void 
_xdg_surface_ack_configure(struct wl_client* client,
                           struct wl_resource* resource,
                           uint32_t serial)
{
  struct vt_xdg_surface_t* surf = wl_resource_get_user_data(resource);
  /* Set last known configure serial */
  surf->last_configure_serial = serial;
}

void 
_xdg_surface_set_window_geometry(struct wl_client* client,
                                 struct wl_resource* resource,
                                 int32_t x, int32_t y,
                                 int32_t width, int32_t height) {
  /* 1. Retrieve internal XDG-Surface handle */
  struct vt_xdg_surface_t* xdg_surf = resource ? wl_resource_get_user_data(resource) : NULL;
  if(!xdg_surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Validate input parameters.
   * According to the spec, both width and height must be > 0 */
  if (width <= 0 || height <= 0) {
    wl_resource_post_error(
      resource,
      XDG_SURFACE_ERROR_INVALID_SIZE,
      "width and height must be greater than zero (got %i×%i)",
      width, height);

    VT_WARN(
      xdg_surf->surf->comp->log,
      "xdg_positioner.set_size: Trying to set invalid width (%i) or height (%i) for XDG surface %p.", width, height, xdg_surf); 
    return;
  }

  /* 3. Send window geometry to requested geometry */
  xdg_surf->have_pending_geom = true;
  xdg_surf->pending_geom.x = x;
  xdg_surf->pending_geom.y = y;
  xdg_surf->pending_geom.w = (uint32_t)width;
  xdg_surf->pending_geom.h = (uint32_t)height;

  printf("SETTING ON SET GEOM: %i, %i, %i, %i\n", x, y, width, height);
  VT_TRACE(
    xdg_surf->surf->comp->log,
    "xdg_surface.set_window_geometry: Set window window geometry of surface %p to (%ix%i, %ix%i).",
    xdg_surf->surf,
    x, y, width, height);
}

void
_xdg_toplevel_destroy(struct wl_client* client, struct wl_resource* resource) {
  wl_resource_destroy(resource);
}
void
_xdg_toplevel_set_parent(struct wl_client* client,
                         struct wl_resource* resource,
                         struct wl_resource* parent_resource) {
  /* 1. Retrieve internal XDG-Toplevel handle */
  struct vt_xdg_toplevel_t* xdg_toplevel = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!xdg_toplevel) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Retrieve parent's internal XDG-Toplevel handle */
  struct vt_xdg_toplevel_t* parent = parent_resource ? wl_resource_get_user_data(parent_resource) : NULL;
  if(parent) {
    /* 3. Insert the request-making XDG Toplevel into the list of children of the 
     * given parent XDG-Toplevel. */
    wl_list_insert(&parent->childs, &xdg_toplevel->link);
  }

  /* 4. Set parent of request-making XDG-Toplevel */ 
  xdg_toplevel->parent = parent;

  VT_TRACE(
    xdg_toplevel->xdg_surf->surf->comp->log,
    "xdg_toplevel.set_parent: Set parent of toplevel %p to %p.",
    xdg_toplevel,
    parent);

}

void
_xdg_toplevel_set_title(struct wl_client* client,
                        struct wl_resource* resource,
                        const char* title)
{
  /* Deallocate the old App Title and strdup() the requested ID */
  struct vt_xdg_toplevel_t* top = wl_resource_get_user_data(resource);
  if (top->title)
    free(top->title);
  top->title = strdup(title ? title : "");
}

void
_xdg_toplevel_set_app_id(struct wl_client* client,
                         struct wl_resource* resource,
                         const char* app_id)
{
  struct vt_xdg_toplevel_t* top = wl_resource_get_user_data(resource);
  /* Deallocate the old App ID and strdup() the requested ID */
  if (top->app_id) free(top->app_id);
  top->app_id = strdup(app_id ? app_id : "");
}

void
_xdg_toplevel_show_window_menu(struct wl_client* client,
                               struct wl_resource* resource,
                               struct wl_resource* seat,
                               uint32_t serial,
                               int32_t x, int32_t y)
{
}

void
_xdg_toplevel_move(struct wl_client* client,
                   struct wl_resource* resource,
                   struct wl_resource* seat,
                   uint32_t serial)
{
}

void
_xdg_toplevel_resize(struct wl_client* client,
                     struct wl_resource* resource,
                     struct wl_resource* seat,
                     uint32_t serial,
                     uint32_t edges)
{
}

void
_xdg_toplevel_set_max_size(struct wl_client* client,
                           struct wl_resource* resource,
                           int32_t width, int32_t height)
{
}

void
_xdg_toplevel_set_min_size(struct wl_client* client,
                           struct wl_resource* resource,
                           int32_t width, int32_t height)
{
}

void
_xdg_toplevel_set_maximized(struct wl_client* client,
                            struct wl_resource* resource)
{
  // optional: ignore
}

void
_xdg_toplevel_unset_maximized(struct wl_client* client,
                              struct wl_resource* resource)
{
}

void
_xdg_toplevel_set_fullscreen(struct wl_client* client,
                             struct wl_resource* resource,
                             struct wl_resource* output)
{
}

void
_xdg_toplevel_unset_fullscreen(struct wl_client* client,
                               struct wl_resource* resource)
{
}

void
_xdg_toplevel_set_minimized(struct wl_client* client,
                            struct wl_resource* resource)
{

}

void 
_xdg_popup_destroy(
  struct wl_client* client, 
  struct wl_resource* resource) {
  wl_resource_destroy(resource);
}

void 
_xdg_popup_grab(
  struct wl_client* client,
  struct wl_resource* resource,
  struct wl_resource* seat,
  uint32_t serial) {
  /* 1. Retrieve internal XDG-Popup handle */
  struct vt_xdg_popup_t* popup = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!popup) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. According to spec, popups need to be grabbed before they get mapped */
  if (popup->mapped) {
    wl_resource_post_error(
      resource,
      XDG_POPUP_ERROR_INVALID_GRAB,
      "xdg_popup.grab requested after popup was mapped");

    VT_WARN(popup->xdg_surf->surf->comp->log, "xdg_popup.grab: requested after popup was mapped.");

    return;
  }

  /* 3. Store grab-seat and gra-serial */
  popup->grab_seat = seat;
  popup->grab_serial = serial;
  popup->has_grab = true;

  /* 4. Set seat's keyboard to grabbed popup */
  vt_seat_set_keyboard_focus(wl_resource_get_user_data(seat),
                             popup->xdg_surf->surf);

  popup->mapped = true;
}

void 
_xdg_popup_reposition(
  struct wl_client* client,
  struct wl_resource* resource,
  struct wl_resource* positioner,
  uint32_t token) {
  /* 1. Retrieve internal XDG-Popup handle */
  struct vt_xdg_popup_t* popup = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!popup) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Update internally stored positioner resource */
  popup->positioner_res = positioner;

  VT_TRACE(popup->parent_xdg_surf->surf->comp->log, "xdg_popup_reposition: token=%i.", token);

  /* 3. Send new positioning properties if positioner is valid*/
  struct vt_xdg_positioner_t* pos = positioner ? wl_resource_get_user_data(positioner) : NULL;
  if (pos) {
    int32_t x = pos->anchor_rect_pos.x + pos->offset_x;
    int32_t y = pos->anchor_rect_pos.y + pos->offset_y;
    uint32_t w = pos->width;
    uint32_t h = pos->height;
    
    popup->pending_geom.x = x;
    popup->pending_geom.y = x;
    popup->pending_geom.w = w;
    popup->pending_geom.h = h;
    printf("SETTING ON REPOS: %i, %i, %i, %i\n", x, y, w, h);

    xdg_popup_send_configure(resource, x, y, w, h);
    xdg_popup_send_repositioned(resource, token);
  }
}

bool 
_xdg_toplevel_send_state(struct vt_xdg_toplevel_t* top, uint32_t state, bool activated) {
  /* [0]: The function returns whether or not the state has been sent successfully */
  /* The 'state' parameter is a XDG_TOPLEVEL_STATE_* value. */
  if(!top || !top->xdg_surf || !top->xdg_surf->surf || !top->xdg_toplevel_res) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return false;
  }

  struct wl_client* client = wl_resource_get_client(top->xdg_toplevel_res);
  if(!client) return false;
  struct wl_display* dsp = wl_client_get_display(client);

  uint32_t serial = wl_display_next_serial(dsp);

  /* 1. Populate the states array with the single given state */
  struct wl_array states;
  wl_array_init(&states);

  /* 2. If the requested state should be activated, add it 
   * to the array of states. In the case of deactivation, 
   * the array stays empty which results in the requested 
   * state being cleared (removed).
   * */
  if(activated) {
    uint32_t* state_elem = wl_array_add(&states, sizeof(*state_elem));
    if(!state_elem) {
      wl_array_release(&states);
      VT_WL_OUT_OF_MEMORY(top->xdg_surf->surf->comp, client);
      return false;
    }
    *state_elem =  state;
  }

  /* 3. Issue the configure request with the changed state
   * (width, height: 0, 0 -> unchanged)*/
  xdg_toplevel_send_configure(top->xdg_toplevel_res, 0, 0, &states);

  /* Deallocate the states array */
  wl_array_release(&states);

  if(!top->xdg_surf->xdg_surf_res) {
    VT_ERROR(top->xdg_surf->surf->comp->log, "_proto_xdg_toplevel_send_state: Toplevel %p has no associated XDG surface resource.",
             top);
    return false;
  }

  /* 4. Send the corresponding xdg_surface.configure with a fresh serial
   * to the xdg surface associated with the toplevel */
  xdg_surface_send_configure(top->xdg_surf->xdg_surf_res, serial); 

  return true;
}



// ===================================================
// =================== PUBLIC API ====================
// ===================================================
bool
vt_proto_xdg_shell_init(struct vt_compositor_t* c, uint32_t version) {
  if(!(_proto.xdg_wm_base = wl_global_create(c->wl.dsp, &xdg_wm_base_interface, version, c, _xdg_wm_base_bind))) {
    VT_ERROR(c->log, "Cannot implement XDG base interface.");
    return false;
  }

  _proto.comp = c;

  VT_TRACE(c->log, "Initialized XDG shell protocol.");
  return true;
}


bool 
vt_proto_xdg_toplevel_set_state_maximized(struct vt_xdg_toplevel_t* top, bool activated) {
  return _xdg_toplevel_send_state(
    top, XDG_TOPLEVEL_STATE_MAXIMIZED, activated);
}

bool
vt_proto_xdg_toplevel_set_state_fullscreen(struct vt_xdg_toplevel_t* top, bool activated) {
  return _xdg_toplevel_send_state(
    top, XDG_TOPLEVEL_STATE_FULLSCREEN, activated);
}

bool
vt_proto_xdg_toplevel_set_state_resizing(struct vt_xdg_toplevel_t* top, bool activated) {
  return _xdg_toplevel_send_state(
    top, XDG_TOPLEVEL_STATE_RESIZING, activated);
}

bool
vt_proto_xdg_toplevel_set_state_activated(struct vt_xdg_toplevel_t* top, bool activated) {
  return _xdg_toplevel_send_state(
    top, XDG_TOPLEVEL_STATE_ACTIVATED, activated);
}

