#include "surface.h"
#include "../input/wl_seat.h"
#include <wayland-server-protocol.h>

void 
vt_surface_mapped(struct vt_surface_t* surf) {
  if (surf->comp->seat) {
    vt_seat_set_keyboard_focus(surf->comp->seat, surf);
  }
}

void
vt_surface_unmapped(struct vt_surface_t* surf) {
  struct vt_seat_t* seat = surf->comp->seat; 
  if (seat && seat->focused.surf == surf) {
    wl_keyboard_send_leave(seat->focused.keyboard->resource, seat->serial++, surf->surf_res);
    seat->focused.surf = NULL;
  }
}
