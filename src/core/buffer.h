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
#include <runara/runara.h>

#include "core_types.h"
#include "pixman.h"
#include "../render/renderer.h"


struct vt_buffer_t {
  struct wl_resource *res;
  struct wl_listener  destroy;
  bool                destroy_linked;

  RnTexture tex;
  void     *render_tex_handle;

  uint32_t refcount;
  uint32_t uses;

  struct vt_renderer_t* renderer;
};

struct vt_linux_explicit_sync_v1_buffer_release_t {
  struct wl_resource* res;
  struct vt_buffer_release_t* release;
};

struct vt_buffer_release_t {
  uint32_t refcount;

  struct wl_list callbacks;

  struct vt_linux_explicit_sync_v1_buffer_release_t *explicit;

  int fence_fd;

  struct vt_renderer_t* renderer;
};

struct vt_buffer_use_t {
  uint32_t refcount;

  struct vt_buffer_t *buf;

  struct vt_buffer_release_t *release;

  int acquire_fence_fd;

  bool release_sent;

  int release_fence_fd;
  
  struct vt_renderer_t* renderer;
};

bool vt_buffer_import(struct vt_buffer_t *buf, const pixman_region32_t *damage);

struct vt_buffer_t* vt_buffer_get_or_create_from_resource(struct vt_renderer_t* renderer, struct wl_resource* res);

struct vt_buffer_t* vt_buffer_ref(struct vt_buffer_t *buf);

void vt_buffer_unref(struct vt_buffer_t **buf);

void vt_buffer_start_use(struct vt_buffer_t *buf);

void vt_buffer_end_use(struct vt_buffer_t *buf);

struct vt_buffer_release_t *vt_buffer_release_ref(struct vt_buffer_release_t *release);

void vt_buffer_release_unref(struct vt_buffer_release_t **release);

struct vt_buffer_use_t *vt_buffer_use_ref(struct vt_buffer_use_t *use);

void vt_buffer_use_unref(struct vt_buffer_use_t **use);

struct vt_buffer_use_t *vt_buffer_use_create_take(
    struct vt_renderer_t *renderer, struct vt_buffer_t **buf,
    struct vt_buffer_release_t **release, int *acquire_fence_fd);

bool vt_buffer_use_set_release_fence_fd(struct vt_buffer_use_t *use,
                                        int                     release_fd);
