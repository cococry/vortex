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

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "src/input/input.h"
#include "src/input/wl_seat.h"
#include "src/protocols/wl_data_device.h"
#include "src/protocols/wl_subcompositor.h"
#include "src/protocols/wl_surface.h"
#include "src/protocols/xdg_shell.h"
#include "src/render/renderer.h"

#include <dirent.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <linux/vt.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <glad.h>
#include <wayland-server.h>

#include "config.h"
#include "core_types.h"
#include "scene.h"
#include "surface.h"

#include "compositor.h"

#define _SUBSYS_NAME "COMPOSITOR"

static void _vt_comp_frame_handler(void *data);

static bool _vt_comp_render_output(struct vt_compositor_t *c,
                                   struct vt_output_t     *output);

static void _vt_comp_log_help();

static bool _vt_comp_wl_init(struct vt_compositor_t *c);

static void _vt_comp_wl_bind(struct wl_client *client, void *data,
                             uint32_t version, uint32_t id);

static void _vt_comp_wl_surface_create(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t            id);

static void
     _vt_comp_wl_region_handle_resource_destroy(struct wl_resource *resource);
void _vt_comp_wl_surface_create_region(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t            id);

static void _vt_comp_wl_region_destroy(struct wl_client   *client,
                                       struct wl_resource *resource);

static void _vt_comp_wl_region_add(struct wl_client   *client,
                                   struct wl_resource *resource, int32_t x,
                                   int32_t y, int32_t width, int32_t height);

static void _vt_comp_wl_region_subtract(struct wl_client   *client,
                                        struct wl_resource *resource, int32_t x,
                                        int32_t y, int32_t width,
                                        int32_t height);

static const struct wl_compositor_interface compositor_impl = {
    .create_surface = _vt_comp_wl_surface_create,
    .create_region = _vt_comp_wl_surface_create_region};

static const struct wl_region_interface region_impl = {
    .add = _vt_comp_wl_region_add,
    .destroy = _vt_comp_wl_region_destroy,
    .subtract = _vt_comp_wl_region_subtract,
};

static void *_vt_comp_dl_handle = NULL;

void _vt_comp_frame_handler(void *data) {
  struct vt_output_t *output = data;
  if (!output)
    return;
  struct vt_compositor_t *c = output->backend->comp;
  if (!c)
    return;
  if (output->backend->comp->suspended) {
    // Avoid busy loop
    output->repaint_pending = false;
    return;
  }

  if (!c->backend->impl.prepare_output_frame(c->backend, output)) {
    // Avoid busy loop
    output->repaint_pending = false;
    return;
  }
  if (!_vt_comp_render_output(c, output)) {
    // Avoid busy loop
    output->repaint_pending = false;
    return;
  }
  VT_TRACE(c->log, "Pending repaint on output %p got satisfied.", output);
  if (output->repaint_source) {
    wl_event_source_remove(output->repaint_source);
    output->repaint_source = NULL;
  }
  output->repaint_pending = false;
}

/* Heed my words struggeler... */
void vt_comp_frame_done(struct vt_compositor_t *c, struct vt_output_t *output,
                        uint32_t t) {
  if (!c || !output)
    return;

  struct vt_rendered_surface_t *entry, *tmp;

  wl_list_for_each_safe(entry, tmp, &output->rendered_surfaces, link) {
    if (entry->surf)
      vt_surface_frame_done(entry->surf, t);

    wl_list_remove(&entry->link);
    free(entry);
  }

  wl_list_init(&output->rendered_surfaces);
  VT_TRACE(c->log, "Sent frame callbacks for output %p.", output);
}

void vt_comp_frame_done_all(struct vt_compositor_t *c, uint32_t t) {
  struct vt_surface_t *surf;
  wl_list_for_each(surf, &c->surfaces, link) { vt_surface_frame_done(surf, t); }

  VT_TRACE(surf->comp->log, "Sent wl_callback.done() for all pending frame "
                            "callbacks of all surfaces.");
}

bool _vt_comp_render_output(struct vt_compositor_t *c,
                            struct vt_output_t     *output) {
  if (!c || !c->backend || !c->backend->impl.handle_frame || !output)
    return false;

  vt_comp_repaint_scene(c, output);
  c->backend->impl.handle_frame(c->backend, output);
  output->repaint_pending = false;

  return true;
}

