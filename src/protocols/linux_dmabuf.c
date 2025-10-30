#include "linux_dmabuf.h"
#include "../render/dmabuf.h"

#include <errno.h>
#include <linux-dmabuf-v1-server-protocol.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

struct vt_proto_linux_dmabuf_v1_t {
  struct wl_global* global;
  struct wl_listener dsp_destroy;
  struct vt_compositor_t* comp;
};

struct vt_linux_dmabuf_v1_params_t {
  struct wl_resource* res;
  struct vt_dmbuf_attr_t attr;
};

struct vt_linux_dmabuf_v1_packed_feedback_tranche_t {
	dev_t target_device;
	uint32_t flags; 
	struct wl_array indices;
};

struct vt_linux_dmabuf_v1_packed_feedback_t {
	dev_t dev_main;
	int entries_fd;
	size_t entries_size;

	size_t n_tranches;
	struct vt_linux_dmabuf_v1_packed_feedback_tranche_t* tranches;
};


struct vt_linux_dmabuf_v1_packed_feedback_entry_t {
	uint64_t mod;
	uint32_t format;
	uint32_t pad; // unused
};

static void _proto_linux_dmabuf_v1_bind(struct wl_client *client, void *data,
                                        uint32_t version, uint32_t id);

static void _proto_linux_dmabuf_v1_destroy(struct vt_proto_linux_dmabuf_v1_t* dmabuf);

static void _proto_linux_dmabuf_v1_handle_dsp_destroy(struct wl_listener* listener, void* data);

static void _linux_dmabuf_v1_destroy(
  struct wl_client* client,
  struct wl_resource* resource);

static void _linux_dmabuf_v1_create_params(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t params_id);

static void _linux_dmabuf_v1_get_default_feedback(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id);

static void _linux_dmabuf_v1_get_surface_feedback(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t id,
  struct wl_resource* surface);

static void _linux_dmabuf_v1_params_destroy(
  struct wl_client* client,
  struct wl_resource* resource);

static void _linux_dmabuf_v1_params_add(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t fd,
  uint32_t plane_idx,
  uint32_t offset,
  uint32_t stride,
  uint32_t modifier_hi,
  uint32_t modifier_lo);

static void _linux_dmabuf_v1_params_create(struct wl_client *client,
		       struct wl_resource *resource,
		       int32_t width,
		       int32_t height,
		       uint32_t format,
		       uint32_t flags);
	
static void _linux_dmabuf_v1_params_create_immed(struct wl_client *client,
			     struct wl_resource *resource,
			     uint32_t buffer_id,
			     int32_t width,
			     int32_t height,
			     uint32_t format,
			     uint32_t flags);

static void _linux_dmabuf_v1_params_handle_res_destroy(
  struct wl_resource *resource
);

static void _linux_dmabuf_v1_feedback_destroy(
  struct wl_client *client,
  struct wl_resource *resource
);

static bool _linux_dmabuf_set_default_feedback(
  struct vt_dmabuf_feedback_t* feedback
);

static bool _linux_dmabuf_pack_feedback(
  struct vt_dmabuf_feedback_t* feedback,
  struct vt_linux_dmabuf_v1_packed_feedback_t* o_packed
);

static struct vt_proto_linux_dmabuf_v1_t* _proto;


static const struct zwp_linux_dmabuf_v1_interface _proto_dmabuf_impl = {
  .destroy = _linux_dmabuf_v1_destroy,
	.create_params = _linux_dmabuf_v1_create_params,
	.get_default_feedback = _linux_dmabuf_v1_get_default_feedback,
	.get_surface_feedback = _linux_dmabuf_v1_get_surface_feedback,
};

static const struct zwp_linux_buffer_params_v1_interface _dmabuf_params_impl = {
	.destroy = _linux_dmabuf_v1_params_destroy,
	.add = _linux_dmabuf_v1_params_add,
	.create = _linux_dmabuf_v1_params_create,
	.create_immed = _linux_dmabuf_v1_params_create_immed,
};

static const struct zwp_linux_dmabuf_feedback_v1_interface _dmabuf_feedback_impl = {
	.destroy = _linux_dmabuf_v1_feedback_destroy,
};

