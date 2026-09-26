#include "props.h"
#include <xf86drmMode.h>
#include <string.h>
#include <stdlib.h>


/* Needs to be sorted alphabetically for bsearch() */
static const char *crtc_infos[VT_DRM_CRTC__COUNT] = {
    [VT_DRM_CRTC_ACTIVE] = "ACTIVE",
    [VT_DRM_CRTC_BACKGROUND_COLOR] = "BACKGROUND_COLOR",
    [VT_DRM_CRTC_CTM] = "CTM",
    [VT_DRM_CRTC_DEGAMMA_LUT] = "DEGAMMA_LUT",
    [VT_DRM_CRTC_DEGAMMA_LUT_SIZE] = "DEGAMMA_LUT_SIZE",
    [VT_DRM_CRTC_GAMMA_LUT] = "GAMMA_LUT",
    [VT_DRM_CRTC_GAMMA_LUT_SIZE] = "GAMMA_LUT_SIZE",
    [VT_DRM_CRTC_MODE_ID] = "MODE_ID",
    [VT_DRM_CRTC_OUT_FENCE_PTR] = "OUT_FENCE_PTR",
    [VT_DRM_CRTC_VRR_ENABLED] = "VRR_ENABLED",
};

static int compare_prop_name(const void *key, const void *elem) {
  const char        *name = key;
  const char *const *entry = elem;

  return strcmp(name, *entry);
}

static bool _drm_get_props(int drm_fd, uint32_t id, uint32_t type,
                           const char *names[], size_t names_len,
                           uint32_t *out) {
  memset(out, 0, names_len * sizeof(*out));

  drmModeObjectProperties *props = drmModeObjectGetProperties(drm_fd, id, type);
  if (props == NULL) {
    return false;
  }

  for (uint32_t i = 0; i < props->count_props; ++i) {
    drmModePropertyRes *prop = drmModeGetProperty(drm_fd, props->props[i]);
    if (prop == NULL) {
      continue;
    }

    const char *const *match = bsearch(prop->name, names, names_len,
                                       sizeof(names[0]), compare_prop_name);

    if (match != NULL) {
      size_t index = (size_t)(match - names);
      out[index] = prop->prop_id;
    }

    drmModeFreeProperty(prop);
  }

  drmModeFreeObjectProperties(props);
  return true;
}

bool drm_get_crtc_props(int drm_fd, uint32_t id, uint32_t *o_props) {

  return _drm_get_props(drm_fd, id, DRM_MODE_OBJECT_CRTC, crtc_infos,
                        VT_DRM_CRTC__COUNT, o_props);
}
