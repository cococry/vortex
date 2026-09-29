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

#include "drm_format.h"
#include <assert.h>
#include <wayland-util.h>

void vt_drm_format_init(struct vt_drm_format_t *fmt, uint32_t format) {
  assert(fmt);

  wl_array_init(&fmt->mods);
  fmt->format = format;
}

struct vt_drm_format_modifier_t *
vt_drm_format_get_mod(struct vt_drm_format_t *fmt, uint64_t mod) {
  /* 1. Validate input */
  if (!fmt)
    return NULL;

  /* 2. Search for matching modifier */
  struct vt_drm_format_modifier_t *it;
  wl_array_for_each(it, &fmt->mods) {
    if (it->mod == mod)
      return it;
  }
  return NULL;
}

struct vt_drm_format_modifier_t *
vt_drm_format_add_mod(struct vt_drm_format_t *fmt, uint64_t mod) {
  assert(fmt);

  struct vt_drm_format_modifier_t *present = vt_drm_format_get_mod(fmt, mod);
  if (present)
    return present;

  struct vt_drm_format_modifier_t *add = wl_array_add(&fmt->mods, sizeof(*add));
  if (!add)
    return NULL;

  add->mod = mod;
  add->_egl_ext_only = false;

  return add;
}

void vt_drm_format_fini(struct vt_drm_format_t *fmt) {
  assert(fmt);

  wl_array_release(&fmt->mods);
}

bool vt_drm_format_add_mods(struct vt_drm_format_t *fmt,
                            struct wl_array        *mods) {
  assert(fmt && mods);

  struct vt_drm_format_modifier_t *it;
  wl_array_for_each(it, mods) {
    struct vt_drm_format_modifier_t *mod = vt_drm_format_add_mod(fmt, it->mod);
    if (!mod)
      return false;
    mod->_egl_ext_only = it->_egl_ext_only;
  }

  return true;
}

size_t vt_drm_format_mod_count(const struct vt_drm_format_t *fmt) {
  assert(fmt);

  return fmt->mods.size / sizeof(struct vt_drm_format_modifier_t);
}

size_t vt_drm_format_array_count(const struct wl_array *formats) {
  assert(formats);

  return formats->size / sizeof(struct vt_drm_format_t);
}

struct vt_drm_format_t *vt_drm_format_array_push(struct wl_array *formats,
                                                 struct vt_drm_format_t *fmt) {
  assert(formats && fmt);

  struct vt_drm_format_t *present =
      vt_drm_format_array_get_format(formats, fmt);
  if (present)
    return present;

  struct vt_drm_format_t *add = wl_array_add(formats, sizeof(*add));
  if (!add)
    return NULL;

  vt_drm_format_init(add, fmt->format);

  /* deep copy */
  if (!vt_drm_format_add_mods(add, &fmt->mods)) {
    wl_array_release(&add->mods);
    formats->size -= sizeof(*add);
    return NULL;
  }

  return add;
}
struct vt_drm_format_t *vt_drm_format_array_push_pair(struct wl_array *formats,
                                                      uint32_t         format,
                                                      uint64_t         mod) {
  assert(formats);

  struct vt_drm_format_t fmt = {0};
  vt_drm_format_init(&fmt, format);

  if(!vt_drm_format_add_mod(&fmt, mod)) {
    vt_drm_format_fini(&fmt);
    return NULL;
  }

  struct vt_drm_format_t *pushed = vt_drm_format_array_push(formats, &fmt);

  vt_drm_format_fini(&fmt);

  return pushed; 
}

struct vt_drm_format_t *
vt_drm_format_array_get_format(struct wl_array              *formats,
                               const struct vt_drm_format_t *find) {

  assert(formats);

  if (!find)
    return false;

  const size_t n_mods_find = vt_drm_format_mod_count(find);

  struct vt_drm_format_t *fmt;
  wl_array_for_each(fmt, formats) {
    if (fmt->format != find->format ||
        vt_drm_format_mod_count(fmt) != n_mods_find)
      continue;

    bool                             match = true;
    struct vt_drm_format_modifier_t *it;
    wl_array_for_each(it, &find->mods) {
      if (!vt_drm_format_get_mod(fmt, it->mod)) {
        match = false;
        break;
      }
    }
    if (match) {
      return fmt;
    }
  }
  return NULL;
}

void vt_drm_format_array_free(struct wl_array *formats) {
  /* 1. Validate input parameter */
  if (!formats)
    return;

  /* 2. Free modifiers in each stored format */
  struct vt_drm_format_t *fmt;
  wl_array_for_each(fmt, formats) { wl_array_release(&fmt->mods); }

  /* 3. Release the formats array */
  wl_array_release(formats);
}
