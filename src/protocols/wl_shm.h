#pragma once

#include "../core/core_types.h"

bool vt_proto_wl_shm_init(
    struct vt_compositor_t* comp,
    uint32_t* drm_formats, uint32_t n_formats);
