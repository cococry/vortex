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

#include "wl_subcompositor.h"
#include "src/core/content_update.h"
#include "src/core/core_types.h"
#include "src/core/scene.h"
#include "src/core/surface.h"
#include "src/protocols/xdg_shell.h"
#include <wayland-server-core.h>
#include <wayland-util.h>

static void _subsurface_destroy(struct wl_client   *client,
                                struct wl_resource *resource);

static void _subsurface_set_position(struct wl_client   *client,
                                     struct wl_resource *resource, int32_t x,
                                     int32_t y);

static void _subsurface_place_above(struct wl_client   *client,
                                    struct wl_resource *resource,
                                    struct wl_resource *sibling_resource);

static void _subsurface_place_below(struct wl_client   *client,
                                    struct wl_resource *resource,
                                    struct wl_resource *sibling_resource);

static void _subsurface_set_sync(struct wl_client   *client,
                                 struct wl_resource *resource);

static void _subsurface_set_desync(struct wl_client   *client,
                                   struct wl_resource *resource);

static void _subcompositor_bind(struct wl_client *client, void *data,
                                uint32_t version, uint32_t id);

static void _subcompositor_destroy(struct wl_client   *client,
                                   struct wl_resource *resource);

static void _subcompositor_get_subsurface(struct wl_client   *client,
                                          struct wl_resource *resource,
                                          uint32_t            id,
                                          struct wl_resource *surface_resource,
                                          struct wl_resource *parent_resource);

static const struct wl_subcompositor_interface subcompositor_interface = {
    _subcompositor_destroy, _subcompositor_get_subsurface};

static const struct wl_subsurface_interface subsurface_impl = {
    _subsurface_destroy,     _subsurface_set_position, _subsurface_place_above,
    _subsurface_place_below, _subsurface_set_sync,     _subsurface_set_desync};

static const struct vt_surface_role_impl_t subsurface_role_impl = {
    .type = VT_SURFACE_ROLE_SUBSURFACE,
    .validate_commit = NULL,
    .commit = NULL,
    .apply = NULL,
};

static void _subcompositor_bind(struct wl_client *client, void *data,
                                uint32_t version, uint32_t id) {
  (void)version;

  struct vt_compositor_t *comp = data;

  struct wl_resource *resource =
      wl_resource_create(client, &wl_subcompositor_interface, 1, id);
  if (resource == NULL) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &subcompositor_interface, comp,
                                 NULL);
}

static void _subcompositor_destroy(struct wl_client   *client,
                                   struct wl_resource *resource) {
  (void)client;
  wl_resource_destroy(resource);
}

static bool _surface_has_ancestor(struct vt_surface_t *surf,
                                  struct vt_surface_t *ancestor) {
  while (surf && vt_surface_has_role(surf, VT_SURFACE_ROLE_SUBSURFACE)) {
    struct vt_subsurface_t *sub = surf->role.data;
    if (!sub)
      return false;
    surf = sub->parent;

    if (surf == ancestor) {
      return true;
    }
  }
  return false;
}

static void _destroy_subsurface(struct vt_subsurface_t *sub) {
  if (!sub)
    return;

  sub->surf->role.data = NULL;
  wl_list_remove(&sub->link);
  wl_list_remove(&sub->stacking_link);
  vt_scene_node_damage_whole(sub->surf->comp, sub->scene_node);
  vt_scene_node_destroy(sub->surf->comp, sub->scene_node);
  free(sub);
}

static void _subsurface_resource_destroy(struct wl_resource *resource) {
  struct vt_subsurface_t *sub = wl_resource_get_user_data(resource);

  if (sub)
    _destroy_subsurface(sub);
}

