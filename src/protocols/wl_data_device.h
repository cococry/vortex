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

#include "../core/core_types.h"

#include "../input/wl_seat.h"

struct vt_data_offer_t {
  struct wl_resource                    *resource;
  struct vt_data_source_t               *source;
  struct wl_listener                     source_destroy_listener;
  uint32_t                               dnd_actions;
  enum wl_data_device_manager_dnd_action preferred_dnd_action;
  bool                                   in_ask;
};

struct vt_data_source_t {
  struct wl_resource                    *resource;
  struct wl_signal                       destroy_signal;
  struct wl_array                        mime_types;
  struct vt_data_offer_t                *offer;
  struct vt_seat_t                      *seat;
  bool                                   accepted;
  bool                                   actions_set;
  bool                                   set_selection;
  uint32_t                               dnd_actions;
  enum wl_data_device_manager_dnd_action current_dnd_action;
  enum wl_data_device_manager_dnd_action compositor_action;

  void (*accept)(struct vt_data_source_t *source, uint32_t serial,
                 const char *mime_type);
  void (*send)(struct vt_data_source_t *source, const char *mime_type,
               int32_t fd);
  void (*cancel)(struct vt_data_source_t *source);
};

bool vt_proto_wl_data_device_init(struct vt_compositor_t *comp);
