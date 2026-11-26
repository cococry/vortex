/*
 * Copyright (c) 2026 Luca Machiedo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _GNU_SOURCE

#include "xdg_shell.h"
#include "src/core/profiling.h"
#include "src/core/util.h"
#include "wl_subcompositor.h"

#include "src/core/content_update.h"
#include "src/core/core_types.h"
#include "src/core/scene.h"
#include "src/core/surface.h"
#include "xdg-shell-protocol.h"

#include <string.h>
#include <wayland-server-core.h>

#define _SUBSYS_NAME "VT_PROTO_XDG_SHELL"

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

  bool     reactive, have_anchor_rect;
  int32_t  parent_width, parent_height;
  uint32_t parent_configure;

  struct vt_compositor_t *comp;
};

struct vt_xdg_configure_t {
  struct wl_list                  link;
  uint32_t                        serial;
  struct vt_xdg_configure_state_t state;
};

struct vt_proto_xdg_shell_t {
  uint64_t                next_generation;
  struct wl_global       *xdg_wm_base;
  struct wl_list          surfaces;
  struct vt_compositor_t *comp;
};

static void _xdg_wm_base_bind(struct wl_client *client, void *data,
                              uint32_t version, uint32_t id);

static void _xdg_wm_base_destroy(struct wl_client   *client,
                                 struct wl_resource *resource);

static void _xdg_wm_base_create_positioner(struct wl_client   *client,
                                           struct wl_resource *resource,
                                           uint32_t            id);

static void
_xdg_wm_base_positioner_handle_resource_destroy(struct wl_resource *resource);

static void _xdg_toplevel_handle_resource_destroy(struct wl_resource *resource);

static void _xdg_surface_handle_resource_destroy(struct wl_resource *resource);

static void _xdg_popup_handle_resource_destroy(struct wl_resource *resource);

static bool _xdg_surface_commit(struct vt_surface_t        *surf,
                                struct vt_content_update_t *cu);

static void _xdg_toplevel_mapping_changed(struct vt_surface_t *surf,
                                          bool                 mapped);

static bool _xdg_toplevel_configure(struct vt_xdg_toplevel_t *top,
                                    int32_t width, int32_t height);

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
                                         struct wl_resource *output_res);

static void _xdg_toplevel_unset_fullscreen(struct wl_client   *client,
                                           struct wl_resource *resource);

static void _xdg_toplevel_set_minimized(struct wl_client   *client,
                                        struct wl_resource *resource);

static void _xdg_popup_destroy(struct wl_client   *client,
                               struct wl_resource *resource);

static void _xdg_popup_grab(struct wl_client   *client,
                            struct wl_resource *resource,
                            struct wl_resource *seat, uint32_t serial);

static int _xdg_anchor_dir_x(uint32_t anchor);

static int _xdg_anchor_dir_y(uint32_t anchor);

static int _xdg_gravity_dir_x(uint32_t gravity);

static int _xdg_gravity_dir_y(uint32_t gravity);

static int64_t _xdg_positioner_axis(int64_t anchor_pos, int64_t anchor_size,
                                    int anchor_dir, int64_t popup_size,
                                    int gravity_dir, int64_t offset);

static bool _xdg_axis_constrained(int64_t pos, int64_t size, int64_t bound_min,
                                  int64_t bound_max);

static void _xdg_slide_axis(int64_t *pos, int64_t size, int64_t bound_min,
                            int64_t bound_max);

static bool _xdg_resize_axis(int64_t *pos, int64_t *size, int64_t bound_min,
                             int64_t bound_max);

static bool _xdg_positioner_calculate_geometry(
    const struct vt_xdg_positioner_t  *pos,
    const struct vt_xdg_window_geom_t *constraint,
    struct vt_xdg_window_geom_t       *out);

static bool _popup_resolve_pos(struct vt_xdg_popup_t       *popup,
                               struct vt_xdg_positioner_t  *pos,
                               struct vt_xdg_window_geom_t *out_geom);

static void _xdg_popup_reposition(struct wl_client   *client,
                                  struct wl_resource *resource,
                                  struct wl_resource *positioner,
                                  uint32_t            token);

static void _xdg_configures_clear(struct vt_xdg_surface_t *xdg);

static struct vt_xdg_configure_t *
_xdg_configure_create(struct vt_xdg_surface_t *xdg);

static struct vt_output_t *_xdg_toplevel_output(struct vt_xdg_toplevel_t *top);

static void _xdg_toplevel_save_geometry(struct vt_xdg_toplevel_t *top);

static void _xdg_positioner_set_reactive(struct wl_client   *client,
                                         struct wl_resource *resource);

static void _xdg_positioner_set_parent_size(struct wl_client   *client,
                                            struct wl_resource *resource,
                                            int32_t width, int32_t height);

static void _xdg_positioner_set_parent_configure(struct wl_client   *client,
                                                 struct wl_resource *resource,
                                                 uint32_t            serial);

static bool _xdg_surface_validate_commit(struct vt_surface_t *surf);

static void _xdg_surface_extend_bounds(struct vt_surface_t *surf, int64_t x,
                                       int64_t y, int64_t bounds[4]);

static bool _xdg_surface_apply(struct vt_surface_t        *surf,
                               struct vt_content_update_t *cu);

static bool _xdg_popup_configure(struct vt_xdg_popup_t *popup, bool reposition,
                                 uint32_t token);

static void _xdg_surface_handle_surface_destroy(struct wl_listener *listener,
                                                void               *data);

static const struct xdg_positioner_interface xdg_positioner_impl = {
    .destroy = _xdg_positioner_destroy,
    .set_size = _xdg_positioner_set_size,
    .set_anchor_rect = _xdg_positioner_set_anchor_rect,
    .set_anchor = _xdg_positioner_set_anchor,
    .set_gravity = _xdg_positioner_set_gravity,
    .set_constraint_adjustment = _xdg_positioner_set_constraint_adjustment,
    .set_offset = _xdg_positioner_set_offset,
    .set_reactive = _xdg_positioner_set_reactive,
    .set_parent_size = _xdg_positioner_set_parent_size,
    .set_parent_configure = _xdg_positioner_set_parent_configure,
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

static const struct vt_surface_role_impl_t xdg_toplevel_role_impl = {
    .type = VT_SURFACE_ROLE_XDG_TOPLEVEL,
    .commit = _xdg_surface_commit,
    .validate_commit = _xdg_surface_validate_commit,
    .apply = _xdg_surface_apply,
    .mapping_changed = _xdg_toplevel_mapping_changed,
};

static const struct vt_surface_role_impl_t xdg_popup_role_impl = {
    .type = VT_SURFACE_ROLE_XDG_POPUP,
    .commit = _xdg_surface_commit,
    .validate_commit = _xdg_surface_validate_commit,
    .apply = _xdg_surface_apply,
    .mapping_changed = _xdg_toplevel_mapping_changed,
};

static struct vt_proto_xdg_shell_t _proto;

static void _xdg_wm_base_bind(struct wl_client *client, void *data,
                              uint32_t version, uint32_t id) {
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

static void _xdg_wm_base_destroy(struct wl_client   *client,
                                 struct wl_resource *resource) {
  (void)client;
  struct vt_xdg_surface_t *xdg;
  wl_list_for_each(xdg, &_proto.surfaces, link) {
    if (xdg->wm_base_res == resource) {
      wl_resource_post_error(resource, XDG_WM_BASE_ERROR_DEFUNCT_SURFACES,
                             "xdg surfaces still exist");
      return;
    }

  }

  /* we can drop the base once its surfaces are gone */
  wl_resource_destroy(resource);
}

