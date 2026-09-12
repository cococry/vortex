#include "wl_surface.h"
#include "pixman.h"
#include "runara/runara.h"
#include "src/core/buffer.h"
#include "src/core/compositor.h"
#include "src/core/content_update.h"
#include "src/core/scene.h"
#include "src/core/surface.h"
#include "src/core/util.h"
#include "src/input/wl_seat.h"
#include "src/render/renderer.h"
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
    new_buf = vt_buffer_from_resource(surf->comp->renderer, buffer);

    if (!new_buf) {
      VT_WL_OUT_OF_MEMORY(surf->comp, client);
      return;
    }

    new_buf = vt_buffer_ref(new_buf);
  }

  /* Modify pending state after everything succeeded */
  if(legacy_offset) {
    surf->pending.offset_set = true;
    surf->pending.offset_x = x;
    surf->pending.offset_y = y;
  }

  /* Replace any previously pending buffer */
  vt_buffer_unref(&surf->pending.buf);

  surf->pending.buf = new_buf;
  surf->pending.buffer_attached = true;
}

static void _surface_drop_current_buffer(struct vt_surface_t *surf) {}

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

static void _surface_frame_callback_destroy(struct wl_resource* resource) {
  struct vt_surface_frame_callback_t *cb = wl_resource_get_user_data(resource);

  if(!cb) return;

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

  struct vt_surface_frame_callback_t* cb = calloc(1, sizeof(*cb));
  if(!cb) {
    wl_resource_destroy(res);
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }
  cb->res = res;
  wl_list_init(&cb->link);

  wl_resource_set_implementation(res, NULL, cb, _surface_frame_callback_destroy);

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
                             &surf->pending.damage_surface, x, y, width,
                             height);

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
  /* [0]: Sets transform options for a surface which the compositor
   * needs to apply in the renderer. */
  struct vt_surface_t *surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 1. Check for invalid input */
  if (transform < WL_OUTPUT_TRANSFORM_NORMAL ||
      transform > WL_OUTPUT_TRANSFORM_FLIPPED_270) {
    wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_TRANSFORM,
                           "invalid transform %d", transform);
    VT_WARN(surf->comp->log, "%p", surf);
    return;
  }

  /* 2. Set the transform */
  surf->buffer_transform = transform;

  VT_TRACE(surf->comp->log,
           "surface.set_buffer_transform: transform=%d for surface %p",
           transform, surf);
}

void _wl_surface_set_buffer_scale(struct wl_client   *client,
                                  struct wl_resource *resource, int32_t scale) {
  /* [0]: Sets buffer scale for HiDPi displays. This needs to be
   * applied in the renderer. */
  struct vt_surface_t *surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 1. Check for invalid input.
   * According to spec, a scale < 1 is not valid. */
  if (scale < 1) {
    wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_SCALE,
                           "invalid buffer scale %d", scale);
    return;
  }

  /* 1. Set the buffer scale*/
  surf->buffer_scale = scale;

  VT_TRACE(surf->comp->log, "surface_set_buffer_scale: scale=%d for surface %p",
           scale, surf);
}

void _wl_surface_offset(struct wl_client *client, struct wl_resource *resource,
                        int32_t x, int32_t y) {
  /* [0]: Sets surface position (non-standard) */
  struct vt_surface_t *surf = wl_resource_get_user_data(resource);
  if (!surf)
    return;

  surf->x = x;
  surf->y = y;

  // Force re-evaluation on next commit
  surf->_mask_outputs_visible_on = 0;

  VT_TRACE(surf->comp->log, "surface_offset: moved surface %p to %d,%d", surf,
           x, y);
}

void _wl_surface_destroy(struct wl_client   *client,
                         struct wl_resource *resource) {
  struct vt_surface_t *surf =
      ((struct vt_surface_t *)wl_resource_get_user_data(resource));

  VT_TRACE(surf->comp->log, "Got surface.destroy: Destroying surface resource.")
  wl_resource_destroy(resource);
}

static void _explicit_sync_surface_destroy(struct vt_surface_t *surf) {
  if (surf->pending.acquire_fence_fd >= 0) {
    close(surf->pending.acquire_fence_fd);
    surf->pending.acquire_fence_fd = -1;
  }

  if (surf->pending.release) {
    surf->pending.release->pending_surface = NULL;
    surf->pending.release = NULL;
  }

  surf->sync.release = NULL;

  if (surf->sync.res) {
    wl_resource_set_user_data(surf->sync.res, NULL);
    surf->sync.res = NULL;
  }
}

