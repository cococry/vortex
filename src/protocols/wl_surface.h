#pragma once

#include "src/core/surface.h"

bool
vt_proto_wl_surface_init(
  struct vt_surface_t* surf, struct wl_client* client, 
  uint32_t id, uint32_t version);
