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

#include <stdbool.h>
#include <stdint.h>
#include <wayland-util.h>

struct vt_drm_format_modifier_t {
  uint64_t mod;
  bool     _egl_ext_only;
};

struct vt_drm_format_t {
  uint32_t        format;
  struct wl_array mods; /* struct vt_drm_format_modifier_t */
};

void vt_drm_format_init(struct vt_drm_format_t* fmt, uint32_t format);

struct vt_drm_format_modifier_t* vt_drm_format_get_mod(struct vt_drm_format_t *fmt, uint64_t mod);

struct vt_drm_format_modifier_t* vt_drm_format_add_mod(struct vt_drm_format_t *fmt, uint64_t mod);

void vt_drm_format_fini(struct vt_drm_format_t *fmt);

bool vt_drm_format_add_mods(struct vt_drm_format_t *fmt, struct wl_array *mods);

size_t vt_drm_format_mod_count(const struct vt_drm_format_t *fmt);

size_t vt_drm_format_array_count(const struct wl_array *formats);

struct vt_drm_format_t *vt_drm_format_array_push(struct wl_array *formats,
                                                 const struct vt_drm_format_t* fmt);

struct vt_drm_format_t *vt_drm_format_array_push_pair(struct wl_array *formats,
                                                      uint32_t         format,
                                                      uint64_t         mod);

struct vt_drm_format_t *
vt_drm_format_array_get_format(struct wl_array              *formats,
                               const struct vt_drm_format_t *find);

void vt_drm_format_array_free(struct wl_array *formats);

bool vt_drm_format_array_intersect(struct wl_array       *dst,
                                   const struct wl_array *a,
                                   const struct wl_array *b);

bool vt_drm_format_array_copy(struct wl_array       *dst,
                              const struct wl_array *src);
