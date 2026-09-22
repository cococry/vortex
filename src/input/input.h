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
#include <xkbcommon/xkbcommon.h>

#include "../core/core_types.h"

enum vt_input_backend_platform_t {
  VT_INPUT_LIBINPUT = 0,
  VT_INPUT_WAYLAND,
  VT_INPUT_UNKNOWN
};

enum vt_input_key_state_t {
  VT_KEY_STATE_RELEASED = 0,
  VT_KEY_STATE_PRESSED = 1,
  VT_KEY_STATE_INVALID = 2
};

enum vt_input_pointer_state_t {
  VT_POINTER_STATE_RELEASED = 0,
  VT_POINTER_STATE_PRESSED = 1,
  VT_POINTER_STATE_INVALID = 2
};

struct vt_kb_modifiers_t {
  uint32_t shift;
  uint32_t ctrl;
  uint32_t alt;
  uint32_t super;
  uint32_t caps;
};

struct vt_input_backend_t;

struct vt_input_backend_interface_t {
  bool (*init)(struct vt_input_backend_t *backend, void *native_handle);
  bool (*terminate)(struct vt_input_backend_t *backend);
  bool (*suspend)(struct vt_input_backend_t *backend);
  bool (*resume)(struct vt_input_backend_t *backend);
};

struct vt_input_backend_t {
  enum vt_input_backend_platform_t    platform;
  struct vt_input_backend_interface_t impl;

  struct vt_compositor_t *comp;
  void                   *user_data;

  struct vt_kb_modifiers_t mods;
  struct xkb_context      *kb_context;
  struct xkb_keymap       *keymap;
  struct xkb_state        *kb_state;
};

void vt_input_implement(struct vt_input_backend_t       *backend,
                        enum vt_input_backend_platform_t platform);
