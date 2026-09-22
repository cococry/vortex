#define _GNU_SOURCE

#include "linux_explicit_sync.h"
#include "src/core/core_types.h"
#include "src/core/surface.h"
#include "src/core/content_update.h"
#include "src/core/util.h"
#include <sys/stat.h>
#include "src/core/surface_addon.h"
#include <wayland-server-core.h>

#include <assert.h>
#include <linux-explicit-synchronization-v1-server-protocol.h>
#include <linux/sync_file.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define _SUBSYS_NAME "VT_PROTO_LINUX_EXPLICIT_SYNC"

static void _linux_explicit_sync_v1_get_synchronization(
    struct wl_client *client, struct wl_resource *resource, uint32_t id,
    struct wl_resource *surface_resource);

static void _linux_explicit_sync_v1_destroy(struct wl_client   *client,
                                            struct wl_resource *resource);

static void _linux_explicit_sync_v1_bind(struct wl_client *client, void *data,
                                         uint32_t version, uint32_t id);

static void _linux_surface_sync_v1_destroy(struct wl_client   *client,
                                           struct wl_resource *resource);

static void _linux_surface_sync_v1_set_acquire_fence(
    struct wl_client *client, struct wl_resource *resource, int32_t fd);

static void _linux_surface_sync_v1_get_release(struct wl_client   *client,
                                               struct wl_resource *resource,
                                               uint32_t            id);

static void _linux_surface_sync_handle_destroy(struct wl_resource *resource);

static void _handle_explict_release_destroy(struct wl_resource *resource);

static void
_linux_explicit_sync_destroy_addon(struct vt_surface_addon_t *addon);

static const struct zwp_linux_explicit_synchronization_v1_interface
    _linux_explicit_sync_v1_impl = {
        .get_synchronization = _linux_explicit_sync_v1_get_synchronization,
        .destroy = _linux_explicit_sync_v1_destroy,
};

static const struct zwp_linux_surface_synchronization_v1_interface
    _linux_surface_sync_v1_impl = {
        .destroy = _linux_surface_sync_v1_destroy,
        .set_acquire_fence = _linux_surface_sync_v1_set_acquire_fence,
        .get_release = _linux_surface_sync_v1_get_release,
};

struct vt_proto_linux_explicit_sync_v1_t {
  struct vt_compositor_t *comp;
};

static bool
_linux_explicit_sync_addon_commit(struct vt_surface_t *surf,
                                    struct vt_content_update_t *cu)
{
  if (!surf || !cu)
    return false;
  struct vt_linux_explicit_sync_v1_surface_state_t *sync =
      surf->proto_state.linux_explicit_sync_v1;

  if (!sync)
    return true;

  cu->acquire_fence_fd = sync->acquire_fence_fd;
  sync->acquire_fence_fd = -1;

  VT_TRACE(
      surf->comp->log,
      "Moved acquire_fence_fd=%i owned by explicit sync to content update %p",
      cu->acquire_fence_fd, cu);

  return true;
}

static const struct vt_surface_addon_impl_t explicit_sync_surface_addon_impl = {
    .name = "linux-explicit-synchronization-v1",
    .destroy = _linux_explicit_sync_destroy_addon,
    .commit = _linux_explicit_sync_addon_commit
};

static struct vt_proto_linux_explicit_sync_v1_t _proto;

struct vt_linux_explicit_sync_v1_surface_state_t *
_linux_explicit_sync_v1_from_surf(struct vt_surface_t *surf) {
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return NULL;
  }

  if (surf->proto_state.linux_explicit_sync_v1)
    return NULL;

  struct vt_linux_explicit_sync_v1_surface_state_t *state =
      calloc(1, sizeof(*state));

  if (!state)
    return NULL;

  state->acquire_fence_fd = -1;
  state->surf = surf;

  surf->proto_state.linux_explicit_sync_v1 = state;

  state->addon.impl = explicit_sync_surface_addon_impl;
  wl_list_insert(&surf->addons, &state->addon.link);

  VT_TRACE(_proto.comp->log,
           "Created linux-explicit-synchronization-v1 surface state %p for %p.",
           state, surf);

  return state;
}

