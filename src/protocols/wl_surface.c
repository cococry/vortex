#include "wl_surface.h"
#include "src/core/compositor.h"
#include "src/render/renderer.h"
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

static void _wl_surface_attach(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *buffer,
  int32_t x,
  int32_t y);

static void _wl_surface_commit(
  struct wl_client *client,
  struct wl_resource *resource);

static void _wl_surface_frame(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t callback);

static void _wl_surface_damage(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);  

static void _wl_surface_set_opaque_region(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *region);

static void _wl_surface_set_input_region(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *region);

static void _wl_surface_set_buffer_transform(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t transform);

static void _wl_surface_set_buffer_scale(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t scale);

static void _wl_surface_damage_buffer(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

static void _wl_surface_offset(struct wl_client *client,
                                    struct wl_resource *resource,
                                    int32_t x,
                                    int32_t y);

static void _wl_surface_destroy(struct wl_client *client,
                                     struct wl_resource *resource);

static void _wl_surface_handle_resource_destroy(struct wl_resource* resource);

static void _wl_surface_associate_with_output(
  struct vt_compositor_t* c, struct vt_surface_t* surf, struct vt_output_t* output);


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

void 
_wl_surface_attach(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *buffer,
  int32_t x,
  int32_t y) {
  // When a client attaches an allocated buffer, store the resource handle 
  // in the surface struct
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  if(!surf) {
    VT_ERROR(((struct vt_compositor_t*)wl_resource_get_user_data(resource))->log,
             "surface_attach: NULL user_data");
    return;
  }
  VT_TRACE(surf->comp->log, "Got compositor.surface_attach.")
    surf->buf_res = buffer;
}

void 
_wl_surface_commit(
  struct wl_client *client,
  struct wl_resource *resource) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  if(!surf) {
    VT_ERROR(surf->comp->log, "compositor.surface_attach: No internal surface data allocated.")
    return;
  }

  VT_TRACE(surf->comp->log, "Got compositor.surface_commit.")
    
  if (!surf) { VT_ERROR(surf->comp->log, "surface_commit: NULL user_data"); return; }

  surf->has_buffer = (surf->buf_res != NULL);
  if (!surf->mapped && surf->has_buffer && surf->xdg_surf && surf->xdg_surf->toplevel->xdg_toplevel_res) {
    // surface is becoming visible
    surf->mapped = true;
    vt_surface_mapped(surf);
  } else if (surf->mapped && !surf->has_buffer) {
    // surface lost its buffer (unmapped)
    surf->mapped = false;
    vt_surface_unmapped(surf);
  }

  // no buffer attached, this commit has no contents 
  if (!surf->has_buffer) {
    return;
  }
  if(pixman_region32_empty(&surf->pending_damage)) return;


  if (!surf->current_damage.data) {
    pixman_region32_init(&surf->current_damage);
  }
  pixman_box32_t ext =* pixman_region32_extents(&surf->pending_damage);
  pixman_region32_clear(&surf->current_damage);
  pixman_region32_union_rect(&surf->current_damage, &surf->current_damage,
                             ext.x1, ext.y1, ext.x2 - ext.x1, ext.y2 - ext.y1);
  pixman_region32_clear(&surf->pending_damage);


  // If the size of the surface changed, 
  // we need to recalculate the outputs that the surface is visible on 
  if(surf->width != surf->tex.width || surf->height != surf->tex.height) {
    surf->_mask_outputs_visible_on = 0;
  }
  surf->width = surf->tex.width;
  surf->height = surf->tex.height;

  if(!surf->_mask_outputs_visible_on) {
    struct vt_output_t* output;
    wl_list_for_each(output, &surf->comp->outputs, link_global) {
      _wl_surface_associate_with_output(surf->comp, surf, output);
    }
    // Mark as needing redraw
    pixman_region32_clear(&surf->current_damage);
    pixman_region32_union_rect(&surf->current_damage,
                               &surf->current_damage, 0, 0, surf->width, surf->height);
  }
 
  pixman_region32_intersect_rect(&surf->current_damage,
                                 &surf->current_damage,
                                 0, 0, surf->width, surf->height);


  VT_TRACE(surf->comp->log, "compositor.surface_commit: Scheduling repaint to render commited buffer.");

  // Schedule a repaint for all outputs that the surface intersects with
  struct vt_output_t* output;

  // importing the buffer
  struct vt_renderer_t* r = surf->comp->renderer;
  if(r && r->impl.import_buffer) {
    r->impl.import_buffer(r, surf, surf->buf_res);
    // Tell the client we're finsied uploading its buffer
    VT_TRACE(surf->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Sending release."); 
    wl_buffer_send_release(surf->buf_res);
  }

    VT_TRACE(surf->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Sending release."); 
  wl_list_for_each(output, &surf->comp->outputs, link_global) {
    if(!(surf->_mask_outputs_visible_on & (1u << output->id))) {
      VT_WARN(surf->comp->log, "Not rerendering for surface %p because not on output %p.\n", surf, output);
      continue;
    }
    pixman_box32_t ext = *pixman_region32_extents(&surf->current_damage);
    pixman_region32_union_rect(&output->damage, &output->damage,
                               surf->x + ext.x1, surf->y + ext.y1,
                               ext.x2 - ext.x1, ext.y2 - ext.y1); 
    vt_comp_schedule_repaint(surf->comp, output);
  }

  
  VT_TRACE(surf->comp->log, "VT_PROTO_LINUX_DMABUF_V1: Finsihed commit."); 
}


void
_wl_surface_frame(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t callback) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  if(!surf) {
    VT_ERROR(surf->comp->log, "compositor.surface_frame: No internal surface data allocated.")
    return;
  }

  VT_TRACE(surf->comp->log, "Got compositor.surface_frame.")
  struct wl_resource* res = wl_resource_create(client, &wl_callback_interface, 1, callback);

  // Store the frame callback in the list of pending frame callbacks.
  // wl_callback_send_done must be called for each of the pending callbacks
  // after the next page flip event completes in order to correctly handle 
  // frame pacing ( see send_frame_callbacks() ).
  if(surf->cb_pool.n_cbs >= VT_MAX_FRAME_CBS) {
    VT_WARN(surf->comp->log, "Surface %p already has %i frame callbacks queued - dropping new one.", surf->cb_pool.n_cbs);
    return;
  }
  surf->cb_pool.cbs[surf->cb_pool.n_cbs++] = res;

  VT_TRACE(surf->comp->log, "compositor.surface_frame: Inserting frame callback into list of surface %p.", surf)

    surf->needs_frame_done = true;
  surf->comp->any_frame_cb_pending = true;
}

void 
_wl_surface_damage(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  if(!surf) {
    VT_ERROR(surf->comp->log, "compositor.surface_damage: No internal surface data allocated.")
    return;
  }

  pixman_region32_union_rect(&surf->pending_damage, &surf->pending_damage,
                             x, y, width, height);

  pixman_box32_t extents = *pixman_region32_extents(&surf->pending_damage);


  struct vt_output_t* output;
  wl_list_for_each(output, &surf->comp->outputs, link_global) {
    if (!(surf->_mask_outputs_visible_on & (1u << output->id)))
      continue;

    output->needs_damage_rebuild = true;
  }
  surf->damaged = true;

}
void 
_wl_surface_damage_buffer(struct wl_client *client,
                                  struct wl_resource *resource,
                               int32_t x,
                               int32_t y,
                               int32_t width,
                               int32_t height) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  if(!surf) {
    VT_ERROR(surf->comp->log, "compositor.surface_damage: No internal surface data allocated.")
    return;
  }

  pixman_region32_union_rect(&surf->pending_damage, &surf->pending_damage,
                             x, y, width, height);

  pixman_box32_t extents = *pixman_region32_extents(&surf->pending_damage);


  struct vt_output_t* output;
  wl_list_for_each(output, &surf->comp->outputs, link_global) {
    if (!(surf->_mask_outputs_visible_on & (1u << output->id)))
      continue;

    output->needs_damage_rebuild = true;
  }
  surf->damaged = true;
}


