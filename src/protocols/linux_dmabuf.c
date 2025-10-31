#define _GNU_SOURCE
#include "linux_dmabuf.h"
#include "../render/dmabuf.h"

#include <errno.h>
#include <fcntl.h>
#include <linux-dmabuf-v1-server-protocol.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#include <xf86drmMode.h>
#include <xf86drm.h>

#include "../core/util.h"

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
	struct vt_linux_dmabuf_v1_packed_feedback_tranche_t tranches[];
};


struct vt_linux_dmabuf_v1_packed_feedback_entry_t {
	uint64_t mod;
	uint32_t format;
	uint32_t pad; // unused
};

struct vt_proto_linux_dmabuf_v1_t {
  struct wl_global* global;
  struct wl_listener dsp_destroy;
  struct vt_compositor_t* comp;

  struct wl_array default_formats;
  struct vt_linux_dmabuf_v1_packed_feedback_t* default_feedback; 
  int32_t fd_main_dev;
};

struct vt_linux_dmabuf_v1_params_t {
  struct wl_resource* res;
  struct vt_dmabuf_attr_t attr;
  bool has_mod;
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

static void _linux_dmabuf_v1_buffer_handle_res_destroy(
  struct wl_resource *resource
);

static void _linux_dmabuf_v1_feedback_destroy(
  struct wl_client *client,
  struct wl_resource *resource
);

static void _linux_dmabuf_v1_buffer_destroy(struct wl_client *client,
		struct wl_resource *resource);

static bool _linux_dmabuf_set_default_feedback(
  struct vt_proto_linux_dmabuf_v1_t* proto,
  struct vt_dmabuf_feedback_t* feedback
);

static bool _linux_dmabuf_pack_feedback(
  struct vt_dmabuf_feedback_t* feedback,
  struct vt_linux_dmabuf_v1_packed_feedback_t** o_packed
);

static bool _linux_dmabuf_is_importable(
  struct vt_dmabuf_attr_t* attr
);
static bool _linux_dmabuf_close_params(
  struct vt_linux_dmabuf_v1_params_t* params
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

static const struct wl_buffer_interface wl_buffer_impl = {
	.destroy = _linux_dmabuf_v1_buffer_destroy,
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

void _free_formats(struct wl_array *formats) {
  if (!formats)
    return;

  struct vt_dmabuf_drm_format_t *fmt;
  wl_array_for_each(fmt, formats) {
    free(fmt->mods);
    fmt->mods = NULL;
  }

  wl_array_release(formats);
}

void 
_proto_linux_dmabuf_v1_destroy(struct vt_proto_linux_dmabuf_v1_t* dmabuf) {
  _free_formats(&dmabuf->default_formats);
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
	struct vt_surface_t* surf = wl_resource_get_user_data(resource);

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
  struct vt_linux_dmabuf_v1_params_t* params = wl_resource_get_user_data(resource);
  if (!params) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
      "params was already used to create a wl_buffer");
    close(fd);
    return;
  }

  if (plane_idx >= VT_DMABUF_PLANES_CAP) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
      "plane index %u > planes cap: %u", plane_idx, VT_DMABUF_PLANES_CAP);
    close(fd);
    return;
  }
  if (params->attr.fds[plane_idx] != -1) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET,
      "a dmabuf with FD %d has already been added for plane %u",
      params->attr.fds[plane_idx], plane_idx);
    close(fd);
    return;
  }

  // all planes must have the same modifier 
  uint64_t mod = ((uint64_t)modifier_hi << 32) | modifier_lo;
  if (params->has_mod && mod != params->attr.mod) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
      "sent modifier %" PRIu64 " for plane %u, expected"
      " modifier %" PRIu64 " like other planes",
      mod, plane_idx, params->attr.mod);
    close(fd);
    return;
  }

	params->attr.mod = mod;
	params->has_mod  = true;

	params->attr.fds[plane_idx] = fd;
	params->attr.offsets[plane_idx] = offset;
	params->attr.strides[plane_idx] = stride;
	params->attr.num_planes++;

}

