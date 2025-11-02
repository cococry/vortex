#include "linux_explicit_sync.h"

#include <linux-explicit-synchronization-v1-server-protocol.h>

static void _linux_explicit_sync_v1_get_synchronization(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id,
  struct wl_resource* surface_resource);

static void _linux_explicit_sync_v1_destroy(
  struct wl_client* client,
  struct wl_resource* resource);

static void _linux_surface_sync_v1_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

static void _linux_surface_sync_v1_set_acquire_fence(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t fd);

static void _linux_surface_sync_v1_get_release(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

static const struct zwp_linux_explicit_synchronization_v1_interface _explicit_sync_handler = {
	.get_synchronization =  _linux_explicit_sync_v1_get_synchronization,
	.destroy = _linux_explicit_sync_v1_destroy  
};

const struct zwp_linux_surface_synchronization_v1_interface
linux_surface_synchronization_implementation = {
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

}

void
_linux_explicit_sync_v1_destroy(
  struct wl_client* client,
  struct wl_resource* resource) {
  wl_resource_destroy(resource);
}


void
_linux_surface_sync_v1_destroy(
  struct wl_client *client,
  struct wl_resource *resource) {

}

void
_linux_surface_sync_v1_set_acquire_fence(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t fd) {

}

void
_linux_surface_sync_v1_get_release(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id) {

}

bool vt_proto_linux_explicit_sync_v1_init(
    struct vt_compositor_t* comp, 
    uint32_t version) {

}