static bool _flag_cmp(const char *flag, const char *lng, const char *shrt) {
  return strcmp(flag, lng) == 0 || strcmp(flag, shrt) == 0;
}

static char **_scan_valid_backends(size_t *count_out) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", VORTEX_PREFIX, VORTEX_BACKEND_DIR);

  DIR *dir = opendir(path);
  if (!dir) {
    perror("opendir");
    return NULL;
  }

  struct dirent *entry;
  char         **list = NULL;
  size_t         count = 0;

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
      char  *backend = malloc(core_len + 1);
      if (!backend)
        continue;
      memcpy(backend, name + prefix_len, core_len);
      backend[core_len] = '\0';

      list = realloc(list, (count + 2) * sizeof(char *));
      list[count++] = backend;
      list[count] = NULL;
    }
  }

  closedir(dir);
  if (count_out)
    *count_out = count;
  return list;
}

const char *_vt_comp_handle_cmd_flags(struct vt_compositor_t *c, int argc,
                                      char **argv) {
  if (argc > 1) {
    for (uint32_t i = 1; i < argc; i++) {
      char *flag = argv[i];
      if (_flag_cmp(flag, "--logfile", "-lf")) {
        c->log.stream = fopen(vt_util_log_get_filepath(), "a");
        if (c->log.stream) {
          setvbuf(c->log.stream, NULL, _IONBF, 0);
        } else {
          perror("log fopen");
        }
      } else if (_flag_cmp(flag, "--verbose", "-vb")) {
        c->log.verbose = true;
      } else if (_flag_cmp(flag, "--quiet", "-q")) {
        c->log.quiet = true;
      } else if (_flag_cmp(flag, "-h", "--help")) {
        _vt_comp_log_help();
      } else if (_flag_cmp(flag, "-v", "--version")) {
        printf(_VERSION "\n");
        exit(0);
      } else if (_flag_cmp(flag, "-b", "--backend")) {
        if (i + 1 >= argc) {
          VT_ERROR(c->log, "Missing value for %s", flag);
          exit(1);
        }
        char *backend_str = argv[++i];
        if (strlen(backend_str) > 31)
          exit(1);
        size_t n;
        char **valid_backends = _scan_valid_backends(&n);
        bool   valid = false;
        for (uint32_t i = 0; i < n; i++)
          if (strcmp(backend_str, valid_backends[i]) == 0) {
            valid = true;
            break;
          }
        if (!valid) {
          VT_ERROR(c->log, "Invalid compositor backend: '%s'", backend_str);
          fprintf(stderr, " Valid options for backends are: [ ");
          for (uint32_t i = 0; i < n; i++)
            fprintf(stderr, "%s%s ", valid_backends[i], i != n - 1 ? "," : "");
          fprintf(stderr, "]\n");
          for (uint32_t i = 0; i < n; i++) {
            free(valid_backends[i]);
          }
          free(valid_backends);
          exit(1);
        }
        return backend_str;
      } else if (_flag_cmp(flag, "-bp", "--backend-path")) {
        if (i + 1 >= argc) {
          VT_ERROR(c->log, "Missing value for %s", flag);
          exit(1);
        }
        char *backend_path = argv[++i];
        c->_cmd_line_backend_path = backend_path;
      } else if (_flag_cmp(flag, "-vo", "--virtual-outputs")) {
        if (i + 1 >= argc) {
          VT_ERROR(c->log, "Missing value for %s", flag);
          exit(1);
        }
        c->n_virtual_outputs = atoi(argv[++i]);
        if (c->n_virtual_outputs <= 0)
          ;
        c->n_virtual_outputs = 1;
        VT_TRACE(c->log, "Virtual outputs set to %d", c->n_virtual_outputs);
      } else if (_flag_cmp(flag, "-expt", "--exclude-protocol")) {
        if (i + 1 >= argc) {
          VT_ERROR(c->log, "Missing value for %s", flag);
          exit(1);
        }
        bool disabled = false;
        i++;
        for (uint32_t j = i; j < argc; j++) {
          if (argv[j][0] == '-')
            break;
          if (strcmp(argv[j], "linux-dmabuf") == 0) {
            c->have_proto_dmabuf = false;
            disabled = true;
          } else if (strcmp(argv[j], "linux-dmabuf-explicit-sync") == 0) {
            c->have_proto_dmabuf_explicit_sync = false;
            disabled = true;
          } else {
            VT_ERROR(c->log,
                     "Protocol %s is not valid, valid protocols are: "
                     "[ 'linux-dmabuf', 'linux-dmabuf-explicit-sync' ] ",
                     argv[j]);
            exit(1);
          }
        }
      } else {
        VT_ERROR(c->log,
                 "invalid option -- '%s'. Use --help to see valid options",
                 flag);
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
         "Write logs to a logfile (~/.local/state/vortex/logs/ or if available "
         "$XDG_STATE_HOME/vortex/logs)");
  printf("%-35s %s\n", "-q, --quiet", "Run in quiet mode (no logging)");
  printf("%-35s %s\n", "-vo, --virtual-outputs [val]",
         "Specify the number of virtual outputs (windows) in nested mode");
  printf("%-35s %s\n", "-expt, --exclude-protocol [val]",
         "Specifies optional protocols to exlcude. Valid options are: "
         "'linux-dmabuf', 'linux-dmabuf-explicit-sync");
  printf("%-35s %s", "-b, --backend [val]",
         "Specifies the sink backend of the compositor.");
  printf(" Valid options for backends are: [ ");
  size_t n;
  char **valid_backends = _scan_valid_backends(&n);
  for (uint32_t i = 0; i < n; i++)
    printf("'%s'%s ", valid_backends[i], i != n - 1 ? "," : "");
  printf("]\n");

  for (uint32_t i = 0; i < n; i++)
    free(valid_backends[i]);
  free(valid_backends);

  printf("%-35s %s\n", "-bp, --backend-path [val]",
         "Specifies the path of the .so file to load as the compositor's sink "
         "backend");
  exit(0);
}

void _vt_comp_wl_bind(struct wl_client *client, void *data, uint32_t version,
                      uint32_t id) {
  struct wl_resource *res =
      wl_resource_create(client, &wl_compositor_interface, version, id);
  wl_resource_set_implementation(res, &compositor_impl, data, NULL);
}

void _vt_comp_load_backend(struct vt_compositor_t *c, const char *backend_name,
                           const char *backend_path) {
  if (_vt_comp_dl_handle) {
    VT_WARN(c->log,
            "Trying to reload backend during runtime, this is not supported.");
    return;
  }

  char path[PATH_MAX];
  if (backend_path) {
    sprintf(path, "%s", backend_path);
  } else {
    sprintf(path, (VORTEX_PREFIX "/" VORTEX_BACKEND_DIR "/lib%s-backend.so"),
            backend_name);
  }

  _vt_comp_dl_handle = dlopen(path, RTLD_NOW);
  if (!_vt_comp_dl_handle) {
    const char *err = dlerror();
    log_fatal(c->log, "%s (%s)", path, err ? err : "unknown error");
    return;
  }

  char sym[64];
  sprintf(sym, "backend_implement_%s", backend_name);
  backend_implement_func_t sym_ptr = dlsym(_vt_comp_dl_handle, sym);

  if (!sym_ptr) {
    dlclose(_vt_comp_dl_handle);
    log_fatal(c->log, "Backend %s does not export backend_implement_%s.", path,
              backend_name);
    return;
  }

  if (!sym_ptr(c)) {
    dlclose(_vt_comp_dl_handle);
    log_fatal(c->log, "backend %s failed to initialize.", path);
    return;
  }
  VT_TRACE(c->log, "called implement function '%s' for backend '%s'", sym,
           backend_name);
}

void _vt_comp_wl_surface_create(struct wl_client   *client,
                                struct wl_resource *resource, uint32_t id) {
  struct vt_compositor_t *c =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!c)
    return;

  VT_TRACE(c->log,
           "Got wl_compositor.surface_create: Started managing surface.");

  struct vt_surface_t *surf = calloc(1, sizeof(*surf));
  if (!surf) {
    VT_WL_OUT_OF_MEMORY(c, client);
    return;
  }

  surf->comp = c;

  if (!vt_surface_init(surf)) {
    VT_ERROR(c->log,
             "wl_compositor.surface_create: Failed to initialize surface.");
    return;
  }

  if (!vt_proto_wl_surface_init(surf, client, id, 4)) {
    VT_ERROR(c->log, "wl_compositor.surface_create: Failed to create surface.");
    return;
  }


  vt_scene_node_add_child(c, c->root_node, vt_scene_node_create(c, surf));
}

void _vt_comp_wl_region_handle_resource_destroy(struct wl_resource *resource) {
  struct vt_region_t *r = resource ? wl_resource_get_user_data(resource) : NULL;
  VT_TRACE(r->comp->log, "region.destroy_resoure: destroying region %p", r);
  pixman_region32_fini(&r->region);
  free(r);
}

void _vt_comp_wl_surface_create_region(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t            id) {
  struct vt_compositor_t *comp =
      resource ? wl_resource_get_user_data(resource) : NULL;
  if (!comp) {
    wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                           "compositor resource missing compositor user data");
    return;
  }

  VT_TRACE(comp->log, "compositor.create_region: creating region...");

  struct vt_region_t *region = calloc(1, sizeof(*region));
  if (!region) {
    wl_client_post_no_memory(client);
    VT_WL_OUT_OF_MEMORY(comp, client);
    return;
  }

  pixman_region32_init(&region->region);
  region->comp = comp;

  struct wl_resource *res = wl_resource_create(
      client, &wl_region_interface, wl_resource_get_version(resource), id);

  wl_resource_set_implementation(res, &region_impl, region,
                                 _vt_comp_wl_region_handle_resource_destroy);

  VT_TRACE(comp->log, "compositor.create_region: created region %p", region);
}

