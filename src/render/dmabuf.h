/*
 * Copyright (c) 2026 Luca Machiedo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include "../core/core_types.h"
#include "../core/session.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <wayland-util.h>

enum vt_dmabuf_tranche_flags_t {
  VT_DMABUF_TRANCHE_FLAG_DIRECT_SCANOUT =
      1 // equivalent to  ZWP_LINUX_DMABUF_FEEDBACK_V1_TRANCHE_FLAGS_SCANOUT
};

struct vt_dmabuf_feedback_t {
  struct vt_compositor_t *comp;
  struct vt_device_t     *dev_main;
  struct wl_array         tranches; // array of vt_dmabuf_tranche_t
};

struct vt_dmabuf_tranche_t {
  struct vt_device_t            *target_device;
  enum vt_dmabuf_tranche_flags_t flags;
  struct wl_array                formats; // array of vt_dmabuf_drm_format_t
};

struct vt_dmabuf_format_modifier_t {
  uint64_t mod;
  bool     _egl_ext_only;
};

struct vt_dmabuf_drm_format_t {
  uint32_t                            format;
  size_t                              len;
  struct vt_dmabuf_format_modifier_t *mods;
};
