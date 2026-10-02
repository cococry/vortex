#include "output.h"

#include "core_types.h"

bool vt_output_track_presented_surface(struct vt_output_t  *output,
                                       struct vt_surface_t *surface) {
  assert(output && surface);

  struct vt_presented_surface_t *entry = calloc(1, sizeof(*entry));
  if (!entry)
    return false;

  entry->surf = surface;

  wl_list_insert(output->presented_surfaces.prev, &entry->link);

  return true;
}