void _wl_surface_handle_resource_destroy(struct wl_resource *resource) {
  struct vt_surface_t *surf = wl_resource_get_user_data(resource);

  const int32_t x = surf->x;
  const int32_t y = surf->y;
  const int32_t w = surf->buf ? surf->buf->tex.width : 0;
  const int32_t h = surf->buf ? surf->buf->tex.height : 0;

  VT_TRACE(surf->comp->log, "Got surface.destroy handler: Unmanaging client.")

  if (surf->mapped)
    vt_surface_unmapped(surf);

  /* Unlink from lists */
  wl_list_remove(&surf->link);

  struct vt_seat_t *seat = surf->comp ? surf->comp->seat : NULL;

  /* no seat field may retain this pointer past free(surf). */
  if (seat) {
    if (seat->kb_focus.surf == surf) {
      seat->kb_focus.surf = NULL;
      seat->kb_focus.client = NULL;
    }

    if (seat->ptr_focus.surf == surf) {
      seat->ptr_focus.surf = NULL;
      seat->ptr_focus.client = NULL;
    }

    if (seat->cursor.surf == surf) {
      seat->cursor.surf = NULL;
      seat->cursor.owner = NULL;
    }
  }

  /* Focus stack must not retain the surface. */
  if (!wl_list_empty(&surf->link_focus)) {
    wl_list_remove(&surf->link_focus);
    wl_list_init(&surf->link_focus);
  }

  /* Deallocate pixman regions */
  pixman_region32_fini(&surf->pending.damage);
  pixman_region32_fini(&surf->damage);

  pixman_region32_init(&surf->pending.input_region);
  pixman_region32_init(&surf->input_region);
  
  pixman_region32_init(&surf->pending.opaque_region);
  pixman_region32_fini(&surf->opaque_region);

  /* Destroy the attached render texture */
  _surface_drop_current_buffer(surf);

  /* destroy dmabuf resources of the surface */
  if (surf->comp->have_proto_dmabuf)
    vt_proto_linux_dmabuf_v1_surface_destroy(surf);

  if (surf->scene_node)
    vt_scene_node_destroy(surf->comp, surf->scene_node);

  struct vt_output_t   *output;
  wl_list_for_each(output, &surf->comp->outputs, link_global) {
    if (!(surf->_mask_outputs_visible_on & (1u << output->id)))
      continue;
    // Damage the part of the screen where the surface was located
    // and schedule a repaint
    pixman_region32_union_rect(&output->damage, &output->damage, x, y, w, h);
    vt_comp_schedule_repaint(surf->comp, output);

    output->needs_damage_rebuild = true;
  }

  _explicit_sync_surface_destroy(surf);

  /* Free surface handle */
  wl_resource_set_user_data(resource, NULL);
  free(surf);
}

void _wl_surface_associate_with_output(struct vt_compositor_t *c,
                                       struct vt_surface_t    *surf,
                                       struct vt_output_t     *output) {
  if(!surf || !c || !output) 
    return;

  if(!surf->buf) {
    surf->_mask_outputs_visible_on = 0;
    return;
  }

  // TODO: Not use buffer width
  RnTexture tex = surf->buf->tex;
  if (surf->x + tex.width <= output->x ||
      surf->x >= output->x + output->width ||
      surf->y + tex.height <= output->y ||
      surf->y >= output->y + output->height)
    return;
  surf->_mask_outputs_visible_on |= (1u << output->id);
}

bool vt_proto_wl_surface_init(struct vt_surface_t *surf,
                              struct wl_client *client, uint32_t id,
                              uint32_t version) {
  if (!surf) {
    VT_PARAM_CHECK_FAIL(surf->comp);
    return false;
  }

  // Get the surface's wayland resource
  struct wl_resource *res =
      wl_resource_create(client, &wl_surface_interface, 4, id);
  if (!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return false;
  }
  wl_resource_set_implementation(res, &surface_impl, surf,
                                 _wl_surface_handle_resource_destroy);
  surf->surf_res = res;

  _proto.comp = surf->comp;

  return true;
}