void
_wl_surface_set_opaque_region(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *region) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  if (!surf) return;

  if (region) {
    struct vt_region_t* r = wl_resource_get_user_data(region);
    pixman_region32_copy(&surf->opaque_region, &r->region);
  } else {
    pixman_region32_clear(&surf->opaque_region);
  }

  VT_TRACE(surf->comp->log, "compositor.surface_set_opaque_region: updated opaque region for surface %p", surf);
}

void 
_wl_surface_set_input_region(struct wl_client *client,
                             struct wl_resource *resource,
                             struct wl_resource *region) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  if (!surf) return;

  if (region) {
    struct vt_region_t* r = wl_resource_get_user_data(region);
    pixman_region32_copy(&surf->input_region, &r->region);
  } else {
    // NULL means entire surface is input region
    pixman_region32_init_rect(&surf->input_region, 0, 0, surf->width, surf->height);
  }

  VT_TRACE(surf->comp->log, "compositor.surface_set_input_region: updated input region for surface %p", surf);
}

void
_wl_surface_set_buffer_transform(struct wl_client *client,
                                 struct wl_resource *resource,
                                 int32_t transform) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  if (!surf) return;

  if (transform < WL_OUTPUT_TRANSFORM_NORMAL ||
    transform > WL_OUTPUT_TRANSFORM_FLIPPED_270) {
    wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_TRANSFORM,
                           "invalid transform %d", transform);
    return;
  }

  surf->buffer_transform = transform;

  VT_TRACE(surf->comp->log, "surface_set_buffer_transform: transform=%d for surface %p", transform, surf);
}