void
_linux_dmabuf_params_create(
  struct wl_resource* resource,
  uint32_t buf_id,
  int32_t width,
  int32_t height,
  uint32_t format,
  uint32_t flags) {
  struct vt_linux_dmabuf_v1_params_t* params = wl_resource_get_user_data(resource);
  if (!params) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
      "params was already used to create a wl_buffer");
    return;
  }

  // we used up those params to create a buffer
  wl_resource_set_user_data(resource, NULL);
	free(params);

  if (!params->attr.num_planes) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
      "no dmabuf has been added to the params");

	}

  if (params->attr.fds[0] == -1) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
      "no dmabuf has been added for plane 0");
    _linux_dmabuf_close_params(params);
  }

  bool has_gap = false;
  int highest_plane = -1;

  for (int i = 0; i < VT_DMABUF_PLANES_CAP; i++) {
    if (params->attr.fds[i] >= 0) {
      highest_plane = i;
    }
  }

  for (int i = 0; i < highest_plane; i++) {
    if (params->attr.fds[i] < 0) {
      has_gap = true;
      break;
    }
  }

  if (has_gap) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
      "gap in dmabuf planes");
    _linux_dmabuf_close_params(params);
  }

  if (!zwp_linux_buffer_params_v1_flags_is_valid(flags,  wl_resource_get_version(resource))) {
    wl_resource_post_error(resource,
                           ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                           "Unknown dmabuf flags %"PRIu32, flags);
    _linux_dmabuf_close_params(params);
  }


  if (flags != 0) {
    if (buf_id == 0) {
      VT_ERROR(_proto->comp->log, " VT_PROTO_LINUX_DMABUF_V1: DMABUF flags aren't supported.");
      zwp_linux_buffer_params_v1_send_failed(resource);
    } else {
      wl_resource_post_error(
        resource,
        ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER,
        "importing the supplied dmabufs failed");
    }
    _linux_dmabuf_close_params(params);
  }


  params->attr.width = width;
  params->attr.height = height;
  params->attr.format = format;

  if (width <= 0 || height <= 0) {
    wl_resource_post_error(
      resource,
      ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS,
      "invalid width %i or height %i", width, height);
    _linux_dmabuf_close_params(params);
  }

  for (int i = 0; i < params->attr.num_planes; i++) {
    if ((uint64_t)params->attr.offsets[i]
      + params->attr.strides[i] > UINT32_MAX) {
      wl_resource_post_error(
        resource,
        ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
        "size overflow for plane %d", i);
      _linux_dmabuf_close_params(params);
    }

    if ((uint64_t)params->attr.offsets[i]
      + (uint64_t)params->attr.strides[i] * height > UINT32_MAX) {
      wl_resource_post_error(
        resource,
        ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
        "size overflow for plane %d", i);
      _linux_dmabuf_close_params(params);
    }
    off_t size = lseek(params->attr.fds[i], 0, SEEK_END);
    if (size == -1) continue;

    if (params->attr.offsets[i] > size) {
      wl_resource_post_error(
        resource,
        ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
        "invalid offset %" PRIu32 " for plane %d",
        params->attr.offsets[i], i);
      _linux_dmabuf_close_params(params);
    }

    if (params->attr.offsets[i] + params->attr.strides[i] > size ||
				params->attr.strides[i] == 0) {
			wl_resource_post_error(resource,
				ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
				"invalid stride %" PRIu32 " for plane %d",
				params->attr.strides[i], i);
      _linux_dmabuf_close_params(params);
		}

		// planes > 0 might be subsampled according to fourcc format
		if (i == 0 && params->attr.offsets[i] +
				params->attr.strides[i] * height > size) {
			wl_resource_post_error(resource,
				ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
				"invalid buffer stride or height for plane %d", i);
      _linux_dmabuf_close_params(params);
		}
  }

  if(!_linux_dmabuf_is_importable(&params->attr)) {
    if (buf_id == 0) {
      VT_ERROR(_proto->comp->log, " VT_PROTO_LINUX_DMABUF_V1: DMABUF flags aren't supported.");
      zwp_linux_buffer_params_v1_send_failed(resource);
    } else {
      wl_resource_post_error(
        resource,
        ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER,
        "importing the supplied dmabufs failed");
    }
    _linux_dmabuf_close_params(params);
  }

  struct vt_linux_dmabuf_v1_buffer_t* buf = calloc(1, sizeof(*buf));
  if(!buf) {
    wl_resource_post_no_memory(resource);
    _linux_dmabuf_close_params(params);
  }
  buf->w = width;
  buf->h = height;

	struct wl_client* client = wl_resource_get_client(resource);
	buf->res = wl_resource_create(client, &wl_buffer_interface,1, buf_id);
  if(!buf->res) {
    free(buf);
    wl_resource_post_no_memory(resource);
    _linux_dmabuf_close_params(params);
  }

  buf->attr = params->attr;

	wl_resource_set_implementation(buf->res,
		&wl_buffer_impl, buf, _linux_dmabuf_v1_buffer_handle_res_destroy);

  if (!buf_id) {
    zwp_linux_buffer_params_v1_send_created(
      resource,
      buf->res);
  }

}