void _vt_comp_wl_region_destroy(struct wl_client   *client,
                                struct wl_resource *resource) {
  wl_resource_destroy(resource);
}

void _vt_comp_wl_region_add(struct wl_client   *client,
                            struct wl_resource *resource, int32_t x, int32_t y,
                            int32_t width, int32_t height) {
  struct vt_region_t *r = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!r)
    return;
  pixman_region32_union_rect(&r->region, &r->region, x, y, width, height);
}

void _vt_comp_wl_region_subtract(struct wl_client   *client,
                                 struct wl_resource *resource, int32_t x,
                                 int32_t y, int32_t width, int32_t height) {
  struct vt_region_t *r = resource ? wl_resource_get_user_data(resource) : NULL;
  if (!r)
    return;
  pixman_region32_t rect;
  pixman_region32_init_rect(&rect, x, y, width, height);
  pixman_region32_subtract(&r->region, &r->region, &rect);
  pixman_region32_fini(&rect);
}

static void _wl_region_add(struct wl_client   *client,
                           struct wl_resource *resource, int32_t x, int32_t y,
                           int32_t width, int32_t height) {
  struct vt_region_t *r = resource ? wl_resource_get_user_data(resource) : NULL;
  pixman_region32_union_rect(&r->region, &r->region, x, y, width, height);
}

