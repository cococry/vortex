#include "buffer.h"
#include "linux-explicit-synchronization-v1-server-protocol.h"
#include "src/core/core_types.h"
#include "src/core/util.h"
#include "src/render/renderer.h"
#include <errno.h>
#include <unistd.h>
#include <wayland-server-protocol.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define _SUBSYS_NAME "BUFFERS"

static void _buffer_destroy_notify(struct wl_listener *listener, void *data);
static void _buffer_destroy(struct vt_buffer_t *buf);
static struct vt_buffer_t *
_buffer_create_from_resource(struct vt_renderer_t *renderer,
                             struct wl_resource   *res);

static void _buffer_destroy_notify(struct wl_listener *listener, void *data) {
  (void)data;
  struct vt_buffer_t *buf = wl_container_of(listener, buf, destroy);

  if (!buf)
    return;

  VT_TRACE(buf->renderer->comp->log,
           "Buffer resource %p (wrapper: %p) destroyed.", buf->res, buf);

  /* Resource has been destroyed, clear the pointer */
  buf->res = NULL;

  vt_buffer_unref(&buf);
}

static void _buffer_destroy(struct vt_buffer_t *buf) {
  if (!buf || !buf->renderer || !buf->renderer->comp)
    return;

  assert(buf->res == NULL);

  struct vt_renderer_t *r = buf->renderer;

  VT_TRACE(r->comp->log, "Dropping buffer resource wrapper %p (resource: %p)",
           buf, buf->res);

  /* Destroy associated texture handle */
  if (r->impl.destroy_buffer_texture)
    r->impl.destroy_buffer_texture(r, buf);

  /* Unlink destroy notifier */
  if (buf->destroy_linked) {
    wl_list_remove(&buf->destroy.link);
    wl_list_init(&buf->destroy.link);
    buf->destroy_linked = false;
  }

  VT_TRACE(r->comp->log, "Dropped buffer resource wrapper %p", buf);

  free(buf);
}

static struct vt_buffer_t *
_buffer_create_from_resource(struct vt_renderer_t *renderer,
                             struct wl_resource   *res) {
  if (!renderer || !renderer->comp) {
    return NULL;
  }

  struct vt_buffer_t *buf = calloc(1, sizeof(*buf));

  if (!buf) {
    VT_ERROR(renderer->comp->log, "calloc() failed: %s", strerror(errno));
    return NULL;
  }

  buf->renderer = renderer;

  wl_list_init(&buf->destroy.link);
  buf->destroy.notify = _buffer_destroy_notify;

  wl_resource_add_destroy_listener(res, &buf->destroy);
  buf->destroy_linked = true;

  buf->res = res;

  buf->refcount = 1;

  VT_TRACE(renderer->comp->log,
           "Allocated buffer resource wrapper %p (resource: %p)", buf, res);

  return buf;
}

bool vt_buffer_import(struct vt_buffer_t      *buf,
                      const pixman_region32_t *damage) {
  if (!buf || !buf->renderer || !buf->renderer->comp ||
      !buf->renderer->impl.import_buffer)
    return false;

  if (!buf->renderer->impl.import_buffer(buf->renderer, buf, damage)) {
    VT_ERROR(buf->renderer->comp->log, "Failed to import to buffer %p", buf);
    return false;
  }

  return true;
}

struct vt_buffer_t *
vt_buffer_get_or_create_from_resource(struct vt_renderer_t *renderer,
                                      struct wl_resource   *res) {
  if (!renderer || !res)
    return NULL;

  /* Have we already wrapped this wl_buffer? */
  struct wl_listener *listener =
      wl_resource_get_destroy_listener(res, _buffer_destroy_notify);

  if (listener) {
    struct vt_buffer_t *buf = wl_container_of(listener, buf, destroy);

    return buf; /* borrowed reference */
  }

  /* Create a new vt_buffer_t and wrap the wl_buffer */
  return _buffer_create_from_resource(renderer, res);
}

struct vt_buffer_t *vt_buffer_ref(struct vt_buffer_t *buf) {
  if (!buf)
    return NULL;

  buf->refcount++;

  VT_TRACE(buf->renderer->comp->log, "Referenced buffer: buf=%p refs=%u", buf,
           buf->refcount);

  return buf;
}

void vt_buffer_unref(struct vt_buffer_t **buf_ptr) {
  if (!buf_ptr || !*buf_ptr)
    return;

  struct vt_buffer_t *buf = *buf_ptr;
  *buf_ptr = NULL;

  assert(buf->refcount > 0);

  VT_TRACE(buf->renderer->comp->log, "Unreferenced buffer: buf=%p refs=%u", buf,
           buf->refcount);

  if (--buf->refcount != 0)
    return;

  _buffer_destroy(buf);
}

void vt_buffer_start_use(struct vt_buffer_t *buf) {
  if (!buf)
    return;

  buf->uses++;

  VT_TRACE(buf->renderer->comp->log,
           "Started buffer use: buf=%p uses=%u refs=%u", buf, buf->uses,
           buf->refcount);
}

