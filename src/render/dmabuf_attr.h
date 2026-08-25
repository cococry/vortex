#pragma once

#include <stdint.h>

#include "dmabuf_const.h"

struct vt_dmabuf_attr_t {
  int32_t  num_planes;
  uint32_t offsets[VT_DMABUF_PLANES_CAP];
  uint32_t strides[VT_DMABUF_PLANES_CAP];
  int32_t  fds[VT_DMABUF_PLANES_CAP];
  int32_t  width, height;
  uint32_t format;
  uint64_t mod;
};
