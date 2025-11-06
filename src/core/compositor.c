#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "src/input/input.h"
#include "src/input/wl_seat.h"
#include "src/protocols/xdg_shell.h"
#include "src/protocols/linux_dmabuf.h"
#include "src/protocols/wl_surface.h"
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

static void _vt_comp_wl_surface_create(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

static void _vt_comp_wl_region_handle_resource_destroy(struct wl_resource* resource);
void _vt_comp_wl_surface_create_region(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

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
      else if(_flag_cmp(flag, "-expt", "--exclude-protocol")) {
        if (i + 1 >= argc) {
          VT_ERROR(c->log, "Missing value for %s", flag);
          exit(1);
        }
        bool disabled = false;
        i++;
        for(uint32_t j = i; j < argc; j++) {
          if(argv[j][0] == '-') break;
          if(strcmp(argv[j], "linux-dmabuf") == 0) {
            c->have_proto_dmabuf = false;
            disabled = true;
            VT_WARN(c->log, "Disabled protcol '%s'", argv[j]);
          } else if(strcmp(argv[j], "linux-dmabuf-explicit-sync") == 0) {
            c->have_proto_dmabuf_explicit_sync = false;
            disabled = true;
            VT_WARN(c->log, "Disabled protocol '%s'", argv[j]);
          } else {
            VT_ERROR(
              c->log, 
              "Protocol %s is not valid, valid protocols are: "
              "[ 'linux-dmabuf', 'linux-dmabuf-explicit-sync' ] ", argv[j]);
            exit(1);
          }
        }
      }
      else {
        VT_ERROR(c->log, "invalid option -- '%s'. Use --help to see valid options", flag);
        exit(1);
      }
    }
  }
  return NULL;
}

void _vt_comp_log_help() {
  printf("Usage: vortex [option:s] (value:s)\n");
  printf("Options: \n");
  printf("%-35s %s\n", "-h, --help", "Show this help message and exit");
  printf("%-35s %s\n", "-v, --version", "Show version information");
  printf("%-35s %s\n", "-vb, --verbose", "Log verbose (trace) output");
  printf("%-35s %s\n", "-lf, --logfile", 
         "Write logs to a logfile (~/.local/state/vortex/logs/ or if available $XDG_STATE_HOME/vortex/logs)");
  printf("%-35s %s\n", "-q, --quiet", "Run in quiet mode (no logging)");
  printf("%-35s %s\n", "-vo, --virtual-outputs [val]", "Specify the number of virtual outputs (windows) in nested mode");
  printf("%-35s %s\n", "-expt, --exclude-protocol [val]", "Specifies optional protocols to exlcude. Valid options are: 'linux-dmabuf', 'linux-dmabuf-explicit-sync");
  printf("%-35s %s", "-b, --backend [val]", "Specifies the sink backend of the compositor.");
  printf(" Valid options for backends are: [ "); 
  size_t n;
  char** valid_backends = _scan_valid_backends(&n);
  for(uint32_t i = 0; i < n; i++)
    printf("'%s'%s ", valid_backends[i], i != n - 1 ? "," : "");
  printf("]\n"); 
  free(valid_backends);
  printf("%-35s %s\n", "-bp, --backend-path [val]", "Specifies the path of the .so file to load as the compositor's sink backend");
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

  bool visibility_updated = !(surf->_mask_outputs_visible_on & (1u << output->id));

  surf->_mask_outputs_visible_on |= (1u << output->id);

  if(visibility_updated) {
    output->needs_damage_rebuild = true;
  }
}

void 
_vt_comp_wl_surface_create(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id) {
  struct vt_compositor_t* c = wl_resource_get_user_data(resource);
  if(!c) {
    log_fatal(c->log, "compositor.surface_create: User data is NULL.");
    return;
  }

  VT_TRACE(c->log, "Got compositor.surface_create: Started managing surface.")
  // Allocate the struct to store protocol information about the surface
  struct vt_surface_t* surf = VT_ALLOC(c, sizeof(*surf));
  surf->comp = c;
  surf->x = 20;
  surf->y = 20;

  // Init the damage regions
  pixman_region32_init(&surf->current_damage);
  pixman_region32_init(&surf->pending_damage);
  pixman_region32_init(&surf->opaque_region);
  pixman_region32_init(&surf->input_region);

  // Add the surface to list of surfaces in the compositor
  wl_list_insert(&c->surfaces, &surf->link);
  VT_TRACE(c->log, "compositor.surface_create: Inserted surface into list.")

  VT_TRACE(c->log, "compositor.surface_create: Setting surface implementation.")

  if(!vt_proto_wl_surface_init(surf, client, id, 4)) {
    VT_ERROR(c->log, "compositor.surface_create: Failed to create surface.");
    return;
  }

}

void 
_vt_comp_wl_region_handle_resource_destroy(struct wl_resource* resource) {
  struct vt_region_t* r = wl_resource_get_user_data(resource);
  VT_TRACE(r->comp->log, "region.destroy_resoure: destroying region %p", r);
  pixman_region32_fini(&r->region);
  free(r);
}