static void _xdg_wm_base_create_positioner(struct wl_client   *client,
                                           struct wl_resource *resource,
                                           uint32_t            id) {
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
      (void *)res);
}

static void
_xdg_wm_base_positioner_handle_resource_destroy(struct wl_resource *resource) {
  struct vt_xdg_positioner_t *pos =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!pos) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(
      pos->comp->log,
      "xdg_positioner.resource_destroy: destroyed positioner with resource %p.",
      (void *)resource);

  /* 1. Unlink pointers within internal handle */
  pos->res = NULL;
  /* 2. Free Internal handle*/
  free(pos);
  /* 3. Clear resource user data */
  wl_resource_set_user_data(resource, NULL);
}

static void
_xdg_toplevel_handle_resource_destroy(struct wl_resource *resource) {
  struct vt_xdg_toplevel_t *top =
      resource ? wl_resource_get_user_data(resource) : NULL;

  if (!top) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  if (top->xdg_surf)
    top->xdg_surf->unmap_applied = false;

  _xdg_toplevel_mapping_changed(top->xdg_surf ? top->xdg_surf->surf : NULL,
                                false);
  free(top->title);
  free(top->app_id);
  top->xdg_toplevel_res = NULL;

  if (top->xdg_surf && top->xdg_surf->surf)
    vt_surface_set_mapped(top->xdg_surf->surf, false);

  if (top->xdg_surf) {
    _xdg_configures_clear(top->xdg_surf);
    top->xdg_surf->initial_configure_sent = false;
    top->xdg_surf->configured = false;
    top->xdg_surf->toplevel = NULL;
    top->xdg_surf->generation = 0;
  }

  if (top->parent) {
    if (!wl_list_empty(&top->link)) {
      wl_list_remove(&top->link);
      wl_list_init(&top->link);
    }

    top->parent = NULL;
  }

  wl_resource_set_user_data(resource, NULL);
  free(top);
}

static void _xdg_surface_handle_resource_destroy(struct wl_resource *resource) {
  struct vt_xdg_surface_t *xdg =
      resource ? wl_resource_get_user_data(resource) : NULL;

  if (!xdg) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  struct vt_surface_t *surf = xdg->surf;

  if (surf)
    vt_surface_set_mapped(surf, false);

  if (xdg->toplevel) {
    if (xdg->toplevel->xdg_surf == xdg)
      xdg->toplevel->xdg_surf = NULL;

    xdg->toplevel = NULL;
  }

  if (xdg->popup) {
    wl_list_remove(&xdg->popup->parent_link);
    wl_list_init(&xdg->popup->parent_link);
    if (xdg->popup->xdg_surf == xdg)
      xdg->popup->xdg_surf = NULL;

    xdg->popup = NULL;
  }

  struct vt_xdg_popup_t *child, *tmp;
  wl_list_for_each_safe(child, tmp, &xdg->popups, parent_link) {
    wl_list_remove(&child->parent_link);
    wl_list_init(&child->parent_link);
    child->parent_xdg_surf = NULL;
    xdg_popup_send_popup_done(child->xdg_popup_res);
  }

  _xdg_configures_clear(xdg);
  wl_list_remove(&xdg->link);
  wl_list_remove(&xdg->surface_destroy.link);
  if (surf)
    surf->role.data = NULL;

  xdg->surf = NULL;
  xdg->xdg_surf_res = NULL;

  wl_resource_set_user_data(resource, NULL);
  free(xdg);
}

static void _xdg_popup_handle_resource_destroy(struct wl_resource *resource) {
  struct vt_xdg_popup_t *popup =
      resource ? wl_resource_get_user_data(resource) : NULL;

  if (!popup) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  popup->xdg_popup_res = NULL;

  if (popup->xdg_surf && popup->xdg_surf->surf)
    vt_surface_set_mapped(popup->xdg_surf->surf, false);

  if (popup->xdg_surf) {
    _xdg_configures_clear(popup->xdg_surf);
    popup->xdg_surf->initial_configure_sent = false;
    popup->xdg_surf->configured = false;
    if (popup->xdg_surf->popup == popup)
      popup->xdg_surf->popup = NULL;

    popup->xdg_surf->generation = 0;
  }

  if (popup->child_popup)
    popup->child_popup->parent_popup = NULL;

  if (popup->parent_popup && popup->parent_popup->child_popup == popup)
    popup->parent_popup->child_popup = NULL;

  wl_list_remove(&popup->parent_link);
  free(popup->positioner);
  popup->parent_xdg_surf = NULL;

  wl_resource_set_user_data(resource, NULL);
  free(popup);
}

static bool _xdg_surface_commit(struct vt_surface_t        *surf,
                                struct vt_content_update_t *cu) {
  VT_PROFILE_SCOPE();
  struct vt_xdg_surface_t *xdg = surf->role.data;
  if (!xdg)
    return false;

  if (cu->state.buffer_attached && !cu->state.buf &&
      xdg->initial_configure_sent) {
    xdg->unmap_applied = false;
    _xdg_toplevel_mapping_changed(surf, false);
    cu->xdg.generation = xdg->generation;
    cu->xdg.unmap = true;
    return true;
  }

  cu->xdg.generation = xdg->generation;
  cu->xdg.have_geom = xdg->have_pending_geom;
  cu->xdg.geom = xdg->pending_geom;
  xdg->have_pending_geom = false;
  cu->xdg.have_configure = xdg->have_acked_configure;
  cu->xdg.configure = xdg->acked_configure;
  xdg->have_acked_configure = false;

  if (xdg->toplevel) {
    struct vt_xdg_toplevel_t *top = xdg->toplevel;
    top->min_width = top->pending_min_width;
    top->min_height = top->pending_min_height;
    top->max_width = top->pending_max_width;
    top->max_height = top->pending_max_height;
  }

  if (!xdg->initial_configure_sent) {
    if (xdg->toplevel) {
      if (wl_resource_get_version(xdg->toplevel->xdg_toplevel_res) >=
          XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
        uint32_t        values[] = {XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE,
                                    XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN};
        struct wl_array caps = {
            .size = sizeof(values), .alloc = sizeof(values), .data = values};
        xdg_toplevel_send_wm_capabilities(xdg->toplevel->xdg_toplevel_res,
                                          &caps);
      }

      xdg->initial_configure_sent = true;
      return _xdg_toplevel_configure(xdg->toplevel, 0, 0);
    }

    if (xdg->popup) {
      if (!_xdg_popup_configure(xdg->popup, false, 0))
        return false;

      xdg->initial_configure_sent = true;
    }

  }

  return true;
}

static void _xdg_toplevel_mapping_changed(struct vt_surface_t *surf,
                                          bool                 mapped) {
  VT_PROFILE_SCOPE();
  if (!surf)
    return;

  struct vt_xdg_surface_t *xdg = surf->role.data;
  if (!xdg)
    return;

  if (xdg->popup)
    xdg->popup->mapped = mapped;

  if (mapped || xdg->unmap_applied) {
    xdg->unmap_applied = false;
    return;
  }

  _xdg_configures_clear(xdg);
  xdg->initial_configure_sent = false;
  xdg->configured = false;
  xdg->have_pending_geom = false;
  xdg->have_window_geom = false;
  xdg->geom = (struct vt_xdg_window_geom_t){0};
  struct vt_xdg_popup_t *popup;
  wl_list_for_each(popup, &xdg->popups, parent_link) {
    if (popup->xdg_surf->surf)
      vt_surface_set_mapped(popup->xdg_surf->surf, false);

    xdg_popup_send_popup_done(popup->xdg_popup_res);
  }

  if (!xdg->toplevel)
    return;

  struct vt_xdg_toplevel_t *top = xdg->toplevel;
  struct vt_xdg_toplevel_t *new_parent = top->parent;

  struct vt_xdg_toplevel_t *child, *tmp;
  wl_list_for_each_safe(child, tmp, &top->childs, link) {
    wl_list_remove(&child->link);
    wl_list_init(&child->link);

    child->parent = new_parent;

    if (new_parent)
      wl_list_insert(&new_parent->childs, &child->link);
  }

  if (top->parent) {
    wl_list_remove(&top->link);
    wl_list_init(&top->link);
    top->parent = NULL;
  }

  top->fullscreen = false;
  top->maximized = false;
  top->activated = false;
  top->resizing = false;
  top->applied_fullscreen = false;
  top->applied_maximized = false;
  top->fullscreen_output = NULL;
  top->maximized_output = NULL;
  top->have_restore_geom = false;
  top->min_width = top->min_height = 0;
  top->max_width = top->max_height = 0;
  top->pending_min_width = top->pending_min_height = 0;
  top->pending_max_width = top->pending_max_height = 0;
  free(top->title);
  free(top->app_id);
  top->title = NULL;
  top->app_id = NULL;
}

