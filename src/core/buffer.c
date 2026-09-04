#include "buffer.h"
#include "src/core/core_types.h"
#include "src/core/util.h"
#include "src/render/renderer.h"
#include <errno.h>

#define _SUBSYS_NAME "BUFFERS"

static void _buffer_unref_raw(struct vt_buffer_t *buf) {
  if(!buf) return;

  buf->refcount--;

  if(buf->refcount != 0) return;

  vt_buffer_destroy(buf);
}

static void _buffer_destroyed(struct wl_listener *listener, void *data) {
  struct vt_buffer_t *buf = wl_container_of(listener, buf, destroy);

  if(!buf) return;

  buf->res = NULL;

  _buffer_unref_raw(buf);
}

struct vt_buffer_t* _buffer_create_from_resource(struct vt_renderer_t* renderer, struct wl_resource* res) {
  if(!renderer || !renderer->comp) {
    return NULL;
  }

  struct vt_buffer_t* buf = calloc(1, sizeof(*buf));

  if (!buf) {
    VT_ERROR(renderer->comp->log, "calloc() failed: %s", strerror(errno));
    return NULL;
  }
  
  buf->renderer = renderer;

  wl_list_init(&buf->destroy.link);
  buf->destroy.notify = _buffer_destroyed;

  wl_resource_add_destroy_listener(res, &buf->destroy);
  buf->destroy_linked = true;

  buf->refcount = 1;

  return buf;
}

bool vt_buffer_import(struct vt_buffer_t      *buf,
                      const pixman_region32_t *damage) {
  if (!buf || !buf->renderer || !buf->renderer->comp ||
      !buf->renderer->impl.import_buffer)
    return false;
  
  if (!buf->renderer->impl.import_buffer(buf->renderer, buf, damage)) {
    VT_ERROR(buf->renderer->comp->log,
             "Failed to import to buffer %p", buf);
    return false;
  }

  return true;
}

struct vt_buffer_t* vt_buffer_from_resource(struct vt_renderer_t* renderer, struct wl_resource* res) {
  if (!renderer || !res)
    return NULL;

  /* Have we already wrapped this wl_buffer? */
  struct wl_listener *listener =
      wl_resource_get_destroy_listener(res, _buffer_destroyed);

  if (listener) {
    struct vt_buffer_t *buf = wl_container_of(listener, buf, destroy);

    return buf; /* borrowed reference */
  }

  /* Create a new vt_buffer_t and wrap the wl_buffer */
  return _buffer_create_from_resource(renderer, res);

}

void vt_buffer_destroy(struct vt_buffer_t *buf) {
  if (!buf || !buf->renderer || !buf->renderer->comp)
    return;

  struct vt_renderer_t *r = buf->renderer;

  VT_TRACE(r->comp->log, "Dropping buffer %p", buf);

  if (r && r->impl.destroy_buffer_texture)
    r->impl.destroy_buffer_texture(r, buf);

  if (buf->res) {
    wl_buffer_send_release(buf->res);
  }
  buf->res = NULL;

  if (buf->destroy_linked) {
    wl_list_remove(&buf->destroy.link);
    wl_list_init(&buf->destroy.link);
    buf->destroy_linked = false;
  }

  if (r && r->comp)
    VT_TRACE(r->comp->log, "Successfully dropped buffer %p", buf);

  free(buf);
}

struct vt_buffer_t *vt_buffer_ref(struct vt_buffer_t *buf) {
  if (buf)
    buf->refcount++;

  return buf;
}

void vt_buffer_unref(struct vt_buffer_t **buf) {
  if(!buf || !(*buf)) return;

  (*buf)->refcount--;

  if((*buf)->refcount != 0) return;

  vt_buffer_destroy(*buf);

  *buf = NULL;
}
