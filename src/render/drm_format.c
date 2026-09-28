#include "drm_format.h"

bool vt_drm_format_has_mod(struct vt_drm_format_t *fmt, uint64_t mod) {
  /* 1. Validate input */
  if (!fmt)
    return false;

  /* 2. Search for matching modifier */
  for (size_t i = 0; i < fmt->len; i++) {
    if (fmt->mods[i].mod == mod)
      return true;
  }

  return false;
}

void vt_drm_format_free_array(struct wl_array *formats) {
  /* 1. Validate input parameter */
  if (!formats)
    return;

  /* 2. Free modifiers in each stored format */
  struct vt_drm_format_t *fmt;
  wl_array_for_each(fmt, formats) {
    free(fmt->mods);
    fmt->mods = NULL;
  }

  /* 3. Release the formats array */
  wl_array_release(formats);
}