void
_linux_dmabuf_v1_params_create(
  struct wl_client* client,
  struct wl_resource* resource,
  int32_t width,
  int32_t height,
  uint32_t format,
  uint32_t flags) {
  _linux_dmabuf_params_create(resource, 0, width, height, format, flags);
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
  _linux_dmabuf_params_create(resource, buffer_id, width, height, format, flags);
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
  _linux_dmabuf_close_params(params);
  free(params);
}

void 
_linux_dmabuf_v1_buffer_handle_res_destroy(
  struct wl_resource *resource
) {
  if(!resource) return;
  struct vt_linux_dmabuf_v1_buffer_t* buf = wl_resource_get_user_data(resource);
  if(!buf) return;
  buf->res = NULL;
}

void 
_linux_dmabuf_v1_feedback_destroy(
  struct wl_client* client,
  struct wl_resource* resource
) {
  (void)client;
  wl_resource_destroy(resource);
}


void 
_linux_dmabuf_v1_buffer_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}


static bool
_format_exists(struct wl_array* fmts, struct vt_dmabuf_drm_format_t* find)  {
  struct vt_dmabuf_drm_format_t* fmt;
  wl_array_for_each(fmt, fmts) {
    if (fmt->format != find->format || fmt->len != find->len)
      continue;

    bool match = true;
    for (size_t i = 0; i < fmt->len; i++) {
      if (fmt->mods[i].mod != find->mods[i].mod) {
        match = false;
        break;
      }
    }
    if (match)
      return true;
  }
  return false;
}

static bool _accumulate_tranche_formats(
  struct wl_array* all_formats, struct wl_array* tranches, 
  size_t* n_formats, bool persistent) {
  if(!all_formats || !tranches) return false;
  struct vt_dmabuf_tranche_t* tranche;
  wl_array_for_each(tranche, tranches) {
    if(!tranche) continue;
    struct vt_dmabuf_drm_format_t* fmt;
    wl_array_for_each(fmt, &tranche->formats) {
      if(!fmt) continue;
      if(!_format_exists(all_formats, fmt)) {
        struct vt_dmabuf_drm_format_t* fmt_add = wl_array_add(all_formats, sizeof(*fmt_add));
        if(!fmt_add) {
          if(n_formats) *n_formats = 0;
          return false;
        }
        if (persistent) {
          // deep copy
          fmt_add->format = fmt->format;
          fmt_add->len = fmt->len;

          if (fmt->len > 0 && fmt->mods) {
            fmt_add->mods = calloc(fmt->len,
                                   sizeof(*fmt_add->mods));
            if (!fmt_add->mods) {
              // Rollback allocation
              wl_array_release(all_formats);
              if (n_formats)
                *n_formats = 0;
              return false;
            }
            memcpy(fmt_add->mods, fmt->mods,
                   fmt->len * sizeof(*fmt->mods));
          } else {
            fmt_add->mods = NULL;
          }
        } else {
          // shallow copy 
          memcpy(fmt_add, fmt, sizeof(*fmt_add));
        }
        if(n_formats) *n_formats += fmt->len;
      }
    }
  }
  return true;
}

bool
_linux_dmabuf_set_default_feedback(
  struct vt_proto_linux_dmabuf_v1_t* proto,
  struct vt_dmabuf_feedback_t* feedback
) {
  if(!feedback || !feedback->comp || !feedback->comp->session) return false;

  struct vt_linux_dmabuf_v1_packed_feedback_t* packed = NULL;
  if(!_linux_dmabuf_pack_feedback(feedback, &packed)) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Failed to pack default DMABUF feedback.");
    return false;
  }

  struct vt_session_t* s = feedback->comp->session;

  if(!s->impl.get_native_handle || !s->impl.finish_native_handle || !s->impl.get_native_handle_render_node) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Session backend does not implement required functionality for DMABUF.");
    return false;
  }

  void* native_main_dev = s->impl.get_native_handle(s, feedback->dev_main);

  int32_t fd_main_dev = -1;
  const char* native_render_node = s->impl.get_native_handle_render_node(s, native_main_dev);
  if(native_render_node) {
    fd_main_dev = open(native_render_node,  O_RDWR | O_CLOEXEC);
    if(fd_main_dev < 0) {
      VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Failed to open render node '%s': %s",feedback->dev_main->path, strerror(errno)); 
      free(packed);
      s->impl.finish_native_handle(s, native_main_dev);
      return false;
    }
    s->impl.finish_native_handle(s, native_main_dev);
  } else {
    VT_TRACE(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Device '%s' is not a render node, skipping default feedback.");
    s->impl.finish_native_handle(s, native_main_dev);
    free(packed);
    return false;
  }

  if(proto->default_formats.size != 0)
    _free_formats(&proto->default_formats);

  wl_array_init(&proto->default_formats);
  if(!_accumulate_tranche_formats(&proto->default_formats, &feedback->tranches, NULL, true)) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Failed to accumulate default formats.");
    s->impl.finish_native_handle(s, native_main_dev);
    close(fd_main_dev);
    free(packed);
    return false;
  }

  if(proto->default_feedback) {
    free(proto->default_feedback);
  }
  proto->default_feedback = packed;

  if (proto->fd_main_dev >= 0) close(proto->fd_main_dev);
  proto->fd_main_dev = fd_main_dev;

  VT_TRACE(feedback->comp->log,
          "VT_PROTO_LINUX_DMABUF_V1: Default feedback set: %i formats, render node %s",
          proto->default_formats.size / sizeof(struct vt_dmabuf_drm_format_t),
          native_render_node);

  return true;

}


