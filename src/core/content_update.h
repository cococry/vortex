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

#include "surface.h"

enum vt_content_update_type_t {
  VT_CU_SYNC = 0,
  VT_CU_DESYNC,
};

struct vt_content_update_t {
  struct vt_surface_t *surf;

  struct vt_surface_state_pending_t state;

  enum vt_content_update_type_t type;

  struct wl_list queue_link;
  struct wl_list retire_link;
  struct wl_list constraints;
  struct wl_list dependants;
  struct wl_list dependencies;

  bool applied, queued, prepared;

  struct vt_buffer_use_t *buffer_use;
  int                     acquire_fence_fd;
};

/* Holds a content update back until resolved, e.g. while a client's GPU work
 * for the attached buffer has not been submitted yet. destroy runs if the
 * update dies first. */
struct vt_content_update_constraint_t {
  struct wl_list link;
  void (*destroy)(struct vt_content_update_constraint_t *constraint);
};

struct vt_content_update_dependency_t {
  struct vt_content_update_t *cu;
  struct vt_content_update_t *dependency;

  struct wl_list dependant_link;
  struct wl_list dependency_link;
};

struct vt_content_update_t *
vt_content_update_create(struct vt_surface_t               *surf,
                         struct vt_surface_state_pending_t *state,
                         enum vt_content_update_type_t      type);

bool vt_content_update_finish_create(struct vt_content_update_t *cu);

void vt_content_update_destroy(struct vt_content_update_t *cu);

bool vt_content_update_add_dependency(struct vt_content_update_t *cu,
                                      struct vt_content_update_t *dependency);

bool vt_content_update_apply_dag(struct vt_content_update_t *root);

// true while cu or an update queued ahead of it is held by a constraint
bool vt_content_update_waits(const struct vt_content_update_t *cu);

void vt_content_update_add_constraint(
    struct vt_content_update_t *cu, struct vt_content_update_constraint_t *c);

/* Detaches the constraint and applies whatever became ready. cu may be
 * destroyed by the time this returns. */
void vt_content_update_resolve_constraint(
    struct vt_content_update_t *cu, struct vt_content_update_constraint_t *c);

bool vt_content_update_reaches(struct vt_content_update_t *from,
                               struct vt_content_update_t *target);
