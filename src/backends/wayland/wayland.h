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

#include "../../core/core_types.h"

bool backend_init_wl(struct vt_backend_t *backend);

bool backend_is_dmabuf_importable_wl(struct vt_backend_t     *backend,
                                     struct vt_dmabuf_attr_t *attr,
                                     int32_t                  device_fd);

bool backend_implement_wl(struct vt_compositor_t *comp);

bool backend_handle_frame_wl(struct vt_backend_t *backend,
                             struct vt_output_t  *output);

bool backend_terminate_wl(struct vt_backend_t *backend);

bool backend_prepare_output_frame_wl(struct vt_backend_t *backend,
                                     struct vt_output_t  *output);
