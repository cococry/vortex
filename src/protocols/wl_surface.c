#include "wl_surface.h"
#include "pixman.h"
#include "runara/runara.h"
#include "src/core/buffer.h"
#include "src/core/compositor.h"
#include "src/core/scene.h"
#include "src/core/surface.h"
#include "src/core/util.h"
#include "src/input/wl_seat.h"
#include "src/render/renderer.h"
#include <stdbool.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

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
  /* 1. When a client attaches a buffer, we store the resource handle
   * in the internal vt_surface_t struct. */
  struct vt_surface_t *surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(surf->comp->log, "Got compositor.surface_attach.");

  /* Replace any previously pending buffer */
  vt_buffer_unref(&surf->pending.buf);

  if(buffer) {
    struct vt_buffer_t* buf = vt_buffer_from_resource(surf->comp->renderer, buffer);

    if(!buf) {
      VT_WL_OUT_OF_MEMORY(surf->comp, client);
      return;
    }

    surf->pending.buf = vt_buffer_ref(buf); 
  } else { 
    /* attach(NULL) */
    surf->pending.buf = NULL;
  }

  surf->pending.dx = x;
  surf->pending.dy = y;

  surf->pending.buffer_attached = true;
}

static uint32_t _surface_effective_output_mask(struct vt_surface_t *surf) {
  while (surf) {
    if (surf->_mask_outputs_visible_on)
      return surf->_mask_outputs_visible_on;

    if (!surf->subsurface)
      break;

    surf = surf->subsurface->parent;
  }

  return 0;
}

static void
_surface_drop_current_buffer(struct vt_surface_t *surf)
{
}