static bool _xdg_toplevel_configure(struct vt_xdg_toplevel_t *top,
                                    int32_t width, int32_t height) {
  VT_PROFILE_SCOPE();
  if (!top || !top->xdg_surf || !top->xdg_surf->surf || !top->xdg_toplevel_res)
    return false;

  struct vt_xdg_surface_t *xdg = top->xdg_surf;
  if (!xdg->initial_configure_sent)
    return true;

  struct vt_output_t *output =
      top->fullscreen ? top->fullscreen_output : top->maximized_output;
  if (!output)
    output = _xdg_toplevel_output(top);

  struct vt_xdg_configure_t *cfg = _xdg_configure_create(xdg);
  if (!cfg)
    return false;

  cfg->state.fullscreen = top->fullscreen;
  cfg->state.maximized = top->maximized;
  if ((top->fullscreen || top->maximized) && output) {
    cfg->state.positioned = true;
    cfg->state.geom = (struct vt_xdg_window_geom_t){
        output->x, output->y, output->width, output->height};
    cfg->state.output_geom = cfg->state.geom;
    width = output->width;
    height = output->height;
  } else if (top->have_restore_geom) {
    cfg->state.positioned = true;
    cfg->state.geom = top->restore_geom;
    width = top->restore_geom.w;
    height = top->restore_geom.h;
  }

  if (!top->fullscreen && !top->maximized) {
    if (width && top->max_width)
      width = VT_MIN(width, top->max_width);

    if (height && top->max_height)
      height = VT_MIN(height, top->max_height);

    if (width)
      width = VT_MAX(width, top->min_width);

    if (height)
      height = VT_MAX(height, top->min_height);
  }

  uint32_t values[8];
  size_t   count = 0;
  if (top->activated)
    values[count++] = XDG_TOPLEVEL_STATE_ACTIVATED;

  if (top->fullscreen)
    values[count++] = XDG_TOPLEVEL_STATE_FULLSCREEN;

  if (top->maximized)
    values[count++] = XDG_TOPLEVEL_STATE_MAXIMIZED;

  if (top->resizing)
    values[count++] = XDG_TOPLEVEL_STATE_RESIZING;

  int version = wl_resource_get_version(top->xdg_toplevel_res);
  if ((top->fullscreen || top->maximized) && version >= 7) {
    values[count++] = XDG_TOPLEVEL_STATE_CONSTRAINED_LEFT;
    values[count++] = XDG_TOPLEVEL_STATE_CONSTRAINED_RIGHT;
    values[count++] = XDG_TOPLEVEL_STATE_CONSTRAINED_TOP;
    values[count++] = XDG_TOPLEVEL_STATE_CONSTRAINED_BOTTOM;
  }

  struct wl_array states = {
      .size = count * sizeof(*values), .alloc = sizeof(values), .data = values};
  if (version >= XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION)
    xdg_toplevel_send_configure_bounds(top->xdg_toplevel_res,
                                       output ? output->width : 0,
                                       output ? output->height : 0);

  xdg_toplevel_send_configure(top->xdg_toplevel_res, width, height, &states);
  xdg_surface_send_configure(xdg->xdg_surf_res, cfg->serial);
  return true;
}

static void _xdg_wm_base_get_xdg_surface(struct wl_client   *client,
                                         struct wl_resource *resource,
                                         uint32_t            id,
                                         struct wl_resource *surface_res) {
  struct vt_surface_t *surf =
      surface_res ? wl_resource_get_user_data(surface_res) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  struct vt_xdg_surface_t *existing;
  wl_list_for_each(existing, &_proto.surfaces, link) {
    if (existing->surf == surf) {
      wl_resource_post_error(resource, XDG_WM_BASE_ERROR_ROLE,
                             "surface already has an xdg surface");
      return;
    }

  }

  if ((surf->role.impl &&
       surf->role.impl->type != VT_SURFACE_ROLE_XDG_TOPLEVEL &&
       surf->role.impl->type != VT_SURFACE_ROLE_XDG_POPUP) ||
      vt_surface_get_buffer(surf) || surf->pending.buf) {
    wl_resource_post_error(resource, XDG_WM_BASE_ERROR_ROLE,
                           "surface already has a role or buffer");
    return;
  }

  /* we set up the base before assigning its role */
  struct wl_resource *res = wl_resource_create(
      client, &xdg_surface_interface, wl_resource_get_version(resource), id);

  if (!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  /* 2. Allcoate internal surface handle and assign pointers. */
  struct vt_xdg_surface_t *xdg_surf = calloc(1, sizeof(*xdg_surf));
  if (!xdg_surf) {
    wl_resource_destroy(res);
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  xdg_surf->geom_node = vt_scene_node_create_container(surf->comp);

  xdg_surf->subsurface_layer = vt_scene_node_create_container(surf->comp);

  xdg_surf->popup_layer = vt_scene_node_create_container(surf->comp);

  vt_scene_node_add_child(surf->comp, surf->scene_node,
                          xdg_surf->subsurface_layer);

  vt_scene_node_add_child(surf->comp, surf->scene_node, xdg_surf->geom_node);

  vt_scene_node_add_child(surf->comp, xdg_surf->geom_node,
                          xdg_surf->popup_layer);

  wl_list_init(&xdg_surf->configures);
  wl_list_init(&xdg_surf->popups);
  wl_list_insert(_proto.surfaces.prev, &xdg_surf->link);
  xdg_surf->wm_base_res = resource;
  xdg_surf->surface_destroy.notify = _xdg_surface_handle_surface_destroy;
  wl_resource_add_destroy_listener(surf->res, &xdg_surf->surface_destroy);
  xdg_surf->surf = surf;
  xdg_surf->xdg_surf_res = res;

  /* 3. Set handler functions via the implementation */
  wl_resource_set_implementation(res, &xdg_surface_impl, xdg_surf,
                                 _xdg_surface_handle_resource_destroy);

  VT_TRACE(surf->comp->log, "xdg_wm_base.get_xdg_surface with resource %p.",
           (void *)res);
}

static void _xdg_wm_base_pong(struct wl_client   *client,
                              struct wl_resource *resource, uint32_t serial) {
  (void)client;
  (void)resource;
  (void)serial;
}

static void _xdg_positioner_destroy(struct wl_client   *client,
                                    struct wl_resource *resource) {
  (void)client;
  wl_resource_destroy(resource);
}

static void _xdg_positioner_set_size(struct wl_client   *client,
                                     struct wl_resource *resource,
                                     int32_t width, int32_t height) {
  VT_PROFILE_SCOPE();
  (void)client;
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
            width, height, (void *)pos);
    return;
  }

  /* 2. Update to reflect requested positioner data in internal handle */
  pos->width = width;
  pos->height = height;

  VT_TRACE(_proto.comp->log,
           "xdg_positioner.set_size: Size %ix%i for positioner %p.", width,
           height, (void *)pos);
}

