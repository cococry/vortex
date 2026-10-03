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

#include <stdbool.h>
#include <stdint.h>

/* Needs to be sorted according to strcmp() for bsearch() */
enum drm_connector_property_t {
  VT_DRM_CONNECTOR_CRTC_ID = 0,
  VT_DRM_CONNECTOR__COUNT
};

/* Needs to be sorted according to strcmp() for bsearch() */
enum drm_crtc_property_t {
  VT_DRM_CRTC_ACTIVE = 0,
  VT_DRM_CRTC_BACKGROUND_COLOR,
  VT_DRM_CRTC_CTM,
  VT_DRM_CRTC_DEGAMMA_LUT,
  VT_DRM_CRTC_DEGAMMA_LUT_SIZE,
  VT_DRM_CRTC_GAMMA_LUT,
  VT_DRM_CRTC_GAMMA_LUT_SIZE,
  VT_DRM_CRTC_MODE_ID,
  VT_DRM_CRTC_OUT_FENCE_PTR,
  VT_DRM_CRTC_VRR_ENABLED,
  VT_DRM_CRTC__COUNT
};

/* Needs to be sorted according to strcmp() for bsearch() */
enum drm_plane_property_t {
  VT_DRM_PLANE_CRTC_H = 0,
  VT_DRM_PLANE_CRTC_ID,
  VT_DRM_PLANE_CRTC_W,
  VT_DRM_PLANE_CRTC_X,
  VT_DRM_PLANE_CRTC_Y,
  VT_DRM_PLANE_FB_DAMAGE_CLIPS,
  VT_DRM_PLANE_FB_ID,
  VT_DRM_PLANE_IN_FENCE_FD,
  VT_DRM_PLANE_IN_FORMATS,
  VT_DRM_PLANE_SIZE_HINTS,
  VT_DRM_PLANE_SRC_H,
  VT_DRM_PLANE_SRC_W,
  VT_DRM_PLANE_SRC_X,
  VT_DRM_PLANE_SRC_Y,
  VT_DRM_PLANE_ROTATION,
  VT_DRM_PLANE_TYPE,
  VT_DRM_PLANE__COUNT
};

bool drm_kms_props_get_connector(int drm_fd, uint32_t id, uint32_t *o_props);

bool drm_kms_props_get_crtc(int drm_fd, uint32_t id, uint32_t *o_props);

bool drm_kms_props_get_plane(int drm_fd, uint32_t id, uint32_t *o_props);

bool drm_kms_props_get_prop(int drm_fd, uint32_t id, uint32_t prop,
                            uint64_t *o_prop);