static void _wl_region_subtract(struct wl_client   *client,
                                struct wl_resource *resource, int32_t x,
                                int32_t y, int32_t width, int32_t height) {
  struct vt_region_t *r = resource ? wl_resource_get_user_data(resource) : NULL;
  pixman_region32_t   rect;
  pixman_region32_init_rect(&rect, x, y, width, height);
  pixman_region32_subtract(&r->region, &r->region, &rect);
  pixman_region32_fini(&rect);
}

static void _wl_region_handle_destroy(struct wl_resource *resource) {
  struct vt_region_t *r = resource ? wl_resource_get_user_data(resource) : NULL;
  pixman_region32_fini(&r->region);
  free(r);
}
bool _vt_comp_wl_init(struct vt_compositor_t *c) {
  if (!(c->wl.dsp = wl_display_create())) {
    VT_ERROR(c->log, "cannot create wayland display.");
    return false;
  }

  VT_TRACE(c->log, "Sucessfully created wayland display.");

  wl_list_init(&c->surfaces);

  if (!(c->wl.evloop = wl_display_get_event_loop(c->wl.dsp))) {
    VT_ERROR(c->log, "Cannot get wayland event loop.");
    return false;
  }

  wl_global_create(c->wl.dsp, &wl_compositor_interface, 4, c, _vt_comp_wl_bind);

  if (!vt_proto_xdg_shell_init(c, 1)) {
    VT_ERROR(c->log, "Cannot initialize XDG shell protocol.");
    return false;
  }

  if (!vt_proto_wl_data_device_init(c)) {
    VT_ERROR(c->log, "Cannot initialize Wayland data device protocol.");
    return false;
  }

  if (!vt_proto_wl_subcompositor_init(c)) {
    VT_ERROR(c->log, "Cannot initialize Wayland subcompositor protocol.");
    return false;
  }

  const char *socket_name = wl_display_add_socket_auto(c->wl.dsp);
  if (!socket_name) {
    VT_ERROR(
        c->log,
        "Failed to create Wayland socket: no clients will be able to connect.");
    return false;
  } else {
    VT_TRACE(c->log, "Wayland display ready on socket '%s'.", socket_name);
  }

  return true;
}