static void _xdg_positioner_set_anchor_rect(struct wl_client   *client,
                                            struct wl_resource *resource,
                                            int32_t x, int32_t y, int32_t width,
                                            int32_t height) {
  VT_PROFILE_SCOPE();
  (void)client;
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
            width, height, (void *)pos);
    return;
  }

  /* 3. Update to reflect requested positioner data in internal handle */
  pos->have_anchor_rect = true;
  pos->anchor_rect_pos.x = x;
  pos->anchor_rect_pos.y = y;
  pos->anchor_rect_size.width = width;
  pos->anchor_rect_size.height = height;

  VT_TRACE(pos->comp->log,
           "xdg_positioner.set_anchor_rect: Pos %ix%i, size %ix%i for "
           "positioner %p.",
           x, y, width, height, (void *)pos);
}

static void _xdg_positioner_set_anchor(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t            anchor) {
  VT_PROFILE_SCOPE();
  (void)client;
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
            anchor, (void *)pos);

    return;
  }

  /* 3. Update to reflect requested positioner data in internal handle */
  pos->anchor = anchor;

  VT_TRACE(pos->comp->log,
           "xdg_positioner.set_anchor: Set anchor of positioner %p to %i.",
           (void *)pos, anchor);
}

static void _xdg_positioner_set_gravity(struct wl_client   *client,
                                        struct wl_resource *resource,
                                        uint32_t            gravity) {
  VT_PROFILE_SCOPE();
  (void)client;
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
            gravity, (void *)pos);

    return;
  }

  /* 3. Update to reflect requested positioner data in internal handle */
  pos->gravity = gravity;

  VT_TRACE(pos->comp->log,
           "xdg_positioner.set_gravity: Set gravity of positioner %p to %i.",
           (void *)pos, gravity);
}

static void
_xdg_positioner_set_constraint_adjustment(struct wl_client   *client,
                                          struct wl_resource *resource,
                                          uint32_t constraint_adjustment) {
  VT_PROFILE_SCOPE();
  (void)client;
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
            constraint_adjustment, (void *)pos);

    return;
  }

  /* 3. Update to reflect requested positioner data in internal handle */
  pos->constraint_adjustment = constraint_adjustment;

  VT_TRACE(pos->comp->log,
           "xdg_positioner.set_constraint_adjustment: Set constraint "
           "adjustment of positioner %p to %i.",
           (void *)pos, constraint_adjustment);
}

static void _xdg_positioner_set_offset(struct wl_client   *client,
                                       struct wl_resource *resource, int32_t x,
                                       int32_t y) {
  VT_PROFILE_SCOPE();
  (void)client;
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
           (void *)pos, x, y);
}

static void _xdg_surface_destroy(struct wl_client   *client,
                                 struct wl_resource *resource) {
  (void)client;
  struct vt_xdg_surface_t *xdg = wl_resource_get_user_data(resource);
  if (xdg->toplevel || xdg->popup) {
    wl_resource_post_error(resource, XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                           "role object still exists");
    return;
  }

  wl_resource_destroy(resource);
}

