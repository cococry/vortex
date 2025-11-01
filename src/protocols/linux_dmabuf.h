#pragma once

#include "../core/core_types.h"
#include "../render/dmabuf.h"
#include "../core/surface.h"

struct vt_linux_dmabuf_v1_buffer_t {
  uint32_t w, h;
  struct vt_dmabuf_attr_t attr;
  struct wl_resource* res;
};

bool vt_proto_linux_dmabuf_v1_init(
    struct vt_compositor_t* comp, 
    struct vt_dmabuf_feedback_t* default_feedback, 
    uint32_t version);

struct vt_linux_dmabuf_v1_buffer_t* vt_proto_linux_dmabuf_v1_from_buffer_res(
    struct wl_resource* res);

void vt_proto_linux_dmabuf_v1_surface_destroy(struct vt_surface_t* surf);