bool 
_linux_dmabuf_pack_feedback(
  struct vt_dmabuf_feedback_t* feedback,
  struct vt_linux_dmabuf_v1_packed_feedback_t** o_packed
) {
  if(!feedback) return false;
  if(!feedback->tranches.size) return false;
 

  VT_TRACE(feedback->comp->log,
           "VT_PROTO_LINUX_DMABUF_V1: Packing feedback...");

  struct wl_array all_formats;
  wl_array_init(&all_formats);
  size_t entries_len = 0, entries_size = 0;
  _accumulate_tranche_formats(&all_formats, &feedback->tranches, &entries_len, false);
  
  VT_TRACE(feedback->comp->log,
           "VT_PROTO_LINUX_DMABUF_V1: Accumulated all formats of all tranches.");

  if(!entries_len) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Format entries of packed DMABUF feedback is empty.");
    wl_array_release(&all_formats);
    return false;
  }
  entries_size = entries_len * sizeof(struct vt_linux_dmabuf_v1_packed_feedback_entry_t);

  // Allocate a read-only-read-write pair of file descriptors for the feedback data:
  // We write our data into the readwrite FD once and then use the readonly FD as the table FD 
  // so that clients can only read (not write to) the feedback data.
  int rw_fd, ro_fd;
  if(!(vt_util_allocate_shm_rwro_pair(feedback->comp, entries_size, &rw_fd, &ro_fd))) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Failed to allocate SHM pair for packed DMABUF feedback format entries.");
    wl_array_release(&all_formats);
    return false;
  }

  VT_TRACE(feedback->comp->log,
           "VT_PROTO_LINUX_DMABUF_V1: Allocated SHM for the packed feedback data..");

  // map the entries 
  struct vt_linux_dmabuf_v1_packed_feedback_entry_t* entries = mmap(
    NULL, entries_size, PROT_READ | PROT_WRITE, MAP_SHARED, rw_fd, 0);
  if(entries == MAP_FAILED) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Failed to mmap SHM for packed DMABUF feedback format entries: %s.", strerror(errno));
    close(ro_fd);
    close(rw_fd);
    wl_array_release(&all_formats);
    return false;
  }	

  VT_TRACE(feedback->comp->log,
           "VT_PROTO_LINUX_DMABUF_V1: mmap()ed the packed feedback data.");
  // fill the table 
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
    wl_array_release(&all_formats);
    close(ro_fd);
    return false;
  }
  
  VT_TRACE(feedback->comp->log,
           "VT_PROTO_LINUX_DMABUF_V1: Filled the table of packed feedback data.");
  
  // unmap the table (we are finished reading to it) 
  munmap(entries, entries_size);

  // seal the read-write FD so that clients cannot do weird shit
  if (fcntl(rw_fd, F_ADD_SEALS,
            F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) < 0) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: fcntl(F_ADD_SEALS) failed: %s", strerror(errno));
    close(ro_fd);
    close(rw_fd);
    return false;
  }
  
  VT_TRACE(feedback->comp->log,
           "VT_PROTO_LINUX_DMABUF_V1: Sealed RW FD for packed  feedback data.");

  // after sealing, close the read-write FD
  close(rw_fd);

  
  VT_TRACE(feedback->comp->log,
           "VT_PROTO_LINUX_DMABUF_V1: Building packed feedback...");

  struct vt_linux_dmabuf_v1_packed_feedback_t* packed = calloc(
    1, sizeof(*packed) + feedback->tranches.size);
  if(!packed) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Out of memory."); 
    close(ro_fd);
    wl_array_release(&all_formats);
    return false;
  }

  // build the packed feedback
  packed->dev_main      = feedback->dev_main->dev;
  packed->n_tranches    = feedback->tranches.size / sizeof(struct vt_dmabuf_tranche_t);
  packed->entries_size  = entries_size;
  packed->entries_fd    = ro_fd; 
 
  
  VT_TRACE(feedback->comp->log,
           "VT_PROTO_LINUX_DMABUF_V1: Building packed feedback tranches (Total Tranches: %i)...", packed->n_tranches);


  // we need to get the all_fmt_data as a struct vt_dmabuf_drm_format_t* to correctly loop 
  // over its bytes
  struct vt_dmabuf_drm_format_t* all_fmt_data = all_formats.data;
  size_t n_all_formats = all_formats.size / sizeof(struct vt_dmabuf_drm_format_t);
  if (all_formats.size % sizeof(struct vt_dmabuf_drm_format_t) != 0) {
    VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Corrupted all_formats array: misaligned size %i", all_formats.size);
    wl_array_release(&all_formats);
    close(ro_fd);
    return false;
  }

  struct vt_dmabuf_tranche_t *tranches = feedback->tranches.data;
  for (uint32_t i = 0; i < packed->n_tranches; i++) {
    VT_TRACE(feedback->comp->log,
             "VT_PROTO_LINUX_DMABUF_V1: Building packed feedback tranche %u...", i);

    struct vt_linux_dmabuf_v1_packed_feedback_tranche_t* tranche_packed = &packed->tranches[i];
    struct vt_dmabuf_tranche_t* tranche = &tranches[i];

    tranche_packed->target_device = tranche->target_device->dev;
    tranche_packed->flags = tranche->flags;
    wl_array_init(&tranche_packed->indices);

    for (size_t j = 0; j < n_all_formats; j++) {
      if (_format_exists(&tranche->formats, &all_fmt_data[j])) {
        uint16_t* add = wl_array_add(&tranche_packed->indices, sizeof(*add));
        if (!add) {
          VT_ERROR(feedback->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Out of memory.");
          close(ro_fd);
          wl_array_release(&all_formats);
          return false;
        }
        *add = j;
      }
    }
  }

  *o_packed = packed;

  close(packed->entries_fd);
  wl_array_release(&all_formats);

  VT_TRACE(feedback->comp->log,
           "VT_PROTO_LINUX_DMABUF_V1: Successfully packed default feedback.");

  return true;
}

