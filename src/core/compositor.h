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
