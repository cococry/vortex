#pragma once

#include "src/core/core_types.h"
void vt_focus_policy_mapped(struct vt_compositor_t *comp,
                            struct vt_surface_t    *surf);

void vt_focus_policy_unmapped(struct vt_compositor_t *comp,
                              struct vt_surface_t    *surf);