void _wl_surface_commit(struct wl_client   *client,
                        struct wl_resource *resource) {
  /*[0]: This function is the core of the wl_surface protocol.
   * We use the buffer resource we got from the prior surface.attach
   * event to import buffer data into the renderer.
   *
   * The handler implicitly handles damaging regions that got updated
   * by the commit. */
  struct vt_surface_t *surf = wl_resource_get_user_data(resource);
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(surf->comp->log, "Got surface.commit for surface %p.", surf)

  if (!surf) {
    VT_ERROR(surf->comp->log, "surface_commit: NULL user_data");
    return;
  }

  bool has_damage = !pixman_region32_empty(&surf->pending.damage);

  /* 1. If the size of the surface changed, we need to
   * recalculate the outputs that the surface is visible on */
  // TODO: do this ^  

  /* 2. Import attached buffer into the renderer */
  struct vt_renderer_t *r = surf->comp->renderer;

  bool had_buffer_attached = surf->pending.buffer_attached;

  if (surf->pending.buffer_attached) {
    struct vt_buffer_t *new_buffer = surf->pending.buf;

    if (!new_buffer) {
      VT_TRACE(surf->comp->log,
               "Got wl_surface.attach(NULL), dropping buffer and unmapping "
               "surface %p",
               surf);
      if (surf->buf != NULL) {
        vt_comp_surf_mark_damaged(surf->comp, surf);
      }

      /* Drop ownership of current buffer */
      vt_buffer_unref(&surf->buf);

      vt_surface_unmapped(surf);
    } else {
      VT_TRACE(surf->comp->log,
               "Importing new buffer (resource: %p) for surface %p", new_buffer,
               surf);

      /* Import from the new resource */
      surf->buf->res = new_buffer->res;

      if (!vt_buffer_import(surf->buf, &surf->damage)) {
        vt_buffer_unref(&surf->buf);
        return;
      }

      /* Drop ownership of current buffer */
      vt_buffer_unref(&surf->buf);

      surf->buf = new_buffer;
    }

    surf->pending.buf = NULL;
    surf->pending.buffer_attached = false;
  }

  pixman_region32_clear(&surf->damage);

  if (has_damage) {
    pixman_region32_union(&surf->damage, &surf->damage,
                          &surf->pending.damage);
  }

  pixman_region32_clear(&surf->pending.damage);

  if (had_buffer_attached && surf->buf) {
    // TODO: maybe not do this 
    pixman_region32_union_rect(&surf->damage, &surf->damage, 0, 0,
                               surf->buf->tex.width, surf->buf->tex.height);

    pixman_region32_intersect_rect(&surf->damage, &surf->damage, 0, 0,
                                   surf->buf->tex.width, surf->buf->tex.height);
  }

  if (surf->role_impl.commit)
    surf->role_impl.commit(surf);

  /* 4. Calculate current damage region  */
  if (!surf->_mask_outputs_visible_on) {
    /* Re-populate the output bitfield of the surface */
    struct vt_output_t *output;
    wl_list_for_each(output, &surf->comp->outputs, link_global) {
      _wl_surface_associate_with_output(surf->comp, surf, output);
      output->needs_damage_rebuild = true;
    }
  }

  /* Apply committed surface offset. */
  int32_t dx = surf->pending.dx;
  int32_t dy = surf->pending.dy;

  surf->dx = dx;
  surf->dy = dy;

  if (surf->type == VT_SURFACE_TYPE_CURSOR &&
      surf->comp->seat->cursor.surf == surf) {

    surf->comp->seat->cursor.hotspot_x -= dx;
    surf->comp->seat->cursor.hotspot_y -= dy;
  }

  /* consumed */
  surf->pending.dx = 0;
  surf->pending.dy = 0;

  bool is_valid_xdg_surf =
      surf->xdg_surf &&
      ((surf->xdg_surf->toplevel &&
        surf->xdg_surf->toplevel->xdg_toplevel_res) ||
       (surf->xdg_surf->popup && surf->xdg_surf->popup->xdg_popup_res));

  /* 5. If the surface has not yet been mapped and has a
   * valid XDG Surface and XDG Surface role, trigger a map request. */
  if (!surf->mapped && surf->buf && is_valid_xdg_surf) {
    vt_surface_mapped(surf);
  }

  if (surf->subsurface || surf->type == VT_SURFACE_TYPE_CURSOR) {
    surf->mapped = surf->buf != NULL;
  }

  bool needs_repaint = surf->mapped && (has_damage || had_buffer_attached ||
                                        surf->cb_pool.n_cbs > 0);

  if (needs_repaint) {
    pixman_region32_t global_damage;
    pixman_region32_init(&global_damage);
    pixman_region32_copy(&global_damage, &surf->damage);

    pixman_region32_translate(&global_damage, surf->x, surf->y);

    /* 6. Set damage regions and schedule a repaint for
     * all outputs that the surface intersects with */
    struct vt_output_t *output;

    wl_list_for_each(output, &surf->comp->outputs, link_global) {
      if (!(surf->_mask_outputs_visible_on & (1u << output->id)))
        continue;

      pixman_region32_union(&output->damage, &output->damage, &global_damage);

      vt_comp_schedule_repaint(surf->comp, output);
    }
    pixman_region32_fini(&global_damage);
  }

  if (surf->pending.input_region_changed) {
    surf->input_region_set = surf->pending.input_region_set;

    pixman_region32_copy(&surf->input_region, &surf->pending.input_region);

    pixman_region32_clear(&surf->pending.input_region);

    surf->pending.input_region_changed = false;
  }

  if (surf->pending.opaque_region_changed) {
    pixman_region32_copy(&surf->opaque_region, &surf->pending.opaque_region);

    pixman_region32_clear(&surf->pending.opaque_region);

    surf->pending.opaque_region_changed = false;
  }

  struct vt_surface_release_t *release = surf->pending.release;
  surf->pending.release = NULL;
  if (release) {
    release->pending_surface = NULL;
  }

  surf->sync.release = release;

  surf->sync.acquire_fence_fd = surf->pending.acquire_fence_fd;
  surf->pending.acquire_fence_fd = -1;

  VT_TRACE(surf->comp->log, "surface.commit Finsihed commit.");

  VT_TRACE(surf->comp->log,
           "COMMIT surf=%p parent=%p mapped=%d "
           "mask=0x%x buffer resource=%p",
           surf, surf->subsurface ? surf->subsurface->parent : NULL,
           surf->mapped, surf->_mask_outputs_visible_on,
           surf->buf ? surf->buf->res : NULL);
}

