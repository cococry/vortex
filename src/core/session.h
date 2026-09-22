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

#include "core_types.h"
#include <sys/types.h>

#include <wayland-server-core.h>

struct vt_session_t;

struct vt_device_t {
  int32_t        fd, device_id;
  __dev_t        dev;
  struct wl_list link;
  char           path[64];
};

struct vt_session_interface_t {
  bool (*init)(struct vt_session_t *session);
  bool (*terminate)(struct vt_session_t *session);
  bool (*open_device)(struct vt_session_t *session, struct vt_device_t *dev,
                      const char *path);
  bool (*manage_device)(struct vt_session_t *session, struct vt_device_t *dev);
  bool (*close_device)(struct vt_session_t *session, struct vt_device_t *dev);
  bool (*unmanage_device)(struct vt_session_t *session,
                          struct vt_device_t  *dev);
  void *(*get_native_handle)(struct vt_session_t *session,
                             struct vt_device_t  *dev);
  bool (*finish_native_handle)(struct vt_session_t *session, void *handle);
  const char *(*get_native_handle_render_node)(struct vt_session_t *session,
                                               void                *handle);
  struct vt_device_t *(*device_from_fd)(struct vt_session_t *session,
                                        uint32_t             fd);
};

struct vt_session_t {
  struct vt_compositor_t *comp;

  struct wl_signal ev_session_terminate;

  bool active;

  struct vt_session_interface_t impl;

  void *user_data, *native_handle;
  char  name[64];
};
