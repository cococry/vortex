#include "surface.h"
#include "../input/wl_seat.h"
#include "src/core/compositor.h"
#include "src/core/util.h"
#include "src/protocols/xdg_shell.h"
#include <wayland-server-protocol.h>
#include <wayland-util.h>

void 
vt_surface_mapped(struct vt_surface_t* surf) {
  if(!surf) {
    VT_WARN(surf->comp->log, "SURFACE: Trying to map NULL surface.");
    return;
  }
  struct vt_seat_t* seat = surf->comp->seat; 


  surf->mapped = true;

  struct vt_surface_t* under_cursor = vt_comp_pick_surface(surf->comp, seat->pointer_x, seat->pointer_y);

  if(surf != seat->kb_focus.surf)
    vt_seat_send_keyboard_leave(surf->comp->seat);

  vt_seat_set_keyboard_focus(seat, surf);
  if(under_cursor) {
    if(under_cursor != seat->ptr_focus.surf)
      vt_seat_send_pointer_leave(seat);
    vt_seat_set_pointer_focus(seat, under_cursor, seat->pointer_x, seat->pointer_y);
  } 

}

void
vt_surface_unmapped(struct vt_surface_t* surf) {
  if(!surf || !surf->comp || !surf->comp->seat) return;

  struct vt_seat_t* seat = surf->comp->seat; 
  
  surf->mapped = false;


  struct vt_surface_t* new_focus  = NULL;
  // If surface had parent, revert to parent 
  if(surf->xdg_surf && surf->xdg_surf->toplevel && surf->xdg_surf->toplevel->parent) {
    struct vt_xdg_toplevel_t* parent_surface = surf->xdg_surf->toplevel->parent; 
    if(!parent_surface || !parent_surface->xdg_surf) {
      VT_ERROR(surf->comp->log, "SURFACE: Trying to revert focus to invalid parent.");
      return;
    }
    
    new_focus = parent_surface->xdg_surf->surf;
  } else {
    // Revert focus to surface that is under the cursor
    new_focus = vt_comp_pick_surface(surf->comp, seat->pointer_x, seat->pointer_y);
  }

  vt_seat_set_pointer_focus(seat, new_focus, seat->pointer_x, seat->pointer_y);
  vt_seat_set_keyboard_focus(seat, new_focus);


}
