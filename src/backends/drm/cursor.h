#pragma once

#include <stdbool.h>
#include <stdint.h>

struct vt_drm_cursor_image_t *drm_cursor_image_ref(struct vt_drm_cursor_image_t *img);
void drm_cursor_image_unref(struct vt_drm_cursor_image_t **img_ptr);
