#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "src/input/input.h"
#include "src/input/wl_seat.h"
#include "src/protocols/xdg_shell.h"
#include "src/protocols/linux_dmabuf.h"
#include "src/render/renderer.h"

#include <linux/vt.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <dirent.h>

#include <glad.h>
#include <wayland-server.h>

#include "config.h"
#include "scene.h"
#include "surface.h"
#include "core_types.h"

#include "compositor.h"

static void _vt_comp_frame_handler(void* data);

static bool _vt_comp_render_output(struct vt_compositor_t* c, struct vt_output_t* output);


static void _vt_comp_log_help();

static bool _vt_comp_wl_init(struct vt_compositor_t* c);

static void  _vt_comp_wl_bind(struct wl_client *client, void *data,
                           uint32_t version, uint32_t id);


static void _vt_comp_associate_surface_with_output(struct vt_compositor_t* c, struct vt_surface_t* surf, struct vt_output_t* output);

void _vt_comp_wl_surface_create(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

void _vt_comp_wl_surface_create_region(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

static void _vt_comp_wl_surface_attach(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *buffer,
  int32_t x,
  int32_t y);

static void _vt_comp_wl_surface_commit(
  struct wl_client *client,
  struct wl_resource *resource);

static void _vt_comp_wl_surface_frame(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t callback);

static void _vt_comp_wl_surface_damage(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);  

static void _vt_comp_wl_surface_set_opaque_region(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *region);

static void _vt_comp_wl_surface_set_input_region(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *region);

static void _vt_comp_wl_surface_set_buffer_transform(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t transform);

static void _vt_comp_wl_surface_set_buffer_scale(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t scale);

static void _vt_comp_wl_surface_damage_buffer(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

static void _vt_comp_wl_surface_offset(struct wl_client *client,
                                    struct wl_resource *resource,
                                    int32_t x,
                                    int32_t y);

static void _vt_comp_wl_surface_destroy(struct wl_client *client,
                                     struct wl_resource *resource);

static void _vt_comp_wl_surface_handle_resource_destroy(struct wl_resource* resource);

static void _vt_comp_wl_region_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

static void _vt_comp_wl_region_add(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

static void _vt_comp_wl_region_subtract(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);


static const struct wl_compositor_interface compositor_impl = {
  .create_surface = _vt_comp_wl_surface_create,
  .create_region = _vt_comp_wl_surface_create_region 
};

static const struct wl_surface_interface surface_impl = {
  .attach = _vt_comp_wl_surface_attach,
  .commit = _vt_comp_wl_surface_commit,
  .damage = _vt_comp_wl_surface_damage, 
  .frame = _vt_comp_wl_surface_frame,
  .set_opaque_region = _vt_comp_wl_surface_set_opaque_region,
  .set_input_region = _vt_comp_wl_surface_set_input_region,
  .set_buffer_scale = _vt_comp_wl_surface_set_buffer_scale,
  .set_buffer_transform = _vt_comp_wl_surface_set_buffer_transform,
  .offset = _vt_comp_wl_surface_offset, 
  .destroy = _vt_comp_wl_surface_destroy,
  .damage_buffer = _vt_comp_wl_surface_damage_buffer,
};

static const struct wl_region_interface region_impl = {
  .add = _vt_comp_wl_region_add,
  .destroy= _vt_comp_wl_region_destroy,
  .subtract = _vt_comp_wl_region_subtract,
};

static void* _vt_comp_dl_handle = NULL;

void 
_vt_comp_frame_handler(void *data) {
  struct vt_output_t* output = data;
  if(!output) return;
  struct vt_compositor_t* c = output->backend->comp;
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
  if(!_vt_comp_render_output(c, output)) {
    // Avoid busy loop
    output->repaint_pending = false;
    return;
  }
  VT_TRACE(c->log, "Pending repaint on output %p got satisfied.", output);
  if(output->repaint_source) {
    wl_event_source_remove(output->repaint_source);
    output->repaint_source = NULL;
  }
  output->repaint_pending = false;
}

/* Heed my words struggeler... */
void 
vt_comp_frame_done(struct vt_compositor_t *c, struct vt_output_t* output, uint32_t t) {
  // Basically iterate each surface on a specific output and for each surface iterate 
  // each frame pending callback (since the last page flip on that output) and 
  // let the client know we're done rendering their frames by calling 
  // wl_callback_send_done. 
  //
  // [!] This is the mechanism by which we achive vblank frame pacing.
  struct vt_surface_t *surf;
  wl_list_for_each(surf, &c->surfaces, link) {
    if(!surf->needs_frame_done) continue; 

    surf->_mask_outputs_presented_on |= (1u << output->id);

    if(!(surf->_mask_outputs_visible_on & (1u << output->id))) continue;

    if((surf->_mask_outputs_presented_on & surf->_mask_outputs_visible_on) == surf->_mask_outputs_visible_on) {
      for (uint32_t i = 0; i < surf->cb_pool.n_cbs; i++) {
        if(!surf->cb_pool.cbs[i]) continue;
        wl_callback_send_done(surf->cb_pool.cbs[i], t);
        wl_resource_destroy(surf->cb_pool.cbs[i]);
        if(!c->sent_frame_cbs) c->sent_frame_cbs = true;
      }
      surf->needs_frame_done = false;
      surf->cb_pool.n_cbs = 0;
      surf->_mask_outputs_presented_on = 0;
      VT_TRACE(surf->comp->log, "Sent wl_callback.done() for all pending frame callbacks on output %p.", output)
    }
  }
  c->any_frame_cb_pending = false;

}

void 
vt_comp_frame_done_all(struct vt_compositor_t *c, uint32_t t) {
  // Basically iterate each surface and for each surface iterate 
  // each frame pending callback (since the last page flip) and 
  // let the client know we're done rendering their frames by calling 
  // wl_callback_send_done
  struct vt_surface_t *surf;
  wl_list_for_each(surf, &c->surfaces, link) {
    if(!surf->needs_frame_done) continue; 

    for (uint32_t i = 0; i < surf->cb_pool.n_cbs; i++) {
      //if(!surf->cb_pool.cbs[i]) continue;
      wl_callback_send_done(surf->cb_pool.cbs[i], t);
      wl_resource_destroy(surf->cb_pool.cbs[i]);
      if(!c->sent_frame_cbs) c->sent_frame_cbs = true;
    }
    surf->needs_frame_done = false;
    surf->cb_pool.n_cbs = 0;
    VT_TRACE(surf->comp->log, "Sent wl_callback.done() for all pending frame callbacks.")
  }
  c->any_frame_cb_pending = false;
}

bool
_vt_comp_render_output(struct vt_compositor_t* c, struct vt_output_t* output) {
  if(!c || !c->backend || !c->backend->impl.handle_frame || !output) return false;

  vt_comp_repaint_scene(c, output);
  c->backend->impl.handle_frame(c->backend, output);
  output->repaint_pending = false;

  return true;
}


static bool 
_flag_cmp(const char* flag, const char* lng, const char* shrt) {
  return strcmp(flag, lng) == 0 || strcmp(flag, shrt) == 0;
}

static char** _scan_valid_backends(size_t *count_out) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", VORTEX_PREFIX, VORTEX_BACKEND_DIR);

  DIR *dir = opendir(path);
  if (!dir) {
    perror("opendir");
    return NULL;
  }

  struct dirent *entry;
  char **list = NULL;
  size_t count = 0;

  while ((entry = readdir(dir)) != NULL) {
    const char *name = entry->d_name;
    const char *prefix = "lib";
    const char *suffix = "-backend.so";

    size_t len = strlen(name);
    size_t prefix_len = strlen(prefix);
    size_t suffix_len = strlen(suffix);

    if (len > prefix_len + suffix_len &&
      strncmp(name, prefix, prefix_len) == 0 &&
      strcmp(name + len - suffix_len, suffix) == 0) {

      // Extract <name> part between prefix and suffix
      size_t core_len = len - prefix_len - suffix_len;
      char *backend = malloc(core_len + 1);
      if (!backend) continue;
      memcpy(backend, name + prefix_len, core_len);
      backend[core_len] = '\0';

      list = realloc(list, (count + 2) * sizeof(char*));
      list[count++] = backend;
      list[count] = NULL;
    }
  }

  closedir(dir);
  if (count_out) *count_out = count;
  return list;
}

const char*
_vt_comp_handle_cmd_flags(struct vt_compositor_t* c, int argc, char** argv) {
  if(argc > 1) {
    for(uint32_t i = 1; i < argc; i++) {
      char* flag = argv[i];
      if(_flag_cmp(flag, "--logfile", "-lf")) {
        c->log.stream = fopen(vt_util_log_get_filepath(), "a");  
        if (c->log.stream)
          setvbuf(c->log.stream, NULL, _IONBF, 0);
        else
          perror("log fopen");
      } else if(_flag_cmp(flag, "--verbose", "-vb")) {
        c->log.verbose = true;
      } else if(_flag_cmp(flag, "--quiet", "-q")) {
        c->log.quiet = true;
      } else if(_flag_cmp(flag, "-h", "--help")) {
        _vt_comp_log_help();
      } else if(_flag_cmp(flag, "-v", "--version")) {
        printf(_VERSION"\n");
        exit(0);
      } else if(_flag_cmp(flag, "-b", "--backend")) {
        if (i + 1 >= argc) {
          VT_ERROR(c->log, "Missing value for %s", flag);
          exit(1);
        }
        char* backend_str = argv[++i];
        if(strlen(backend_str) > 31) exit(1);
        size_t n;
        char** valid_backends = _scan_valid_backends(&n);
        bool valid = false;
        for(uint32_t i = 0; i < n; i++)
          if(strcmp(backend_str, valid_backends[i]) == 0) { valid = true; break; }
        if(!valid) {
          VT_ERROR(c->log, "Invalid compositor backend: '%s'", backend_str);
          fprintf(stderr, " Valid options for backends are: [ "); 
          for(uint32_t i = 0; i < n; i++)
            fprintf(stderr, "%s%s ", valid_backends[i], i != n - 1 ? "," : "");
          fprintf(stderr, "]\n"); 
          exit(1);
        }
        free(valid_backends);
        return backend_str;
      } 
      else if(_flag_cmp(flag, "-bp", "--backend-path")) {
        if (i + 1 >= argc) {
          VT_ERROR(c->log, "Missing value for %s", flag);
          exit(1);
        }
        char* backend_path = argv[++i];
        c->_cmd_line_backend_path = backend_path;
      }
      else if(_flag_cmp(flag, "-vo", "--virtual-outputs")) {
        if (i + 1 >= argc) {
          VT_ERROR(c->log, "Missing value for %s", flag);
          exit(1);
        }
        c->n_virtual_outputs = atoi(argv[++i]);
        if (c->n_virtual_outputs <= 0)
          c->n_virtual_outputs = 1;
        VT_TRACE(c->log, "Virtual outputs set to %d", c->n_virtual_outputs);
      }
      else {
        VT_ERROR(c->log, "invalid option -- '%s'", flag);
        exit(1);
      }
    }
  }
  return NULL;
}

void _vt_comp_log_help() {
  printf("Usage: vortex [option:s] (value:s)\n");
  printf("Options: \n");
  printf("%-30s %s\n", "-h, --help", "Show this help message and exit");
  printf("%-30s %s\n", "-v, --version", "Show version information");
  printf("%-30s %s\n", "-vb, --verbose", "Log verbose (trace) output");
  printf("%-30s %s\n", "-lf, --logfile", 
         "Write logs to a logfile (~/.local/state/vortex/logs/ or if available $XDG_STATE_HOME/vortex/logs)");
  printf("%-30s %s\n", "-q, --quiet", "Run in quiet mode");
  printf("%-30s %s\n", "-vo, --virtual-outputs [val]", "Specify the number of virtual outputs (windows) in nested mode");
  printf("%-30s %s", "-b, --backend [val]", "Specifies the sink backend of the compositor.");
  printf(" Valid options for backends are: [ "); 
  size_t n;
  char** valid_backends = _scan_valid_backends(&n);
  for(uint32_t i = 0; i < n; i++)
    printf("'%s'%s ", valid_backends[i], i != n - 1 ? "," : "");
  printf("]\n"); 
  free(valid_backends);
  printf("%-30s %s\n", "-bp, --backend-path [val]", "Specifies the path of the .so file to load as the compositor's sink backend");
  exit(0);
}

void 
_vt_comp_wl_bind(struct wl_client *client, void *data,
              uint32_t version, uint32_t id) {
  struct wl_resource *res = wl_resource_create(client,&wl_compositor_interface,version,id);
  wl_resource_set_implementation(res,&compositor_impl, data, NULL);
}


void 
_vt_comp_load_backend(struct vt_compositor_t* c, const char* backend_name, const char* backend_path) {
  if(_vt_comp_dl_handle) {
    VT_WARN(c->log, "Trying to reload backend during runtime, this is not supported.");
    return;
  }

  char path[PATH_MAX];
  if(backend_path) {
    sprintf(path, "%s", backend_path);
  } else {
    sprintf(
      path,
      (VORTEX_PREFIX "/" VORTEX_BACKEND_DIR "/lib%s-backend.so"), 
      backend_name);
  }

  _vt_comp_dl_handle = dlopen(path, RTLD_NOW);
  if(!_vt_comp_dl_handle) {
    const char *err = dlerror();
    log_fatal(c->log, "%s (%s)", path, err ? err : "unknown error");
    return;
  }

  char sym[64];
  sprintf(sym, "backend_implement_%s", backend_name);
  backend_implement_func_t sym_ptr = dlsym(_vt_comp_dl_handle, sym);

  if(!sym_ptr) {
    dlclose(_vt_comp_dl_handle);
    log_fatal(c->log, "Backend %s does not export backend_implement_%s.", path, backend_name);
    return;
  }

  if(!sym_ptr(c)) {
    dlclose(_vt_comp_dl_handle);
    log_fatal(c->log, "backend %s failed to initialize.", path);
    return;
  }
  VT_TRACE(c->log, "called implement function '%s' for backend '%s'", sym, backend_name);
}

void _vt_comp_associate_surface_with_output(struct vt_compositor_t* c, struct vt_surface_t* surf, struct vt_output_t* output) {
  // Skip if surface and output don’t intersect
  if (surf->x + surf->width  <= output->x ||
    surf->x >= output->x + output->width ||
    surf->y + surf->height <= output->y ||
    surf->y >= output->y + output->height) return;
  surf->_mask_outputs_visible_on |= (1u << output->id);

}

void 
_vt_comp_wl_surface_create(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id) {
  struct vt_compositor_t* c = wl_resource_get_user_data(resource);
  VT_TRACE(c->log, "Got compositor.surface_create: Started managing surface.")
  // Allocate the struct to store protocol information about the surface
  struct vt_surface_t* surf = VT_ALLOC(c, sizeof(*surf));
  surf->comp = c;
  surf->x = 20;
  surf->y = 20;

  // Init the damage regions
  pixman_region32_init(&surf->current_damage);
  pixman_region32_init(&surf->pending_damage);

  // Add the surface to list of surfaces in the compositor
  wl_list_insert(&c->surfaces, &surf->link);
  VT_TRACE(c->log, "compositor.surface_create: Inserted surface into list.")

  // Get the surface's wayland resource
  struct wl_resource* res = wl_resource_create(client, &wl_surface_interface, 4, id);
  wl_resource_set_implementation(res, &surface_impl, surf, _vt_comp_wl_surface_handle_resource_destroy);
  surf->surf_res = res;

  VT_TRACE(c->log, "compositor.surface_create: Setting surface implementation.")
}

void 
_vt_comp_wl_surface_create_region(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id) {
  struct wl_resource* res = wl_resource_create(
    client, &wl_region_interface, 
    wl_resource_get_version(resource), id);
  wl_resource_set_implementation(res, &region_impl, NULL, NULL);
}

void 
_vt_comp_wl_surface_attach(
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
_vt_comp_wl_surface_commit(
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
  if (!surf->mapped && surf->has_buffer && surf->xdg_surf && surf->xdg_surf->toplevel.xdg_toplevel_res) {
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

  if (!surf->current_damage.data) {
    pixman_region32_init(&surf->current_damage);
  }

  if(pixman_region32_empty(&surf->pending_damage)) return;

  pixman_box32_t extents = *pixman_region32_extents(&surf->pending_damage);

  if (surf->current_damage.data && surf->current_damage.data->numRects == 0)
    pixman_region32_copy(&surf->current_damage, &surf->pending_damage);
  else
    pixman_region32_union_rect(&surf->current_damage, &surf->current_damage,
                               extents.x1, extents.y1,
                               extents.x2 - extents.x1,
                               extents.y2 - extents.y1);

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
      _vt_comp_associate_surface_with_output(surf->comp, surf, output);
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
    wl_buffer_send_release(surf->buf_res);
  }

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

  pixman_region32_clear(&surf->current_damage);
}


void
_vt_comp_wl_surface_frame(
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
_vt_comp_wl_surface_damage(
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

  pixman_region32_union_rect(
    &surf->pending_damage, &surf->pending_damage,
    x, y, width, height);

  surf->damaged = true;

}
void 
_vt_comp_wl_surface_damage_buffer(struct wl_client *client,
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

  pixman_region32_union_rect(
    &surf->pending_damage, &surf->pending_damage,
    x, y, width, height);

  surf->damaged = true;
}


void
_vt_comp_wl_surface_set_opaque_region(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *region) {

}

void 
_vt_comp_wl_surface_set_input_region(struct wl_client *client,
                                  struct wl_resource *resource,
                                  struct wl_resource *region) {

}

void
_vt_comp_wl_surface_set_buffer_transform(struct wl_client *client,
                                      struct wl_resource *resource,
                                      int32_t transform) {

}

void
_vt_comp_wl_surface_set_buffer_scale(struct wl_client *client,
                                  struct wl_resource *resource,
                                  int32_t scale) {

}

void
_vt_comp_wl_surface_offset(struct wl_client *client,
                        struct wl_resource *resource,
                        int32_t x,
                        int32_t y) {

}

void
_vt_comp_wl_surface_destroy(struct wl_client *client,
                         struct wl_resource *resource) {
  struct vt_surface_t* surf = ((struct vt_surface_t*)wl_resource_get_user_data(resource));
    
  VT_TRACE(surf->comp->log, 
            "Got surface.destroy: Destroying surface resource.")
  wl_resource_destroy(resource);

}

void 
_vt_comp_wl_surface_handle_resource_destroy(struct wl_resource* resource) {
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

  // Schedule a repaint for all outputs that the surface intersects with
  struct vt_output_t* output;
  struct vt_renderer_t* r = surf->comp->renderer;
  if(r && r->impl.destroy_surface_texture) {
    r->impl.destroy_surface_texture(r, surf);
  }

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
  }
}

void 
_vt_comp_wl_region_destroy(
  struct wl_client *client,
  struct wl_resource *resource) {

}

void
_vt_comp_wl_region_add(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height) {

}

void 
_vt_comp_wl_region_subtract(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height) {

}

uint32_t vt_comp_merge_damaged_regions(pixman_box32_t *merged,
                                    pixman_region32_t *region) {
  if (pixman_region32_empty(region)) return 0;

  uint32_t n_rects = 0;
  pixman_box32_t *rects = pixman_region32_rectangles(region, &n_rects);
  if(!n_rects) return 0;

  if (n_rects > VT_MAX_DAMAGE_RECTS) {
    merged[0] = *pixman_region32_extents(region);
    return 1; 
  }

  memcpy(merged, rects, sizeof(pixman_box32_t) * n_rects);

  return n_rects;
}

bool 
_vt_comp_wl_init(struct vt_compositor_t* c) {
  if(!(c->wl.dsp = wl_display_create())) {
    VT_ERROR(c->log, "cannot create wayland display.");
    return false;
  }

  VT_TRACE(c->log, "Sucessfully created wayland display.");

  wl_list_init(&c->surfaces);

  if(!(c->wl.evloop = wl_display_get_event_loop(c->wl.dsp))) {
    VT_ERROR(c->log, "Cannot get wayland event loop.");
    return false;
  }

  wl_display_init_shm(c->wl.dsp);

  wl_display_add_shm_format(c->wl.dsp, WL_SHM_FORMAT_ARGB8888);
  wl_display_add_shm_format(c->wl.dsp, WL_SHM_FORMAT_XRGB8888);

  wl_global_create(c->wl.dsp, &wl_compositor_interface, 4, c, _vt_comp_wl_bind); 

  if(!vt_proto_xdg_shell_init(c, 1)) {
    VT_ERROR(c->log, "Cannot initialize XDG shell protocol.");
    return false;
  }

  const char *socket_name = wl_display_add_socket_auto(c->wl.dsp);
  if (!socket_name) {
    VT_ERROR(c->log, "Failed to create Wayland socket: no clients will be able to connect.");
    return false; 
  } else {
    VT_TRACE(c->log, "Wayland display ready on socket '%s'.", socket_name);
  }


  return true;
}

#include <signal.h>
#include <execinfo.h>

static struct vt_compositor_t* vt_global_compositor;

static void vt_sig_handler(int sig) {
    void *trace[32];
    size_t n = backtrace(trace, 32);
    fprintf(stderr, "\n[VT] Caught signal %d (%s)\n", sig, strsignal(sig));

    // Also log to compositor log file if open
    FILE *f = vt_global_compositor && vt_global_compositor->log.stream 
              ? vt_global_compositor->log.stream 
              : stderr;
    fprintf(f, "\n[VT] ===== Fatal signal %d (%s) =====\n", sig, strsignal(sig));
    backtrace_symbols_fd(trace, n, fileno(f));
    fflush(f);

    // Re-raise so you still get a core dump if desired
    signal(sig, SIG_DFL);
    raise(sig);
}
bool
vt_comp_init(struct vt_compositor_t* c, int argc, char** argv) {
  vt_util_arena_init(&c->arena, 1024 * 1024 * 2);

  vt_global_compositor = c;
  signal(SIGSEGV, vt_sig_handler);
  signal(SIGABRT, vt_sig_handler);
  signal(SIGFPE,  vt_sig_handler);
  signal(SIGILL,  vt_sig_handler);
  signal(SIGBUS,  vt_sig_handler);

  wl_list_init(&c->outputs);

  c->log.stream = stdout;
  c->log.verbose = false;
  c->log.quiet = false;

  c->backend = VT_ALLOC(c, sizeof(*c->backend));
  c->backend->comp = c;
  
  c->session = VT_ALLOC(c, sizeof(*c->session));
  c->session->comp = c;
  
  c->renderer = VT_ALLOC(c, sizeof(*c->renderer));
  c->renderer->comp = c;
  vt_renderer_implement(c->renderer, VT_RENDERING_BACKEND_EGL_OPENGL);
  
  c->input_backend = VT_ALLOC(c, sizeof(*c->input_backend));
  c->input_backend->comp = c;
  
  c->seat = VT_ALLOC(c, sizeof(*c->seat));
  c->seat->comp = c;

  const char* backend_str = _vt_comp_handle_cmd_flags(c, argc, argv);
  if(!backend_str) {
    if(getenv("WAYLAND_DISPLAY"))
      backend_str = "wl";
    else 
      backend_str = "drm";
  }

  if(strcmp(backend_str, "wl") == 0 && !c->n_virtual_outputs)
    c->n_virtual_outputs = 1;

  _vt_comp_load_backend(c, backend_str, c->_cmd_line_backend_path);

  if(!_vt_comp_wl_init(c)) {
    VT_ERROR(c->log, "Failed to initialize wayland state.")
    return false;
  }

  // Initialize session
  if(c->session->impl.init)
    c->session->impl.init(c->session);
  
  // Initialize backend 
  if(!c->backend->impl.init(c->backend)) {
    VT_ERROR(c->log, "Failed to initialize compositor backend.")
    return false;
  }

  enum vt_input_backend_platform_t input_backend = VT_INPUT_UNKNOWN;
  switch(c->backend->platform) {
    case VT_BACKEND_DRM_GBM: 
      input_backend = VT_INPUT_LIBINPUT;
      break;
    case VT_BACKEND_WAYLAND:
      input_backend = VT_INPUT_WAYLAND;
      break;
  }
  vt_input_implement(c->input_backend, input_backend); 

  if(c->input_backend->impl.init) { 
    c->input_backend->impl.init(c->input_backend, 
                                c->session->native_handle);
  }


  vt_seat_init(c->seat);
    
  VT_TRACE(c->log, "Initialized wayland seat.");
  
  struct vt_output_t* output;
  wl_list_for_each(output, &c->outputs, link_global) {
    output->repaint_pending = false;
    vt_comp_schedule_repaint(c, output);
  }

  struct vt_scene_node_t* root = vt_scene_node_create(c, 0, 0, output->width, output->height);
 
  vt_scene_node_add_child(c, root, vt_scene_node_create(c, 20, 20, 20, 20));

  for(uint32_t i = 0; i < root->child_count; i++) {
    printf("Root children: %f, %f, %f, %f\n",
           root->childs[i]->x, 
           root->childs[i]->y, 
           root->childs[i]->w, 
           root->childs[i]->h
           );
  }

  return true;
}

void
vt_comp_run(struct vt_compositor_t *c) {
  c->running = true;
  VT_TRACE(c->log, "Entering main event loop...");
  while (c->running) {
    wl_event_loop_dispatch(c->wl.evloop, -1);
    wl_display_flush_clients(c->wl.dsp);
  }
}

bool
vt_comp_terminate(struct vt_compositor_t *c) {
  VT_TRACE(c->log, "Shutting down...");
  c->running = false;

  if(!(c->backend->impl.terminate(c->backend))) {
    VT_ERROR(c->log, "Failed to terminate backend");
    return false;
  }

  vt_seat_terminate(c->seat);
  
  c->input_backend->impl.terminate(c->input_backend);

  if(c->session->impl.terminate)
    c->session->impl.terminate(c->session);

  free(c->session);

  // Shut down wayland 
  if (c->wl.dsp) {
    wl_display_destroy_clients(c->wl.dsp);
    wl_display_destroy(c->wl.dsp);
    c->wl.dsp = NULL;
  }

  // Clean up log
  VT_TRACE(c->log, "Shutdown complete.");

  if (c->log.stream && c->log.stream != stdout && c->log.stream != stderr) {
    fclose(c->log.stream);
    c->log.stream = NULL;
  }
  
  vt_util_arena_destroy(&c->arena); 

  dlclose(_vt_comp_dl_handle);

  exit(0);
}

void 
vt_comp_schedule_repaint(struct vt_compositor_t *c, struct vt_output_t* output) {
  if(!c || !output || !c->backend) return;
  if (c->suspended) {
    VT_WARN(c->log, "Trying to schedule repaint while compositor is suspended.");
    return;
  }
  if (output->repaint_pending) {
    return;
  }
  output->needs_repaint = true;
  if(!output->repaint_pending) {
    output->repaint_pending = true;
    output->repaint_source = wl_event_loop_add_idle(c->wl.evloop, _vt_comp_frame_handler, output);
  }

  VT_TRACE(c->log, "Scheduling repaint on output %p.", output);

}
void vt_comp_repaint_scene(struct vt_compositor_t *c, struct vt_output_t *output) {
  if (!c || !output || !c->backend || !c->renderer) return;

  struct vt_renderer_t* r = c->renderer;
  r->impl.begin_frame(r, output);

  pixman_box32_t damage_boxes[VT_MAX_DAMAGE_RECTS];
  int32_t n_damage = vt_comp_merge_damaged_regions(damage_boxes, &output->damage);

  // Damage Pass
  r->impl.stencil_damage_pass(r, output);
  r->impl.begin_scene(r, output);
  for(uint32_t i = 0; i < n_damage; i++) {
    const pixman_box32_t b = damage_boxes[i];
    r->impl.draw_rect(r, b.x1, b.y1, b.x2 - b.x1, b.y2 - b.y1, 0xFFFFFF);
  }
  r->impl.end_scene(r, output);


  //Composite pass
  r->impl.composite_pass(r, output);
  r->impl.begin_scene(r, output);
  r->impl.set_clear_color(r, output, 0xffffff);

  struct vt_surface_t* surf;
  wl_list_for_each_reverse(surf, &c->surfaces, link) {
    if(!surf->damaged) continue;
    if (surf->x + surf->width  <= output->x ||
      surf->x >= output->x + output->width ||
      surf->y + surf->height <= output->y ||
      surf->y >= output->y + output->height) continue;

    if(!pixman_region32_intersect_rect(&output->damage, &output->damage, surf->x, surf->y, surf->width, surf->height)) continue;

    r->impl.draw_surface(r, surf, surf->x - output->x, surf->y - output->y);
  }

  r->impl.end_scene(r, output);

  r->impl.end_frame(r, output, damage_boxes, n_damage);

  pixman_region32_clear(&output->damage);
  output->needs_repaint = false;
}

void vt_comp_invalidate_all_surfaces(struct vt_compositor_t *comp) {
  if (!comp) return;

  VT_TRACE(comp->log, "Invalidating all surface GPU imports...");

  struct vt_surface_t *surf, *tmp;
  wl_list_for_each_safe(surf, tmp, &comp->surfaces, link) {
    // Destroy GPU texture if surface has one
    if (surf->tex.id) {
      struct vt_renderer_t* r = comp->renderer; 
      if ( r && r->impl.destroy_surface_texture) {
        r->impl.destroy_surface_texture(r, surf);
      }
      memset(&surf->tex, 0, sizeof(surf->tex));
    }

    // Force re-import
    surf->_mask_outputs_visible_on = 0;


    // Optionally, if the client still has a buffer attached, ask it to repaint
    if (surf->buf_res) {
      // Send a frame done to poke the client
      uint32_t t = vt_util_get_time_msec();
      struct wl_resource *cb = wl_resource_create(
        wl_resource_get_client(surf->buf_res),
        &wl_callback_interface, 1, 0);
      if (cb)
        wl_callback_send_done(cb, t);
    }
  }
}
