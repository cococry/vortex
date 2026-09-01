#define _GNU_SOURCE

#include "xdg_shell.h"
#include "src/core/util.h"

#include "src/core/core_types.h"
#include "src/core/scene.h"
#include "src/core/surface.h"
#include "xdg-shell-protocol.h"

#include <string.h>
#include <wayland-server-core.h>

#define _SUBSYS_NAME "VT_PROTO_XDG_SHELL"

void _xdg_wm_base_bind(struct wl_client *client, void *data, uint32_t version,
                       uint32_t id);

void _xdg_wm_base_destroy(struct wl_client   *client,
                          struct wl_resource *resource);

void _xdg_wm_base_create_positioner(struct wl_client   *client,
                                    struct wl_resource *resource, uint32_t id);

static void
_xdg_wm_base_positioner_handle_resource_destroy(struct wl_resource *resource);

static void _xdg_surface_handle_resource_destroy(struct wl_resource *resource);

static void _xdg_toplevel_handle_resource_destroy(struct wl_resource *resource);

static void _xdg_popup_handle_resource_destroy(struct wl_resource *resource);

static void _xdg_wm_base_get_xdg_surface(struct wl_client   *client,
                                         struct wl_resource *resource,
                                         uint32_t            id,
                                         struct wl_resource *surface_res);

static void _xdg_wm_base_pong(struct wl_client   *client,
                              struct wl_resource *resource, uint32_t serial);

static void _xdg_positioner_destroy(struct wl_client   *client,
                                    struct wl_resource *resource);

static void _xdg_positioner_set_size(struct wl_client   *client,
                                     struct wl_resource *resource,
                                     int32_t width, int32_t height);

static void _xdg_positioner_set_anchor_rect(struct wl_client   *client,
                                            struct wl_resource *resource,
                                            int32_t x, int32_t y, int32_t width,
                                            int32_t height);

static void _xdg_positioner_set_anchor(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t            anchor);

static void _xdg_positioner_set_gravity(struct wl_client   *client,
                                        struct wl_resource *resource,
                                        uint32_t            gravity);

static void
_xdg_positioner_set_constraint_adjustment(struct wl_client   *client,
                                          struct wl_resource *resource,
                                          uint32_t constraint_adjustment);

static void _xdg_positioner_set_offset(struct wl_client   *client,
                                       struct wl_resource *resource, int32_t x,
                                       int32_t y);

static void _xdg_surface_destroy(struct wl_client   *client,
                                 struct wl_resource *resource);

static void _xdg_surface_get_toplevel(struct wl_client   *client,
                                      struct wl_resource *resource,
                                      uint32_t            id);

static void _xdg_surface_get_popup(struct wl_client   *client,
                                   struct wl_resource *resource, uint32_t id,
                                   struct wl_resource *parent_surface,
                                   struct wl_resource *positioner);

static void _xdg_surface_ack_configure(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t            serial);

static void _xdg_surface_set_window_geometry(struct wl_client   *client,
                                             struct wl_resource *resource,
                                             int32_t x, int32_t y,
                                             int32_t width, int32_t height);

static void _xdg_toplevel_destroy(struct wl_client   *client,
                                  struct wl_resource *resource);

static void _xdg_toplevel_set_parent(struct wl_client   *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *parent_resource);

static void _xdg_toplevel_set_title(struct wl_client   *client,
                                    struct wl_resource *resource,
                                    const char         *title);

static void _xdg_toplevel_set_app_id(struct wl_client   *client,
                                     struct wl_resource *resource,
                                     const char         *app_id);

static void _xdg_toplevel_show_window_menu(struct wl_client   *client,
                                           struct wl_resource *resource,
                                           struct wl_resource *seat,
                                           uint32_t serial, int32_t x,
                                           int32_t y);

static void _xdg_toplevel_move(struct wl_client   *client,
                               struct wl_resource *resource,
                               struct wl_resource *seat, uint32_t serial);

static void _xdg_toplevel_resize(struct wl_client   *client,
                                 struct wl_resource *resource,
                                 struct wl_resource *seat, uint32_t serial,
                                 uint32_t edges);

static void _xdg_toplevel_set_max_size(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       int32_t width, int32_t height);

static void _xdg_toplevel_set_min_size(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       int32_t width, int32_t height);

static void _xdg_toplevel_set_maximized(struct wl_client   *client,
                                        struct wl_resource *resource);

static void _xdg_toplevel_unset_maximized(struct wl_client   *client,
                                          struct wl_resource *resource);

static void _xdg_toplevel_set_fullscreen(struct wl_client   *client,
                                         struct wl_resource *resource,
                                         struct wl_resource *output);

static void _xdg_toplevel_unset_fullscreen(struct wl_client   *client,
                                           struct wl_resource *resource);

static void _xdg_toplevel_set_minimized(struct wl_client   *client,
                                        struct wl_resource *resource);

static void _xdg_popup_destroy(struct wl_client   *client,
                               struct wl_resource *resource);

static void _xdg_popup_grab(struct wl_client   *client,
                            struct wl_resource *resource,
                            struct wl_resource *seat, uint32_t serial);

static void _xdg_popup_reposition(struct wl_client   *client,
                                  struct wl_resource *resource,
                                  struct wl_resource *positioner,
                                  uint32_t            token);

static bool _xdg_toplevel_send_state(struct vt_xdg_toplevel_t *top,
                                     uint32_t state, bool activated);

struct vt_xdg_positioner_t {
  struct wl_resource *res;
  int32_t             width;
  int32_t             height;
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

  struct vt_compositor_t *comp;
};

