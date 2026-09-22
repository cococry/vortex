#include "wl_surface.h"
#include "pixman.h"
#include "src/core/buffer.h"
#include "src/core/content_update.h"
#include "src/core/scene.h"
#include "src/core/surface.h"
#include "src/core/util.h"
#include "src/input/wl_seat.h"
#include "src/render/renderer.h"
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#define _SUBSYS_NAME "SURFACE"

static void _wl_surface_attach(struct wl_client   *client,
                               struct wl_resource *resource,
                               struct wl_resource *buffer, int32_t x,
                               int32_t y);

static void _wl_surface_commit(struct wl_client   *client,
                               struct wl_resource *resource);

static void _wl_surface_frame(struct wl_client   *client,
                              struct wl_resource *resource, uint32_t callback);

static void _wl_surface_damage(struct wl_client   *client,
                               struct wl_resource *resource, int32_t x,
                               int32_t y, int32_t width, int32_t height);

static void _wl_surface_set_opaque_region(struct wl_client   *client,
                                          struct wl_resource *resource,
                                          struct wl_resource *region);

static void _wl_surface_set_input_region(struct wl_client   *client,
                                         struct wl_resource *resource,
                                         struct wl_resource *region);

static void _wl_surface_set_buffer_transform(struct wl_client   *client,
                                             struct wl_resource *resource,
                                             int32_t             transform);

static void _wl_surface_set_buffer_scale(struct wl_client   *client,
                                         struct wl_resource *resource,
                                         int32_t             scale);

static void _wl_surface_damage_buffer(struct wl_client   *client,
                                      struct wl_resource *resource, int32_t x,
                                      int32_t y, int32_t width, int32_t height);

static void _wl_surface_offset(struct wl_client   *client,
                               struct wl_resource *resource, int32_t x,
                               int32_t y);

static void _wl_surface_destroy(struct wl_client   *client,
                                struct wl_resource *resource);

static void _wl_surface_handle_resource_destroy(struct wl_resource *resource);

static void _wl_surface_associate_with_output(struct vt_compositor_t *c,
                                              struct vt_surface_t    *surf,
                                              struct vt_output_t     *output);

static const struct wl_surface_interface surface_impl = {
    .attach = _wl_surface_attach,
    .commit = _wl_surface_commit,
    .damage = _wl_surface_damage,
    .frame = _wl_surface_frame,
    .set_opaque_region = _wl_surface_set_opaque_region,
    .set_input_region = _wl_surface_set_input_region,
    .set_buffer_scale = _wl_surface_set_buffer_scale,
    .set_buffer_transform = _wl_surface_set_buffer_transform,
    .offset = _wl_surface_offset,
    .destroy = _wl_surface_destroy,
    .damage_buffer = _wl_surface_damage_buffer,
};

struct vt_proto_wl_surface_t {
  struct vt_compositor_t *comp;
};

static struct vt_proto_wl_surface_t _proto;

void _wl_surface_attach(struct wl_client *client, struct wl_resource *resource,
                        struct wl_resource *buffer, int32_t x, int32_t y) {
  struct vt_surface_t *surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(surf->comp->log, "Got compositor.surface_attach.");

  bool legacy_offset = false;

  /* Offset handling */
  if (wl_resource_get_version(resource) >= WL_SURFACE_OFFSET_SINCE_VERSION) {
    if (x != 0 || y != 0) {
      wl_resource_post_error(
          resource, WL_SURFACE_ERROR_INVALID_OFFSET,
          "wl_surface.attach x/y must be zero for wl_surface version >= %u",
          WL_SURFACE_OFFSET_SINCE_VERSION);
      return;
    }
  } else {
    legacy_offset = true;
  }

  /* Buffer handling */
  struct vt_buffer_t *new_buf = NULL;

  if (buffer) {
    /* Lazily allocate vt_buffer_t wrapper */
    new_buf = vt_buffer_get_or_create_from_resource(surf->comp->renderer, buffer);

    if (!new_buf) {
      VT_WL_OUT_OF_MEMORY(surf->comp, client);
      return;
    }

    new_buf = vt_buffer_ref(new_buf);

    VT_TRACE(surf->comp->log,
             "attach: res=%p id=%u wrapper=%p refs=%u active_uses=%u", resource,
             wl_resource_get_id(resource), new_buf, new_buf->refcount,
             new_buf->uses);
  }

  /* Modify pending state after everything succeeded */
  if (legacy_offset) {
    surf->pending.offset_set = true;
    surf->pending.offset_x = x;
    surf->pending.offset_y = y;
  }

  /* Replace any previously pending buffer */
  vt_buffer_unref(&surf->pending.buf);

  surf->pending.buf = new_buf;
  surf->pending.buffer_attached = true;
}

