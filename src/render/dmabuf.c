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