struct vt_proto_xdg_shell_t {
  struct wl_global       *xdg_wm_base;
  struct vt_compositor_t *comp;
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

static bool _popup_resolve_pos(struct vt_xdg_popup_t       *popup,
                               struct vt_xdg_positioner_t  *pos,
                               struct vt_xdg_window_geom_t *o_geom);

void _xdg_wm_base_bind(struct wl_client *client, void *data, uint32_t version,
                       uint32_t id) {
  /* 1. Allocate resource for the XDG WM-base interface */
  struct wl_resource *res =
      wl_resource_create(client, &xdg_wm_base_interface, version, id);
  if (!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  /* 2. Set handler functions via the implementation */
  wl_resource_set_implementation(res, &xdg_wm_base_impl, data, NULL);
}

void _xdg_wm_base_destroy(struct wl_client   *client,
                          struct wl_resource *resource) {
  /* Destroy the XDG WM-base interface resource */
  wl_resource_destroy(resource);
}

void _xdg_wm_base_create_positioner(struct wl_client   *client,
                                    struct wl_resource *resource, uint32_t id) {
  /* 1. Allocate resource for the positioner interface */
  struct wl_resource *res = wl_resource_create(
      client, &xdg_positioner_interface, wl_resource_get_version(resource), id);

  if (!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  /* 2. Allocate internal data for the positioner handle.
   * We are not yet setting any positioning related data in
   * this call. */

  struct vt_xdg_positioner_t *pos = calloc(1, sizeof(*pos));
  if (!pos) {
    wl_resource_destroy(res);
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  struct vt_compositor_t *comp =
      resource ? wl_resource_get_user_data(resource) : NULL;
  pos->comp = comp;
  pos->res = res;

  /* 3. Set handler functions via the implementation */
  wl_resource_set_implementation(
      res, &xdg_positioner_impl, pos,
      _xdg_wm_base_positioner_handle_resource_destroy);

  VT_TRACE(
      pos->comp->log,
      "xdg_wm_base.create_positioner: created positioner with resource %p.",
      res);
}

void _xdg_wm_base_positioner_handle_resource_destroy(
    struct wl_resource *resource) {
  struct vt_xdg_positioner_t *pos =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(
      pos->comp->log,
      "xdg_positioner.resource_destroy: destroyed positioner with resource %p.",
      resource);

  /* 1. Unlink pointers within internal handle */
  pos->res = NULL;
  /* 2. Free Internal handle*/
  free(pos);
  /* 3. Clear resource user data */
  wl_resource_set_user_data(resource, NULL);
}

void _xdg_toplevel_handle_resource_destroy(struct wl_resource *resource) {
  struct vt_xdg_toplevel_t *top =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!top) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }
  top->xdg_toplevel_res = NULL;

  /* 1. Unmap all children surfaces of the toplevel and remove
   * them from this toplevel's children list. */
  struct vt_xdg_toplevel_t *child, *tmp;
  wl_list_for_each_safe(child, tmp, &top->childs, link) {
    /* Remove child from list first to avoid list corruption
     * in unmap handle. */
    if (!wl_list_empty(&child->link)) {
      wl_list_remove(&child->link);
      wl_list_init(&child->link);
    }

    /* Unmap child */
    child->parent = NULL;
    if (child->xdg_surf && child->xdg_surf->surf) {
      vt_surface_unmapped(child->xdg_surf->surf);
    }
  }

  /* 2. Unmap the toplevel surface itself. */
  if (top->xdg_surf && top->xdg_surf->surf) {
    vt_surface_unmapped(top->xdg_surf->surf);
  }

  /* 3. Unlink internal pointers and deallocate the toplevel
   * handle associated with the resource. */
  if (top->xdg_surf)
    top->xdg_surf->toplevel = NULL;

  if (top->parent) {
    wl_list_remove(&top->link);
    wl_list_init(&top->link);

    top->parent = NULL;
  }

  wl_resource_set_user_data(resource, NULL);
  free(top);
}

void _xdg_surface_handle_resource_destroy(struct wl_resource *resource) {
  struct vt_xdg_surface_t *xdg =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!xdg) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* Unlink internal pointers and deallocate the toplevel
   * handle associated with the resource. */

  if (xdg->toplevel) {
    if (xdg->toplevel->xdg_surf == xdg)
      xdg->toplevel->xdg_surf = NULL;

    xdg->toplevel = NULL;
  }

  if (xdg->popup) {
    if (xdg->popup->xdg_surf == xdg)
      xdg->popup->xdg_surf = NULL;

    xdg->popup = NULL;
  }

  if (xdg->surf) {
    if (xdg->surf->xdg_surf == xdg)
      xdg->surf->xdg_surf = NULL;

    xdg->surf = NULL;
  }

  xdg->xdg_surf_res = NULL;

  wl_resource_set_user_data(resource, NULL);
  free(xdg);
}

void _xdg_popup_handle_resource_destroy(struct wl_resource *resource) {
  struct vt_xdg_popup_t *popup =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!popup) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 1. Unmap the popup's surface */
  if (popup->xdg_surf && popup->xdg_surf->surf) {
    vt_surface_unmapped(popup->xdg_surf->surf);
  }

  /* 2. Unlink internal pointers and deallocate the toplevel
   * handle associated with the resource. */
  popup->xdg_popup_res = NULL;
  if (popup->parent_xdg_surf)
    popup->parent_xdg_surf->popup = NULL;
  popup->parent_xdg_surf = NULL;
  wl_resource_set_user_data(resource, NULL);
  free(popup);
}

