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

#include "props.h"
#include <stdlib.h>
#include <string.h>
#include <xf86drmMode.h>

/* Needs to be sorted alphabetically for bsearch() */
static const char *connector_infos[VT_DRM_CONNECTOR__COUNT] = {
    [VT_DRM_CONNECTOR_CRTC_ID] = "CRTC_ID",
};

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

/* Needs to be sorted according to strcmp() for bsearch() */
static const char *plane_infos[VT_DRM_PLANE__COUNT] = {
    [VT_DRM_PLANE_CRTC_H] = "CRTC_H",
    [VT_DRM_PLANE_CRTC_ID] = "CRTC_ID",
    [VT_DRM_PLANE_CRTC_W] = "CRTC_W",
    [VT_DRM_PLANE_CRTC_X] = "CRTC_X",
    [VT_DRM_PLANE_CRTC_Y] = "CRTC_Y",
    [VT_DRM_PLANE_FB_DAMAGE_CLIPS] = "FB_DAMAGE_CLIPS",
    [VT_DRM_PLANE_FB_ID] = "FB_ID",
    [VT_DRM_PLANE_IN_FENCE_FD] = "IN_FENCE_FD",
    [VT_DRM_PLANE_IN_FORMATS] = "IN_FORMATS",
    [VT_DRM_PLANE_SIZE_HINTS] = "SIZE_HINTS",
    [VT_DRM_PLANE_SRC_H] = "SRC_H",
    [VT_DRM_PLANE_SRC_W] = "SRC_W",
    [VT_DRM_PLANE_SRC_X] = "SRC_X",
    [VT_DRM_PLANE_SRC_Y] = "SRC_Y",
    [VT_DRM_PLANE_ROTATION] = "rotation",
    [VT_DRM_PLANE_TYPE] = "type",
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

bool drm_kms_props_get_connector(int drm_fd, uint32_t id, uint32_t *o_props) {
  return _drm_get_props(drm_fd, id, DRM_MODE_OBJECT_CONNECTOR, connector_infos,
                        VT_DRM_CONNECTOR__COUNT, o_props);
}

bool drm_kms_props_get_crtc(int drm_fd, uint32_t id, uint32_t *o_props) {
  return _drm_get_props(drm_fd, id, DRM_MODE_OBJECT_CRTC, crtc_infos,
                        VT_DRM_CRTC__COUNT, o_props);
}

bool drm_kms_props_get_plane(int drm_fd, uint32_t id, uint32_t *o_props) {
  return _drm_get_props(drm_fd, id, DRM_MODE_OBJECT_PLANE, plane_infos,
                        VT_DRM_PLANE__COUNT, o_props);
}

bool drm_kms_props_get_prop(int drm_fd, uint32_t id, uint32_t prop,
                            uint64_t *o_prop) {
  if (prop == 0 || o_prop == NULL) {
    return false;
  }

  drmModeObjectProperties *props =
      drmModeObjectGetProperties(drm_fd, id, DRM_MODE_OBJECT_ANY);

  if (props == NULL) {
    return false;
  }

  for (uint32_t i = 0; i < props->count_props; ++i) {
    if (props->props[i] == prop) {
      *o_prop = props->prop_values[i];
      drmModeFreeObjectProperties(props);
      return true;
    }
  }

  drmModeFreeObjectProperties(props);
  return false;
}