void _wl_surface_frame(struct wl_client *client, struct wl_resource *resource,
                       uint32_t callback) {
  struct vt_surface_t *surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  VT_TRACE(surf->comp->log, "Got compositor.surface_frame.")

  struct wl_resource *res =
      wl_resource_create(client, &wl_callback_interface, 1, callback);
  if (!res) {
    VT_WL_OUT_OF_MEMORY(_proto.comp, client);
    return;
  }

  /* Store the frame callback in the list of pending frame callbacks.
   * wl_callback_send_done must be called for each of the pending callbacks
   * after the next "page flip" (next sink backend frame) event completes
   * in order to correctly handle frame pacing ( see send_frame_callbacks() ).
   */
  if (surf->cb_pool.n_cbs >= VT_MAX_FRAME_CBS) {
    VT_WARN(
        surf->comp->log,
        "Surface %p already has %i frame callbacks queued - dropping new one.",
        surf->cb_pool.n_cbs);
    return;
  }
  surf->cb_pool.cbs[surf->cb_pool.n_cbs++] = res;

  VT_TRACE(surf->comp->log,
           "surface.frame: Inserting frame callback into list of surface %p.",
           surf);

  surf->needs_frame_done = true;
  surf->comp->any_frame_cb_pending = true;
}

void _wl_surface_damage(struct wl_client *client, struct wl_resource *resource,
                        int32_t x, int32_t y, int32_t width, int32_t height) {
  struct vt_surface_t *surf =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 1. Union the requested damage into the pending damage region
   * of the surface. */
  pixman_region32_union_rect(&surf->pending.damage, &surf->pending.damage, x, y,
                             width, height);

  /* 2. Makr all outputs the surface intersects with for needing a damage
   * rebuild. */
  struct vt_output_t *output;
  wl_list_for_each(output, &surf->comp->outputs, link_global) {
    if (!(surf->_mask_outputs_visible_on & (1u << output->id)))
      continue;
    output->needs_damage_rebuild = true;
  }

  /* 3. Set surface .damaged flag to avoid calling
   * pixman_region32_empty(surf->damage) */
  surf->damaged = true;
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

  /* 1. Union the requested damage into the pending damage region
   * of the surface. */
  pixman_region32_union_rect(&surf->pending.damage, &surf->pending.damage, x, y,
                             width, height);

  /* 2. Makr all outputs the surface intersects with for needing a damage
   * rebuild. */
  struct vt_output_t *output;
  wl_list_for_each(output, &surf->comp->outputs, link_global) {
    if (!(surf->_mask_outputs_visible_on & (1u << output->id)))
      continue;

    output->needs_damage_rebuild = true;
  }

  /* 3. Set surface .damaged flag to avoid calling
   * pixman_region32_empty(surf->damage) */
  surf->damaged = true;
}

void _wl_surface_set_opaque_region(struct wl_client   *client,
                                   struct wl_resource *resource,
                                   struct wl_resource *region) {
  /* [0]: Sets the region in which the surface is opaque (not transparent).
   * We can use this for occlusion tracking in the scene graph. */
  struct vt_surface_t *surf = wl_resource_get_user_data(resource);
  if (!surf) {
    VT_PARAM_CHECK_FAIL(_proto.comp);
    return;
  }

  /* 1. A NULL region means there is no opaque region in the surface.
   * If the region is set, we copy the internal handler of the given
   * region resource (pixman_region32_t) into the opaque_region region
   * of the surface. */
  pixman_region32_clear(&surf->pending.opaque_region);

  if (region) {
    struct vt_region_t *r = wl_resource_get_user_data(region);

    if (!r)
      return;

    pixman_region32_copy(&surf->pending.opaque_region, &r->region);
  }

  VT_TRACE(surf->comp->log,
           "surface.set_opaque_region: pending opaque region for surface %p",
           (void *)surf);
}

void _wl_surface_set_input_region(struct wl_client   *client,
                                  struct wl_resource *resource,
                                  struct wl_resource *region) {
  /* [0]: Sets the region in which the surface accepts input
   * events. */
  struct vt_surface_t *surf =
      resource ? wl_resource_get_user_data(resource) : NULL;

  if (!surf)
    return;

  surf->pending.input_region_changed = true;
  surf->pending.input_region_set = region != NULL;

  /* 1. A NULL region means the entire surface accepts input events.
   * If the region is not NULL, we copy the user data of the
   * region resource (pixman_region32_t) into the pending input_region
   * of the surface. By default we clear the pending input_region. */

  pixman_region32_clear(&surf->pending.input_region);

  if (region) {
    struct vt_region_t *r = wl_resource_get_user_data(region);

    if (!r)
      return;

    pixman_region32_copy(&surf->pending.input_region, &r->region);
  }

  VT_TRACE(surf->comp->log,
           "surface.set_input_region: pending input region for surface %p",
           (void *)surf);
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