// ===================================================
// ================ GLOBAL PROTOCOL ==================
// ===================================================

void 
_proto_linux_dmabuf_v1_bind(struct wl_client *client, void *data,
  uint32_t version, uint32_t id) {	
  struct vt_proto_linux_dmabuf_v1_t* proto = data;
	struct wl_resource* res;
  if(!(res = wl_resource_create(client,
		&zwp_linux_dmabuf_v1_interface, version, id))) {
		wl_client_post_no_memory(client);
		return;
  }
	wl_resource_set_implementation(res, &_proto_dmabuf_impl,
		proto, NULL);
}

void 
_proto_linux_dmabuf_v1_destroy(struct vt_proto_linux_dmabuf_v1_t* dmabuf) {
  // TODO
}

void 
_proto_linux_dmabuf_v1_handle_dsp_destroy(struct wl_listener* listener, void* data) {
  struct vt_proto_linux_dmabuf_v1_t* dmabuf = wl_container_of(listener, dmabuf, dsp_destroy);
  _proto_linux_dmabuf_v1_destroy(dmabuf);
}


// ===================================================
// ============== PER-BUFFER PROTOCOL ================
// ===================================================


void 
_linux_dmabuf_v1_destroy(struct wl_client* client,
			      struct wl_resource* resource) {
  wl_resource_destroy(resource);
}
	
void 
_linux_dmabuf_v1_create_params(struct wl_client* client,
			      struct wl_resource* resource,
			      uint32_t params_id) {
  struct vt_linux_dmabuf_v1_params_t* params;
  if(!(params = calloc(1, sizeof(*params)))) {
    wl_resource_post_no_memory(resource);
    return;
  }
  memset(params->attr.fds, -1, sizeof(params->attr.fds));

  if(!(params->res = wl_resource_create(
    client, &zwp_linux_buffer_params_v1_interface,
    wl_resource_get_version(resource), params_id))) {
    free(params);
    wl_client_post_no_memory(client);
    return;
  }
	wl_resource_set_implementation(params->res,
		&_dmabuf_params_impl, params, _linux_dmabuf_v1_params_handle_res_destroy);
}
	
void 
_linux_dmabuf_v1_get_default_feedback(struct wl_client *client,
				     struct wl_resource *resource,
				     uint32_t id) {
	struct wl_resource* res;
  if(!(res = wl_resource_create(client,
		&zwp_linux_dmabuf_feedback_v1_interface, wl_resource_get_version(resource), id))) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(
    res, &_dmabuf_feedback_impl,
    NULL, NULL);
}

void 
_linux_dmabuf_v1_get_surface_feedback(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id,
  struct wl_resource *surface) {

}

void
_linux_dmabuf_v1_params_destroy(
  struct wl_client* client,
  struct wl_resource* resource) {
  wl_resource_destroy(resource);
}

void
_linux_dmabuf_v1_params_add(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t fd,
  uint32_t plane_idx,
  uint32_t offset,
  uint32_t stride,
  uint32_t modifier_hi,
  uint32_t modifier_lo) {

}

void
_linux_dmabuf_v1_params_create(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t width,
  int32_t height,
  uint32_t format,
  uint32_t flags) {

}
	
void 
_linux_dmabuf_v1_params_create_immed(
  struct wl_client* client,
  struct wl_resource* resource,
  uint32_t buffer_id,
  int32_t width,
  int32_t height,
  uint32_t format,
  uint32_t flags) {

}

void 
_linux_dmabuf_v1_params_handle_res_destroy(
  struct wl_resource* resource
) {
  if(!resource) return;
  struct vt_linux_dmabuf_v1_params_t* params;
  if(!(params = wl_resource_get_user_data(resource))) {
    return;
  }
  for(uint32_t i = 0; i < params->attr.num_planes; i++) {
    if(params->attr.fds[i] < 0) continue;
    close(params->attr.fds[i]);
    params->attr.fds[i] = -1;
  }
  params->attr.num_planes = 0;
  free(params);
}

void 
_linux_dmabuf_v1_feedback_destroy(
  struct wl_client* client,
  struct wl_resource* resource
) {
  (void)client;
  wl_resource_destroy(resource);
}

