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

#include "../core/surface_addon.h"
#include "../render/dmabuf.h"

#include "../render/dmabuf_attr.h"

struct vt_linux_dmabuf_v1_buffer_t {
  uint32_t                w, h;
  struct vt_dmabuf_attr_t attr;
  struct wl_resource     *res;

  struct vt_buffer_t *buf;
};

struct vt_linux_dmabuf_v1_surface_state_t {
  struct vt_surface_addon_t addon;
  struct vt_surface_t *surf;
  struct wl_list       link;

  struct vt_linux_dmabuf_v1_packed_feedback_t *feedback;

  struct wl_list res_feedback;
};

bool vt_proto_linux_dmabuf_v1_init(
    struct vt_compositor_t *comp, struct vt_dmabuf_feedback_t *default_feedback,
    uint32_t version);

struct vt_linux_dmabuf_v1_buffer_t *
vt_proto_linux_dmabuf_v1_from_buffer_res(struct wl_resource *res);

bool vt_proto_linux_dmabuf_v1_set_surface_feedback(struct vt_surface_t *surf);
struct vt_buffer_t *
vt_proto_linux_dmabuf_v1_get_buffer(struct wl_resource *res);