static void _xdg_surface_get_toplevel(struct wl_client   *client,
                                      struct wl_resource *resource,
                                      uint32_t            id) {

  /* 1. Retrieve internal XDG-Surface handle from the resource
   * the request came from. */
  struct vt_xdg_surface_t *xdg_surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!xdg_surf || !xdg_surf->surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. According to spec, XDG-Surfaces can only ever be
   * assigned one role.*/
  if (xdg_surf->toplevel || xdg_surf->popup) {
    wl_resource_post_error(resource, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                           "xdg_surface already has a role");

    VT_WARN(_proto.comp->log, "XDG surface %p already has another role.",
            (void *)xdg_surf);
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
  struct vt_xdg_toplevel_t *toplevel = calloc(1, sizeof(*toplevel));

  if (!toplevel) {
    wl_resource_destroy(res);
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  toplevel->xdg_surf = xdg_surf;
  toplevel->xdg_toplevel_res = res;
  toplevel->parent = NULL;
  wl_list_init(&toplevel->childs);
  wl_list_init(&toplevel->link);

  /*
   * xdg_surface.get_toplevel assigns the associated wl_surface
   * its permanent xdg_toplevel role.
   */
  if (!vt_surface_set_role(xdg_surf->surf, &xdg_toplevel_role_impl, xdg_surf)) {
    /* protocol error */
    wl_resource_post_error(resource, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                           "cannot set xdg_toplevel surface role");

    wl_resource_destroy(res);
    free(toplevel);
    return;
  }

  _proto.next_generation++;
  xdg_surf->generation = _proto.next_generation;
  xdg_surf->toplevel = toplevel;

  /* 5. Set handler functions via the implementation */
  wl_resource_set_implementation(res, &xdg_toplevel_impl, xdg_surf->toplevel,
                                 _xdg_toplevel_handle_resource_destroy);
}

static void _xdg_surface_get_popup(struct wl_client   *client,
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

  if (parent_xdg_surf &&
      (parent_xdg_surf == popup_xdg_surf ||
       (!parent_xdg_surf->toplevel && !parent_xdg_surf->popup))) {
    wl_resource_post_error(popup_xdg_surf->wm_base_res,
                           XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT,
                           "invalid popup parent");
    return;
  }

  /* we only give each surface one role object */
  if (popup_xdg_surf->toplevel || popup_xdg_surf->popup) {
    wl_resource_post_error(resource, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                           "xdg_surface already has a role");

    VT_WARN(_proto.comp->log, "XDG surface %p already has another role.",
            (void *)popup_xdg_surf);

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

  wl_list_init(&popup->parent_link);
  popup->xdg_popup_res = res;
  popup->parent_xdg_surf = parent_xdg_surf;
  popup->xdg_surf = popup_xdg_surf;

  /* 5. Set handler functions via the implementation */

  /* 6. Send popup configure with positioner data*/
  struct vt_xdg_positioner_t *pos =
      positioner ? wl_resource_get_user_data(positioner) : NULL;

  if (!pos) {
    wl_resource_destroy(res);
    free(popup);
    return;
  }

  struct vt_xdg_window_geom_t geom;

  if (!_xdg_positioner_calculate_geometry(pos, NULL, &geom)) {
    wl_resource_post_error(popup_xdg_surf->wm_base_res,
                           XDG_WM_BASE_ERROR_INVALID_POSITIONER,
                           "positioner is incomplete or invalid");

    wl_resource_destroy(res);
    free(popup);
    return;
  }

  popup->positioner = malloc(sizeof(*pos));
  if (!popup->positioner) {
    wl_resource_destroy(res);
    free(popup);
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  *popup->positioner = *pos;
  popup->reactive = pos->reactive;

  /*
   * xdg_surface.get_popup assigns the associated wl_surface
   * its permanent xdg_popup role.
   */
  if (!vt_surface_set_role(popup_xdg_surf->surf, &xdg_popup_role_impl,
                           popup_xdg_surf)) {
    /* protocol error */
    wl_resource_post_error(resource, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED,
                           "cannot set xdg_popup surface role");

    wl_resource_destroy(res);
    free(popup->positioner);
    free(popup);
    return;
  }

  /*
   * Publish the popup only after all fallible setup and role assignment
   * have succeeded.
   */
  _proto.next_generation++;
  popup_xdg_surf->generation = _proto.next_generation;
  popup_xdg_surf->popup = popup;

  wl_resource_set_implementation(res, &xdg_popup_impl, popup_xdg_surf->popup,
                                 _xdg_popup_handle_resource_destroy);

  if (parent_xdg_surf)
    wl_list_insert(parent_xdg_surf->popups.prev, &popup->parent_link);

  if (!popup_xdg_surf->surf || !popup->parent_xdg_surf)
    return;

  struct vt_scene_node_t *parent_node = popup->parent_xdg_surf->geom_node;

  if (popup->parent_xdg_surf && popup->parent_xdg_surf->popup_layer) {
    parent_node = popup->parent_xdg_surf->popup_layer;
  }

  vt_scene_node_reparent(popup_xdg_surf->surf->comp,
                         popup_xdg_surf->surf->scene_node, parent_node);
}

static void _xdg_surface_ack_configure(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t            serial) {
  (void)client;
  struct vt_xdg_surface_t *xdg = wl_resource_get_user_data(resource);
  if (!xdg->toplevel && !xdg->popup) {
    wl_resource_post_error(resource, XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                           "surface has no role object");
    return;
  }

  struct vt_xdg_configure_t *cfg, *found = NULL;
  wl_list_for_each(cfg, &xdg->configures, link) {
    if (cfg->serial == serial) {
      found = cfg;
      break;
    }

  }

  if (!found) {
    wl_resource_post_error(resource, XDG_SURFACE_ERROR_INVALID_SERIAL,
                           "unknown or consumed configure serial %u", serial);
    return;
  }

  xdg->last_acked_serial = serial;
  xdg->acked_configure = found->state;
  xdg->have_acked_configure = true;
  xdg->configured = true;
  struct vt_xdg_configure_t *tmp;
  wl_list_for_each_safe(cfg, tmp, &xdg->configures, link) {
    bool done = cfg == found;
    wl_list_remove(&cfg->link);
    free(cfg);
    if (done)
      break;
  }

}

static void _xdg_surface_set_window_geometry(struct wl_client   *client,
                                             struct wl_resource *resource,
                                             int32_t x, int32_t y,
                                             int32_t width, int32_t height) {
  VT_PROFILE_SCOPE();
  (void)client;
  /* 1. Retrieve internal XDG-Surface handle */
  struct vt_xdg_surface_t *xdg_surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!xdg_surf || !xdg_surf->surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  if (!xdg_surf->toplevel && !xdg_surf->popup) {
    wl_resource_post_error(resource, XDG_SURFACE_ERROR_NOT_CONSTRUCTED,
                           "surface has no role object");
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
            width, height, (void *)xdg_surf);
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
           (void *)xdg_surf->surf, x, y, width, height);
}

static void _xdg_toplevel_destroy(struct wl_client   *client,
                                  struct wl_resource *resource) {
  (void)client;
  wl_resource_destroy(resource);
}

static void _xdg_toplevel_set_parent(struct wl_client   *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *parent_resource) {
  (void)client;
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

  struct vt_xdg_toplevel_t *ancestor = parent;
  while (ancestor) {
    if (ancestor == xdg_toplevel) {
      wl_resource_post_error(resource, XDG_TOPLEVEL_ERROR_INVALID_PARENT,
                             "parent creates a cycle");
      return;
    }

    ancestor = ancestor->parent;
  }

  if (parent && (!parent->xdg_surf || !parent->xdg_surf->surf ||
                 !parent->xdg_surf->surf->mapped))
    parent = NULL;

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
    struct vt_scene_node_t *node = xdg_toplevel->xdg_surf->surf->scene_node;
    if (node->parent) {
      struct vt_scene_node_t *layer = node->parent;
      vt_scene_node_damage_whole(node->comp, node);
      vt_scene_node_remove_child(layer, node);
      vt_scene_node_add_child(node->comp, layer, node);
    }

  }

  VT_TRACE(xdg_toplevel->xdg_surf->surf->comp->log,
           "xdg_toplevel.set_parent: Set parent of toplevel %p to %p.",
           (void *)xdg_toplevel, (void *)parent);
}

static void _xdg_toplevel_set_title(struct wl_client   *client,
                                    struct wl_resource *resource,
                                    const char         *title) {
  (void)client;
  /* Deallocate the old App Title and strdup() the requested ID */
  struct vt_xdg_toplevel_t *top = wl_resource_get_user_data(resource);
  char                     *value = strdup(title ? title : "");
  if (!value) {
    wl_client_post_no_memory(client);
    return;
  }

  free(top->title);
  top->title = value;
}

static void _xdg_toplevel_set_app_id(struct wl_client   *client,
                                     struct wl_resource *resource,
                                     const char         *app_id) {
  (void)client;
  struct vt_xdg_toplevel_t *top = wl_resource_get_user_data(resource);
  /* Deallocate the old App ID and strdup() the requested ID */
  char *value = strdup(app_id ? app_id : "");
  if (!value) {
    wl_client_post_no_memory(client);
    return;
  }

  free(top->app_id);
  top->app_id = value;
}

static void _xdg_toplevel_show_window_menu(struct wl_client   *client,
                                           struct wl_resource *resource,
                                           struct wl_resource *seat,
                                           uint32_t serial, int32_t x,
                                           int32_t y) {
  (void)client;
  (void)resource;
  (void)seat;
  (void)serial;
  (void)x;
  (void)y;
}

static void _xdg_toplevel_move(struct wl_client   *client,
                               struct wl_resource *resource,
                               struct wl_resource *seat, uint32_t serial) {
  VT_PROFILE_SCOPE();
  (void)client;
  (void)resource;
  (void)seat;
  (void)serial;
}

static void _xdg_toplevel_resize(struct wl_client   *client,
                                 struct wl_resource *resource,
                                 struct wl_resource *seat, uint32_t serial,
                                 uint32_t edges) {
  VT_PROFILE_SCOPE();
  (void)client;
  (void)seat;
  (void)serial;
  if (!xdg_toplevel_resize_edge_is_valid(edges,
                                         wl_resource_get_version(resource)))
    wl_resource_post_error(resource, XDG_TOPLEVEL_ERROR_INVALID_RESIZE_EDGE,
                           "invalid resize edge");
}

static void _xdg_toplevel_set_max_size(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       int32_t width, int32_t height) {
  (void)client;
  struct vt_xdg_toplevel_t *top = wl_resource_get_user_data(resource);
  if (width < 0 || height < 0) {
    wl_resource_post_error(resource, XDG_TOPLEVEL_ERROR_INVALID_SIZE,
                           "negative size limit");
    return;
  }

  top->pending_max_width = width;
  top->pending_max_height = height;
}

static void _xdg_toplevel_set_min_size(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       int32_t width, int32_t height) {
  (void)client;
  struct vt_xdg_toplevel_t *top = wl_resource_get_user_data(resource);
  if (width < 0 || height < 0) {
    wl_resource_post_error(resource, XDG_TOPLEVEL_ERROR_INVALID_SIZE,
                           "negative size limit");
    return;
  }

  top->pending_min_width = width;
  top->pending_min_height = height;
}

static void _xdg_toplevel_set_maximized(struct wl_client   *client,
                                        struct wl_resource *resource) {
  (void)client;
  vt_proto_xdg_toplevel_set_state_maximized(wl_resource_get_user_data(resource),
                                            true);
}

static void _xdg_toplevel_unset_maximized(struct wl_client   *client,
                                          struct wl_resource *resource) {
  (void)client;
  vt_proto_xdg_toplevel_set_state_maximized(wl_resource_get_user_data(resource),
                                            false);
}

static void _xdg_toplevel_set_fullscreen(struct wl_client   *client,
                                         struct wl_resource *resource,
                                         struct wl_resource *output_res) {
  (void)client;
  struct vt_xdg_toplevel_t *top = wl_resource_get_user_data(resource);
  if (!top || !top->xdg_surf || !top->xdg_surf->surf)
    return;

  top->fullscreen_output =
      output_res ? wl_resource_get_user_data(output_res) : NULL;
  vt_proto_xdg_toplevel_set_state_fullscreen(top, true);
}

static void _xdg_toplevel_unset_fullscreen(struct wl_client   *client,
                                           struct wl_resource *resource) {
  (void)client;
  vt_proto_xdg_toplevel_set_state_fullscreen(
      wl_resource_get_user_data(resource), false);
}

static void _xdg_toplevel_set_minimized(struct wl_client   *client,
                                        struct wl_resource *resource) {
  VT_PROFILE_SCOPE();
  (void)client;
  (void)resource;
}

static void _xdg_popup_destroy(struct wl_client   *client,
                               struct wl_resource *resource) {
  (void)client;
  struct vt_xdg_popup_t *popup = wl_resource_get_user_data(resource);
  if (popup->child_popup) {
    wl_resource_post_error(popup->xdg_surf->wm_base_res,
                           XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP,
                           "nested grabbing popup still exists");
    return;
  }

  wl_resource_destroy(resource);
}

static void _xdg_popup_grab(struct wl_client   *client,
                            struct wl_resource *resource,
                            struct wl_resource *seat, uint32_t serial) {
  VT_PROFILE_SCOPE();
  (void)client;
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

  struct vt_xdg_surface_t *parent = popup->parent_xdg_surf;
  if (!parent || (parent->popup && !parent->popup->has_grab)) {
    wl_resource_post_error(
        popup->xdg_surf->wm_base_res, XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT,
        "grabbing popup requires a toplevel or grabbing popup parent");
    return;
  }

  if (parent->popup) {
    if (parent->popup->child_popup && parent->popup->child_popup != popup) {
      wl_resource_post_error(popup->xdg_surf->wm_base_res,
                             XDG_WM_BASE_ERROR_NOT_THE_TOPMOST_POPUP,
                             "popup parent already has a grabbing child");
      return;
    }

    popup->parent_popup = parent->popup;
    parent->popup->child_popup = popup;
  }

  /* we keep the grab information until the popup is dismissed */
  popup->grab_seat = seat;
  popup->grab_serial = serial;
  popup->has_grab = true;

  /* 4. Set seat's keyboard to grabbed popup */
  vt_seat_set_keyboard_focus(wl_resource_get_user_data(seat),
                             popup->xdg_surf->surf);
}

static int _xdg_anchor_dir_x(uint32_t anchor) {
  VT_PROFILE_SCOPE();
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
  VT_PROFILE_SCOPE();
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
  VT_PROFILE_SCOPE();
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
  VT_PROFILE_SCOPE();
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
  VT_PROFILE_SCOPE();
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
  VT_PROFILE_SCOPE();
  return pos < bound_min || pos + size > bound_max;
}

static void _xdg_slide_axis(int64_t *pos, int64_t size, int64_t bound_min,
                            int64_t bound_max) {
  VT_PROFILE_SCOPE();
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
  VT_PROFILE_SCOPE();
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
  VT_PROFILE_SCOPE();
  if (!pos || !out)
    return false;

  if (pos->width <= 0 || pos->height <= 0)
    return false;

  if (!pos->have_anchor_rect)
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

static bool _popup_resolve_pos(struct vt_xdg_popup_t       *popup,
                               struct vt_xdg_positioner_t  *pos,
                               struct vt_xdg_window_geom_t *out_geom) {
  VT_PROFILE_SCOPE();
  if (!popup || !pos || !out_geom || !popup->parent_xdg_surf ||
      !popup->parent_xdg_surf->geom_node || !pos->comp) {
    return false;
  }

  struct vt_output_t *output = vt_scene_node_primary_output(
      pos->comp, popup->parent_xdg_surf->geom_node);

  if (!output)
    return _xdg_positioner_calculate_geometry(pos, NULL, out_geom);

  struct vt_box_t *rect =
      vt_scene_node_get_global_bounds(popup->parent_xdg_surf->geom_node);

  if (!rect) {
    return false;
  }

  struct vt_xdg_window_geom_t constraint = {
      .x = output->x - rect->x,
      .y = output->y - rect->y,
      .w = output->width,
      .h = output->height,
  };

  if (pos->parent_configure) {
    struct vt_xdg_surface_t *parent = popup->parent_xdg_surf;
    if (parent->last_acked_serial == pos->parent_configure &&
        parent->acked_configure.positioned) {
      constraint.x = output->x - parent->acked_configure.geom.x;
      constraint.y = output->y - parent->acked_configure.geom.y;
    }

    struct vt_xdg_configure_t *cfg;
    wl_list_for_each(cfg, &popup->parent_xdg_surf->configures, link) {
      if (cfg->serial == pos->parent_configure && cfg->state.positioned) {
        constraint.x = output->x - cfg->state.geom.x;
        constraint.y = output->y - cfg->state.geom.y;
        break;
      }
    }

  }

  return _xdg_positioner_calculate_geometry(pos, &constraint, out_geom);
}

static void _xdg_popup_reposition(struct wl_client   *client,
                                  struct wl_resource *resource,
                                  struct wl_resource *positioner,
                                  uint32_t            token) {
  (void)client;
  struct vt_xdg_popup_t      *popup = wl_resource_get_user_data(resource);
  struct vt_xdg_positioner_t *pos = wl_resource_get_user_data(positioner);
  if (!popup || !pos || !popup->xdg_surf->surf)
    return;

  *popup->positioner = *pos;
  popup->reactive = pos->reactive;
  if (popup->xdg_surf->initial_configure_sent)
    _xdg_popup_configure(popup, true, token);
}

static void _xdg_configures_clear(struct vt_xdg_surface_t *xdg) {
  struct vt_xdg_configure_t *cfg, *tmp;
  wl_list_for_each_safe(cfg, tmp, &xdg->configures, link) {
    wl_list_remove(&cfg->link);
    free(cfg);
  }

  xdg->have_acked_configure = false;
}

static struct vt_xdg_configure_t *
_xdg_configure_create(struct vt_xdg_surface_t *xdg) {
  struct vt_xdg_configure_t *cfg = calloc(1, sizeof(*cfg));
  if (!cfg) {
    wl_client_post_no_memory(wl_resource_get_client(xdg->xdg_surf_res));
    return NULL;
  }

  cfg->serial = wl_display_next_serial(xdg->surf->comp->wl.dsp);
  wl_list_insert(xdg->configures.prev, &cfg->link);
  xdg->last_configure_serial = cfg->serial;
  return cfg;
}

static struct vt_output_t *_xdg_toplevel_output(struct vt_xdg_toplevel_t *top) {
  struct vt_surface_t *surf = top->xdg_surf->surf;
  struct vt_output_t  *output =
      vt_scene_node_primary_output(surf->comp, surf->scene_node);
  if (!output && !wl_list_empty(&surf->comp->outputs))
    output = wl_container_of(surf->comp->outputs.next, output, link_global);

  return output;
}

static void _xdg_toplevel_save_geometry(struct vt_xdg_toplevel_t *top) {
  if (top->fullscreen || top->maximized)
    return;

  struct vt_xdg_surface_t *xdg = top->xdg_surf;
  struct vt_box_t         *bounds =
      vt_scene_node_get_global_bounds(xdg->surf->scene_node);
  top->restore_geom = (struct vt_xdg_window_geom_t){
      .x = bounds->x + xdg->geom.x,
      .y = bounds->y + xdg->geom.y,
      .w = xdg->geom.w ? xdg->geom.w : (uint32_t)xdg->surf->applied.width,
      .h = xdg->geom.h ? xdg->geom.h : (uint32_t)xdg->surf->applied.height};
  top->have_restore_geom = true;
}

static void _xdg_positioner_set_reactive(struct wl_client   *client,
                                         struct wl_resource *resource) {
  (void)client;
  struct vt_xdg_positioner_t *pos = wl_resource_get_user_data(resource);
  pos->reactive = true;
}

static void _xdg_positioner_set_parent_size(struct wl_client   *client,
                                            struct wl_resource *resource,
                                            int32_t width, int32_t height) {
  (void)client;
  struct vt_xdg_positioner_t *pos = wl_resource_get_user_data(resource);
  pos->parent_width = width;
  pos->parent_height = height;
}

static void _xdg_positioner_set_parent_configure(struct wl_client   *client,
                                                 struct wl_resource *resource,
                                                 uint32_t            serial) {
  (void)client;
  struct vt_xdg_positioner_t *pos = wl_resource_get_user_data(resource);
  pos->parent_configure = serial;
}

static bool _xdg_surface_validate_commit(struct vt_surface_t *surf) {
  struct vt_xdg_surface_t *xdg = surf->role.data;
  if (!xdg || (!xdg->toplevel && !xdg->popup)) {
    wl_resource_post_error(surf->res, WL_DISPLAY_ERROR_INVALID_OBJECT,
                           "xdg surface has no live role object");
    return false;
  }

  if (surf->pending.buffer_attached && surf->pending.buf && !xdg->configured) {
    wl_resource_post_error(
        xdg->xdg_surf_res, XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
        "buffer committed before acknowledging the initial configure");
    return false;
  }

  struct vt_xdg_toplevel_t *top = xdg->toplevel;
  if (top && ((top->pending_max_width &&
               top->pending_min_width > top->pending_max_width) ||
              (top->pending_max_height &&
               top->pending_min_height > top->pending_max_height))) {
    wl_resource_post_error(top->xdg_toplevel_res,
                           XDG_TOPLEVEL_ERROR_INVALID_SIZE,
                           "minimum size exceeds maximum size");
    return false;
  }

  if (xdg->popup && (!xdg->popup->parent_xdg_surf ||
                     (surf->pending.buffer_attached && surf->pending.buf &&
                      (!xdg->popup->parent_xdg_surf->surf ||
                       !xdg->popup->parent_xdg_surf->surf->mapped)))) {
    wl_resource_post_error(xdg->wm_base_res,
                           XDG_WM_BASE_ERROR_INVALID_POPUP_PARENT,
                           "popup parent must be mapped");
    return false;
  }

  return true;
}

static void _xdg_surface_extend_bounds(struct vt_surface_t *surf, int64_t x,
                                       int64_t y, int64_t bounds[4]) {
  if (surf->applied.width > 0 && surf->applied.height > 0) {
    bounds[0] = VT_MIN(bounds[0], x);
    bounds[1] = VT_MIN(bounds[1], y);
    bounds[2] = VT_MAX(bounds[2], x + surf->applied.width);
    bounds[3] = VT_MAX(bounds[3], y + surf->applied.height);
  }

  struct vt_subsurface_t *sub;
  wl_list_for_each(sub, &surf->subsurface.stacking, stacking_link) {
    if (sub->surf && sub->scene_node)
      _xdg_surface_extend_bounds(sub->surf, x + sub->scene_node->x,
                                 y + sub->scene_node->y, bounds);
  }

}

static bool _xdg_surface_apply(struct vt_surface_t        *surf,
                               struct vt_content_update_t *cu) {
  struct vt_xdg_surface_t *xdg = surf->role.data;
  if (!xdg)
    return true;

  if (cu->xdg.unmap)
    xdg->unmap_applied = true;

  if (cu->xdg.generation != xdg->generation)
    return true;

  if (cu->xdg.unmap) {
    xdg->geom = (struct vt_xdg_window_geom_t){0};
    xdg->have_window_geom = false;
    xdg->geom_node->rect_w = 0;
    xdg->geom_node->rect_h = 0;
    vt_scene_node_set_position(xdg->geom_node, 0, 0);
    vt_scene_node_mark_geometry_dirty(xdg->geom_node);
  }

  if (cu->xdg.unmap)
    return true;

  struct vt_xdg_window_geom_t geom = xdg->geom;
  if (cu->xdg.have_geom ||
      (!xdg->have_window_geom &&
       (cu->state.buffer_attached || cu->state.buffer_scale_changed ||
        cu->state.buffer_transform_changed ||
        !wl_list_empty(&surf->subsurface.stacking)))) {
    int64_t bounds[] = {INT64_MAX, INT64_MAX, INT64_MIN, INT64_MIN};
    _xdg_surface_extend_bounds(surf, 0, 0, bounds);
    if (cu->xdg.have_geom) {
      geom = cu->xdg.geom;
      xdg->have_window_geom = true;
      if (bounds[0] < bounds[2] && bounds[1] < bounds[3]) {
        int64_t x = VT_MAX((int64_t)geom.x, bounds[0]);
        int64_t y = VT_MAX((int64_t)geom.y, bounds[1]);
        int64_t right = VT_MIN((int64_t)geom.x + geom.w, bounds[2]);
        int64_t bottom = VT_MIN((int64_t)geom.y + geom.h, bounds[3]);
        if (right <= x || bottom <= y) {
          wl_resource_post_error(
              xdg->xdg_surf_res, XDG_SURFACE_ERROR_INVALID_SIZE,
              "window geometry does not intersect the surface tree");
          return true;
        }

        geom = (struct vt_xdg_window_geom_t){x, y, right - x, bottom - y};
      }
    } else if (bounds[0] < bounds[2] && bounds[1] < bounds[3]) {
      geom = (struct vt_xdg_window_geom_t){
          bounds[0], bounds[1], bounds[2] - bounds[0], bounds[3] - bounds[1]};
    }

  }

  bool geometry_changed = memcmp(&geom, &xdg->geom, sizeof(geom)) != 0;
  if (geometry_changed) {
    int32_t dx = xdg->geom.x - geom.x;
    int32_t dy = xdg->geom.y - geom.y;
    xdg->geom = geom;
    vt_scene_node_set_position(xdg->geom_node, geom.x, geom.y);
    xdg->geom_node->rect_w = geom.w;
    xdg->geom_node->rect_h = geom.h;
    vt_scene_node_mark_geometry_dirty(xdg->geom_node);
    if (!cu->xdg.have_configure || !cu->xdg.configure.positioned)
      vt_scene_node_set_position(surf->scene_node, surf->scene_node->x + dx,
                                 surf->scene_node->y + dy);
  }

  if (cu->xdg.have_configure) {
    struct vt_xdg_configure_state_t *cfg = &cu->xdg.configure;
    if (xdg->toplevel) {
      struct vt_xdg_toplevel_t *top = xdg->toplevel;
      bool changed = top->applied_fullscreen != cfg->fullscreen ||
                     top->applied_maximized != cfg->maximized ||
                     memcmp(&top->applied_output_geom, &cfg->output_geom,
                            sizeof(cfg->output_geom)) != 0;
      top->applied_fullscreen = cfg->fullscreen;
      top->applied_maximized = cfg->maximized;
      top->applied_output_geom = cfg->output_geom;
      if (changed) {
        vt_scene_invalidate_layer_plans(surf->comp);
        struct vt_output_t *output;
        wl_list_for_each(output, &surf->comp->outputs, link_global) {
          vt_output_damage_whole(output);
        }
      }

      if (!cfg->fullscreen && !cfg->maximized && !top->fullscreen &&
          !top->maximized)
        top->have_restore_geom = false;
    }

    if (cfg->positioned) {
      int32_t x = cfg->geom.x - xdg->geom.x;
      int32_t y = cfg->geom.y - xdg->geom.y;
      if (xdg->popup) {
        xdg->popup->configured_geom = cfg->geom;
      } else {
        if (cfg->fullscreen) {
          int32_t width =
              xdg->geom.w ? (int32_t)xdg->geom.w : surf->applied.width;
          int32_t height =
              xdg->geom.h ? (int32_t)xdg->geom.h : surf->applied.height;
          x += ((int32_t)cfg->output_geom.w - width) / 2;
          y += ((int32_t)cfg->output_geom.h - height) / 2;
        }

        if (surf->scene_node->parent) {
          struct vt_box_t *parent =
              vt_scene_node_get_global_bounds(surf->scene_node->parent);
          x -= parent->x;
          y -= parent->y;
        }
      }

      vt_scene_node_set_position(surf->scene_node, x, y);
    }

  }

  if (geometry_changed || cu->xdg.have_configure)
    vt_proto_xdg_surface_position_changed(surf);

  return true;
}

static bool _xdg_popup_configure(struct vt_xdg_popup_t *popup, bool reposition,
                                 uint32_t token) {
  struct vt_xdg_window_geom_t geom;
  if (!_popup_resolve_pos(popup, popup->positioner, &geom)) {
    wl_resource_post_error(popup->xdg_surf->wm_base_res,
                           XDG_WM_BASE_ERROR_INVALID_POSITIONER,
                           "positioner is incomplete or has invalid geometry");
    return false;
  }

  if (!reposition && popup->xdg_surf->initial_configure_sent &&
      memcmp(&geom, &popup->pending_ack_geom, sizeof(geom)) == 0)
    return true;

  struct vt_xdg_configure_t *cfg = _xdg_configure_create(popup->xdg_surf);
  if (!cfg)
    return false;

  cfg->state.geom = geom;
  cfg->state.positioned = true;
  popup->pending_ack_geom = geom;
  if (reposition)
    xdg_popup_send_repositioned(popup->xdg_popup_res, token);

  xdg_popup_send_configure(popup->xdg_popup_res, geom.x, geom.y, geom.w,
                           geom.h);
  xdg_surface_send_configure(popup->xdg_surf->xdg_surf_res, cfg->serial);
  return true;
}

static void _xdg_surface_handle_surface_destroy(struct wl_listener *listener,
                                                void               *data) {
  (void)data;
  struct vt_xdg_surface_t *xdg =
      wl_container_of(listener, xdg, surface_destroy);
  vt_surface_set_mapped(xdg->surf, false);
  xdg->surf->role.data = NULL;
  struct vt_xdg_popup_t *popup, *tmp;
  wl_list_for_each_safe(popup, tmp, &xdg->popups, parent_link) {
    wl_list_remove(&popup->parent_link);
    wl_list_init(&popup->parent_link);
    popup->parent_xdg_surf = NULL;
    if (popup->xdg_surf->surf)
      vt_scene_node_reparent(xdg->surf->comp, popup->xdg_surf->surf->scene_node,
                             xdg->surf->comp->scene->root);
  }

  xdg->surf = NULL;
  xdg->geom_node = NULL;
  xdg->popup_layer = NULL;
  xdg->subsurface_layer = NULL;
  wl_list_remove(&xdg->surface_destroy.link);
  wl_list_init(&xdg->surface_destroy.link);
}

// ===================================================
// =================== PUBLIC API ====================
// ===================================================
bool vt_proto_xdg_shell_init(struct vt_compositor_t *c, uint32_t version) {
  wl_list_init(&_proto.surfaces);
  version = VT_MIN(version, 7);
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
  VT_PROFILE_SCOPE();
  if (!top || !top->xdg_surf || !top->xdg_surf->surf)
    return false;

  if (activated) {
    _xdg_toplevel_save_geometry(top);
    if (!top->maximized_output)
      top->maximized_output = _xdg_toplevel_output(top);

    if (!top->maximized_output)
      activated = false;
  }

  top->maximized = activated;
  if (!activated)
    top->maximized_output = NULL;

  return _xdg_toplevel_configure(top, 0, 0);
}

bool vt_proto_xdg_toplevel_set_state_fullscreen(struct vt_xdg_toplevel_t *top,
                                                bool activated) {
  VT_PROFILE_SCOPE();
  if (!top || !top->xdg_surf || !top->xdg_surf->surf)
    return false;

  if (activated) {
    _xdg_toplevel_save_geometry(top);
    if (!top->fullscreen_output)
      top->fullscreen_output = _xdg_toplevel_output(top);

    if (!top->fullscreen_output)
      activated = false;
  }

  top->fullscreen = activated;
  if (!activated)
    top->fullscreen_output = NULL;

  return _xdg_toplevel_configure(top, 0, 0);
}

bool vt_proto_xdg_toplevel_set_state_resizing(struct vt_xdg_toplevel_t *top,
                                              bool activated) {
  VT_PROFILE_SCOPE();
  if (!top)
    return false;

  top->resizing = activated;
  return _xdg_toplevel_configure(top, 0, 0);
}

bool vt_proto_xdg_toplevel_set_state_activated(struct vt_xdg_toplevel_t *top,
                                               bool activated) {
  VT_PROFILE_SCOPE();
  if (!top)
    return false;

  top->activated = activated;

  if (top->fullscreen && top->fullscreen_output) {
    return _xdg_toplevel_configure(top, top->fullscreen_output->width,
                                   top->fullscreen_output->height);
  }

  return _xdg_toplevel_configure(top, 0, 0);
}

void vt_proto_xdg_surface_position_changed(struct vt_surface_t *surf) {
  if (!surf || !surf->role.impl ||
      (surf->role.impl->type != VT_SURFACE_ROLE_XDG_TOPLEVEL &&
       surf->role.impl->type != VT_SURFACE_ROLE_XDG_POPUP))
    return;

  struct vt_xdg_surface_t *xdg = surf->role.data;
  if (!xdg)
    return;

  struct vt_xdg_popup_t *popup;
  wl_list_for_each(popup, &xdg->popups, parent_link) {
    if (!popup->xdg_surf || !popup->xdg_surf->surf)
      continue;

    if (popup->reactive && popup->xdg_surf->initial_configure_sent)
      _xdg_popup_configure(popup, false, 0);

    vt_proto_xdg_surface_position_changed(popup->xdg_surf->surf);
  }

}

void vt_proto_xdg_shell_output_changed(struct vt_compositor_t *comp,
                                       struct vt_output_t     *output,
                                       bool                    removed) {
  if (_proto.comp != comp || !_proto.surfaces.next)
    return;

  struct vt_xdg_surface_t *xdg;
  wl_list_for_each(xdg, &_proto.surfaces, link) {
    if (!xdg->surf)
      continue;

    struct vt_xdg_toplevel_t *top = xdg->toplevel;
    if (top) {
      if (removed && top->fullscreen_output == output)
        top->fullscreen_output = NULL;

      if (removed && top->maximized_output == output)
        top->maximized_output = NULL;

      if (removed && wl_list_empty(&comp->outputs)) {
        top->fullscreen = false;
        top->maximized = false;
        top->applied_fullscreen = false;
      }

      _xdg_toplevel_configure(top, 0, 0);
    }

    if (xdg->popup && xdg->popup->reactive && xdg->initial_configure_sent)
      _xdg_popup_configure(xdg->popup, false, 0);
  }

}
