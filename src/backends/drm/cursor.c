#include "cursor.h"
#include "fb.h"

static void _drm_cursor_image_finish(struct vt_drm_backend_state_t *drm,
                                     struct vt_drm_cursor_image_t  *image) {
  if (!drm || !image)
    return;

  if (image->has_fb) {
    drm_fb_finish(drm, &image->fb);
    image->has_fb = false;
  }

  if (image->bo) {
    gbm_bo_destroy(image->bo);
    image->bo = NULL;
  }

  if (image->use)
    vt_buffer_use_unref(&image->use);

  image->width = 0;
  image->height = 0;

  free(image);
}

struct vt_drm_cursor_image_t *
drm_cursor_image_ref(struct vt_drm_cursor_image_t *img) {
  assert(img);

  img->refcount++;

  return img;
}

void drm_cursor_image_unref(struct vt_drm_cursor_image_t **img_ptr) {

  assert(img_ptr && *img_ptr);

  struct vt_drm_cursor_image_t *img = *img_ptr;
  assert(img->drm);

  *img_ptr = NULL;

  assert(img->refcount > 0);

  if (--img->refcount != 0)
    return;

  _drm_cursor_image_finish(img->drm, img);
}
