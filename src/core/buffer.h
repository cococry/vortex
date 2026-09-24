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

#include <wayland-server-core.h>

#include "core_types.h"
#include "../render/dmabuf_attr.h"
#include "../render/shm_attr.h"

struct vt_buffer_t;

struct vt_buffer_implementation_t {
  bool (*get_dmabuf)(struct vt_buffer_t *buf, struct vt_dmabuf_attr_t *o_attr);
  bool (*get_shm)(struct vt_buffer_t *buffer, struct vt_shm_attr_t *o_attr);
};

struct vt_buffer_release_implementation_t {
  void (*finish)(struct vt_buffer_release_t *release, int release_fence_fd);
  void (*destroy)(struct vt_buffer_release_t *release);

  bool (*needs_release_fence)(struct vt_buffer_release_t *release);
};

struct vt_buffer_attachment_implementation_t {
  void (*destroy)(struct vt_buffer_t *buf, void *owner, void *data);
  void (*end_use)(struct vt_buffer_t *buf, void *owner, void *data);
};

struct vt_buffer_attachment_t {
  struct vt_buffer_t *buf;
  const void         *owner;
  void               *data;

  const struct vt_buffer_attachment_implementation_t *impl;

  struct wl_list link_buf;
  struct wl_list link_owner;
};

struct vt_buffer_t {
  struct vt_compositor_t* comp;

  uint32_t refcount, uses;

  uint32_t width, height;

  const struct vt_buffer_implementation_t *impl;

  struct wl_list attachments;
};

struct vt_buffer_release_t {
  struct vt_compositor_t *comp;

  uint32_t refcount;
  bool finished;

  const struct vt_buffer_release_implementation_t* impl;
};

struct vt_buffer_use_t {
  struct vt_compositor_t *comp;

  uint32_t refcount;

  struct vt_buffer_t *buf;
  const void         *owner;

  struct vt_buffer_release_t *release;

  int acquire_fence_fd;
  int release_fence_fd;
};

struct vt_buffer_t *vt_buffer_create(struct vt_compositor_t *comp,
                                     uint32_t width, uint32_t height,
                                     const struct vt_buffer_implementation_t *impl);

struct vt_buffer_t *vt_buffer_ref(struct vt_buffer_t *buf);

void vt_buffer_unref(struct vt_buffer_t **buf);

bool vt_buffer_get_dmabuf(struct vt_buffer_t *buf, struct vt_dmabuf_attr_t * o_attr);

bool vt_buffer_get_shm(struct vt_buffer_t *buf, struct vt_shm_attr_t* o_attr);

void vt_buffer_begin_use(struct vt_buffer_t *buf); 

void vt_buffer_end_use(struct vt_buffer_t *buf); 

struct vt_buffer_attachment_t *
vt_buffer_add_attachment(struct vt_buffer_t *buf, const void *owner, void *data,
                         const struct vt_buffer_attachment_implementation_t *impl);

struct vt_buffer_attachment_t *
vt_buffer_find_attachment(struct vt_buffer_t *buf, const void *owner,
                          const struct vt_buffer_attachment_implementation_t *impl);

void vt_buffer_release_init_and_ref(
    struct vt_buffer_release_t *release, struct vt_compositor_t *comp,
    struct vt_buffer_release_implementation_t *impl);

struct vt_buffer_release_t *
vt_buffer_release_ref(struct vt_buffer_release_t *release);

void vt_buffer_release_unref(struct vt_buffer_release_t **release);

void vt_buffer_release_finish(struct vt_buffer_release_t *release,
                              int                         release_fence_fd);

bool vt_buffer_release_needs_fence(struct vt_buffer_release_t *release);

struct vt_buffer_use_t *vt_buffer_use_ref(struct vt_buffer_use_t *use);

void vt_buffer_use_unref(struct vt_buffer_use_t **use);

struct vt_buffer_use_t *vt_buffer_use_create_take(
    struct vt_compositor_t *comp, const void* owner, struct vt_buffer_t **buf,
    struct vt_buffer_release_t **release, int *acquire_fence_fd);

bool vt_buffer_use_set_release_fence_fd(struct vt_buffer_use_t *use,
                                        int                     release_fd);

