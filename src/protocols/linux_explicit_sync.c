#include "linux_explicit_sync.h"
#include "src/core/core_types.h"
#include "src/core/surface.h"

#include <assert.h>
#include <linux-explicit-synchronization-v1-server-protocol.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/sync_file.h>

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
  void* data, uint32_t version,
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
	.get_synchronization =  _linux_explicit_sync_v1_get_synchronization,
	.destroy = _linux_explicit_sync_v1_destroy  
};

const struct zwp_linux_surface_synchronization_v1_interface
_linux_surface_sync_v1_impl = {
	.destroy =  _linux_surface_sync_v1_destroy,
	.set_acquire_fence = _linux_surface_sync_v1_set_acquire_fence,
	.get_release = _linux_surface_sync_v1_get_release,
};


void
_linux_explicit_sync_v1_get_synchronization(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id,
  struct wl_resource* surface_resource) {
  struct vt_surface_t* surf = wl_resource_get_user_data(surface_resource);
  if (surf->sync.res) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_EXPLICIT_SYNCHRONIZATION_V1_ERROR_SYNCHRONIZATION_EXISTS,
      "wl_surface@%"PRIu32" already has a synchronization object",
      wl_resource_get_id(surface_resource));
    return;
  }
  surf->sync.res = wl_resource_create(
    client,
    &zwp_linux_surface_synchronization_v1_interface,
    wl_resource_get_version(resource), id);

  surf->sync.acquire_fence_fd = -1;
  surf->sync.release_fence_fd = -1;

  if (!surf->sync.res) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(
    surf->sync.res,
    &_linux_surface_sync_v1_impl,
    surf,
    _linux_surface_sync_handle_destroy);
}

void
_linux_explicit_sync_v1_destroy(
  struct wl_client* client,
  struct wl_resource* resource) {
  wl_resource_destroy(resource);
}

static void _linux_explicit_sync_v1_bind(
  struct wl_client *client,
  void *data, uint32_t version,
  uint32_t id) {
  struct vt_compositor_t* comp = (struct vt_compositor_t*)data;
  struct wl_resource* res;
  if(!(res = wl_resource_create(
    client,
    &zwp_linux_explicit_synchronization_v1_interface,
    version, id))) {

    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(
    res,
    &_linux_explicit_sync_v1_impl,
    comp, NULL);
  
  VT_TRACE(comp->log, "VT_PROTO_LINUX_EXPLICIT_SYNC: Client bound with version %i.", version); 
}


void
_linux_surface_sync_v1_destroy(
  struct wl_client *client,
  struct wl_resource *resource) {	
  (void)client;
  wl_resource_destroy(resource);
}


void
_linux_surface_sync_v1_set_acquire_fence(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t fd) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  if (!surf) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
      "surface no longer exists");
    close(fd);
    return;
  }
  if (surf->sync.acquire_fence_fd != -1) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_DUPLICATE_FENCE,
      "already have a fence fd");
    close(fd);
    return;
  }
  if(surf->sync.acquire_fence_fd != fd) {
    if(surf->sync.acquire_fence_fd >= 0) close(surf->sync.acquire_fence_fd);
    surf->sync.acquire_fence_fd = fd;
  }

  VT_TRACE(surf->comp->log, "VT_PROTO_LINUX_EXPLICIT_SYNC: Got set_acquire_fence with FD: %i", fd); 
}

void
_linux_surface_sync_v1_get_release(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  struct wl_resource* res_release;
  if (!surf) {
    wl_resource_post_error(resource,
                           ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_NO_SURFACE,
                           "surface no longer exists");
    return;
  }
  if (surf->sync.res_release) {
    wl_resource_post_error(resource,
                           ZWP_LINUX_SURFACE_SYNCHRONIZATION_V1_ERROR_DUPLICATE_RELEASE,
                           "already has a buffer release");
    return;
  }

  surf->sync.release_fence_fd = -1;

  res_release = wl_resource_create(
    client,
    &zwp_linux_buffer_release_v1_interface,
    wl_resource_get_version(resource), id);
  if (!res_release) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(res_release, NULL, surf, _linux_surface_res_release_handle_destroy);

  surf->sync.res_release = res_release;

  VT_TRACE(surf->comp->log, "VT_PROTO_LINUX_EXPLICIT_SYNC: Got get_release."); 
}

void 
_linux_surface_sync_handle_destroy(struct wl_resource* resource) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  if(!surf) return;
  surf->sync.res = NULL;
  if(surf->sync.acquire_fence_fd >= 0) {
    close(surf->sync.acquire_fence_fd);
  }
  surf->sync.acquire_fence_fd = -1;
}

void 
_linux_surface_res_release_handle_destroy(struct wl_resource* resource) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  surf->sync.res_release = NULL;
  if (surf->sync.release_fence_fd >= 0) 
    close(surf->sync.release_fence_fd);
  surf->sync.release_fence_fd = -1;
}

bool vt_proto_linux_explicit_sync_v1_init(
    struct vt_compositor_t* comp, 
    uint32_t version) {
  if (!wl_global_create(
    comp->wl.dsp,
    &zwp_linux_explicit_synchronization_v1_interface,
    version, comp,
    _linux_explicit_sync_v1_bind)) {
    return false;
  }

  VT_TRACE(comp->log, "VT_PROTO_LINUX_EXPLICIT_SYNC: Initialized protocol."); 

  return true;

}

void
vt_proto_linux_explicit_sync_v1_err(struct wl_resource* resource, const char* msg) {
	uint32_t id = wl_resource_get_id(resource);
	const char* class = wl_resource_get_class(resource);
	struct wl_client* client = wl_resource_get_client(resource);
	struct wl_resource* dsp_res = wl_client_get_object(client, 1);
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  if(!client || !surf || dsp_res) return;
  VT_TRACE(surf->comp->log, "VT_PROTO_LINUX_EXPLICIT_SYNC: Sending server error '%s'.", msg); 
  wl_resource_post_error(
    dsp_res,
    WL_DISPLAY_ERROR_INVALID_OBJECT,
    "linux_explicit_synchronization server error "
    "with %s@%"PRIu32": %s",
    class, id, msg);
}

