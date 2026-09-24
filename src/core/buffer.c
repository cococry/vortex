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

#include "buffer.h"
#include "src/core/core_types.h"
#include "src/core/util.h"
#include "src/render/dmabuf_attr.h"
#include "src/render/renderer.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-protocol.h>

#include <assert.h>
#include <stdlib.h>
#include <wayland-util.h>

#define _SUBSYS_NAME "BUFFERS"

static void _buffer_destroy(struct vt_buffer_t *buf);
static void _buffer_remove_attachment(struct vt_buffer_attachment_t *attachment);


static void _buffer_remove_attachment(struct vt_buffer_attachment_t *attachment) {
  if (!attachment || !attachment->buf) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return;
  }

  struct vt_buffer_t *buf = attachment->buf;

  wl_list_remove(&attachment->link_buf);
  wl_list_remove(&attachment->link_owner);

  if (attachment->impl && attachment->impl->destroy) {
    attachment->impl->destroy(buf, attachment->owner, attachment->data);
  }

  free(attachment);
}

static void _buffer_destroy(struct vt_buffer_t *buf) {
  if (!buf || !buf->impl || !buf->comp) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return;
  }

  VT_TRACE(buf->comp->log, "Destroying buffer %p", buf);

  struct vt_buffer_attachment_t *attachment, *tmp;

  wl_list_for_each_safe(attachment, tmp, &buf->attachments, link_buf) {
    _buffer_remove_attachment(attachment);
  }

  VT_TRACE(buf->comp->log, "Destroyed buffer %p", buf);

  free(buf);
}

struct vt_buffer_t *vt_buffer_create(struct vt_compositor_t *comp,
                                     uint32_t width, uint32_t height,
                                     const struct vt_buffer_implementation_t *impl) {
  if (!comp || !impl) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return NULL;
  }

  struct vt_buffer_t *buf = calloc(1, sizeof(*buf));

  if (!buf) {
    VT_ERROR(comp->log, "Out of memory.");
    return NULL;
  }

  buf->width = width;
  buf->height = height;
  buf->impl = impl;
  buf->comp = comp;

  buf->refcount = 1;

  wl_list_init(&buf->attachments);

  return buf;
}

struct vt_buffer_t *vt_buffer_ref(struct vt_buffer_t *buf) {
  if (!buf || !buf->comp) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return NULL;
  }

  buf->refcount++;

  VT_TRACE(buf->comp->log, "Referenced buffer: buf=%p refs=%u", buf,
           buf->refcount);

  return buf;
}

void vt_buffer_unref(struct vt_buffer_t **buf_ptr) {
  if (!buf_ptr || !*buf_ptr || !(*buf_ptr)->comp) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return;
  }

  struct vt_buffer_t *buf = *buf_ptr;
  *buf_ptr = NULL;

  assert(buf->refcount > 0);

  VT_TRACE(buf->comp->log, "Unreferenced buffer: buf=%p refs=%u", buf,
           buf->refcount);

  if (--buf->refcount != 0)
    return;

  _buffer_destroy(buf);
}

struct vt_dmabuf_attr_t *vt_buffer_get_dmabuf(struct vt_buffer_t *buf) {
  if (!buf || !buf->impl || !buf->impl->get_dmabuf)
    return NULL;

  return buf->impl->get_dmabuf(buf);
}

struct vt_buffer_attachment_t *
vt_buffer_add_attachment(struct vt_buffer_t *buf, const void *owner, void *data,
                         const struct vt_buffer_attachment_implementation_t *impl) {

  if (!buf || !buf->comp) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return NULL;
  }

  struct vt_buffer_attachment_t *attachment = calloc(1, sizeof(*attachment));

  if (!attachment) {
    VT_ERROR(buf->comp->log, "Out of memory.");
    return NULL;
  }

  attachment->buf = buf;
  attachment->owner = owner;
  attachment->data = data;
  attachment->impl = impl;

  wl_list_init(&attachment->link_buf);
  wl_list_init(&attachment->link_owner);

  wl_list_insert(&buf->attachments, &attachment->link_buf);

  return attachment;
}

struct vt_buffer_attachment_t *
vt_buffer_find_attachment(struct vt_buffer_t *buf, const void *owner,
                          const struct vt_buffer_attachment_implementation_t *impl) {
  if (!buf || !buf->comp || !impl) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return NULL;
  }
  struct vt_buffer_attachment_t *attachment;

  wl_list_for_each(attachment, &buf->attachments, link_buf) {
    if (attachment->impl != impl)
      continue;

    if (owner && attachment->owner != owner)
      continue;

    return attachment;
  }

  return NULL;
}