void _wl_surface_commit(struct wl_client   *client,
                        struct wl_resource *resource) {
  struct vt_surface_t *surf = wl_resource_get_user_data(resource);
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(surf->comp->log, "Got wl_surface.commit for surface %p.", surf)

  if (!vt_surface_validate_commit(surf)) {
    VT_ERROR(surf->comp->log, "wl_surface.commit: Commit validation failed");
    return;
  }

  if (!vt_surface_emit_content_update(surf)) {
    VT_ERROR(surf->comp->log,
             "wl_surface.commit: Failed to emit content update");
    return;
  }

  VT_TRACE(surf->comp->log, "surface.commit Finsihed commit.");
}

static void _surface_frame_callback_destroy(struct wl_resource *resource) {
  struct vt_surface_frame_callback_t *cb = wl_resource_get_user_data(resource);

  if (!cb)
    return;

  wl_list_remove(&cb->link);
  wl_list_init(&cb->link);

  free(cb);
}

void _wl_surface_frame(struct wl_client *client, struct wl_resource *resource,
                       uint32_t callback) {
  struct vt_surface_t *surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(surf->comp->log, "Got wl_surface.frame");

  struct wl_resource *res =
      wl_resource_create(client, &wl_callback_interface, 1, callback);

  if (!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  struct vt_surface_frame_callback_t *cb = calloc(1, sizeof(*cb));
  if (!cb) {
    wl_resource_destroy(res);
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }
  cb->res = res;
  wl_list_init(&cb->link);

  wl_resource_set_implementation(res, NULL, cb,
                                 _surface_frame_callback_destroy);

  wl_list_insert(surf->pending.frame_callbacks.prev, &cb->link);

  VT_TRACE(surf->comp->log,
           "wl_surface.frame: Queued callback %p for surface %p.", cb, surf);
}
void _wl_surface_damage(struct wl_client *client, struct wl_resource *resource,
                        int32_t x, int32_t y, int32_t width, int32_t height) {
  struct vt_surface_t *surf =
      resource ? wl_resource_get_user_data(resource) : NULL;

  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(surf->comp->log, "Got wl_surface.damage");

  pixman_region32_union_rect(&surf->pending.damage_surface,
                             &surf->pending.damage_surface, x, y, width,
                             height);

  VT_TRACE(surf->comp->log,
           "wl_surface.damage: Accumulated damage [x: %i, y: %i, w: %i, "
           "h: %i] into pending surface damage of surface %p",
           x, y, width, height, surf);
}

void _wl_surface_damage_buffer(struct wl_client   *client,
                               struct wl_resource *resource, int32_t x,
                               int32_t y, int32_t width, int32_t height) {
  struct vt_surface_t *surf =
      resource ? wl_resource_get_user_data(resource) : NULL;

  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(surf->comp->log, "Got wl_surface.damage_buffer");

  pixman_region32_union_rect(&surf->pending.damage_buffer,
                             &surf->pending.damage_buffer, x, y, width, height);

  VT_TRACE(surf->comp->log,
           "wl_surface.damage_buffer: Accumulated damage [x: %i, y: %i, w: %i, "
           "h: %i] into pending buffer damage of surface %p",
           x, y, width, height, surf);
}

void _wl_surface_set_opaque_region(struct wl_client   *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *region) {
  struct vt_surface_t *surf = wl_resource_get_user_data(resource);
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  struct vt_region_t *r = NULL;

  if (region) {
    r = wl_resource_get_user_data(region);
    if (!r)
      return;
  }

  pixman_region32_clear(&surf->pending.opaque_region);

  if (region) {
    pixman_region32_copy(&surf->pending.opaque_region, &r->region);
  }

  surf->pending.opaque_region_changed = true;

  VT_TRACE(surf->comp->log,
           "wl_surface.set_opaque_region: updated pending opaque region for "
           "surface %p",
           surf);
}

void _wl_surface_set_input_region(struct wl_client   *client,
                                  struct wl_resource *resource,
                                  struct wl_resource *region) {
  struct vt_surface_t *surf = wl_resource_get_user_data(resource);
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  struct vt_region_t *r = NULL;

  if (region) {
    r = wl_resource_get_user_data(region);
    if (!r)
      return;
  }

  pixman_region32_clear(&surf->pending.input_region);

  surf->pending.input_region_infinite = r == NULL;
  if (r) {
    pixman_region32_copy(&surf->pending.input_region, &r->region);
  }

  surf->pending.input_region_changed = true;

  VT_TRACE(surf->comp->log,
           "wl_surface.set_input_region: updated pending input region for "
           "surface %p",
           surf);
}

void _wl_surface_set_buffer_transform(struct wl_client   *client,
                                      struct wl_resource *resource,
                                      int32_t             transform) {
  struct vt_surface_t *surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  if (transform < WL_OUTPUT_TRANSFORM_NORMAL ||
      transform > WL_OUTPUT_TRANSFORM_FLIPPED_270) {
    wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_TRANSFORM,
                           "invalid transform %d", transform);
    VT_WARN(surf->comp->log, "%p", surf);
    return;
  }

  surf->pending.buffer_transform = transform;

  surf->pending.buffer_transform_changed = true;

  VT_TRACE(surf->comp->log,
           "wl_surface.set_buffer_transform: Set pending transform=%d for "
           "surface %p",
           transform, surf);
}