void
_wl_surface_set_buffer_scale(struct wl_client *client,
                             struct wl_resource *resource,
                             int32_t scale) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  if (!surf) return;

  if (scale < 1) {
    wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_SCALE,
                           "invalid buffer scale %d", scale);
    return;
  }

  surf->buffer_scale = scale;

  VT_TRACE(surf->comp->log, "surface_set_buffer_scale: scale=%d for surface %p", scale, surf);
}

void
_wl_surface_offset(struct wl_client *client,
                   struct wl_resource *resource,
                   int32_t x,
                   int32_t y) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
  if (!surf) return;

  surf->x = x;
  surf->y = y;

  surf->_mask_outputs_visible_on = 0; // Force re-evaluation on next commit

  VT_TRACE(surf->comp->log, "surface_offset: moved surface %p to %d,%d", surf, x, y);
}

void
_wl_surface_destroy(struct wl_client *client,
                    struct wl_resource *resource) {
  struct vt_surface_t* surf = ((struct vt_surface_t*)wl_resource_get_user_data(resource));
    
  VT_TRACE(surf->comp->log, 
            "Got surface.destroy: Destroying surface resource.")
  wl_resource_destroy(resource);

}

void 
_wl_surface_handle_resource_destroy(struct wl_resource* resource) {
  struct vt_surface_t* surf = wl_resource_get_user_data(resource);
 
  if (surf->mapped) {
    vt_surface_unmapped(surf);
    surf->mapped = false;
  }

  int32_t x = surf->x;
  int32_t y = surf->y;
  int32_t w = surf->width;
  int32_t h = surf->height;
  VT_TRACE(surf->comp->log, 
            "Got surface.destroy handler: Unmanaging client.")

  wl_list_remove(&surf->link);

  pixman_region32_fini(&surf->pending_damage);
  pixman_region32_fini(&surf->current_damage);
  pixman_region32_fini(&surf->input_region);
  pixman_region32_fini(&surf->opaque_region);

  // Schedule a repaint for all outputs that the surface intersects with
  struct vt_output_t* output;
  struct vt_renderer_t* r = surf->comp->renderer;
  if(r && r->impl.destroy_surface_texture) {
    r->impl.destroy_surface_texture(r, surf);
  }

  // destroy dmabuf resources of the surface
  vt_proto_linux_dmabuf_v1_surface_destroy(surf);

  wl_list_for_each(output, &surf->comp->outputs, link_global) {
    if(!(surf->_mask_outputs_visible_on & (1u << output->id))) continue;
    // Destory surface texture with the renderer associated with the first output 
    // the surface is on
    // Damage the part of the screen where the surface was located 
    // and schedule a repaint
    pixman_region32_union_rect(
      &output->damage, &output->damage,
      x, y, w, h); 
    vt_comp_schedule_repaint(surf->comp, output);

    output->needs_damage_rebuild = true;
  }
}

void 
_wl_surface_associate_with_output(
  struct vt_compositor_t* c, struct vt_surface_t* surf, struct vt_output_t* output) {
  if (surf->x + surf->width  <= output->x ||
    surf->x >= output->x + output->width ||
    surf->y + surf->height <= output->y ||
    surf->y >= output->y + output->height) return;
  surf->_mask_outputs_visible_on |= (1u << output->id);
}



bool
vt_proto_wl_surface_init(
  struct vt_surface_t* surf, struct wl_client* client, 
  uint32_t id, uint32_t version) {
  if(!surf) return false;

  // Get the surface's wayland resource
  struct wl_resource* res = wl_resource_create(client, &wl_surface_interface, 4, id);
  wl_resource_set_implementation(res, &surface_impl, surf, _wl_surface_handle_resource_destroy);
  surf->surf_res = res;

  return true;
}