void 
_vt_comp_wl_surface_create_region(struct wl_client *client,
                                  struct wl_resource *resource,
                                  uint32_t id) {
  struct vt_compositor_t* comp = wl_resource_get_user_data(resource);
  if (!comp) {
    wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                           "compositor resource missing compositor user data");
    return;
  }

  VT_TRACE(comp->log, "compositor.create_region: creating region...");

  struct vt_region_t* region = calloc(1, sizeof(*region));
  if (!region) {
    wl_client_post_no_memory(client);
    return;
  }

  pixman_region32_init(&region->region);
  region->comp = comp;

  struct wl_resource* res = wl_resource_create(
    client, &wl_region_interface, wl_resource_get_version(resource), id);

  wl_resource_set_implementation(res, &region_impl, region,
                                 _vt_comp_wl_region_handle_resource_destroy);

  VT_TRACE(comp->log, "compositor.create_region: created region %p", region);
}

void 
_vt_comp_wl_region_destroy(
  struct wl_client *client,
  struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void
_vt_comp_wl_region_add(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height) {
  struct vt_region_t* r = wl_resource_get_user_data(resource);
  if(!r) return;
  pixman_region32_union_rect(&r->region, &r->region, x, y, width, height);
}

void 
_vt_comp_wl_region_subtract(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height) {
  struct vt_region_t* r = wl_resource_get_user_data(resource);
  if(!r) return;
  pixman_region32_t rect;
  pixman_region32_init_rect(&rect, x, y, width, height);
  pixman_region32_subtract(&r->region, &r->region, &rect);
  pixman_region32_fini(&rect);
}

static void _wl_region_add(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x, int32_t y, int32_t width, int32_t height) {
  struct vt_region_t* r = wl_resource_get_user_data(resource);
  pixman_region32_union_rect(&r->region, &r->region, x, y, width, height);
}

static void _wl_region_subtract(struct wl_client *client,
                                struct wl_resource *resource,
                                int32_t x, int32_t y, int32_t width, int32_t height) {
  struct vt_region_t* r = wl_resource_get_user_data(resource);
  pixman_region32_t rect;
  pixman_region32_init_rect(&rect, x, y, width, height);
  pixman_region32_subtract(&r->region, &r->region, &rect);
  pixman_region32_fini(&rect);
}

static void _wl_region_handle_destroy(struct wl_resource* resource) {
  struct vt_region_t* r = wl_resource_get_user_data(resource);
  pixman_region32_fini(&r->region);
  free(r);
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
    fprintf(stderr, "\n[vortex] Caught signal %d (%s)\n", sig, strsignal(sig));

    // Also log to compositor log file if open
    FILE *f = vt_global_compositor && vt_global_compositor->log.stream 
              ? vt_global_compositor->log.stream 
              : stderr;
    fprintf(f, "\n[vortex] ===== Fatal signal %d (%s) =====\n", sig, strsignal(sig));
    backtrace_symbols_fd(trace, n, fileno(f));
    fflush(f);

    // Re-raise so you still get a core dump if desired
    signal(sig, SIG_DFL);
    raise(sig);
}
bool
vt_comp_init(struct vt_compositor_t* c, int argc, char** argv) {
  vt_util_arena_init(&c->arena, 1024 * 1024 * 2);
  vt_util_arena_init(&c->frame_arena, 1024 * 1024 * 2);

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

  c->have_proto_dmabuf = true;
  c->have_proto_dmabuf_explicit_sync = true;

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

  return true;
}

void
vt_comp_run(struct vt_compositor_t *c) {
  c->running = true;
  VT_TRACE(c->log, "Entering main event loop...");
  while (c->running) {
    vt_util_arena_reset(&c->frame_arena);

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

  if(c->session->impl.terminate) {
    c->session->impl.terminate(c->session);
  }

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
  vt_util_arena_destroy(&c->frame_arena); 

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

  if(output->needs_damage_rebuild) {
    output->n_damage_boxes = vt_comp_merge_damaged_regions(output->cached_damage, &output->damage);
    output->needs_damage_rebuild = false;
  }
  // Damage Pass
  r->impl.stencil_damage_pass(r, output);
  r->impl.begin_scene(r, output);
  for(uint32_t i = 0; i < output->n_damage_boxes; i++) {
    const pixman_box32_t b = output->cached_damage[i];
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
    if(!(surf->_mask_outputs_visible_on & (1u << output->id))) continue;

    if(!pixman_region32_intersect_rect(&output->damage, &output->damage, surf->x, surf->y, surf->width, surf->height)) continue;

    r->impl.draw_surface(r, output, surf, surf->x - output->x, surf->y - output->y);
  }

  r->impl.end_scene(r, output);

  r->impl.end_frame(r, output, output->cached_damage, output->n_damage_boxes);

  pixman_region32_clear(&output->damage);
  output->needs_repaint = false;
}

void vt_comp_invalidate_all_surfaces(struct vt_compositor_t *comp) {
  if (!comp) return;

  VT_TRACE(comp->log, "Invalidating all surface GPU imports...");
  struct vt_output_t* output;
  wl_list_for_each(output, &comp->outputs, link_global) {
    output->needs_damage_rebuild = true;
  } 

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
