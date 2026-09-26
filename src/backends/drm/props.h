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


bool drm_get_crtc_props(int drm_fd, uint32_t id,
                        uint32_t *o_props);