bool
_linux_dmabuf_set_default_feedback(
  struct vt_dmabuf_feedback_t* feedback
) {
  struct vt_linux_dmabuf_v1_packed_feedback_t packed;
  if(!_linux_dmabuf_pack_feedback(feedback, &packed)) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Failed to pack default DMABUF feedback.");
    return false;
  }
}

static bool 
_format_exists(struct wl_array* fmts, struct vt_dmabuf_drm_format_t* find) {
  struct vt_dmabuf_drm_format_t* fmt;
  wl_array_for_each(fmt, fmts) {
    if(memcmp(fmt, find, sizeof(*find)) == 0) return true;
  }
  return false;
}

bool 
_linux_dmabuf_pack_feedback(
  struct vt_dmabuf_feedback_t* feedback,
  struct vt_linux_dmabuf_v1_packed_feedback_t* o_packed
) {
  if(!feedback) return false;
  struct wl_array all_formats;
  wl_array_init(&all_formats);

  size_t entries_len = 0, entries_size = 0;
  struct vt_dmabuf_tranche_t* tranche;
  wl_array_for_each(tranche, &feedback->tranches) {
    if(!tranche) continue;
    struct vt_dmabuf_drm_format_t* fmt;
    wl_array_for_each(fmt, &tranche->formats) {
      if(!fmt) continue;
      if(!_format_exists(&all_formats, fmt)) {
        struct vt_dmabuf_drm_format_t* fmt_add = wl_array_add(&all_formats, sizeof(*fmt_add));
        memcpy(fmt_add, fmt, sizeof(*fmt_add));
        entries_len += fmt->len;
      }
    }
  }
  if(!entries_len) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Format entries of packed DMABUF feedback is empty.");
    wl_array_release(&all_formats);
    return false;
  }
  entries_size = entries_len * sizeof(struct vt_linux_dmabuf_v1_packed_feedback_entry_t);

  int rw_fd, ro_fd;
  if(!(vt_util_allocate_shm_rwro_pair(feedback->comp, entries_size, &rw_fd, &ro_fd))) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Failed to allocate SHM pair for packed DMABUF feedback format entries.");
    wl_array_release(&all_formats);
    return false;
  }

  struct vt_linux_dmabuf_v1_packed_feedback_entry_t* entries = mmap(
    NULL, entries_size, PROT_READ | PROT_WRITE, MAP_SHARED, rw_fd, 0);
  if(entries == MAP_FAILED) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Failed to mmap SHM for packed DMABUF feedback format entries: %s.", strerror(errno));
    wl_array_release(&all_formats);
    return false;
  }

	close(rw_fd);

  struct vt_dmabuf_drm_format_t* fmt;
  uint32_t n_entries = 0;
  wl_array_for_each(fmt, &all_formats) {
    if(!fmt) continue;
    for(uint32_t i = 0; i < fmt->len; i++) {
      entries[n_entries++] = (struct vt_linux_dmabuf_v1_packed_feedback_entry_t){
        .format = fmt->format,
        .mod    = fmt->mods[i].mod,
      };
    }
  }
  if(n_entries != entries_len) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Mapped entries of DMABUF feedback entries does not match internal format entries."); 
    return false;
    wl_array_release(&all_formats);
  }
	munmap(entries, entries_size);

  return true;
}



// ===================================================
// =================== PUBLIC API ====================
// ===================================================
bool 
vt_proto_linux_dmabuf_v1_init(struct vt_compositor_t* comp, struct vt_dmabuf_feedback_t* default_feedback, uint32_t version) {
  if(!(_proto = VT_ALLOC(comp, sizeof(*_proto)))) return false;
  _proto->comp = comp;

  if(!(_proto->global = wl_global_create(comp->wl.dsp, &zwp_linux_dmabuf_v1_interface,
     4, _proto, _proto_linux_dmabuf_v1_bind))) return false;

  _proto->dsp_destroy.notify = _proto_linux_dmabuf_v1_handle_dsp_destroy;
  wl_display_add_destroy_listener(comp->wl.dsp, &_proto->dsp_destroy);


  VT_TRACE(comp->log, "VT_PROTO_LINUX_DMABUF_V1: Initialized DMABUF protocol.");

  return true;

}