static void _subcompositor_get_subsurface(struct wl_client   *client,
                                          struct wl_resource *resource,
                                          uint32_t            id,
                                          struct wl_resource *surface_resource,
                                          struct wl_resource *parent_resource) {
  struct vt_surface_t *surf = wl_resource_get_user_data(surface_resource);

  struct vt_surface_t *parent = wl_resource_get_user_data(parent_resource);

  if (!surf || !parent)
    return;

  if (surf == parent) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_PARENT,
                           "surface cannot be its own parent");
    return;
  }

  if (_surface_has_ancestor(parent, surf)) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_PARENT,
                           "subsurface relationship would create a cycle");
    return;
  }

  if (surf->role.impl && surf->role.impl->type != VT_SURFACE_ROLE_SUBSURFACE) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                           "surface already has another role");
    return;
  }

  if (surf->role.impl && surf->role.impl->type == VT_SURFACE_ROLE_SUBSURFACE &&
      surf->role.data) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                           "surface already has a wl_subsurface object");
    return;
  }

  if (!surf->scene_node || !parent->scene_node) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                           "surface or parent has no scene representation");
    return;
  }

  struct vt_subsurface_t *sub = calloc(1, sizeof(*sub));
  if (!sub) {
    wl_client_post_no_memory(client);
    return;
  }

  sub->resource = wl_resource_create(client, &wl_subsurface_interface, 1, id);

  if (!sub->resource) {
    free(sub);
    wl_client_post_no_memory(client);
    return;
  }

  sub->surf = surf;
  sub->parent = parent;
  sub->synchronized = true;
  sub->scene_node = surf->scene_node;

  struct vt_scene_node_t *parent_node = parent->scene_node;

  if (vt_surface_has_role(parent, VT_SURFACE_ROLE_XDG_TOPLEVEL) ||
      vt_surface_has_role(parent, VT_SURFACE_ROLE_XDG_POPUP)) {
    struct vt_xdg_surface_t *xdg = parent->role.data;

    if (xdg && xdg->subsurface_layer)
      parent_node = xdg->subsurface_layer;
  }

  if (!vt_surface_set_role(surf, &subsurface_role_impl, sub)) {
    wl_resource_destroy(sub->resource);
    free(sub);
    return;
  }

  vt_scene_node_reparent(surf->comp, sub->scene_node, parent_node);

  static uint64_t next_stacking_id = 0;
  sub->stacking_id = ++next_stacking_id;
  wl_list_insert(parent->subsurface.childs.prev, &sub->link);
  wl_list_insert(parent->subsurface.stacking.prev, &sub->stacking_link);

  wl_resource_set_implementation(sub->resource, &subsurface_impl, sub,
                                 _subsurface_resource_destroy);
}

static void _subsurface_destroy(struct wl_client   *client,
                                struct wl_resource *resource) {
  (void)client;
  wl_resource_destroy(resource);
}

static void _subsurface_set_position(struct wl_client   *client,
                                     struct wl_resource *resource, int32_t x,
                                     int32_t y) {
  (void)client;

  struct vt_subsurface_t *sub = wl_resource_get_user_data(resource);
  if (!sub)
    return;

  if (sub->surf && sub->surf->scene_node) {
    vt_scene_node_set_position(sub->surf->scene_node, x, y);
  }
}

static void _subsurface_place(struct wl_resource *resource,
                              struct wl_resource *sibling_resource,
                              bool                above) {
  struct vt_subsurface_t *sub = wl_resource_get_user_data(resource);
  struct vt_surface_t    *sibling = wl_resource_get_user_data(sibling_resource);
  if (!sub || !sub->parent)
    return;

  struct vt_subsurface_t *reference = NULL;
  if (sibling != sub->parent) {
    struct vt_subsurface_t *it;
    wl_list_for_each(it, &sub->parent->subsurface.childs, link) {
      if (it != sub && it->surf == sibling) {
        reference = it;
        break;
      }
    }
    if (!reference) {
      wl_resource_post_error(resource, WL_SUBSURFACE_ERROR_BAD_SURFACE,
                             "reference surface is not a sibling or parent");
      return;
    }
  }

  wl_list_remove(&sub->link);
  if (reference) {
    sub->pending_below_parent = reference->pending_below_parent;
    wl_list_insert(above ? &reference->link : reference->link.prev, &sub->link);
  } else {
    struct wl_list         *position = &sub->parent->subsurface.childs;
    struct vt_subsurface_t *it;
    wl_list_for_each(it, &sub->parent->subsurface.childs, link) {
      if (!it->pending_below_parent)
        break;
      position = &it->link;
    }
    sub->pending_below_parent = !above;
    wl_list_insert(position, &sub->link);
  }
}