void _linux_explicit_sync_v1_get_synchronization(
    struct wl_client *client, struct wl_resource *resource, uint32_t id,
    struct wl_resource *surface_resource) {

  struct vt_surface_t *surf =
      surface_resource ? wl_resource_get_user_data(surface_resource) : NULL;

  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  if (surf->proto_state.linux_explicit_sync_v1) {
    wl_resource_post_error(
        resource,
        ZWP_LINUX_EXPLICIT_SYNCHRONIZATION_V1_ERROR_SYNCHRONIZATION_EXISTS,
        "wl_surface@%" PRIu32 " already has a synchronization object",
        wl_resource_get_id(surface_resource));
    return;
  }

  struct wl_resource *res = wl_resource_create(
      client, &zwp_linux_surface_synchronization_v1_interface,
      wl_resource_get_version(resource), id);

  if (!res) {
    VT_WL_OUT_OF_MEMORY(surf->comp, client);
    return;
  }

  struct vt_linux_explicit_sync_v1_surface_state_t *state =
      _linux_explicit_sync_v1_from_surf(surf);

  if (!state) {
    wl_resource_destroy(res);
    VT_WL_OUT_OF_MEMORY(surf->comp, client);
    return;
  }

  state->res = res;

  wl_resource_set_implementation(res, &_linux_surface_sync_v1_impl, state,
                                 _linux_surface_sync_handle_destroy);

  VT_TRACE(surf->comp->log,
           "linux_explicit_sync.get_synchronization: created synchronization "
           "object for surface %p.",
           surf);
}

void _linux_explicit_sync_v1_destroy(struct wl_client   *client,
                                     struct wl_resource *resource) {
  /* Destroy the global explicit sync interface resource */
  wl_resource_destroy(resource);
}

static void _linux_explicit_sync_v1_bind(struct wl_client *client, void *data,
                                         uint32_t version, uint32_t id) {
  struct vt_compositor_t *comp = (struct vt_compositor_t *)data;
  struct wl_resource     *res = wl_resource_create(
      client, &zwp_linux_explicit_synchronization_v1_interface, version, id);

  if (!res) {
    VT_WL_OUT_OF_MEMORY(comp, client);
    return;
  }

  wl_resource_set_implementation(res, &_linux_explicit_sync_v1_impl, comp,
                                 NULL);

  VT_TRACE(comp->log, "linux_explicit_sync.bind: client bound with version %i.",
           version);
}

void _linux_surface_sync_v1_destroy(struct wl_client   *client,
                                    struct wl_resource *resource) {
  (void)client;
  wl_resource_destroy(resource);
}

void _linux_surface_sync_v1_set_acquire_fence(struct wl_client   *client,
                                              struct wl_resource *resource,
                                              int32_t             fd) {
  struct vt_linux_explicit_sync_v1_surface_state_t *state =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!state) {
    wl_resource_post_error(
        resource, ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
        "surface explicit sync state no longer exists");
    close(fd);
    return;
  }

  if (!state->surf) {
    wl_resource_post_error(
        resource, ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
        "surface longer exists");
    close(fd);
    return;
  }

  if (state->acquire_fence_fd >= 0) {
    wl_resource_post_error(
        resource, ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_DUPLICATE_FENCE,
        "already have a fence fd");
    close(fd);
    return;
  }

  state->acquire_fence_fd = fd;

  VT_TRACE(state->surf->comp->log,
           "linux_surface_sync.set_acquire_fence: set acquire fence FD=%i for "
           "surface %p.",
           fd, state->surf);
}

