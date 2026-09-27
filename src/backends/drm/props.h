#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Needs to be sorted alphabetically for bsearch() */
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

enum drm_plane_property_t {
	VT_DRM_PLANE_CRTC_H = 0,
	VT_DRM_PLANE_CRTC_ID,
	VT_DRM_PLANE_CRTC_W,
	VT_DRM_PLANE_CRTC_X,
	VT_DRM_PLANE_CRTC_Y,
	VT_DRM_PLANE_FB_DAMAGE_CLIPS,
	VT_DRM_PLANE_FB_ID,
	VT_DRM_PLANE_IN_FORMATS,
	VT_DRM_PLANE_SRC_H,
	VT_DRM_PLANE_SRC_W,
	VT_DRM_PLANE_SRC_X,
	VT_DRM_PLANE_SRC_Y,
	VT_DRM_PLANE_ROTATION,
	VT_DRM_PLANE_TYPE,
	VT_DRM_PLANE__COUNT
};

bool drm_kms_props_get_crtc(int drm_fd, uint32_t id, uint32_t *o_props);

bool drm_kms_props_get_plane(int drm_fd, uint32_t id, uint32_t *o_props);

bool drm_kms_props_get_prop(int drm_fd, uint32_t id, uint32_t prop,
			    uint64_t *o_prop);