static void _subsurface_place_above(struct wl_client   *client,
                                    struct wl_resource *resource,
                                    struct wl_resource *sibling_resource) {
  (void)client;
  _subsurface_place(resource, sibling_resource, true);
}

static void _subsurface_place_below(struct wl_client   *client,
                                    struct wl_resource *resource,
                                    struct wl_resource *sibling_resource) {
  (void)client;
  _subsurface_place(resource, sibling_resource, false);
}

bool vt_subsurface_capture_stacking(struct vt_content_update_t *cu) {
  size_t count = wl_list_length(&cu->surf->subsurface.childs);
  if (!count)
    return true;
  cu->subsurface_stack = calloc(count, sizeof(*cu->subsurface_stack));
  if (!cu->subsurface_stack)
    return false;
  struct vt_subsurface_t *sub;
  wl_list_for_each(sub, &cu->surf->subsurface.childs, link) {
    cu->subsurface_stack[cu->subsurface_stack_count++] =
        (struct vt_subsurface_stack_entry_t){sub->stacking_id,
                                             sub->pending_below_parent};
  }
  return true;
}

void vt_subsurface_apply_stacking(struct vt_content_update_t *cu) {
  /* IDs let queued parent commits safely outlive destroyed subsurfaces. */
  struct wl_list *position = &cu->surf->subsurface.stacking;
  for (size_t i = 0; i < cu->subsurface_stack_count; i++) {
    struct vt_subsurface_t *sub;
    wl_list_for_each(sub, &cu->surf->subsurface.childs, link) {
      if (sub->stacking_id != cu->subsurface_stack[i].id)
        continue;
      wl_list_remove(&sub->stacking_link);
      wl_list_insert(position, &sub->stacking_link);
      position = &sub->stacking_link;
      sub->below_parent = cu->subsurface_stack[i].below_parent;
      break;
    }
  }
}

void vt_subsurface_surface_destroy(struct vt_surface_t *surf) {
  struct vt_subsurface_t *sub, *tmp;
  wl_list_for_each_safe(sub, tmp, &surf->subsurface.childs, link) {
    wl_resource_set_user_data(sub->resource, NULL);
    _destroy_subsurface(sub);
  }
  if (vt_surface_has_role(surf, VT_SURFACE_ROLE_SUBSURFACE) &&
      surf->role.data) {
    sub = surf->role.data;
    wl_resource_set_user_data(sub->resource, NULL);
    _destroy_subsurface(sub);
  }
}

static void _subsurface_set_sync(struct wl_client   *client,
                                 struct wl_resource *resource) {
  (void)client;

  struct vt_subsurface_t *sub = wl_resource_get_user_data(resource);

  if (sub)
    sub->synchronized = true;
}

static void _subsurface_set_desync(struct wl_client   *client,
                                   struct wl_resource *resource) {
  (void)client;

  struct vt_subsurface_t *sub = wl_resource_get_user_data(resource);

  if (sub)
    sub->synchronized = false;
}

bool vt_proto_wl_subcompositor_init(struct vt_compositor_t *comp) {

  if (!wl_global_create(comp->wl.dsp, &wl_subcompositor_interface, 1, comp,
                        _subcompositor_bind)) {
    return false;
  }

  return true;
}
