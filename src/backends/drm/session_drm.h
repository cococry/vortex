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

#include <libinput.h>
#include <libseat.h>
#include <libudev.h>
#include <wayland-server-core.h>

#include "../../core/session.h"

struct vt_session_drm_t {
  struct libseat      *seat;
  struct udev         *udev;
  char                 seat_name[64];
  struct udev_monitor *udev_mon;

  struct wl_signal ev_drm_add_card, ev_drm_change_card, ev_drm_remove_card;

  struct wl_signal ev_seat_enable, ev_seat_disable;
  bool             _first_libseat_enable;

  struct wl_list devices;
};

struct vt_session_drm_event_t {
  const char *device_node_name;
};

bool vt_session_init_drm(struct vt_session_t *session);

bool vt_session_terminate_drm(struct vt_session_t *session);

bool vt_session_open_device_drm(struct vt_session_t *session,
                                struct vt_device_t *dev, const char *path);

bool vt_session_manage_device_drm(struct vt_session_t *session,
                                  struct vt_device_t  *dev);

bool vt_session_close_device_drm(struct vt_session_t *session,
                                 struct vt_device_t  *dev);

bool vt_session_unmanage_device_drm(struct vt_session_t *session,
                                    struct vt_device_t  *dev);

void *vt_session_get_native_handle_drm(struct vt_session_t *session,
                                       struct vt_device_t  *dev);

bool vt_session_finish_native_handle_drm(struct vt_session_t *session,
                                         void                *handle);

const char *
vt_session_get_native_handle_render_node(struct vt_session_t *session,
                                         void                *handle);

// =======================
// ====== DRM ONLY =======
// =======================
struct vt_device_t *vt_session_device_from_fd_drm(struct vt_session_t *session,
                                                  uint32_t             fd);

uint32_t vt_session_enumerate_cards_drm(struct vt_session_t *session,
                                        struct vt_device_t **devs,
                                        const uint32_t       max_devs);

bool vt_session_switch_vt_drm(struct vt_session_t *session, uint32_t vt);