void vt_buffer_release_init_and_ref(
    struct vt_buffer_release_t *release, struct vt_compositor_t *comp,
    struct vt_buffer_release_implementation_t *impl) {
  if (!release)
    return;

  release->comp = comp;
  release->impl = impl;
  release->finished = false;
  release->refcount = 1;
}

struct vt_buffer_release_t *
vt_buffer_release_ref(struct vt_buffer_release_t *release) {
  if (!release || !release->comp) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return NULL;
  }

  release->refcount++;

  VT_TRACE(release->comp->log, "Referenced buffer release: release=%p refs=%u",
           release, release->refcount);

  return release;
}

void vt_buffer_release_unref(struct vt_buffer_release_t **release_ptr) {
  if (!release_ptr || !*release_ptr || !(*release_ptr)->comp) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return;
  }

  struct vt_buffer_release_t *release = *release_ptr;
  *release_ptr = NULL;

  assert(release->refcount > 0);

  VT_TRACE(release->comp->log,
           "Unreferenced buffer release: release=%p refs=%u", release,
           release->refcount);

  if (--release->refcount != 0)
    return;

  VT_TRACE(release->comp->log, "Destroying buffer release %p", release);

  assert(release->impl->destroy);
  release->impl->destroy(release);
}

void vt_buffer_release_finish(struct vt_buffer_release_t *release,
                              int                         release_fence_fd) {
  if (!release) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return;
  }
  if (release->finished)
    return;

  release->finished = true;

  if (release->impl->finish)
    release->impl->finish(release, release_fence_fd);
}

bool vt_buffer_release_needs_fence(struct vt_buffer_release_t *release) {
  if (!release || !release->impl)
    return false;

  if (!release->impl->needs_release_fence)
    return false;

  return release->impl->needs_release_fence(release);
}

struct vt_buffer_use_t *vt_buffer_use_ref(struct vt_buffer_use_t *use) {
  if (!use || !use->comp) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return NULL;
  }

  use->refcount++;

  VT_TRACE(use->comp->log, "Referenced buffer use: use=%p refs=%u", use,
           use->refcount);

  return use;
}

static void _buffer_use_destroy(struct vt_buffer_use_t *use) {
  if (!use || !use->comp || !use->buf) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return;
  }

  VT_TRACE(use->comp->log,
           "Buffer use destroy: use=%p release=%p acquire_fence_fd=%i", use,
           use->release, use->acquire_fence_fd);

  if (use->release)
    vt_buffer_release_finish(use->release, use->release_fence_fd);

  if (use->release_fence_fd >= 0) {
    close(use->release_fence_fd);
    use->release_fence_fd = -1;
  }

  if (use->acquire_fence_fd >= 0) {
    close(use->acquire_fence_fd);
    use->acquire_fence_fd = -1;
  }

  if (use->release)
    vt_buffer_release_unref(&use->release);

  vt_buffer_unref(&use->buf);

  free(use);
}

void vt_buffer_use_unref(struct vt_buffer_use_t **use) {
  if (!use || !*use) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return;
  }

  struct vt_buffer_use_t *use_data = *use;

  *use = NULL;

  assert(use_data->refcount > 0);

  if (--use_data->refcount != 0) {
    return;
  }

  _buffer_use_destroy(use_data);
}

struct vt_buffer_use_t *vt_buffer_use_create_take(
    struct vt_compositor_t *comp, const void *owner, struct vt_buffer_t **buf,
    struct vt_buffer_release_t **release, int *acquire_fence_fd) {
  if (!buf || !*buf || !comp) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return NULL;
  }

  struct vt_buffer_use_t *use = calloc(1, sizeof(*use));
  if (!use)
    return NULL;

  use->comp = comp;
  use->owner = owner;

  use->refcount = 1;

  use->buf = *buf;
  *buf = NULL;

  if (release) {
    use->release = *release;
    *release = NULL;
  }

  use->acquire_fence_fd = acquire_fence_fd ? *acquire_fence_fd : -1;

  if (acquire_fence_fd)
    *acquire_fence_fd = -1;

  use->release_fence_fd = -1;

  VT_TRACE(use->comp->log, "Buffer use create: use=%p taking buf=%p release=%p",
           use, use->buf, use->release);

  return use;
}

bool vt_buffer_use_set_release_fence_fd(struct vt_buffer_use_t *use, int fd) {
  if (!use || !use->comp || fd < 0) {
    VT_PARAM_CHECK_FAIL_HEADLESS();
    return false;
  }

  int owned_fd = dup(fd);
  if (owned_fd < 0) {
    VT_ERROR(use->comp->log, "dup() failed: %s", strerror(errno));
    return false;
  }

  if (use->release_fence_fd >= 0)
    close(use->release_fence_fd);

  use->release_fence_fd = owned_fd;

  VT_TRACE(use->comp->log, "Set buffer use=%p release_fence_fd to %i", use,
           use->release_fence_fd);

  return true;
}