void vt_buffer_end_use(struct vt_buffer_t *buf) {
  if (!buf) {
    return;
  }

  assert(buf->uses > 0);

  buf->uses--;

  VT_TRACE(buf->renderer->comp->log, "Ended buffer use: buf=%p uses=%u refs=%u",
           buf, buf->uses, buf->refcount);

  if (buf->uses != 0)
    return;

  /* Send wl_buffer.release to associated resource once no uses remain */
  if (buf->res) {
    wl_buffer_send_release(buf->res);

    VT_TRACE(buf->renderer->comp->log, "Sent wl_buffer.release: buf=%p res=%p",
             buf, buf->res);
  }
}

struct vt_buffer_release_t *
vt_buffer_release_ref(struct vt_buffer_release_t *release) {
  if (!release)
    return NULL;

  release->refcount++;

  VT_TRACE(release->renderer->comp->log,
           "Referenced buffer release: release=%p refs=%u", release,
           release->refcount);

  return release;
}

void vt_buffer_release_unref(struct vt_buffer_release_t **release_ptr) {
  if (!release_ptr || !*release_ptr)
    return;

  struct vt_buffer_release_t *release = *release_ptr;
  *release_ptr = NULL;

  assert(release->refcount > 0);

  VT_TRACE(release->renderer->comp->log,
           "Unreferenced buffer release: release=%p refs=%u", release,
           release->refcount);

  if (--release->refcount != 0)
    return;

  free(release);
}

struct vt_buffer_use_t *vt_buffer_use_ref(struct vt_buffer_use_t *use) {
  if (!use)
    return NULL;

  use->refcount++;

  VT_TRACE(use->renderer->comp->log, "Referenced buffer use: use=%p refs=%u",
           use, use->refcount);

  return use;
}

static void _buffer_use_destroy(struct vt_buffer_use_t *use) {
  if (!use)
    return;

  VT_TRACE(use->renderer->comp->log,
           "Buffer use destroy: use=%p release=%p explicit=%p res=%p fence=%d",
           use, use->release, use->release ? use->release->explicit : NULL,
           use->release && use->release->explicit ? use->release->explicit->res
                                                  : NULL,
           use->release_fence_fd);

  struct vt_buffer_release_t *release = use->release;
  if (release && release->explicit && release->explicit->res) {
    struct wl_resource *res = release->explicit->res;

    if (use->release_fence_fd >= 0) {
      zwp_linux_buffer_release_v1_send_fenced_release(res,
                                                      use->release_fence_fd);
    } else {
      zwp_linux_buffer_release_v1_send_immediate_release(res);
    }

    wl_resource_destroy(res);
  }

  if (use->release_fence_fd >= 0) {
    close(use->release_fence_fd);
    use->release_fence_fd = -1;
  }

  if (use->acquire_fence_fd >= 0) {
    close(use->acquire_fence_fd);
    use->acquire_fence_fd = -1;
  }

  if (use->buf)
    vt_buffer_end_use(use->buf);

  vt_buffer_release_unref(&use->release);
  vt_buffer_unref(&use->buf);

  free(use);
}

void vt_buffer_use_unref(struct vt_buffer_use_t **use) {
  if (!use || !*use)
    return;

  struct vt_buffer_use_t *use_data = *use;

  *use = NULL;

  assert(use_data->refcount > 0);

  if (--use_data->refcount != 0) {
    return;
  }

  _buffer_use_destroy(use_data);
}

struct vt_buffer_use_t *vt_buffer_use_create_take(
    struct vt_renderer_t *renderer, struct vt_buffer_t **buf,
    struct vt_buffer_release_t **release, int *acquire_fence_fd) {
  if (!buf || !*buf || !renderer || !renderer->comp)
    return NULL;

  struct vt_buffer_use_t *use = calloc(1, sizeof(*use));
  if (!use)
    return NULL;

  use->renderer = renderer;

  use->refcount = 1;

  use->buf = *buf;
  *buf = NULL;

  if (use->buf)
    vt_buffer_start_use(use->buf);

  if (release) {
    use->release = *release;
    *release = NULL;
  }

  use->acquire_fence_fd = acquire_fence_fd ? *acquire_fence_fd : -1;

  if (acquire_fence_fd)
    *acquire_fence_fd = -1;

  use->release_fence_fd = -1;

  VT_TRACE(
      use->renderer->comp->log,
      "Buffer use create: use=%p taking buf=%p release=%p explicit release=%p",
      use, use->buf, use->release,
      use->release ? use->release->explicit : NULL);

  return use;
}

bool vt_buffer_use_set_release_fence_fd(struct vt_buffer_use_t *use, int fd) {
  if (!use || fd < 0)
    return false;

  int owned_fd = dup(fd);
  if (owned_fd < 0)
    return false;

  if (use->release_fence_fd >= 0)
    close(use->release_fence_fd);

  use->release_fence_fd = owned_fd;

  return true;
}
