#include "vt_background.h"
#include "core/output.h"
#include "core/surface.h"
#include "vt-background-v1-server-protocol.h"

#include <wayland-server.h>

static bool background_apply(struct vt_surface_t *surf,
                              struct vt_content_update_t *cu) {
  (void)cu;
  struct vt_background_t *bg = surf->role.data;
  if (bg)
    vt_output_set_background(bg->output, surf->current_buf_use);
  return true;
}

static void background_surface_destroy(struct wl_listener *listener, void *data) {
  (void)data;
  struct vt_background_t *bg = wl_container_of(listener, bg, surface_destroy);
  wl_resource_destroy(bg->resource);
}

static void background_destroy(struct wl_client   *client,
                               struct wl_resource *resource) {
  (void)client;
  wl_resource_destroy(resource);
}

static const struct vt_surface_role_impl_t background_role_impl = {
    .type = VT_SURFACE_ROLE_VT_BACKGROUND,
    .validate_commit = NULL,
    .commit = NULL,
    .apply = background_apply,

};

static void background_resource_destroy(struct wl_resource *resource) {
  struct vt_background_t *bg = wl_resource_get_user_data(resource);

  if (bg->output && bg->output->background.owner == bg) {
    bg->output->background.owner = NULL;
    vt_output_set_background(bg->output, NULL);
  }

  wl_list_remove(&bg->surface_destroy.link);
  bg->surface->role.data = NULL;
  free(bg);
}

static const struct vt_background_v1_interface background_impl = {
    .destroy = background_destroy,
};

static void get_background(struct wl_client *client, struct wl_resource *shell,
                           uint32_t id, struct wl_resource *surface_resource,
                           struct wl_resource *output_resource) {
  struct vt_surface_t *surf = wl_resource_get_user_data(surface_resource);

  struct vt_output_t *output = wl_resource_get_user_data(output_resource);

  if (surf->role.impl &&
      (surf->role.impl->type != VT_SURFACE_ROLE_VT_BACKGROUND ||
       surf->role.data)) {
    wl_resource_post_error(shell, VT_BACKGROUND_SHELL_V1_ERROR_ROLE,
                           "surface already has a role");
    return;
  }

  if (output->background.owner) {
    wl_resource_post_error(shell, VT_BACKGROUND_SHELL_V1_ERROR_OUTPUT_BUSY,
                           "output already has a background");
    return;
  }

  struct wl_resource *res =
      wl_resource_create(client, &vt_background_v1_interface, 1, id);

  if (!res) {
    wl_client_post_no_memory(client);
    return;
  }

  struct vt_background_t *bg = calloc(1, sizeof(*bg));
  if (!bg) {
    wl_resource_destroy(res);
    wl_client_post_no_memory(client);
    return;
  }

  bg->resource = res;
  bg->surface = surf;
  bg->output = output;

  if (!vt_surface_set_role(surf, &background_role_impl, bg)) {
    wl_resource_post_error(shell, VT_BACKGROUND_SHELL_V1_ERROR_ROLE,
                           "cannot set surface role");
    wl_resource_destroy(res);
    free(bg);
    return;
  }

  bg->surface_destroy.notify = background_surface_destroy;
  wl_resource_add_destroy_listener(surf->res, &bg->surface_destroy);
  output->background.owner = bg;

  wl_resource_set_implementation(res, &background_impl, bg,
                                 background_resource_destroy);

  vt_background_v1_send_configure(res, output->width, output->height);
}

static const struct vt_background_shell_v1_interface shell_impl = {
    .get_background = get_background,
};

static void bind_shell(struct wl_client *client, void *data, uint32_t version,
                       uint32_t id) {
  struct wl_resource *res = wl_resource_create(
      client, &vt_background_shell_v1_interface, version, id);

  if (!res) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(res, &shell_impl, data, NULL);
}

bool vt_proto_vt_background_init(struct vt_compositor_t *comp) {
  if (!comp)
    return false;

  return wl_global_create(comp->wl.dsp, &vt_background_shell_v1_interface,
                          1, comp, bind_shell) != NULL;
}
