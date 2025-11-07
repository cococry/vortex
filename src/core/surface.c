#include "surface.h"
#include "../input/wl_seat.h"
#include "src/core/util.h"
#include <wayland-server-protocol.h>

void 
vt_surface_mapped(struct vt_surface_t* surf) {
  if(!surf) {
    VT_WARN(surf->comp->log, "SURFACE: Trying to map NULL surface.");
  }
  struct vt_seat_t* seat = surf->comp->seat; 

  vt_seat_set_keyboard_focus(seat, surf);
}

void
vt_surface_unmapped(struct vt_surface_t* surf) {
}
