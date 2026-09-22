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
#include "surface.h"

bool vt_comp_init(struct vt_compositor_t *c, int argc, char **argv);

void vt_comp_run(struct vt_compositor_t *c);

bool vt_comp_terminate(struct vt_compositor_t *c);

void vt_comp_frame_done(struct vt_compositor_t *c, struct vt_output_t *output,
                        uint32_t t);

void vt_comp_frame_done_all(struct vt_compositor_t *c, uint32_t t);

void vt_comp_schedule_repaint(struct vt_compositor_t *c,
                              struct vt_output_t     *output);

void vt_comp_repaint_scene(struct vt_compositor_t *c,
                           struct vt_output_t     *output);

struct vt_surface_t *vt_comp_pick_surface(struct vt_compositor_t *comp,
                                          double x, double y);
