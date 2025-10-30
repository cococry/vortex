#pragma once

#include "../core/core_types.h"
#include "../render/dmabuf.h"

struct vt_linux_dmabuf_v1_buffer_t {

};

bool vt_proto_linux_dmabuf_v1_init(
    struct vt_compositor_t* comp, 
    struct vt_dmabuf_feedback_t* default_feedback, 
    uint32_t version);
