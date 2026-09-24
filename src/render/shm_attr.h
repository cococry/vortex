#pragma once

#include <stdint.h>
#include <stdlib.h>

struct vt_shm_attr_t {
  void    *data;
  size_t   size;
  int32_t  width;
  int32_t  height;
  int32_t  stride;
  uint32_t format;
};
