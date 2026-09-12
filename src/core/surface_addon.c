#include "surface_addon.h"

void vt_surface_addon_destroy(struct vt_surface_addon_t *addon) {
  if (!addon || !addon->impl.destroy)
    return;

  addon->impl.destroy(addon);
}
