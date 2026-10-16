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
#include "../core/surface_addon.h"

struct vt_drm_syncobj_timeline_t;

struct vt_linux_drm_syncobj_v1_surface_state_t {
  struct vt_surface_addon_t addon;
  struct vt_surface_t      *surf;

  struct wl_resource *res;

  // double-buffered, consumed by the next commit
  struct vt_drm_syncobj_timeline_t *acquire_timeline, *release_timeline;
  uint64_t                          acquire_point, release_point;
};

/* drm_fd must support DRM_CAP_SYNCOBJ_TIMELINE and outlive the global, it is
 * the device timelines are imported into. */
bool vt_proto_linux_drm_syncobj_v1_init(struct vt_compositor_t *comp,
                                        int                     drm_fd);