static void _xdg_surface_commit(struct vt_surface_t *surf) {
  struct vt_xdg_surface_t *xdg = surf->xdg_surf;

  if (!xdg)
    return;

  if (xdg->have_pending_geom) {
    xdg->geom = xdg->pending_geom;
    xdg->have_pending_geom = false;

    vt_scene_node_set_position(xdg->geom_node, xdg->geom.x, xdg->geom.y);

    xdg->geom_node->rect.width = (float)xdg->geom.w;

    xdg->geom_node->rect.height = (float)xdg->geom.h;
  }

  struct vt_xdg_popup_t *popup = xdg->popup;

  if (popup && popup->have_acked_geom) {
    popup->configured_geom = popup->acked_geom;

    popup->have_acked_geom = false;

    vt_scene_node_set_position(surf->scene_node, popup->configured_geom.x,
                               popup->configured_geom.y);
  }
}

void _xdg_wm_base_get_xdg_surface(struct wl_client   *client,
                                  struct wl_resource *resource, uint32_t id,
                                  struct wl_resource *surface_res) {
  struct vt_surface_t *surf =
      surface_res ? wl_resource_get_user_data(surface_res) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 1. Allocate resource for the XDG-Surface interface */
  struct wl_resource *res = wl_resource_create(
      client, &xdg_surface_interface, wl_resource_get_version(resource), id);

  if (!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  /* 2. Allcoate internal surface handle and assign pointers. */
  struct vt_xdg_surface_t *xdg_surf = calloc(1, sizeof(*xdg_surf));
  if (!xdg_surf) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  surf->role_impl.commit = _xdg_surface_commit;

  xdg_surf->geom_node =
      vt_scene_node_create_rect_invisible(surf->comp, 0, 0, 0, 0);

  vt_scene_node_add_child(surf->comp, surf->scene_node, xdg_surf->geom_node);

  xdg_surf->surf = surf;
  xdg_surf->xdg_surf_res = res;
  surf->xdg_surf = xdg_surf;

  /* 3. Set handler functions via the implementation */
  wl_resource_set_implementation(res, &xdg_surface_impl, xdg_surf,
                                 _xdg_surface_handle_resource_destroy);

  VT_TRACE(surf->comp->log, "xdg_wm_base.get_xdg_surface with resource %p.",
           res);
}

void _xdg_wm_base_pong(struct wl_client *client, struct wl_resource *resource,
                       uint32_t serial) {}

void _xdg_positioner_destroy(struct wl_client   *client,
                             struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void _xdg_positioner_set_size(struct wl_client   *client,
                              struct wl_resource *resource, int32_t width,
                              int32_t height) {
  /* 1. Retrieve internal positioner handle */
  struct vt_xdg_positioner_t *pos =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* according to spec, both must be > 0 */
  if (width <= 0 || height <= 0) {
    wl_resource_post_error(
        resource, XDG_POSITIONER_ERROR_INVALID_INPUT,
        "width and height must be greater than zero (got %i×%i)", width,
        height);

    VT_WARN(pos->comp->log,
            "xdg_positioner.set_size: Trying to set invalid width (%i) or "
            "height (%i) for positioner %p.",
            width, height, pos);
    return;
  }

  /* 2. Update to reflect requested positioner data in internal handle */
  pos->width = width;
  pos->height = height;

  VT_TRACE(_proto.comp->log,
           "xdg_positioner.set_size: Size %ix%i for positioner %p.", width,
           height, pos);
}

void _xdg_positioner_set_anchor_rect(struct wl_client   *client,
                                     struct wl_resource *resource, int32_t x,
                                     int32_t y, int32_t width, int32_t height) {
  /* 1. Retrieve internal positioner handle */
  struct vt_xdg_positioner_t *pos =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Validate input parameters
   * According to spec, width and height must be positive. */
  if (width < 0 || height < 0) {
    wl_resource_post_error(
        resource, XDG_POSITIONER_ERROR_INVALID_INPUT,
        "width and height must be greater than zero (got %i×%i)", width,
        height);

    VT_WARN(pos->comp->log,
            "xdg_positioner.set_anchor: Trying to set invalid width (%i) or "
            "height (%i) for positioner %p.",
            width, height, pos);
    return;
  }

  /* 3. Update to reflect requested positioner data in internal handle */
  pos->anchor_rect_pos.x = x;
  pos->anchor_rect_pos.y = y;
  pos->anchor_rect_size.width = width;
  pos->anchor_rect_size.height = height;

  VT_TRACE(pos->comp->log,
           "xdg_positioner.set_anchor_rect: Pos %ix%i, size %ix%i for "
           "positioner %p.",
           x, y, width, height, pos);
}

void _xdg_positioner_set_anchor(struct wl_client   *client,
                                struct wl_resource *resource, uint32_t anchor) {
  /* 1. Retrieve internal positioner handle */
  struct vt_xdg_positioner_t *pos =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Validate input parameters */
  if (!xdg_positioner_anchor_is_valid(anchor,
                                      wl_resource_get_version(resource))) {
    wl_resource_post_error(resource, XDG_POSITIONER_ERROR_INVALID_INPUT,
                           "invalid anchor requested (got %i)", anchor);

    VT_WARN(pos->comp->log,
            "xdg_positioner.set_anchor: Trying to set invalid anchor %i for "
            "positioner %p.",
            anchor, pos);

    return;
  }

  /* 3. Update to reflect requested positioner data in internal handle */
  pos->anchor = anchor;

  VT_TRACE(pos->comp->log,
           "xdg_positioner.set_anchor: Set anchor of positioner %p to %i.", pos,
           anchor);
}

void _xdg_positioner_set_gravity(struct wl_client   *client,
                                 struct wl_resource *resource,
                                 uint32_t            gravity) {
  /* 1. Retrieve internal positioner handle */
  struct vt_xdg_positioner_t *pos =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Validate input parameters */
  if (!xdg_positioner_gravity_is_valid(gravity,
                                       wl_resource_get_version(resource))) {
    wl_resource_post_error(resource, XDG_POSITIONER_ERROR_INVALID_INPUT,
                           "invalid gravity requested (got %i)", gravity);

    VT_WARN(pos->comp->log,
            "xdg_positioner.set_gravity: Trying to set invalid gravity %i for "
            "positioner %p.",
            gravity, pos);

    return;
  }

  /* 3. Update to reflect requested positioner data in internal handle */
  pos->gravity = gravity;

  VT_TRACE(pos->comp->log,
           "xdg_positioner.set_gravity: Set gravity of positioner %p to %i.",
           pos, gravity);
}

void _xdg_positioner_set_constraint_adjustment(struct wl_client   *client,
                                               struct wl_resource *resource,
                                               uint32_t constraint_adjustment) {
  /* 1. Retrieve internal positioner handle */
  struct vt_xdg_positioner_t *pos =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Validate input parameters */
  if (!xdg_positioner_constraint_adjustment_is_valid(
          constraint_adjustment, wl_resource_get_version(resource))) {
    wl_resource_post_error(resource, XDG_POSITIONER_ERROR_INVALID_INPUT,
                           "invalid constraint adjustment requested (got %i)",
                           constraint_adjustment);

    VT_WARN(pos->comp->log,
            "xdg_positioner.set_constraint_adjustment: Trying to set invalid "
            "constraint adjustment %i for positioner %p.",
            constraint_adjustment, pos);

    return;
  }

  /* 3. Update to reflect requested positioner data in internal handle */
  pos->constraint_adjustment = constraint_adjustment;

  VT_TRACE(pos->comp->log,
           "xdg_positioner.set_constraint_adjustment: Set constraint "
           "adjustment of positioner %p to %i.",
           pos, constraint_adjustment);
}

void _xdg_positioner_set_offset(struct wl_client   *client,
                                struct wl_resource *resource, int32_t x,
                                int32_t y) {
  /* 1. Retrieve internal positioner handle */
  struct vt_xdg_positioner_t *pos =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Update to reflect requested positioner data in internal handle */
  pos->offset_x = x;
  pos->offset_y = y;

  VT_TRACE(pos->comp->log,
           "xdg_positioner.set_offset: Set offset of positioner %p to %ix%i.",
           pos, x, y);
}

void _xdg_surface_destroy(struct wl_client   *client,
                          struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void send_initial_configure(struct vt_xdg_surface_t *surf) {
  if (!surf || !surf->toplevel) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }
  /* Send empty state request to trigger initial configure. */
  _xdg_toplevel_send_state(surf->toplevel, 0, false);
}

void _xdg_surface_get_toplevel(struct wl_client   *client,
                               struct wl_resource *resource, uint32_t id) {

  /* 1. Retrieve internal XDG-Surface handle from the resource
   * the request came from. */
  struct vt_xdg_surface_t *xdg_surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!xdg_surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. According to spec, XDG-Surfaces can only ever be
   * assigned one role.*/
  if (xdg_surf->toplevel || xdg_surf->popup) {
    wl_resource_post_error(resource, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                           "xdg_surface already has a role");

    VT_WARN(_proto.comp->log, "XDG surface %p already has another role.",
            xdg_surf)
    return;
  }

  /* 3. Allocate resource for the XDG-Toplevel interface */
  struct wl_resource *res = wl_resource_create(
      client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);

  if (!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  /* 4. Allcoate internal toplevel handle and assign pointers. */
  xdg_surf->toplevel = calloc(1, sizeof(*xdg_surf->toplevel));
  if (!xdg_surf->toplevel) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  xdg_surf->toplevel->xdg_surf = xdg_surf;
  xdg_surf->toplevel->xdg_toplevel_res = res;
  xdg_surf->toplevel->parent = NULL;
  wl_list_init(&xdg_surf->toplevel->childs);
  wl_list_init(&xdg_surf->toplevel->link);

  /* 5. Set handler functions via the implementation */
  wl_resource_set_implementation(res, &xdg_toplevel_impl, xdg_surf->toplevel,
                                 _xdg_toplevel_handle_resource_destroy);

  /* 6. send an initial configure */
  send_initial_configure(xdg_surf);
}

void _xdg_surface_get_popup(struct wl_client   *client,
                            struct wl_resource *resource, uint32_t id,
                            struct wl_resource *parent_surface,
                            struct wl_resource *positioner) {

  /* 1. Retrieve internal XDG-Surface handle for both parent toplevel and popup
   * from the resources the request came from. */
  struct vt_xdg_surface_t *popup_xdg_surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  struct vt_xdg_surface_t *parent_xdg_surf =
      parent_surface ? wl_resource_get_user_data(parent_surface) : NULL;

  if (!popup_xdg_surf) {
    return;
  }

  if (!parent_xdg_surf) {
    wl_resource_post_error(resource, XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE,
                           "parentless xdg_popup is not supported");
    return;
  }

  /* 2. According to spec, XDG-Surfaces can only ever be
   * assigned one role.*/
  if (popup_xdg_surf->toplevel || popup_xdg_surf->popup) {
    wl_resource_post_error(resource, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                           "xdg_surface already has a role");

    VT_WARN(_proto.comp->log, "XDG surface %p already has another role.",
            popup_xdg_surf)

    return;
  }

  /* 3. Allocate resource for the XDG-Popup interface */
  struct wl_resource *res = wl_resource_create(
      client, &xdg_popup_interface, wl_resource_get_version(resource), id);

  if (!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  /* 4. Allcoate internal popup handle and assign pointers. */
  struct vt_xdg_popup_t *popup = calloc(1, sizeof(*popup));
  if (!popup) {
    wl_resource_destroy(res);
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  popup->xdg_popup_res = res;
  popup->parent_xdg_surf = parent_xdg_surf;
  popup->xdg_surf = popup_xdg_surf;

  popup_xdg_surf->popup = popup;

  /* 5. Set handler functions via the implementation */
  wl_resource_set_implementation(res, &xdg_popup_impl, popup_xdg_surf->popup,
                                 _xdg_popup_handle_resource_destroy);

  /* 6. Send popup configure with positioner data*/
  struct vt_xdg_positioner_t *pos =
      positioner ? wl_resource_get_user_data(positioner) : NULL;

  if (!pos)
    return;

  struct vt_xdg_window_geom_t geom;

  if (!_popup_resolve_pos(popup, pos, &geom)) {
    VT_ERROR(popup_xdg_surf->surf->comp->log,
             "Failed to resolve initial popup geometry.");
    return;
  }

  uint32_t serial = wl_display_next_serial(popup_xdg_surf->surf->comp->wl.dsp);

  popup->pending_ack_geom = geom;
  popup->pending_ack_serial = serial;
  popup->have_pending_ack_geom = true;

  xdg_popup_send_configure(res, geom.x, geom.y, geom.w, geom.h);

  /* 7. Send the corresponding xdg_surface.configure with a fresh serial
   * to the xdg surface associated with the popup*/
  xdg_surface_send_configure(popup_xdg_surf->xdg_surf_res, serial);

  popup_xdg_surf->last_configure_serial = serial;

  struct vt_surface_t *popup_surf = popup_xdg_surf->surf;
  if (popup_surf)
    vt_scene_node_reparent(popup_surf->comp, popup_surf->scene_node,
                           popup->parent_xdg_surf->geom_node);
}

void _xdg_surface_ack_configure(struct wl_client   *client,
                                struct wl_resource *resource, uint32_t serial) {
  struct vt_xdg_surface_t *xdg = wl_resource_get_user_data(resource);

  if (!xdg)
    return;
  /* Set last known configure serial */
  xdg->last_configure_serial = serial;

  if (xdg->popup) {
    struct vt_xdg_popup_t *popup = xdg->popup;

    if (popup->have_pending_ack_geom && serial == popup->pending_ack_serial) {

      popup->acked_geom = popup->pending_ack_geom;

      popup->have_pending_ack_geom = false;
      popup->have_acked_geom = true;
    }
  }
}

void _xdg_surface_set_window_geometry(struct wl_client   *client,
                                      struct wl_resource *resource, int32_t x,
                                      int32_t y, int32_t width,
                                      int32_t height) {
  /* 1. Retrieve internal XDG-Surface handle */
  struct vt_xdg_surface_t *xdg_surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!xdg_surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Validate input parameters.
   * According to the spec, both width and height must be > 0 */
  if (width <= 0 || height <= 0) {
    wl_resource_post_error(
        resource, XDG_SURFACE_ERROR_INVALID_SIZE,
        "width and height must be greater than zero (got %i×%i)", width,
        height);

    VT_WARN(xdg_surf->surf->comp->log,
            "xdg_positioner.set_size: Trying to set invalid width (%i) or "
            "height (%i) for XDG surface %p.",
            width, height, xdg_surf);
    return;
  }

  /* 3. Send window geometry to requested geometry */
  xdg_surf->have_pending_geom = true;
  xdg_surf->pending_geom.x = x;
  xdg_surf->pending_geom.y = y;
  xdg_surf->pending_geom.w = (uint32_t)width;
  xdg_surf->pending_geom.h = (uint32_t)height;

  VT_TRACE(xdg_surf->surf->comp->log,
           "xdg_surface.set_window_geometry: Set window window geometry of "
           "surface %p to (%ix%i, %ix%i).",
           xdg_surf->surf, x, y, width, height);
}

void _xdg_toplevel_destroy(struct wl_client   *client,
                           struct wl_resource *resource) {
  wl_resource_destroy(resource);
}
void _xdg_toplevel_set_parent(struct wl_client   *client,
                              struct wl_resource *resource,
                              struct wl_resource *parent_resource) {
  /* 1. Retrieve internal XDG-Toplevel handle */
  struct vt_xdg_toplevel_t *xdg_toplevel =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!xdg_toplevel) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Retrieve parent's internal XDG-Toplevel handle */
  struct vt_xdg_toplevel_t *parent =
      parent_resource ? wl_resource_get_user_data(parent_resource) : NULL;

  if (parent == xdg_toplevel->parent)
    return;

  /* Remove the link from the old parent */
  if (xdg_toplevel->parent) {
    wl_list_remove(&xdg_toplevel->link);
    wl_list_init(&xdg_toplevel->link);
  }

  /* 3. Set parent of the XDG Toplevel making the request */
  xdg_toplevel->parent = parent;

  if (parent) {
    /* 4. Insert the request-making XDG Toplevel into the list of children of
     * the parent XDG Toplevel. */
    wl_list_insert(&parent->childs, &xdg_toplevel->link);
  }

  VT_TRACE(xdg_toplevel->xdg_surf->surf->comp->log,
           "xdg_toplevel.set_parent: Set parent of toplevel %p to %p.",
           xdg_toplevel, parent);
}

void _xdg_toplevel_set_title(struct wl_client   *client,
                             struct wl_resource *resource, const char *title) {
  /* Deallocate the old App Title and strdup() the requested ID */
  struct vt_xdg_toplevel_t *top = wl_resource_get_user_data(resource);
  if (top->title)
    free(top->title);
  top->title = strdup(title ? title : "");
}

void _xdg_toplevel_set_app_id(struct wl_client   *client,
                              struct wl_resource *resource,
                              const char         *app_id) {
  struct vt_xdg_toplevel_t *top = wl_resource_get_user_data(resource);
  /* Deallocate the old App ID and strdup() the requested ID */
  if (top->app_id)
    free(top->app_id);
  top->app_id = strdup(app_id ? app_id : "");
}

void _xdg_toplevel_show_window_menu(struct wl_client   *client,
                                    struct wl_resource *resource,
                                    struct wl_resource *seat, uint32_t serial,
                                    int32_t x, int32_t y) {}

void _xdg_toplevel_move(struct wl_client *client, struct wl_resource *resource,
                        struct wl_resource *seat, uint32_t serial) {}

void _xdg_toplevel_resize(struct wl_client   *client,
                          struct wl_resource *resource,
                          struct wl_resource *seat, uint32_t serial,
                          uint32_t edges) {}

void _xdg_toplevel_set_max_size(struct wl_client   *client,
                                struct wl_resource *resource, int32_t width,
                                int32_t height) {}

void _xdg_toplevel_set_min_size(struct wl_client   *client,
                                struct wl_resource *resource, int32_t width,
                                int32_t height) {}

void _xdg_toplevel_set_maximized(struct wl_client   *client,
                                 struct wl_resource *resource) {
  // optional: ignore
}

void _xdg_toplevel_unset_maximized(struct wl_client   *client,
                                   struct wl_resource *resource) {}

void _xdg_toplevel_set_fullscreen(struct wl_client   *client,
                                  struct wl_resource *resource,
                                  struct wl_resource *output) {}

void _xdg_toplevel_unset_fullscreen(struct wl_client   *client,
                                    struct wl_resource *resource) {}

void _xdg_toplevel_set_minimized(struct wl_client   *client,
                                 struct wl_resource *resource) {}

void _xdg_popup_destroy(struct wl_client   *client,
                        struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void _xdg_popup_grab(struct wl_client *client, struct wl_resource *resource,
                     struct wl_resource *seat, uint32_t serial) {
  /* 1. Retrieve internal XDG-Popup handle */
  struct vt_xdg_popup_t *popup =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!popup) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. According to spec, popups need to be grabbed before they get mapped */
  if (popup->mapped) {
    wl_resource_post_error(resource, XDG_POPUP_ERROR_INVALID_GRAB,
                           "xdg_popup.grab requested after popup was mapped");

    VT_WARN(popup->xdg_surf->surf->comp->log,
            "xdg_popup.grab: requested after popup was mapped.");

    return;
  }

  /* 3. Store grab-seat and gra-serial */
  popup->grab_seat = seat;
  popup->grab_serial = serial;
  popup->has_grab = true;

  /* 4. Set seat's keyboard to grabbed popup */
  vt_seat_set_keyboard_focus(wl_resource_get_user_data(seat),
                             popup->xdg_surf->surf);
}

static int _xdg_anchor_dir_x(uint32_t anchor) {
  switch (anchor) {
  case XDG_POSITIONER_ANCHOR_LEFT:
  case XDG_POSITIONER_ANCHOR_TOP_LEFT:
  case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
    return -1;

  case XDG_POSITIONER_ANCHOR_RIGHT:
  case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
  case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
    return 1;

  default:
    return 0;
  }
}

static int _xdg_anchor_dir_y(uint32_t anchor) {
  switch (anchor) {
  case XDG_POSITIONER_ANCHOR_TOP:
  case XDG_POSITIONER_ANCHOR_TOP_LEFT:
  case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
    return -1;

  case XDG_POSITIONER_ANCHOR_BOTTOM:
  case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
  case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
    return 1;

  default:
    return 0;
  }
}

static int _xdg_gravity_dir_x(uint32_t gravity) {
  switch (gravity) {
  case XDG_POSITIONER_GRAVITY_LEFT:
  case XDG_POSITIONER_GRAVITY_TOP_LEFT:
  case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
    return -1;

  case XDG_POSITIONER_GRAVITY_RIGHT:
  case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
  case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
    return 1;

  default:
    return 0;
  }
}

static int _xdg_gravity_dir_y(uint32_t gravity) {
  switch (gravity) {
  case XDG_POSITIONER_GRAVITY_TOP:
  case XDG_POSITIONER_GRAVITY_TOP_LEFT:
  case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
    return -1;

  case XDG_POSITIONER_GRAVITY_BOTTOM:
  case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
  case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT:
    return 1;

  default:
    return 0;
  }
}

static int64_t _xdg_positioner_axis(int64_t anchor_pos, int64_t anchor_size,
                                    int anchor_dir, int64_t popup_size,
                                    int gravity_dir, int64_t offset) {
  int64_t anchor_point;

  if (anchor_dir < 0)
    anchor_point = anchor_pos;
  else if (anchor_dir > 0)
    anchor_point = anchor_pos + anchor_size;
  else
    anchor_point = anchor_pos + anchor_size / 2;

  int64_t pos = anchor_point + offset;

  if (gravity_dir < 0)
    pos -= popup_size;
  else if (gravity_dir == 0)
    pos -= popup_size / 2;

  return pos;
}

static bool _xdg_axis_constrained(int64_t pos, int64_t size, int64_t bound_min,
                                  int64_t bound_max) {
  return pos < bound_min || pos + size > bound_max;
}

static void _xdg_slide_axis(int64_t *pos, int64_t size, int64_t bound_min,
                            int64_t bound_max) {
  int64_t available = bound_max - bound_min;

  if (size > available)
    return;

  if (*pos < bound_min)
    *pos = bound_min;

  if (*pos + size > bound_max)
    *pos = bound_max - size;
}

static bool _xdg_resize_axis(int64_t *pos, int64_t *size, int64_t bound_min,
                             int64_t bound_max) {
  int64_t old_start = *pos;
  int64_t old_end = *pos + *size;

  int64_t new_start = old_start < bound_min ? bound_min : old_start;

  int64_t new_end = old_end > bound_max ? bound_max : old_end;

  if (new_end <= new_start)
    return false;

  *pos = new_start;
  *size = new_end - new_start;

  return true;
}

static bool _xdg_positioner_calculate_geometry(
    const struct vt_xdg_positioner_t  *pos,
    const struct vt_xdg_window_geom_t *constraint,
    struct vt_xdg_window_geom_t       *out) {
  if (!pos || !out)
    return false;

  if (pos->width <= 0 || pos->height <= 0)
    return false;

  if (pos->anchor_rect_size.width <= 0 || pos->anchor_rect_size.height <= 0)
    return false;

  int anchor_x = _xdg_anchor_dir_x(pos->anchor);
  int anchor_y = _xdg_anchor_dir_y(pos->anchor);
  int gravity_x = _xdg_gravity_dir_x(pos->gravity);
  int gravity_y = _xdg_gravity_dir_y(pos->gravity);

  int64_t width = pos->width;
  int64_t height = pos->height;

  /* First, calculate completely unconstrained geometry. */

  int64_t x =
      _xdg_positioner_axis(pos->anchor_rect_pos.x, pos->anchor_rect_size.width,
                           anchor_x, width, gravity_x, pos->offset_x);

  int64_t y =
      _xdg_positioner_axis(pos->anchor_rect_pos.y, pos->anchor_rect_size.height,
                           anchor_y, height, gravity_y, pos->offset_y);

  if (constraint) {
    if (constraint->w == 0 || constraint->h == 0)
      return false;

    int64_t left = constraint->x;
    int64_t top = constraint->y;
    int64_t right = left + constraint->w;
    int64_t bottom = top + constraint->h;

    uint32_t adjust = pos->constraint_adjustment;

    /* Flip */
    if ((adjust & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X) &&
        _xdg_axis_constrained(x, width, left, right)) {

      int flipped_anchor_x = -anchor_x;
      int flipped_gravity_x = -gravity_x;

      int64_t flipped_x = _xdg_positioner_axis(
          pos->anchor_rect_pos.x, pos->anchor_rect_size.width, flipped_anchor_x,
          width, flipped_gravity_x, pos->offset_x);

      if (!_xdg_axis_constrained(flipped_x, width, left, right)) {

        x = flipped_x;

        anchor_x = flipped_anchor_x;
        gravity_x = flipped_gravity_x;
      }
    }

    if ((adjust & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y) &&
        _xdg_axis_constrained(y, height, top, bottom)) {

      int flipped_anchor_y = -anchor_y;
      int flipped_gravity_y = -gravity_y;

      int64_t flipped_y = _xdg_positioner_axis(
          pos->anchor_rect_pos.y, pos->anchor_rect_size.height,
          flipped_anchor_y, height, flipped_gravity_y, pos->offset_y);

      if (!_xdg_axis_constrained(flipped_y, height, top, bottom)) {

        y = flipped_y;

        anchor_y = flipped_anchor_y;
        gravity_y = flipped_gravity_y;
      }
    }

    /* Slide */
    if ((adjust & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X) &&
        _xdg_axis_constrained(x, width, left, right)) {

      _xdg_slide_axis(&x, width, left, right);
    }

    if ((adjust & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y) &&
        _xdg_axis_constrained(y, height, top, bottom)) {

      _xdg_slide_axis(&y, height, top, bottom);
    }

    /* Resize */
    if ((adjust & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_X) &&
        _xdg_axis_constrained(x, width, left, right)) {

      if (!_xdg_resize_axis(&x, &width, left, right))
        return false;
    }

    if ((adjust & XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_RESIZE_Y) &&
        _xdg_axis_constrained(y, height, top, bottom)) {

      if (!_xdg_resize_axis(&y, &height, top, bottom))
        return false;
    }
  }

  if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX ||
      width <= 0 || width > INT32_MAX || height <= 0 || height > INT32_MAX)
    return false;

  out->x = (int32_t)x;
  out->y = (int32_t)y;
  out->w = (uint32_t)width;
  out->h = (uint32_t)height;

  return true;
}

static struct vt_output_t *
_xdg_popup_choose_output(struct vt_xdg_surface_t *parent) {
  struct vt_surface_t *surf = parent->surf;

  struct vt_output_t *output;

  wl_list_for_each(output, &surf->comp->outputs, link_global) {
    if (surf->_mask_outputs_visible_on & (1u << output->id))
      return output;
  }

  return NULL;
}

static bool _popup_resolve_pos(struct vt_xdg_popup_t       *popup,
                               struct vt_xdg_positioner_t  *pos,
                               struct vt_xdg_window_geom_t *out_geom) {
  if (!popup || !pos || !out_geom || !popup->parent_xdg_surf ||
      !popup->parent_xdg_surf->geom_node)
    return false;

  struct vt_output_t *output = _xdg_popup_choose_output(popup->parent_xdg_surf);

  if (!output)
    return false;

  double parent_geom_gx, parent_geom_gy;

  vt_scene_node_get_global_position(popup->parent_xdg_surf->geom_node,
                                    &parent_geom_gx, &parent_geom_gy);

  struct vt_xdg_window_geom_t constraint = {
      .x = output->x - (int32_t)parent_geom_gx,
      .y = output->y - (int32_t)parent_geom_gy,
      .w = output->width,
      .h = output->height,
  };

  return _xdg_positioner_calculate_geometry(pos, &constraint, out_geom);
}

void _xdg_popup_reposition(struct wl_client   *client,
                           struct wl_resource *resource,
                           struct wl_resource *positioner, uint32_t token) {
  struct vt_xdg_popup_t *popup =
      resource ? wl_resource_get_user_data(resource) : NULL;

  if (!popup) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  struct vt_xdg_positioner_t *pos =
      positioner ? wl_resource_get_user_data(positioner) : NULL;

  if (!pos)
    return;

  struct vt_xdg_window_geom_t geom;

  if (!_popup_resolve_pos(popup, pos, &geom))
    return;

  struct vt_xdg_surface_t *xdg = popup->xdg_surf;
  struct vt_compositor_t  *comp = xdg->surf->comp;

  uint32_t serial = wl_display_next_serial(comp->wl.dsp);

  xdg_popup_send_repositioned(popup->xdg_popup_res, token);

  xdg_popup_send_configure(popup->xdg_popup_res, geom.x, geom.y, geom.w,
                           geom.h);

  xdg_surface_send_configure(xdg->xdg_surf_res, serial);

  xdg->last_configure_serial = serial;

  popup->pending_ack_geom = geom;
  popup->pending_ack_serial = serial;
  popup->have_pending_ack_geom = true;

  VT_TRACE(comp->log,
           "xdg_popup.reposition: token=%u serial=%u geom=(%d,%d %ux%u)", token,
           serial, geom.x, geom.y, geom.w, geom.h);
}

bool _xdg_toplevel_send_state(struct vt_xdg_toplevel_t *top, uint32_t state,
                              bool activated) {
  /* [0]: The function returns whether or not the state has been sent
   * successfully */
  /* The 'state' parameter is a XDG_TOPLEVEL_STATE_* value. */
  if (!top || !top->xdg_surf || !top->xdg_surf->surf ||
      !top->xdg_toplevel_res) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return false;
  }

  struct wl_client *client = wl_resource_get_client(top->xdg_toplevel_res);
  if (!client)
    return false;
  struct wl_display *dsp = wl_client_get_display(client);

  uint32_t serial = wl_display_next_serial(dsp);

  /* 1. Populate the states array with the single given state */
  struct wl_array states;
  wl_array_init(&states);

  /* 2. If the requested state should be activated, add it
   * to the array of states. In the case of deactivation,
   * the array stays empty which results in the requested
   * state being cleared (removed).
   * */
  if (activated) {
    uint32_t *state_elem = wl_array_add(&states, sizeof(*state_elem));
    if (!state_elem) {
      wl_array_release(&states);
      VT_WL_OUT_OF_MEMORY(top->xdg_surf->surf->comp, client);
      return false;
    }
    *state_elem = state;
  }

  /* 3. Issue the configure request with the changed state
   * (width, height: 0, 0 -> unchanged)*/
  xdg_toplevel_send_configure(top->xdg_toplevel_res, 0, 0, &states);

  /* Deallocate the states array */
  wl_array_release(&states);

  if (!top->xdg_surf->xdg_surf_res) {
    VT_ERROR(top->xdg_surf->surf->comp->log,
             "_proto_xdg_toplevel_send_state: Toplevel %p has no associated "
             "XDG surface resource.",
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
bool vt_proto_xdg_shell_init(struct vt_compositor_t *c, uint32_t version) {
  if (!(_proto.xdg_wm_base = wl_global_create(c->wl.dsp, &xdg_wm_base_interface,
                                              version, c, _xdg_wm_base_bind))) {
    VT_ERROR(c->log, "Cannot implement XDG base interface.");
    return false;
  }

  _proto.comp = c;

  VT_TRACE(c->log, "Initialized XDG shell protocol.");
  return true;
}

bool vt_proto_xdg_toplevel_set_state_maximized(struct vt_xdg_toplevel_t *top,
                                               bool activated) {
  return _xdg_toplevel_send_state(top, XDG_TOPLEVEL_STATE_MAXIMIZED, activated);
}

bool vt_proto_xdg_toplevel_set_state_fullscreen(struct vt_xdg_toplevel_t *top,
                                                bool activated) {
  return _xdg_toplevel_send_state(top, XDG_TOPLEVEL_STATE_FULLSCREEN,
                                  activated);
}

bool vt_proto_xdg_toplevel_set_state_resizing(struct vt_xdg_toplevel_t *top,
                                              bool activated) {
  return _xdg_toplevel_send_state(top, XDG_TOPLEVEL_STATE_RESIZING, activated);
}

bool vt_proto_xdg_toplevel_set_state_activated(struct vt_xdg_toplevel_t *top,
                                               bool activated) {
  return _xdg_toplevel_send_state(top, XDG_TOPLEVEL_STATE_ACTIVATED, activated);
}