void _wl_surface_set_buffer_scale(struct wl_client   *client,
                                  struct wl_resource *resource, int32_t scale) {
  /* [0]: Sets buffer scale for HiDPi displays. This needs to be
   * applied when rendering. */
  struct vt_surface_t *surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 1. Check for invalid input.
   * According to the spec, a scale < 1 is not valid. */
  if (scale < 1) {
    wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_SCALE,
                           "invalid buffer scale %d", scale);
    return;
  }

  /* 1. Set the buffer scale*/
  surf->pending.buffer_scale = scale;
  surf->pending.buffer_scale_changed = true;

  VT_TRACE(surf->comp->log,
           "wl_surface.set_buffer_scale: Set pending scale=%d for surface %p",
           scale, surf);
}

void _wl_surface_offset(struct wl_client *client, struct wl_resource *resource,
                        int32_t x, int32_t y) {
  struct vt_surface_t *surf = wl_resource_get_user_data(resource);
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  surf->pending.offset_x = x;
  surf->pending.offset_y = y;
  surf->pending.offset_set = true;

  VT_TRACE(surf->comp->log,
           "wl_surface.offset: Set pending offset=[%d, %d] for surface %p", x,
           y, surf);
}

void _wl_surface_destroy(struct wl_client   *client,
                         struct wl_resource *resource) {
  struct vt_surface_t *surf =
      ((struct vt_surface_t *)wl_resource_get_user_data(resource));

  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(surf->comp->log,
           "Got wl_surface.destroy: Destroying surface resource.")

  wl_resource_destroy(resource);
}

void _wl_surface_handle_resource_destroy(struct wl_resource *resource) {
  struct vt_surface_t *surf = wl_resource_get_user_data(resource);

  if (!surf || !surf->comp) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(surf->comp->log, "Got wl_surface.destroy")

  surf->res = NULL;

  vt_surface_set_mapped(surf, false);

  if (surf->comp->seat) {
    struct vt_seat_t *seat = surf->comp->seat;

    assert(seat->kb_focus.surf != surf);
    assert(seat->ptr_focus.surf != surf);
    assert(wl_list_empty(&surf->link_focus));

    if (seat->cursor.surf == surf) {
      seat->cursor.surf = NULL;
      seat->cursor.owner = NULL;
    }
  }

  /* Unlink from compositor list */
  wl_list_remove(&surf->link);

  if (surf->scene_node) {
    vt_scene_node_damage_whole(surf->comp, surf->scene_node);
    vt_scene_node_destroy(surf->comp, surf->scene_node);
    surf->scene_node = NULL;
  }

  vt_surface_pending_state_fini(&surf->pending);
  vt_surface_applied_state_fini(&surf->applied);

  {
    struct vt_content_update_t *cu, *tmp;

    wl_list_for_each_safe(cu, tmp, &surf->content_updates, queue_link) {
      vt_content_update_destroy(cu);
    }
  }

  {
    struct vt_surface_addon_t *addon, *tmp;

    wl_list_for_each_safe(addon, tmp, &surf->addons, link) {
      vt_surface_addon_destroy(addon);
    }
  }

  wl_resource_set_user_data(resource, NULL);
  free(surf);
}

bool vt_proto_wl_surface_init(struct vt_surface_t *surf,
                              struct wl_client *client, uint32_t id,
                              uint32_t version) {
  if (!surf) {
    return false;
  }

  // Get the surface's wayland resource
  struct wl_resource *res =
      wl_resource_create(client, &wl_surface_interface, version, id);

  if (!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return false;
  }
  wl_resource_set_implementation(res, &surface_impl, surf,
                                 _wl_surface_handle_resource_destroy);
  surf->res = res;

  _proto.comp = surf->comp;

  return true;
}