void _linux_surface_sync_v1_get_release(struct wl_client   *client,
                                        struct wl_resource *resource,
                                        uint32_t            id) {
  struct vt_linux_explicit_sync_v1_surface_state_t *state =
      resource ? wl_resource_get_user_data(resource) : NULL;

  if (!state) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    wl_resource_post_error(
        resource, ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
        "surface explicit sync state no longer exists");
    return;
  }

  struct vt_surface_t* surf = state->surf;

  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    wl_resource_post_error(
        resource, ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
        "surface no longer exists");
    return;
  }

  struct vt_buffer_release_t *release =
      vt_surface_state_get_or_create_buffer_release(_proto.comp->renderer,
                                                    &surf->pending);

  if (!release) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  if (release->explicit) {
    wl_resource_post_error(
        resource, ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_DUPLICATE_RELEASE,
        "already has a buffer release");
    return;
  }

  struct wl_resource *res =
      wl_resource_create(client, &zwp_linux_buffer_release_v1_interface,
                         wl_resource_get_version(resource), id);

  if (!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  struct vt_linux_explicit_sync_v1_buffer_release_t *explicit_release =
      calloc(1, sizeof(*explicit_release));

  if (!explicit_release) {
    wl_resource_destroy(res);
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  explicit_release->release = release;
  explicit_release->res = res;
  release->explicit = explicit_release;

  wl_resource_set_implementation(res, NULL, explicit_release,
                                 _handle_explict_release_destroy);

  VT_TRACE(_proto.comp->log,
           "get_release: Pending release=%p explicit=%p res=%p",
           &surf->pending.buffer_release,
           surf->pending.buffer_release->explicit, res);
}

void _linux_surface_sync_handle_destroy(struct wl_resource *resource) {
  struct vt_linux_explicit_sync_v1_surface_state_t *state =
      resource ? wl_resource_get_user_data(resource) : NULL;

  if (!state) {
    return;
  }

  state->res = NULL;

  vt_surface_addon_destroy(&state->addon);
}

void _handle_explict_release_destroy(struct wl_resource *resource) {
  struct vt_linux_explicit_sync_v1_buffer_release_t *explicit =
      resource ? wl_resource_get_user_data(resource) : NULL;

  if (!explicit) {
    return;
  }

  if (explicit->release) {
    explicit->release->explicit = NULL;
    explicit->release = NULL;
  }

  free(explicit);
}

/* ===================================================
 * =================== PUBLIC API ====================
 * =================================================== */
bool vt_proto_linux_explicit_sync_v1_init(struct vt_compositor_t *comp,
                                          uint32_t                version) {
  /* 1. Register global for linux-explicit-synchronization interface */
  if (!wl_global_create(comp->wl.dsp,
                        &zwp_linux_explicit_synchronization_v1_interface,
                        version, comp, _linux_explicit_sync_v1_bind)) {
    VT_ERROR(comp->log, "Cannot implement linux_explicit_sync_v1 interface.");
    return false;
  }

  _proto.comp = comp;

  VT_TRACE(comp->log, "Initialized Linux explicit synchronization protocol.");
  return true;
}

void vt_proto_linux_explicit_sync_v1_err(struct wl_resource *resource,
                                         const char         *msg) {
  /* 1. Retrieve relevant context and client */
  uint32_t id = wl_resource_get_id(resource);
  const char *class = wl_resource_get_class(resource);
  struct wl_client   *client = wl_resource_get_client(resource);
  struct wl_resource *dsp_res = client ? wl_client_get_object(client, 1) : NULL;

  if (!client || !dsp_res) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Post Wayland protocol error */
  wl_resource_post_error(
      dsp_res, WL_DISPLAY_ERROR_INVALID_OBJECT,
      "linux_explicit_synchronization server error with %s@%" PRIu32 ": %s",
      class, id, msg);

  /* 3. Log the protocol warning */
  VT_WARN(_proto.comp->log,
          "linux_explicit_synchronization server error with %s@%" PRIu32 ": %s",
          class, id, msg);
}

static void
_linux_explicit_sync_destroy_addon(struct vt_surface_addon_t *addon) {
  struct vt_linux_explicit_sync_v1_surface_state_t *state =
      wl_container_of(addon, state, addon);

  if (state->acquire_fence_fd >= 0) {
    close(state->acquire_fence_fd);
    state->acquire_fence_fd = -1;
  }

  if (state->res) {
    wl_resource_set_user_data(state->res, NULL);
  }

  if (state->surf) {
    state->surf->proto_state.linux_explicit_sync_v1 = NULL;
    state->surf = NULL;
  }

  free(state);
}