static struct vt_compositor_t *vt_global_compositor;

static void _sig_handler(int sig) {
  void  *trace[32];
  size_t n = backtrace(trace, 32);
  fprintf(stderr, "\n[vortex] Caught signal %d (%s)\n", sig, strsignal(sig));

  FILE *f = vt_global_compositor && vt_global_compositor->log.stream
                ? vt_global_compositor->log.stream
                : stderr;
  fprintf(f, "\n[vortex] ===== Fatal signal %d (%s) =====\n", sig,
          strsignal(sig));
  backtrace_symbols_fd(trace, n, fileno(f));
  fflush(f);

  signal(sig, SIG_DFL);
  raise(sig);
}

static void _handle_output_changed_backend(struct vt_backend_t *backend,
                                           struct vt_output_t  *output) {
  (void)backend;
  (void)output;
  if (backend->comp->root_node) {
    uint32_t            root_w = 0, root_h = 0;
    struct vt_output_t *output;
    wl_list_for_each(output, &backend->comp->outputs, link_global) {
      root_w += output->width;
      root_h += output->height;
    }

    backend->comp->root_node->rect_w = root_w;
    backend->comp->root_node->rect_h = root_h;
    vt_scene_node_update_global_bounds(backend->comp->root_node);
  }
}

bool vt_comp_init(struct vt_compositor_t *c, int argc, char **argv) {
  if(!c) return false;

  vt_util_arena_init(&c->arena, 1024 * 1024 * 2);
  vt_util_arena_init(&c->frame_arena, 1024 * 1024 * 2);

  vt_global_compositor = c;
  signal(SIGSEGV, _sig_handler);
  signal(SIGABRT, _sig_handler);
  signal(SIGFPE, _sig_handler);
  signal(SIGILL, _sig_handler);
  signal(SIGBUS, _sig_handler);

  wl_list_init(&c->focus_stack);
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

  const char *backend_str = _vt_comp_handle_cmd_flags(c, argc, argv);
  if (!backend_str) {
    if (getenv("WAYLAND_DISPLAY"))
      backend_str = "wl";
    else
      backend_str = "drm";
  }

  if (strcmp(backend_str, "wl") == 0 && !c->n_virtual_outputs)
    c->n_virtual_outputs = 1;

  _vt_comp_load_backend(c, backend_str, c->_cmd_line_backend_path);

  c->backend->on_output_change = _handle_output_changed_backend;

  if (!_vt_comp_wl_init(c)) {
    VT_ERROR(c->log, "Failed to initialize wayland state.");
    return false;
  }

  // Initialize session
  if (c->backend->platform == VT_BACKEND_DRM_GBM) {
    if (c->session->impl.init)
      c->session->impl.init(c->session);
  }

  // Initialize backend
  if (!c->backend->impl.init(c->backend)) {
    VT_ERROR(c->log, "Failed to initialize compositor backend.");
    return false;
  }

  enum vt_input_backend_platform_t input_backend = VT_INPUT_UNKNOWN;
  switch (c->backend->platform) {
  case VT_BACKEND_DRM_GBM:
    input_backend = VT_INPUT_LIBINPUT;
    break;
  case VT_BACKEND_WAYLAND:
    input_backend = VT_INPUT_WAYLAND;
    break;
  }
  vt_input_implement(c->input_backend, input_backend);

  if (c->input_backend->impl.init) {
    c->input_backend->impl.init(c->input_backend, c->session->native_handle);
  }

  vt_seat_init(c->seat);

  VT_TRACE(c->log, "Initialized wayland seat.");

  uint32_t            root_w = 0, root_h = 0;
  struct vt_output_t *output;
  wl_list_for_each(output, &c->outputs, link_global) {
    output->repaint_pending = false;
    vt_comp_schedule_repaint(c, output);
    root_w += output->width;
    root_h += output->height;
  }

  c->root_node = vt_scene_node_create_rect(c, 0, 0, root_w, root_h, 0x181818);

  c->root_node->type = VT_SCENE_NODE_ROOT;

  if (!c->have_proto_dmabuf) {
    VT_WARN(c->log, "Running vortex without support for linux-dmabuf protocol");
  }
  if (!c->have_proto_dmabuf_explicit_sync) {
    VT_WARN(c->log, "Running vortex without support for "
                    "linux-dmabuf-explicit-sync protocol");
  }

  return true;
}