// https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/master/types/wlr_linux_dmabuf_v1.c?ref_type=heads#L208
bool 
_linux_dmabuf_is_importable(
  struct vt_dmabuf_attr_t* attr
) {
  // skip check for non DRM setups
  if (_proto->fd_main_dev < 0) return true;

  for (uint32_t i = 0; i < attr->num_planes; i++) {
    uint32_t handle = 0;
    if (drmPrimeFDToHandle(_proto->fd_main_dev, attr->fds[i], &handle) != 0) {
      VT_ERROR(_proto->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Failed to import DMA-BUF FD for plane %i", i);
      return false;
    }
    if (drmCloseBufferHandle(_proto->fd_main_dev, handle) != 0) {
      VT_ERROR(_proto->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Failed to closse buffer handle for plane %i", i);
      return false;
    }
  }
  return true;
}

bool 
_linux_dmabuf_close_params(
  struct vt_linux_dmabuf_v1_params_t* params
) {
  for(uint32_t i = 0; i < params->attr.num_planes; i++) {
    if(params->attr.fds[i] < 0) continue;
    close(params->attr.fds[i]);
    params->attr.fds[i] = -1;
  }
  params->attr.num_planes = 0;
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
  _proto->fd_main_dev = -1;

  if(!_linux_dmabuf_set_default_feedback(_proto, default_feedback)) {
  VT_TRACE(comp->log, "VT_PROTO_LINUX_DMABUF_V1: Failed to set default feedback.");
    wl_global_destroy(_proto->global);
    return false;
  }


  _proto->dsp_destroy.notify = _proto_linux_dmabuf_v1_handle_dsp_destroy;
  wl_display_add_destroy_listener(comp->wl.dsp, &_proto->dsp_destroy);

  VT_TRACE(comp->log, "VT_PROTO_LINUX_DMABUF_V1: Initialized DMABUF protocol.");

  return true;

}
