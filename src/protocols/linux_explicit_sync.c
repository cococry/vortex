#define _GNU_SOURCE

#include "linux_explicit_sync.h"
#include "src/core/core_types.h"
#include "src/core/surface.h"
#include "src/core/util.h"
#include "src/render/dmabuf.h"
#include "src/render/renderer.h"
#include <sys/stat.h>

#include <assert.h>
#include <linux-explicit-synchronization-v1-server-protocol.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/sync_file.h>

#define _SUBSYS_NAME "VT_PROTO_LINUX_EXPLICIT_SYNC"

/* ===================================================
 * ========== STATIC FUNCTION DECLARATIONS ===========
 * =================================================== */
static void _linux_explicit_sync_v1_get_synchronization(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id,
  struct wl_resource* surface_resource);

static void _linux_explicit_sync_v1_destroy(
  struct wl_client* client,
  struct wl_resource* resource);

static void _linux_explicit_sync_v1_bind(
  struct wl_client* client,
  void* data,
  uint32_t version,
  uint32_t id);

static void _linux_surface_sync_v1_destroy(
  struct wl_client* client,
  struct wl_resource* resource);

static void _linux_surface_sync_v1_set_acquire_fence(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t fd);

static void _linux_surface_sync_v1_get_release(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id);

static void _linux_surface_sync_handle_destroy(struct wl_resource* resource);
static void _linux_surface_res_release_handle_destroy(struct wl_resource* resource);

static const struct zwp_linux_explicit_synchronization_v1_interface _linux_explicit_sync_v1_impl = {
  .get_synchronization = _linux_explicit_sync_v1_get_synchronization,
  .destroy = _linux_explicit_sync_v1_destroy,
};

static const struct zwp_linux_surface_synchronization_v1_interface _linux_surface_sync_v1_impl = {
  .destroy = _linux_surface_sync_v1_destroy,
  .set_acquire_fence = _linux_surface_sync_v1_set_acquire_fence,
  .get_release = _linux_surface_sync_v1_get_release,
};

struct vt_proto_linux_explicit_sync_v1_t {
  struct vt_compositor_t* comp;
};

static struct vt_proto_linux_explicit_sync_v1_t _proto;

void 
_linux_explicit_sync_v1_get_synchronization(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id,
  struct wl_resource* surface_resource) {
  /* 1. Retrieve internal surface handle */
  struct vt_surface_t* surf = surface_resource ? wl_resource_get_user_data(surface_resource) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Check if surface already has a synchronization object */
  if (surf->sync.res) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_EXPLICIT_SYNCHRONIZATION_V1_ERROR_SYNCHRONIZATION_EXISTS,
      "wl_surface@%" PRIu32 " already has a synchronization object",
      wl_resource_get_id(surface_resource));
    return;
  }

  /* 3. Allocate resource for synchronization interface */
  struct wl_resource* res = wl_resource_create(
    client,
    &zwp_linux_surface_synchronization_v1_interface,
    wl_resource_get_version(resource),
    id);

  if (!res) {
    VT_WL_OUT_OF_MEMORY(surf->comp, client);
    return;
  }

  /* 4. Initialize synchronization state */
  surf->sync.res = res;
  surf->sync.acquire_fence_fd = -1;
  surf->sync.release_fence_fd = -1;

  /* 5. Set handler functions via the implementation */
  wl_resource_set_implementation(
    res, &_linux_surface_sync_v1_impl,
    surf, _linux_surface_sync_handle_destroy);

  VT_TRACE(surf->comp->log, "linux_explicit_sync.get_synchronization: created synchronization object for surface %p.", surf);
}

void 
_linux_explicit_sync_v1_destroy(struct wl_client* client, struct wl_resource* resource) {
  /* Destroy the global explicit sync interface resource */
  wl_resource_destroy(resource);
}

static void 
_linux_explicit_sync_v1_bind(struct wl_client* client, void* data, uint32_t version, uint32_t id) {
  /* 1. Create global interface resource */
  struct vt_compositor_t* comp = (struct vt_compositor_t*)data;
  struct wl_resource* res = wl_resource_create(
    client,
    &zwp_linux_explicit_synchronization_v1_interface,
    version,
    id);

  if (!res) {
    VT_WL_OUT_OF_MEMORY(comp, client);
    return;
  }

  /* 2. Set implementation and data */
  wl_resource_set_implementation(res, &_linux_explicit_sync_v1_impl, comp, NULL);

  VT_TRACE(comp->log, "linux_explicit_sync.bind: client bound with version %i.", version);
}

void 
_linux_surface_sync_v1_destroy(struct wl_client* client, struct wl_resource* resource) {
  (void)client;
  wl_resource_destroy(resource);
}

