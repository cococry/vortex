#include "wl_subcompositor.h"
#include "src/core/core_types.h"
#include "src/core/scene.h"
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

static void _subcompositor_bind(struct wl_client *client, void *data,
                                uint32_t version, uint32_t id) {

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
  wl_resource_destroy(resource);
}

static bool _surface_has_ancestor(struct vt_surface_t *surf,
                                  struct vt_surface_t *ancestor) {
  while (surf && surf->subsurface) {
    surf = surf->subsurface->parent;

    if (surf == ancestor) {
      return true;
    }
  }
  return false;
}

static void _destroy_subsurface(struct vt_subsurface_t *sub) {
  if (!sub)
    return;

  if (sub->surf && sub->surf->subsurface == sub) {
    sub->surf->subsurface = NULL;
  }
  wl_list_remove(&sub->parent_link);
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

  if (!surf || !parent) {
    return;
  }

  if (surf == parent) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                           "surface cannot be its own parent");
    return;
  }

  if (surf->subsurface) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                           "surface is already a subsurface");
    return;
  }

  if (_surface_has_ancestor(parent, surf)) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                           "subsurface relationship would create a cycle");
    return;
  }

  struct vt_subsurface_t *sub = calloc(1, sizeof(*sub));
  if (!sub) {
    wl_client_post_no_memory(client);
    return;
  }

  sub->resource = wl_resource_create(client, &wl_subsurface_interface, 1, id);

  if (!sub->resource) {
    wl_resource_post_no_memory(resource);
    return;
  }

  sub->surf = surf;
  sub->parent = parent;
  sub->synchronized = true;

  sub->scene_node = surf->scene_node;

  if (!sub->scene_node || !parent->scene_node) {
    wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                           "surface or parent are invalid");
    return;
  }

  struct vt_scene_node_t *parent_node = parent->scene_node;

  if (parent->xdg_surf && parent->xdg_surf->subsurface_layer) {
    parent_node = parent->xdg_surf->subsurface_layer;
  }

  vt_scene_node_reparent(surf->comp, sub->scene_node, parent_node);

  wl_list_insert(&parent->subsurfaces, &sub->parent_link);

  surf->subsurface = sub;

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

  sub->scene_node->rect.x = x;
  sub->scene_node->rect.y = y;
}

static void _subsurface_place_above(struct wl_client   *client,
                                    struct wl_resource *resource,
                                    struct wl_resource *sibling_resource) {
  (void)client;
  (void)resource;
  (void)sibling_resource;

  /* TODO: stacking */
}

static void _subsurface_place_below(struct wl_client   *client,
                                    struct wl_resource *resource,
                                    struct wl_resource *sibling_resource) {
  (void)client;
  (void)resource;
  (void)sibling_resource;

  /* TODO: stacking */
}

static void _subsurface_set_sync(struct wl_client   *client,
                                 struct wl_resource *resource) {
  (void)client;

  struct vt_subsurface_t *sub = wl_resource_get_user_data(resource);

  sub->synchronized = true;
}

static void _subsurface_set_desync(struct wl_client   *client,
                                   struct wl_resource *resource) {
  (void)client;

  struct vt_subsurface_t *sub = wl_resource_get_user_data(resource);

  sub->synchronized = false;
}

bool vt_proto_wl_subcompositor_init(struct vt_compositor_t *comp) {

  if (!wl_global_create(comp->wl.dsp, &wl_subcompositor_interface, 1, comp,
                        _subcompositor_bind)) {
    return false;
  }

  return true;
}
