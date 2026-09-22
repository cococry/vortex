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

#include "focus_policy.h"
#include "src/core/surface.h"
#include "src/protocols/xdg_shell.h"

static void _focus_stack_add(struct vt_compositor_t *comp,
                              struct vt_surface_t    *surf);
static void _focus_stack_remove(struct vt_compositor_t *comp,
                             struct vt_surface_t    *surf);

static void _focus_stack_add(struct vt_compositor_t *comp,
                              struct vt_surface_t    *surf) {
  if (!comp || !surf)
    return;

  _focus_stack_remove(comp, surf);

  wl_list_insert(&comp->focus_stack, &surf->link_focus);
}

static void _focus_stack_remove(struct vt_compositor_t *comp,
                             struct vt_surface_t    *surf) {
  if (!comp || !surf)
    return;

  if (wl_list_empty(&surf->link_focus))
    return;

  wl_list_remove(&surf->link_focus);
  wl_list_init(&surf->link_focus);
}

struct vt_surface_t *_focus_stack_pop(struct vt_compositor_t *comp) {
  if (!comp || !comp->seat)
    return NULL;

  struct wl_list *stack = &comp->focus_stack;

  if (wl_list_empty(stack))
    return NULL;

  struct vt_surface_t *surf = wl_container_of(stack->next, surf, link_focus);

  return surf;
}

void vt_focus_policy_mapped(struct vt_compositor_t *comp,
                            struct vt_surface_t    *surf) {
  if(!comp || !surf || !comp->seat) return;

  if(!surf->mapped) return;

  if(vt_surface_has_role(surf, VT_SURFACE_ROLE_XDG_TOPLEVEL)) {
    _focus_stack_add(comp, surf);
    vt_seat_set_keyboard_focus(comp->seat, surf);

    return;
  }
}

void vt_focus_policy_unmapped(struct vt_compositor_t *comp,
                              struct vt_surface_t *surf) {
  if (!comp || !surf || !comp->seat)
    return;

  struct vt_seat_t *seat = comp->seat;

  _focus_stack_remove(comp, surf);

  if (seat->kb_focus.surf != surf)
    return;

  struct vt_surface_t *new_focus = NULL;

  if (vt_surface_has_role(surf, VT_SURFACE_ROLE_XDG_TOPLEVEL)) {
    struct vt_xdg_surface_t *xdg_surf = surf->role.data;

    if (xdg_surf &&
        xdg_surf->toplevel &&
        xdg_surf->toplevel->parent) {

      struct vt_xdg_toplevel_t *parent =
          xdg_surf->toplevel->parent;

      if (parent->xdg_surf &&
          parent->xdg_surf->surf &&
          vt_surface_effectively_mapped(parent->xdg_surf->surf)) {
        new_focus = parent->xdg_surf->surf;
      }
    }
  }

  else if (vt_surface_has_role(surf, VT_SURFACE_ROLE_XDG_POPUP)) {
    struct vt_xdg_surface_t *xdg_surf = surf->role.data;

    if (xdg_surf &&
        xdg_surf->popup &&
        xdg_surf->popup->parent_xdg_surf) {

      struct vt_xdg_surface_t *parent =
          xdg_surf->popup->parent_xdg_surf;

      if (parent->surf &&
          vt_surface_effectively_mapped(parent->surf)) {
        new_focus = parent->surf;
      }
    }
  }

  if (!new_focus)
    new_focus = _focus_stack_pop(comp);

  vt_seat_set_keyboard_focus(seat, new_focus);
}