void 
_linux_surface_sync_v1_set_acquire_fence(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t fd) {
  /* 1. Retrieve internal surface handle */
  struct vt_surface_t* surf = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!surf) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
      "surface no longer exists");
    close(fd);
    return;
  }

  /* 2. Check for duplicate fence */
  if (surf->sync.acquire_fence_fd != -1) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_DUPLICATE_FENCE,
      "already have a fence fd");
    close(fd);
    return;
  }

  /* 3. Store the fence file descriptor */
  if (surf->sync.acquire_fence_fd != fd) {
    if (surf->sync.acquire_fence_fd >= 0)
      close(surf->sync.acquire_fence_fd);
    surf->sync.acquire_fence_fd = fd;
  }

  VT_TRACE(surf->comp->log, "linux_surface_sync.set_acquire_fence: set acquire fence FD=%i for surface %p.", fd, surf);
}

void 
_linux_surface_sync_v1_get_release(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id) {
  /* 1. Retrieve internal surface handle */
  struct vt_surface_t* surf = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    wl_resource_post_error(
      resource,
      ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
      "surface no longer exists");
    return;
  }

  /* 2. Check for existing release object */
  if (surf->sync.res_release) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_DUPLICATE_RELEASE,
      "already has a buffer release");
    return;
  }

  /* 3. Allocate resource for the buffer release interface */
  struct wl_resource* res_release = wl_resource_create(
    client,
    &zwp_linux_buffer_release_v1_interface,
    wl_resource_get_version(resource),
    id);

  if (!res_release) {
    VT_WL_OUT_OF_MEMORY(surf->comp, client);
    return;
  }

  /* 4. Initialize sync release state */
  surf->sync.release_fence_fd = -1;
  surf->sync.res_release = res_release;

  /* 5. Set destruction handler */
  wl_resource_set_implementation(
    res_release,
    NULL,
    surf,
    _linux_surface_res_release_handle_destroy);

  VT_TRACE(surf->comp->log, "linux_surface_sync.get_release: created buffer release for surface %p.", surf);
}

void 
_linux_surface_sync_handle_destroy(struct wl_resource* resource) {
  /* 1. Retrieve internal surface handle */
  struct vt_surface_t* surf = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(surf->comp->log, "linux_surface_sync.resource_destroy: destroying sync resource %p.", resource);

  /* 2. Clear synchronization handle */
  surf->sync.res = NULL;

  /* 3. Close acquire fence fd if valid */
  if (surf->sync.acquire_fence_fd >= 0)
    close(surf->sync.acquire_fence_fd);

  surf->sync.acquire_fence_fd = -1;
}

void 
_linux_surface_res_release_handle_destroy(struct wl_resource* resource) {
  /* 1. Retrieve internal surface handle */
  struct vt_surface_t* surf = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(surf->comp->log, "linux_surface_release.resource_destroy: destroying release resource %p.", resource);

  /* 2. Clear release handle and close FD if valid */
  surf->sync.res_release = NULL;
  if (surf->sync.release_fence_fd >= 0)
    close(surf->sync.release_fence_fd);
  surf->sync.release_fence_fd = -1;
}


/* ===================================================
 * =================== PUBLIC API ====================
 * =================================================== */
bool 
vt_proto_linux_explicit_sync_v1_init(struct vt_compositor_t* comp, uint32_t version) {
  /* 1. Register global for linux-explicit-synchronization interface */
  if (!wl_global_create(
        comp->wl.dsp,
        &zwp_linux_explicit_synchronization_v1_interface,
        version,
        comp,
        _linux_explicit_sync_v1_bind)) {
    VT_ERROR(comp->log, "Cannot implement linux_explicit_sync_v1 interface.");
    return false;
  }

  _proto.comp = comp;

  VT_TRACE(comp->log, "Initialized Linux explicit synchronization protocol.");
  return true;
}

void 
vt_proto_linux_explicit_sync_v1_err(struct wl_resource* resource, const char* msg) {
  /* 1. Retrieve relevant context and client */
  uint32_t id = wl_resource_get_id(resource);
  const char* class = wl_resource_get_class(resource);
  struct wl_client* client = wl_resource_get_client(resource);
  struct wl_resource* dsp_res = client ? wl_client_get_object(client, 1) : NULL;
  struct vt_surface_t* surf = resource ? wl_resource_get_user_data(resource) : NULL;

  if (!client || !surf || !dsp_res) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 2. Post Wayland protocol error */
  wl_resource_post_error(
    dsp_res,
    WL_DISPLAY_ERROR_INVALID_OBJECT,
    "linux_explicit_synchronization server error with %s@%" PRIu32 ": %s",
    class,
    id,
    msg);

  /* 3. Log the protocol warning */
  VT_WARN(
    surf->comp->log,
    "linux_explicit_synchronization server error with %s@%" PRIu32 ": %s",
    class,
    id,
    msg);
}