void vt_comp_run(struct vt_compositor_t *c) {
  c->running = true;
  VT_TRACE(c->log, "Entering main event loop...");
  while (c->running) {
    vt_util_arena_reset(&c->frame_arena);

    wl_event_loop_dispatch(c->wl.evloop, -1);
    wl_display_flush_clients(c->wl.dsp);
  }
}

bool vt_comp_terminate(struct vt_compositor_t *c) {
  VT_TRACE(c->log, "Shutting down...");
  c->running = false;

  if (!(c->backend->impl.terminate(c->backend))) {
    VT_ERROR(c->log, "Failed to terminate backend");
    return false;
  }

  vt_seat_terminate(c->seat);

  c->input_backend->impl.terminate(c->input_backend);

  if (c->session->impl.terminate) {
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

void vt_comp_schedule_repaint(struct vt_compositor_t *c,
                              struct vt_output_t     *output) {
  if (!c || !output || !c->backend)
    return;
  if (c->suspended) {
    VT_WARN(c->log,
            "Trying to schedule repaint while compositor is suspended.");
    return;
  }
  if (output->repaint_pending) {
    return;
  }
  output->needs_repaint = true;
  if (!output->repaint_pending) {
    output->repaint_pending = true;
    output->repaint_source =
        wl_event_loop_add_idle(c->wl.evloop, _vt_comp_frame_handler, output);
  }

  VT_TRACE(c->log, "Scheduling repaint on output %p.", output);
}

void vt_comp_repaint_scene(struct vt_compositor_t *c,
                           struct vt_output_t     *output) {
  if (!c || !output || !c->backend || !c->renderer)
    return;

  vt_scene_render(c->renderer, output, c->root_node);
}

static bool _surface_accepts_input(struct vt_surface_t *surf, double sx,
                                   double sy) {
  if (!surf || !vt_surface_get_buffer(surf))
    return false;

  if (surf->applied.input_region_infinite) {
    return sx >= 0 && sy >= 0 && sx < surf->applied.width &&
           sy < surf->applied.height;
  }

  return pixman_region32_contains_point(&surf->applied.input_region,
                                        (int32_t)sx, (int32_t)sy, NULL);
}


static struct vt_surface_t *
_scene_pick_surface(struct vt_scene_node_t *node,
                    double parent_x,
                    double parent_y,
                    double px,
                    double py) {
  if (!node)
    return NULL;

  double x = parent_x + node->x;
  double y = parent_y + node->y;

  for (int i = (int)node->child_count - 1; i >= 0; i--) {
    struct vt_scene_node_t *child = node->childs[i];

    if (child->surf &&
        vt_surface_has_role(child->surf, VT_SURFACE_ROLE_CURSOR)) {
      continue;
    }

    struct vt_surface_t *surf =
        _scene_pick_surface(child, x, y, px, py);

    if (surf)
      return surf;
  }

  if (!node->surf)
    return NULL;

  struct vt_surface_t *surf = node->surf;

  if (!vt_surface_effectively_mapped(surf))
    return NULL;

  if (!vt_surface_get_buffer(surf))
    return NULL;

  double w = (double)surf->applied.width;
  double h = (double)surf->applied.height;

  if (px < x || py < y || px >= x + w || py >= y + h)
    return NULL;

  if (!_surface_accepts_input(surf, px - x, py - y))
    return NULL;

  return surf;
}

struct vt_surface_t *vt_comp_pick_surface(struct vt_compositor_t *comp,
                                          double x, double y) {
  struct vt_surface_t *surf = _scene_pick_surface(comp->root_node, 0, 0, x, y);

  return surf;
}

