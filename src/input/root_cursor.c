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

#include "root_cursor.h"
#include "wl_seat.h"

#include "src/core/buffer.h"
#include "src/core/surface.h"
#include "src/core/util.h"

#include <X11/Xcursor/Xcursor.h>
#include <stdlib.h>
#include <wayland-server-protocol.h>

#define _SUBSYS_NAME "CURSOR"

static void _root_cursor_pixels_destroy(struct vt_buffer_t *buf, void *owner,
                                        void *data) {
  (void)buf;
  (void)owner;
  XcursorImageDestroy(data);
}

static const struct vt_buffer_attachment_implementation_t root_pixels_impl = {
    .destroy = _root_cursor_pixels_destroy};

static bool _root_cursor_get_shm(struct vt_buffer_t   *buf,
                                 struct vt_shm_attr_t *attr) {
  struct vt_buffer_attachment_t *pixels =
      vt_buffer_find_attachment(buf, NULL, &root_pixels_impl);
  if (!pixels || !attr)
    return false;

  XcursorImage *image = pixels->data;
  *attr = (struct vt_shm_attr_t){
      .data = image->pixels,
      .size = (size_t)image->width * image->height * sizeof(*image->pixels),
      .width = image->width,
      .height = image->height,
      .stride = image->width * sizeof(*image->pixels),
      .format = WL_SHM_FORMAT_ARGB8888,
  };
  return true;
}

static const struct vt_buffer_implementation_t root_buffer_impl = {
    .get_shm = _root_cursor_get_shm};

bool vt_seat_root_cursor_init(struct vt_seat_t *seat) {
  if (!seat || !seat->comp)
    return false;

  int         size = 24;
  const char *size_env = getenv("XCURSOR_SIZE");
  if (size_env) {
    char *end;
    long  requested = strtol(size_env, &end, 10);
    if (end != size_env && *end == '\0' && requested >= 8 && requested <= 128)
      size = requested;
  }

  const char   *theme = getenv("XCURSOR_THEME");
  XcursorImage *image = XcursorLibraryLoadImage("left_ptr", theme, size);
  if (!image)
    image = XcursorLibraryLoadImage("default", theme, size);
  if (!image) {
    VT_ERROR(seat->comp->log, "Could not load the default Xcursor arrow");
    return false;
  }

  struct vt_surface_t *surf = calloc(1, sizeof(*surf));
  if (!surf) {
    XcursorImageDestroy(image);
    return false;
  }
  surf->comp = seat->comp;
  vt_surface_init(surf);

  struct vt_buffer_t *buf = vt_buffer_create(seat->comp, image->width,
                                             image->height, &root_buffer_impl);
  if (!buf) {
    XcursorImageDestroy(image);
    goto fail;
  }
  if (!vt_buffer_add_attachment(buf, NULL, image, &root_pixels_impl)) {
    XcursorImageDestroy(image);
    vt_buffer_unref(&buf);
    goto fail;
  }

  surf->current_buf_use =
      vt_buffer_use_create_take(seat->comp, surf, &buf, NULL, NULL);
  if (!surf->current_buf_use) {
    vt_buffer_unref(&buf);
    goto fail;
  }

  surf->applied.width = image->width;
  surf->applied.height = image->height;
  pixman_region32_union_rect(&surf->applied.damage, &surf->applied.damage, 0, 0,
                             image->width, image->height);
  surf->mapped =
      true; /* Internal surface: never participates in focus/picking. */

  seat->comp->root_cursor = surf;
  seat->cursor.root_hotspot_x = image->xhot;
  seat->cursor.root_hotspot_y = image->yhot;
  return true;

fail:
  vt_surface_pending_state_fini(&surf->pending);
  vt_surface_applied_state_fini(&surf->applied);
  free(surf);
  return false;
}

void vt_seat_root_cursor_finish(struct vt_seat_t *seat) {
  if (!seat || !seat->comp || !seat->comp->root_cursor)
    return;

  struct vt_surface_t *surf = seat->comp->root_cursor;
  seat->comp->root_cursor = NULL;
  vt_buffer_use_unref(&surf->current_buf_use);
  vt_surface_pending_state_fini(&surf->pending);
  vt_surface_applied_state_fini(&surf->applied);
  free(surf);
}
