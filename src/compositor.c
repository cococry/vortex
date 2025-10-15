#define _GNU_SOURCE
#include "backend.h"
#include "log.h"

#include "backends/drm.h"
#include "backends/wayland.h"
#include "renderers/egl_gl46.h"

#include <time.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>

#include <wayland-server.h>

#include "xdg-shell-protocol.h"

static void _comp_frame_handler(void* data);

static bool _comp_render_output(vt_compositor_t* c, vt_output_t* output);

static void _comp_handle_cmd_flags(vt_compositor_t* c, int argc, char** argv);

static void _comp_log_help();

static void _comp_implement_backend(vt_compositor_t* c);

static void _comp_implement_render(vt_compositor_t* c);

static bool _comp_running_on_hardware(void);

static bool _comp_wl_init(vt_compositor_t* c);

static void  _comp_wl_bind(struct wl_client *client, void *data,
               uint32_t version, uint32_t id);

static void _comp_associate_surface_with_output(vt_compositor_t* c, vt_surface_t* surf, vt_output_t* output);

static void _comp_wl_xdg_wm_base_bind(
  struct wl_client *client, void *data,
  uint32_t version, uint32_t id);

static void _comp_wl_surface_create(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);


static void _comp_wl_xdg_wm_base_destroy(struct wl_client *client, struct wl_resource *resource);

