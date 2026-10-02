#include "dmabuf.h"
#include "src/render/drm_format.h"
#define _SUBSYS_NAME "DMABUF"

struct vt_dmabuf_feedback_t *
vt_dmabuf_feedback_create(struct vt_compositor_t *comp,
                          struct vt_device_t     *dev) {

  assert(comp);

  struct vt_dmabuf_feedback_t *feedback = calloc(1, sizeof(*feedback));

  if (!feedback) {
    VT_ERROR(comp->log, "Out of memory.");
    return NULL;
  }

  feedback->comp = comp;
  feedback->dev_main = dev;

  wl_array_init(&feedback->tranches);

  return feedback;
}

void vt_dmabuf_feedback_fini(struct vt_dmabuf_feedback_t *feedback) {
  assert(feedback);

  if (feedback->tranches.size > 0) {
    struct vt_dmabuf_tranche_t *tranche;
    wl_array_for_each(tranche, &feedback->tranches) {
      vt_drm_format_array_free(&tranche->formats);
    }
  }

  wl_array_release(&feedback->tranches);

  free(feedback);
}

struct vt_dmabuf_tranche_t *
vt_dmabuf_feedback_add_tranche(struct vt_dmabuf_feedback_t   *feedback,
                               struct vt_device_t            *target_device,
                               enum vt_dmabuf_tranche_flags_t flags) {
  assert(feedback && target_device);
  struct vt_dmabuf_tranche_t *tranche =
      wl_array_add(&feedback->tranches, sizeof(*tranche));
  if (!tranche)
    return NULL;

  memset(tranche, 0, sizeof(*tranche));

  tranche->target_device = target_device;
  tranche->flags = flags;

  wl_array_init(&tranche->formats);

  return tranche;
}
