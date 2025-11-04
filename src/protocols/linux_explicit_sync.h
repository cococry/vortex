#pragma once

#include "../core/core_types.h"

bool vt_proto_linux_explicit_sync_v1_init(
    struct vt_compositor_t* comp, 
    uint32_t version);

void vt_proto_linux_explicit_sync_v1_err(
    struct wl_resource* resource, const char* msg);