static void _comp_wl_xdg_wm_base_create_positioner(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

static void _comp_wl_xdg_wm_base_get_xdg_surface(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id,
  struct wl_resource *surface_res);

static void _comp_wl_xdg_wm_base_pong(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t serial);

static void _comp_wl_xdg_positioner_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

static void _comp_wl_xdg_positioner_set_size(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t width,
  int32_t height);

static void _comp_wl_xdg_positioner_set_anchor_rect(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

static void _comp_wl_xdg_positioner_set_anchor(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t anchor);

static void _comp_wl_xdg_positioner_set_gravity(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t gravity);

static void _comp_wl_xdg_positioner_set_constraint_adjustment(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t constraint_adjustment);

static void _comp_wl_xdg_positioner_set_offset(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y);

static void _comp_wl_xdg_surface_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

static void _comp_wl_xdg_surface_get_toplevel(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

static void _comp_wl_xdg_surface_get_popup(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id,
  struct wl_resource *parent_surface,
  struct wl_resource *positioner);

static void _comp_wl_xdg_surface_ack_configure(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t serial);

static void _comp_wl_xdg_surface_set_window_geometry(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

static void _comp_wl_xdg_toplevel_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

static void _comp_wl_xdg_toplevel_set_parent(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *parent_resource);

static void _comp_wl_xdg_toplevel_set_title(
  struct wl_client *client,
  struct wl_resource *resource,
  const char *title);

static void _comp_wl_xdg_toplevel_set_app_id(
  struct wl_client *client,
  struct wl_resource *resource,
  const char *app_id);

static void _comp_wl_xdg_toplevel_show_window_menu(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial,
  int32_t x,
  int32_t y);

static void _comp_wl_xdg_toplevel_move(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial);

static void _comp_wl_xdg_toplevel_resize(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial,
  uint32_t edges);

static void _comp_wl_xdg_toplevel_set_max_size(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t width,
  int32_t height);

static void _comp_wl_xdg_toplevel_set_min_size(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t width,
  int32_t height);

static void _comp_wl_xdg_toplevel_set_maximized(
  struct wl_client *client,
  struct wl_resource *resource);

static void _comp_wl_xdg_toplevel_unset_maximized(
  struct wl_client *client,
  struct wl_resource *resource);

static void _comp_wl_xdg_toplevel_set_fullscreen(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *output);

static void _comp_wl_xdg_toplevel_unset_fullscreen(struct wl_client *client, struct wl_resource *resource);

static void _comp_wl_xdg_toplevel_set_minimized(struct wl_client *client, struct wl_resource *resource);

static void _comp_wl_surface_attach(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *buffer,
  int32_t x,
  int32_t y);

static void _comp_wl_surface_commit(
  struct wl_client *client,
  struct wl_resource *resource);

static void _comp_wl_surface_frame(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t callback);

static void _comp_wl_surface_damage(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);  

static void _comp_wl_surface_set_opaque_region(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *region);

static void _comp_wl_surface_set_input_region(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *region);

static void _comp_wl_surface_set_buffer_transform(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t transform);

static void _comp_wl_surface_set_buffer_scale(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t scale);

static void _comp_wl_surface_damage_buffer(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

static void _comp_wl_surface_offset(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x,
                           int32_t y);

static void _comp_wl_surface_destroy(struct wl_client *client,
                            struct wl_resource *resource);

static void _comp_wl_surface_handle_resource_destroy(struct wl_resource* resource);

static const struct wl_compositor_interface compositor_impl = {
  .create_surface = _comp_wl_surface_create,
  .create_region = NULL
};

static const struct xdg_positioner_interface xdg_positioner_impl = {
  .destroy = _comp_wl_xdg_positioner_destroy,
  .set_size = _comp_wl_xdg_positioner_set_size,
  .set_anchor_rect = _comp_wl_xdg_positioner_set_anchor_rect,
  .set_anchor = _comp_wl_xdg_positioner_set_anchor,
  .set_gravity = _comp_wl_xdg_positioner_set_gravity,
  .set_constraint_adjustment = _comp_wl_xdg_positioner_set_constraint_adjustment,
  .set_offset = _comp_wl_xdg_positioner_set_offset,
};


static const struct xdg_toplevel_interface xdg_toplevel_impl = {
  .destroy = _comp_wl_xdg_toplevel_destroy,
  .set_parent = _comp_wl_xdg_toplevel_set_parent,
  .set_title = _comp_wl_xdg_toplevel_set_title,
  .set_app_id = _comp_wl_xdg_toplevel_set_app_id,
  .show_window_menu = _comp_wl_xdg_toplevel_show_window_menu,
  .move = _comp_wl_xdg_toplevel_move,
  .resize = _comp_wl_xdg_toplevel_resize,
  .set_max_size = _comp_wl_xdg_toplevel_set_max_size,
  .set_min_size = _comp_wl_xdg_toplevel_set_min_size,
  .set_maximized = _comp_wl_xdg_toplevel_set_maximized,
  .unset_maximized = _comp_wl_xdg_toplevel_unset_maximized,
  .set_fullscreen = _comp_wl_xdg_toplevel_set_fullscreen,
  .unset_fullscreen = _comp_wl_xdg_toplevel_unset_fullscreen,
  .set_minimized = _comp_wl_xdg_toplevel_set_minimized,
};


static const struct xdg_surface_interface xdg_surface_impl = {
  .destroy = _comp_wl_xdg_surface_destroy,
  .get_toplevel = _comp_wl_xdg_surface_get_toplevel,
  .get_popup = _comp_wl_xdg_surface_get_popup,
  .ack_configure = _comp_wl_xdg_surface_ack_configure,
  .set_window_geometry = _comp_wl_xdg_surface_set_window_geometry,
};

static const struct xdg_wm_base_interface xdg_wm_base_impl = {
  .destroy = _comp_wl_xdg_wm_base_destroy,
  .create_positioner = _comp_wl_xdg_wm_base_create_positioner,
  .get_xdg_surface = _comp_wl_xdg_wm_base_get_xdg_surface,
  .pong = _comp_wl_xdg_wm_base_pong,
};

static const struct wl_surface_interface surface_impl = {
  .attach = _comp_wl_surface_attach,
  .commit = _comp_wl_surface_commit,
  .damage = _comp_wl_surface_damage, 
  .frame = _comp_wl_surface_frame,
  .set_opaque_region = _comp_wl_surface_set_opaque_region,
  .set_input_region = _comp_wl_surface_set_input_region,
  .set_buffer_scale = _comp_wl_surface_set_buffer_scale,
  .set_buffer_transform = _comp_wl_surface_set_buffer_transform,
  .offset = _comp_wl_surface_offset, 
  .destroy = _comp_wl_surface_destroy,
  .damage_buffer = _comp_wl_surface_damage_buffer, 
};

void 
_comp_frame_handler(void *data) {
  vt_output_t* output = data;
  if(!output) return;
  vt_compositor_t* c = output->backend->comp;
  if(!c) return;
  if(output->backend->comp->suspended) {
    // Avoid busy loop
    output->repaint_pending = false;
    return;
  }

  if(!c->backend->impl.prepare_output_frame(c->backend, output)) {
    // Avoid busy loop
    output->repaint_pending = false;
    return;
  }
  if(!_comp_render_output(c, output)) {
    // Avoid busy loop
    output->repaint_pending = false;
    return;
  }
  log_trace(c->log, "Pending repaint on output %p got satisfied.", output);
  if(output->repaint_source) {
    wl_event_source_remove(output->repaint_source);
    output->repaint_source = NULL;
  }
  output->repaint_pending = false;
}

/* Heed my words struggeler... */
void 
comp_send_frame_callbacks_for_output(vt_compositor_t *c, vt_output_t* output, uint32_t t) {
  // Basically iterate each surface on a specific output and for each surface iterate 
  // each frame pending callback (since the last page flip on that output) and 
  // let the client know we're done rendering their frames by calling 
  // wl_callback_send_done. 
  //
  // [!] This is the mechanism by which we achive vblank frame pacing.
  vt_surface_t *surf;
  wl_list_for_each(surf, &c->surfaces, link) {
    if(!surf->needs_frame_done) continue; 
    
    surf->_mask_outputs_presented_on |= (1u << output->id);

    if(!(surf->_mask_outputs_visible_on & (1u << output->id))) continue;

    if((surf->_mask_outputs_presented_on & surf->_mask_outputs_visible_on) == surf->_mask_outputs_visible_on) {
      vt_frame_cb *cb, *tmp;
      wl_list_for_each_safe(cb, tmp, &surf->frame_cbs, link) {
        wl_callback_send_done(cb->res, t);
        if(!c->sent_frame_cbs) c->sent_frame_cbs = true;
        wl_resource_destroy(cb->res);
        wl_list_remove(&cb->link);
        free(cb);
      }
      surf->needs_frame_done = false;
      surf->_mask_outputs_presented_on = 0;
      log_trace(surf->comp->log, "Sent wl_callback.done() for all pending frame callbacks on output %p.", output)
    }
  }
}

void 
comp_send_frame_callbacks(vt_compositor_t *c, vt_output_t* output, uint32_t t) {
  // Basically iterate each surface and for each surface iterate 
  // each frame pending callback (since the last page flip) and 
  // let the client know we're done rendering their frames by calling 
  // wl_callback_send_done
  vt_surface_t *surf;
  wl_list_for_each(surf, &c->surfaces, link) {
    if(!surf->needs_frame_done) continue; 

    vt_frame_cb *cb, *tmp;
    wl_list_for_each_safe(cb, tmp, &surf->frame_cbs, link) {
      wl_callback_send_done(cb->res, t);
      if(!c->sent_frame_cbs) c->sent_frame_cbs = true;
      wl_resource_destroy(cb->res);
      wl_list_remove(&cb->link);
      free(cb);
    }
    surf->needs_frame_done = false;
    log_trace(surf->comp->log, "Sent wl_callback.done() for all pending frame callbacks on output %p.", output)
  }
}

bool
_comp_render_output(vt_compositor_t* c, vt_output_t* output) {
  if(!c || !output || !c->backend || !c->backend->renderer || 
    !c->backend->renderer->impl.begin_frame   || 
    !c->backend->renderer->impl.draw_surface  || 
    !c->backend->renderer->impl.end_frame     || 
    !c->backend->impl.handle_frame 
  ) return false;

  vt_surface_t *surf;

  c->backend->renderer->impl.begin_frame(c->backend->renderer, output);


  wl_list_for_each_reverse(surf, &c->surfaces, link) {
    // Skip if surface and output don’t intersect
    if (surf->x + surf->width  <= output->x ||
      surf->x >= output->x + output->width ||
      surf->y + surf->height <= output->y ||
      surf->y >= output->y + output->height)
      continue;

    c->backend->renderer->impl.draw_surface(c->backend->renderer, surf,
                                            surf->x - output->x, surf->y - output->y);
  }
  c->backend->renderer->impl.end_frame(c->backend->renderer, output);

  c->backend->impl.handle_frame(c->backend, output);
  output->repaint_pending = false;

  return true;
}

static bool 
_flag_cmp(const char* flag, const char* lng, const char* shrt) {
  return strcmp(flag, lng) == 0 || strcmp(flag, shrt) == 0;
}
void 
_comp_handle_cmd_flags(vt_compositor_t* c, int argc, char** argv) {
  if(argc > 1) {
    for(uint32_t i = 1; i < argc; i++) {
      char* flag = argv[i];
      if(_flag_cmp(flag, "--logfile", "-lf")) {
        c->log.stream = fopen(log_get_filepath(), "w"); 
        if (c->log.stream)
          setvbuf(c->log.stream, NULL, _IONBF, 0);
      } else if(_flag_cmp(flag, "--verbose", "-vb")) {
        c->log.verbose = true;
      } else if(_flag_cmp(flag, "--quiet", "-q")) {
        c->log.quiet = true;
      } else if(_flag_cmp(flag, "-h", "--help")) {
        _comp_log_help();
      } else if(_flag_cmp(flag, "-v", "--version")) {
        printf(_VERSION"\n");
        exit(0);
      } else if(_flag_cmp(flag, "-n", "--nested")) {
        c->backend->platform = VT_BACKEND_WAYLAND;
      } 
      else if(_flag_cmp(flag, "-vo", "--virtual-outputs")) {
        if (i + 1 >= argc) {
          log_error(c->log, "Missing value for %s", flag);
          exit(1);
        }
        c->n_virtual_outputs = atoi(argv[++i]);
        if (c->n_virtual_outputs <= 0)
          c->n_virtual_outputs = 1;
        log_trace(c->log, "Virtual outputs set to %d", c->n_virtual_outputs);
      }
      else {
        log_error(c->log, "invalid option -- '%s'", flag);
        exit(1);
      }
    }
  }
}

void _comp_log_help() {
  printf("Usage: vortex [option:s] (value:s)\n");
  printf("Options: \n");
  printf("%-30s %s\n", "-h, --help", "Show this help message and exit");
  printf("%-30s %s\n", "-v, --version", "Show version information");
  printf("%-30s %s\n", "-vb, --verbose", "Log verbose (trace) output");
  printf("%-30s %s\n", "-lf, --logfile", 
         "Write logs to a logfile (~/.local/state/vortex/logs/ or if available $XDG_STATE_HOME/vortex/logs)");
  printf("%-30s %s\n", "-q, --quiet", "Run in quiet mode");
  printf("%-30s %s\n", "-vo, --virtual-outputs [val]", "Specify the number of virtual outputs (windows) in nested mode");
  exit(0);
}

void 
_comp_implement_backend(vt_compositor_t* c) {
  if(!c || !c->backend) return;

  if(c->backend->platform == VT_BACKEND_DRM_GBM) {
    c->backend->impl = (vt_backend_interface_t){
      .init = backend_init_drm,
      .handle_event = backend_handle_event_drm,
      .suspend = backend_suspend_drm,
      .resume = backend_resume_drm,
      .handle_frame = backend_handle_frame_drm,
      .initialize_active_outputs = backend_initialize_active_outputs_drm, 
      .terminate = backend_terminate_drm,
      .create_output = backend_create_output_drm, 
      .destroy_output = backend_destroy_output_drm, 
      .prepare_output_frame = backend_prepare_output_frame_drm,
      .__handle_input = backend___handle_input_drm
    };
  } else if(c->backend->platform == VT_BACKEND_WAYLAND) {
    c->backend->impl = (vt_backend_interface_t){
      .init = backend_init_wl,
      .handle_event = backend_handle_event_wl,
      .suspend = backend_suspend_wl,
      .resume = backend_resume_wl,
      .handle_frame = backend_handle_frame_wl,
      .initialize_active_outputs = backend_initialize_active_outputs_wl, 
      .terminate = backend_terminate_wl,
      .create_output = backend_create_output_wl, 
      .destroy_output = backend_destroy_output_wl, 
      .prepare_output_frame = backend_prepare_output_frame_wl,
      .__handle_input = backend___handle_input_wl
    };
  } else if(c->backend->platform == VT_BACKEND_SURFACELESS) {
    log_fatal(c->log, "Surfaceless backend is not implemented yet.");
  }
}

void 
_comp_implement_render(vt_compositor_t* c) {
  if(!c || !c->backend || !c->backend->renderer) return;

  if(c->backend->renderer->rendering_backend == VT_RENDERING_BACKEND_EGL_OPENGL) {
    c->backend->renderer->impl = (vt_renderer_interface_t){
      .init = renderer_init_egl, 
      .setup_renderable_output = renderer_setup_renderable_output_egl, 
      .resize_renderable_output = renderer_resize_renderable_output_egl,
      .destroy_renderable_output = renderer_destroy_renderable_output_egl,
      .import_buffer = renderer_import_buffer_egl,
      .drop_context = renderer_drop_context_egl, 
      .begin_frame = renderer_begin_frame_egl,
      .draw_surface = renderer_draw_surface_egl,
      .end_frame = renderer_end_frame_egl,
      .destroy = renderer_destroy_egl
    };
  }
}

bool
_comp_running_on_hardware(void) {
  if (!isatty(STDIN_FILENO))
    return false;

  char *tty = ttyname(STDIN_FILENO);
  if (!tty)
    return false;

  if (strncmp(tty, "/dev/tty", 8) != 0)
    return false;

  struct vt_stat vts;
  if (ioctl(STDIN_FILENO, VT_GETSTATE, &vts) == 0)
    return true;

  return false;
}

void 
_comp_wl_bind(struct wl_client *client, void *data,
               uint32_t version, uint32_t id) {
  struct wl_resource *res = wl_resource_create(client,&wl_compositor_interface,version,id);
  wl_resource_set_implementation(res,&compositor_impl, data, NULL);
}

void _comp_associate_surface_with_output(vt_compositor_t* c, vt_surface_t* surf, vt_output_t* output) {
  // Skip if surface and output don’t intersect
  if (surf->x + surf->width  <= output->x ||
    surf->x >= output->x + output->width ||
    surf->y + surf->height <= output->y ||
    surf->y >= output->y + output->height) return;
  surf->_mask_outputs_visible_on |= (1u << output->id);

}

void
_comp_wl_xdg_wm_base_bind(
  struct wl_client *client, void *data,
  uint32_t version, uint32_t id) {
  struct wl_resource* res = wl_resource_create(client, &xdg_wm_base_interface, version, id);
  wl_resource_set_implementation(res, &xdg_wm_base_impl, data, NULL);
}

void 
_comp_wl_surface_create(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id) {
  vt_compositor_t* c = wl_resource_get_user_data(resource);
  log_trace(c->log, "Got compositor.surface_create: Started managing surface.")
  // Allocate the struct to store compositor information about the surface
  vt_surface_t* surf = COMP_ALLOC(c, sizeof(*surf));
  surf->x = 0;
  surf->y = 0; 
  surf->title  = NULL;
  surf->app_id = NULL;
  surf->comp = c;

  wl_list_init(&surf->frame_cbs);

  // Add the surface to list of surfaces in the compositor
  wl_list_insert(&c->surfaces, &surf->link);
  log_trace(c->log, "compositor.surface_create: Inserted surface into list.")

  // Get the surface's wayland resource
  struct wl_resource* res = wl_resource_create(client, &wl_surface_interface, 4, id);
  wl_resource_set_implementation(res, &surface_impl, surf, _comp_wl_surface_handle_resource_destroy);
  surf->surf_res = res;
  
  log_trace(c->log, "compositor.surface_create: Setting surface implementation.")
}

void 
_comp_wl_xdg_wm_base_destroy(struct wl_client *client, struct wl_resource *resource)
{
  wl_resource_destroy(resource);
}

void 
_comp_wl_xdg_wm_base_create_positioner(struct wl_client *client,
                                             struct wl_resource *resource, uint32_t id)
{
  struct wl_resource *pos = wl_resource_create(client, &xdg_positioner_interface,
                                               wl_resource_get_version(resource), id);
  wl_resource_set_implementation(pos, &xdg_positioner_impl, NULL, NULL);
}

void 
_comp_wl_xdg_wm_base_get_xdg_surface(struct wl_client *client,
                                           struct wl_resource *resource,
                                           uint32_t id, struct wl_resource *surface_res)
{
  struct wl_resource *xdg_surf = wl_resource_create(client, &xdg_surface_interface,
                                                    wl_resource_get_version(resource), id);
  vt_surface_t* surf = wl_resource_get_user_data(surface_res);
  surf->xdg_surf_res = xdg_surf;
  wl_resource_set_implementation(xdg_surf, &xdg_surface_impl, surf, NULL);
}

void 
_comp_wl_xdg_wm_base_pong(struct wl_client *client,
                                struct wl_resource *resource, uint32_t serial)
{
}

void 
_comp_wl_xdg_positioner_destroy(struct wl_client *client,
                                      struct wl_resource *resource)
{
  wl_resource_destroy(resource);
}

void 
_comp_wl_xdg_positioner_set_size(struct wl_client *client,
                                       struct wl_resource *resource,
                                       int32_t width, int32_t height)
{
}

void 
_comp_wl_xdg_positioner_set_anchor_rect(struct wl_client *client,
                                              struct wl_resource *resource,
                                              int32_t x, int32_t y,
                                              int32_t width, int32_t height)
{
}

void 
_comp_wl_xdg_positioner_set_anchor(struct wl_client *client,
                                         struct wl_resource *resource,
                                         uint32_t anchor)
{
}

void 
_comp_wl_xdg_positioner_set_gravity(struct wl_client *client,
                                          struct wl_resource *resource,
                                          uint32_t gravity)
{
}

void 
_comp_wl_xdg_positioner_set_constraint_adjustment(struct wl_client *client,
                                                        struct wl_resource *resource,
                                                        uint32_t constraint_adjustment)
{
}

void 
_comp_wl_xdg_positioner_set_offset(struct wl_client *client,
                                         struct wl_resource *resource,
                                         int32_t x, int32_t y)
{
}

void 
_comp_wl_xdg_surface_destroy(struct wl_client *client,
                                   struct wl_resource *resource)
{
  wl_resource_destroy(resource);
}

void
send_initial_configure(vt_surface_t* surf)
{
  struct wl_array states;
  wl_array_init(&states);

  // 0,0 = let client decide initial size
  xdg_toplevel_send_configure(surf->xdg_toplevel_res, 0, 0, &states);

  xdg_surface_send_configure(surf->xdg_surf_res,
                             wl_display_next_serial(surf->comp->wl.dsp));

  wl_array_release(&states);
}


void 
_comp_wl_xdg_surface_get_toplevel(struct wl_client *client,
                                        struct wl_resource *resource,
                                        uint32_t id)
{
  vt_surface_t* surf = wl_resource_get_user_data(resource);

  struct wl_resource *top = wl_resource_create(client, &xdg_toplevel_interface,
                                               wl_resource_get_version(resource), id);
  wl_resource_set_implementation(top, &xdg_toplevel_impl, surf, NULL);
  surf->xdg_toplevel_res = top;

  // send an initial configure
  send_initial_configure(surf);
}

void 
_comp_wl_xdg_surface_get_popup(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id,
                                     struct wl_resource *parent_surface,
                                     struct wl_resource *positioner)
{
}

void 
_comp_wl_xdg_surface_ack_configure(struct wl_client *client,
                                         struct wl_resource *resource,
                                         uint32_t serial)
{
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  surf->last_configure_serial = serial;
}

void 
_comp_wl_xdg_surface_set_window_geometry(struct wl_client *client,
                                               struct wl_resource *resource,
                                               int32_t x, int32_t y,
                                               int32_t width, int32_t height)
{
}



void
_comp_wl_xdg_toplevel_destroy(struct wl_client *client, struct wl_resource *resource)
{
  wl_resource_destroy(resource);
}

void
_comp_wl_xdg_toplevel_set_parent(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *parent_resource)
{
}

void
_comp_wl_xdg_toplevel_set_title(struct wl_client *client,
                          struct wl_resource *resource,
                          const char *title)
{
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  if (surf->title)
    free(surf->title);
  surf->title = strdup(title ? title : "");
}

void
_comp_wl_xdg_toplevel_set_app_id(struct wl_client *client,
                           struct wl_resource *resource,
                           const char *app_id)
{
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  if (surf->app_id)
    free(surf->app_id);
  surf->app_id = strdup(app_id ? app_id : "");
}

void
_comp_wl_xdg_toplevel_show_window_menu(struct wl_client *client,
                                 struct wl_resource *resource,
                                 struct wl_resource *seat,
                                 uint32_t serial,
                                 int32_t x, int32_t y)
{
  // optional: ignore
}

void
_comp_wl_xdg_toplevel_move(struct wl_client *client,
                     struct wl_resource *resource,
                     struct wl_resource *seat,
                     uint32_t serial)
{
  // optional: ignore
}

void
_comp_wl_xdg_toplevel_resize(struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *seat,
                       uint32_t serial,
                       uint32_t edges)
{
  // optional: ignore
}

void
_comp_wl_xdg_toplevel_set_max_size(struct wl_client *client,
                             struct wl_resource *resource,
                             int32_t width, int32_t height)
{
  // optional: ignore
}

void
_comp_wl_xdg_toplevel_set_min_size(struct wl_client *client,
                             struct wl_resource *resource,
                             int32_t width, int32_t height)
{
  // optional: ignore
}

void
_comp_wl_xdg_toplevel_set_maximized(struct wl_client *client,
                              struct wl_resource *resource)
{
  // optional: ignore
}

void
_comp_wl_xdg_toplevel_unset_maximized(struct wl_client *client,
                                struct wl_resource *resource)
{
}

void
_comp_wl_xdg_toplevel_set_fullscreen(struct wl_client *client,
                               struct wl_resource *resource,
                               struct wl_resource *output)
{
}

void
_comp_wl_xdg_toplevel_unset_fullscreen(struct wl_client *client,
                                 struct wl_resource *resource)
{
}

void
_comp_wl_xdg_toplevel_set_minimized(struct wl_client *client,
                              struct wl_resource *resource)
{
}

void 
_comp_wl_surface_attach(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *buffer,
  int32_t x,
  int32_t y) {
  // When a client attaches an allocated buffer, store the resource handle 
  // in the surface struct
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  if(!surf) {
    log_error(surf->comp->log, "compositor.surface_attach: No internal surface data allocated.")
    return;
  }
  log_trace(surf->comp->log, "Got compositor.surface_attach.")
  surf->buf_res = buffer;
}

void 
_comp_wl_surface_commit(
  struct wl_client *client,
  struct wl_resource *resource) {
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  if(!surf) {
    log_error(surf->comp->log, "compositor.surface_attach: No internal surface data allocated.")
    return;
  }

  log_trace(surf->comp->log, "Got compositor.surface_commit.")

  if (!surf) { log_error(surf->comp->log, "surface_commit: NULL user_data"); return; }

  // No buffer attached, this commit is illegal  
  if (!surf->buf_res) {
    log_warn(surf->comp->log, "compositor.surface_commit: Got commit request without attached buffer.")
    return;
  }

  if(surf->comp->backend->renderer->impl.import_buffer)
    surf->comp->backend->renderer->impl.import_buffer(surf->comp->backend->renderer, surf, surf->buf_res);

  // Tell the client we're finsied uploading its buffer
  wl_buffer_send_release(surf->buf_res);

  // If the size of the surface changed, 
  // we need to recalculate the outputs that the surface is visible on 
  if(surf->width != surf->tex.width || surf->height != surf->tex.height) {
    surf->_mask_outputs_visible_on = 0;
  }
  surf->width = surf->tex.width;
  surf->height = surf->tex.height;

  if(!surf->_mask_outputs_visible_on) {
    vt_output_t* output;
    wl_list_for_each(output, &surf->comp->backend->outputs, link) {
      _comp_associate_surface_with_output(surf->comp, surf, output);
    }
  }

  log_trace(surf->comp->log, "compositor.surface_commit: Scheduling repaint to render commited buffer.")

  // Schedule a repaint for all outputs that the surface intersects with
  vt_output_t* output;
  wl_list_for_each(output, &surf->comp->backend->outputs, link) {
    if(!(surf->_mask_outputs_visible_on & (1u << output->id))) continue;
    comp_schedule_repaint(surf->comp, output);
  }
}


struct vt_frame_cb {
  struct wl_resource *res;
  struct wl_list link;
};
void
_comp_wl_surface_frame(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t callback) {
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  if(!surf) {
    log_error(surf->comp->log, "compositor.surface_frame: No internal surface data allocated.")
    return;
  }

  log_trace(surf->comp->log, "Got compositor.surface_frame.")
  struct vt_frame_cb *node = COMP_ALLOC(surf->comp, sizeof(*node));
  node->res = wl_resource_create(client, &wl_callback_interface, 1, callback);

  // Store the frame callback in the list of pending frame callbacks.
  // wl_callback_send_done must be called for each of the pending callbacks
  // after the next page flip event completes in order to correctly handle 
  // frame pacing ( see send_frame_callbacks() ).
  wl_list_insert(&surf->frame_cbs, &node->link);
  
  log_trace(surf->comp->log, "compositor.surface_frame: Inserting frame callback into list of surface %p.", surf)

  surf->needs_frame_done = true;
 
  if(surf->comp->backend->impl.handle_surface_frame)
    surf->comp->backend->impl.handle_surface_frame(surf->comp->backend, surf);
}

void 
_comp_wl_surface_damage(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height) {

}

void
_comp_wl_surface_set_opaque_region(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *region) {

}

void 
_comp_wl_surface_set_input_region(struct wl_client *client,
                              struct wl_resource *resource,
                              struct wl_resource *region) {

}

void
_comp_wl_surface_set_buffer_transform(struct wl_client *client,
                                  struct wl_resource *resource,
                                  int32_t transform) {

}

void
_comp_wl_surface_set_buffer_scale(struct wl_client *client,
                              struct wl_resource *resource,
                              int32_t scale) {

}

void 
_comp_wl_surface_damage_buffer(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x,
                           int32_t y,
                           int32_t width,
                           int32_t height) {

}

void
_comp_wl_surface_offset(struct wl_client *client,
                    struct wl_resource *resource,
                    int32_t x,
                    int32_t y) {

}

void
_comp_wl_surface_destroy(struct wl_client *client,
                     struct wl_resource *resource) {
  vt_compositor_t* comp = ((vt_surface_t*)wl_resource_get_user_data(resource))->comp;
  log_trace(comp->log, 
            "Got surface.destroy: Destroying surface resource.")
  wl_resource_destroy(resource);
  
}

void 
_comp_wl_surface_handle_resource_destroy(struct wl_resource* resource) {
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  log_trace(surf->comp->log, 
            "Got surface.destroy handler: Unmanaging client.")

  wl_list_remove(&surf->link);
  free(surf);

  // Schedule a repaint for all outputs that the surface intersects with
  vt_output_t* output;
  wl_list_for_each(output, &surf->comp->backend->outputs, link) {
    if(!(surf->_mask_outputs_visible_on & (1u << output->id))) continue;
    comp_schedule_repaint(surf->comp, output);
  }
}


bool 
_comp_wl_init(vt_compositor_t* c) {
  if(!(c->wl.dsp = wl_display_create())) {
    log_error(c->log, "cannot create wayland display.");
    return false;
  }

  log_trace(c->log, "Sucessfully created wayland display.");

  wl_list_init(&c->surfaces);

  if(!(c->wl.evloop = wl_display_get_event_loop(c->wl.dsp))) {
    log_error(c->log, "Cannot get wayland event loop.");
    return false;
  }

  wl_display_init_shm(c->wl.dsp);

  wl_global_create(c->wl.dsp, &wl_compositor_interface, 4, c, _comp_wl_bind); 

  if(!(c->wl.xdg_wm_base = wl_global_create(c->wl.dsp, &xdg_wm_base_interface, 1, NULL, _comp_wl_xdg_wm_base_bind))) {
    log_error(c->log, "Cannot implement XDG base interface.");
    return false;
  }

  const char *socket_name = wl_display_add_socket_auto(c->wl.dsp);
  if (!socket_name) {
    log_error(c->log, "Failed to create Wayland socket: no clients will be able to connect.");
    return false; 
  } else {
    log_trace(c->log, "Wayland display ready on socket '%s'.", socket_name);
  }
  

  return true;
}

static int 
libinput_fd_ready(int fd, uint32_t mask, void *data) {
  vt_compositor_t* c = data;
  static bool alt_down = false, ctrl_down = false;
  libinput_dispatch(c->input);

  struct libinput_event *event;
  while ((event = libinput_get_event(c->input)) != NULL) {
    enum libinput_event_type type = libinput_event_get_type(event);

    switch (type) {
      case LIBINPUT_EVENT_KEYBOARD_KEY: {
        struct libinput_event_keyboard *kbevent =
          libinput_event_get_keyboard_event(event);
        uint32_t key = libinput_event_keyboard_get_key(kbevent);
        enum libinput_key_state state =
          libinput_event_keyboard_get_key_state(kbevent);

        if (state == LIBINPUT_KEY_STATE_PRESSED && key == KEY_LEFTCTRL)
          ctrl_down = true;
        else if (state == LIBINPUT_KEY_STATE_RELEASED && key == KEY_LEFTCTRL)
          ctrl_down = false;

        if (state == LIBINPUT_KEY_STATE_PRESSED && key == KEY_LEFTALT)
          alt_down = true;
        else if (state == LIBINPUT_KEY_STATE_RELEASED && key == KEY_LEFTALT)
          alt_down = false;

        if (state == LIBINPUT_KEY_STATE_PRESSED && key == 41) {
          c->running = false;
        }

        bool mods = (alt_down && ctrl_down &&
          state == LIBINPUT_KEY_STATE_PRESSED);

        if(c->backend->impl.__handle_input)
          c->backend->impl.__handle_input(c->backend, mods, key);
        break;
      }

      default:
        break;
    }

    libinput_event_destroy(event);
  }

  return 0;
}



static inline int input_open_restricted(const char* path, int32_t flags, void* user_data) {
  int fd = open(path, flags);
  return fd < 0 ? -errno : fd;
}

static inline void input_close_restricted(int32_t fd, void* user_data) {
  close(fd);
}


static const struct libinput_interface input_interface = {
  .close_restricted = input_close_restricted,
  .open_restricted = input_open_restricted
};

bool
comp_init(vt_compositor_t* c, int argc, char** argv) {
  c->log.stream = stdout;
  c->log.verbose = false;
  c->log.quiet = false;

  c->backend = calloc(1, sizeof(*c->backend));
  c->backend->renderer = calloc(1, sizeof(*c->backend->renderer));
  c->backend->comp = c;
  c->backend->renderer->comp = c;

  c->backend->platform = VT_BACKEND_DRM_GBM; 
  c->backend->renderer->rendering_backend = VT_RENDERING_BACKEND_EGL_OPENGL;

  c->n_virtual_outputs = 2;

  _comp_handle_cmd_flags(c, argc, argv);

  _comp_implement_backend(c);
  _comp_implement_render(c);

  if(!_comp_wl_init(c)) {
    log_error(c->log, "Failed to initialize wayland state.")
    return false;
  }

  // Initialize backend 
  if(!(c->backend->impl.init(c->backend))) {
    log_error(c->log, "Failed to initialize compositor backend.")
    return false;
  }
  // initialize renderer
  if(!(c->backend->renderer->impl.init(c->backend, c->backend->renderer, c->backend->native_handle))) {
    log_error(c->log, "Failed to initialize rendering backend.")
    return false;
  }

  // Initialize outputs
  if(!(c->backend->impl.initialize_active_outputs(c->backend))) {
    log_error(c->log, "Failed to initialize backend outputs.")
    return false;
  }

  if(c->backend->platform == VT_BACKEND_DRM_GBM) {
    struct udev* udev = udev_new();
    if(!udev) {
      log_error(c->log, "Failed to create udev context.");
      return false;
    } else {
      log_trace(c->log, "Successfully created udev context.");
    }
    c->input = libinput_udev_create_context(&input_interface, NULL, udev);
    if(!c->input) {
      log_error(c->log, "Failed to create libinput context.");
      return false;
    } else {
      log_trace(c->log, "Successfully created libinput context.");
    }
    libinput_udev_assign_seat(c->input, "seat0");
    libinput_dispatch(c->input);

    int li_fd = libinput_get_fd(c->input);
    wl_event_loop_add_fd(c->wl.evloop, li_fd,
                         WL_EVENT_READABLE, libinput_fd_ready, c);
  }

  vt_output_t* output;
  wl_list_for_each(output, &c->backend->outputs, link) {
    output->repaint_pending = false;
    comp_schedule_repaint(c, output);
  }
  return true;
}

void
comp_run(vt_compositor_t *c) {
  c->running = true;
  log_trace(c->log, "Entering main event loop...");
  while (c->running) {
    wl_event_loop_dispatch(c->wl.evloop, -1);
    wl_display_flush_clients(c->wl.dsp);

    if(!(c->backend->impl.handle_event(c->backend))) {
      log_warn(c->log, "Failed to handle backend event.");
    }
  }
}

bool
comp_terminate(vt_compositor_t *c) {
  log_trace(c->log, "Shutting down...");
  c->running = false;

  // Shut down renderer & backend
  if(!c->backend->renderer->impl.destroy(c->backend->renderer)) {
    log_error(c->log, "Failed to terminate renderer.");
    return false;
  }
  if(!(c->backend->impl.terminate(c->backend))) {
    log_error(c->log, "Failed to terminate backend");
    return false;
  }

  // Shut down wayland 
  if (c->wl.dsp) {
    wl_display_destroy_clients(c->wl.dsp);
    wl_display_destroy(c->wl.dsp);
    c->wl.dsp = NULL;
  }

  // Clean up log
  log_trace(c->log, "Shutdown complete.");

  if (c->log.stream && c->log.stream != stdout && c->log.stream != stderr) {
    fclose(c->log.stream);
    c->log.stream = NULL;
  }
}

void 
comp_schedule_repaint(vt_compositor_t *c, vt_output_t* output) {
  if(!c || !output) return;
  if (c->suspended) {
    log_warn(c->log, "Trying to schedule repaint while compositor is suspended.");
    return;
  }
  if (output->repaint_pending) {
    return;
}
  output->needs_repaint = true;
  if(!output->repaint_pending) {
    output->repaint_pending = true;
    output->repaint_source = wl_event_loop_add_idle(c->wl.evloop, _comp_frame_handler, output);
  }
  log_trace(c->log, "Scheduling repaint on output %p.", output);
}


uint32_t 
comp_get_time_msec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

