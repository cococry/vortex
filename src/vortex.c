#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <linux/vt.h>
#include <libudev.h>
#include <wayland-util.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-server-core.h>
#include <wayland-client-core.h>
#include <wayland-client.h>
#include <drm/drm_fourcc.h>
#include <wayland-server-protocol.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libinput.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include <sys/mman.h>
#include <sys/vt.h>
#include <signal.h>
#include <wayland-egl.h>

#include <linux/input-event-codes.h>

#include <runara/runara.h>

#include "glad.h" 
#include "xdg-shell-protocol.h"
#include "xdg-shell-client-protocol.h"


#define _BRAND_NAME "vortex"  
#define _VERSION "alpha 0.1"

#define log_trace(logstate, ...) if(!(logstate).quiet)                              \
{ if((logstate).verbose) { do {                                                     \
  _log_header((logstate).stream, LL_TRACE);                                         \
  fprintf((logstate).stream, __VA_ARGS__);                                          \
  fprintf((logstate).stream, "\n");                                                 \
  if((logstate).stream != stdout &&                                                 \
    (logstate).stream != stderr && !(logstate).quiet) {                             \
    _log_header(stdout, LL_TRACE);                                                  \
    fprintf(stdout, __VA_ARGS__);                                                   \
    fprintf(stdout, "\n");                                                          \
  }                                                                                 \
} while(0); }  }                                                                    \

#define log_warn(logstate, ...) if(!(logstate).quiet) {                             \
  do {                                                                              \
    _log_header((logstate).stream, LL_WARN);                                        \
    fprintf((logstate).stream, __VA_ARGS__);                                        \
    fprintf((logstate).stream, "\n");                                               \
    if((logstate).stream != stdout &&                                               \
      (logstate).stream != stderr && !(logstate).quiet) {                           \
      _log_header(stdout, LL_WARN);                                                 \
      fprintf(stdout, __VA_ARGS__);                                                 \
      fprintf(stdout, "\n");                                                        \
    }                                                                               \
  } while(0); }                                                                     \

#define log_error(logstate, ...) if(!(logstate).quiet) {                            \
  do {                                                                              \
    _log_header((logstate).stream == stdout ? stderr : (logstate).stream, LL_ERR);  \
    fprintf((logstate).stream == stdout ? stderr : (logstate).stream, __VA_ARGS__); \
    fprintf((logstate).stream == stdout ? stderr : (logstate).stream, "\n");        \
    if((logstate).stream != stdout &&                                               \
      (logstate).stream != stderr && !(logstate).quiet) {                           \
      _log_header(stderr, LL_ERR);                                                  \
      fprintf(stderr, __VA_ARGS__);                                                 \
      fprintf(stderr, "\n");                                                        \
    }                                                                               \
  } while(0); }                                                                     \

typedef struct {
  FILE* stream;
  bool verbose, quiet;
} log_state_t;

typedef struct {
  int drm_fd;
  drmModeModeInfo mode;
  uint32_t conn_id;
  uint32_t crtc_id;
  drmEventContext evctx;
} drm_state_t;

typedef struct {
  struct gbm_device* gbm_dev;
  struct gbm_surface* gbm_surf;
} gbm_state_t;

typedef struct {
  bool nested;
  struct wl_display *parent_display;
  struct wl_compositor *parent_compositor;
  struct xdg_wm_base *parent_xdg_wm_base;
  struct wl_surface *parent_surface;
  struct xdg_surface *parent_xdg_surface;
  struct xdg_toplevel *parent_xdg_toplevel;
  struct wl_egl_window *egl_window;

  bool configured;
  int conf_w, conf_h;
} nested_state_t;


typedef struct {
  EGLDisplay egl_dsp;
  EGLContext egl_ctx;
  EGLSurface egl_surf;
  EGLConfig egl_conf;
  EGLint egl_native_vis;
} egl_state_t;

typedef struct {
  struct wl_display* dsp;
  struct wl_event_loop* evloop;
  struct wl_compositor* compositor;
  struct wl_global* xdg_wm_base; 
} wl_state_t;

typedef struct {
  struct wl_resource* surf_res, *buf_res, 
  *xdg_surf_res, *xdg_toplevel_res; 
  RnTexture tex; 
  struct wl_list frame_cbs;
  struct wl_list link;

  uint32_t last_configure_serial;

  char* app_id, *title;

  uint32_t width, height;
} vt_surface_t;

typedef struct {
  wl_state_t  wl;
  log_state_t log;
  drm_state_t drm;
  gbm_state_t gbm;
  egl_state_t egl;
  struct libinput* input;

  RnState* render;
  struct wl_list surfaces;

  int32_t tty_fd;

  struct gbm_bo *current_bo;
  struct gbm_bo *pending_bo;
  struct gbm_bo *prev_bo;
  uint32_t current_fb;
  uint32_t pending_fb;
  uint32_t prev_fb;

  bool suspended;

  bool needs_modeset;
  bool flip_inflight;
  bool repaint_pending;
  bool wants_repaint;
  bool bootstrapped;

  bool vt_acquire_pending, 
      vt_release_pending;

  bool running;

  nested_state_t nested;
} vt_compositor_t;

typedef struct {
  int fd;
  size_t size;
  void* data;
} vt_shm_pool_t;

static vt_compositor_t comp;

typedef enum {
  LL_TRACE = 0,
  LL_WARN,
  LL_ERR,
  LL_COUNT
} log_level_t;

static char* _log_get_filepath();
static void _log_header(FILE* stream, log_level_t lvl);
static void _log_help();

static bool flag_cmp(const char* flag, const char* lng, const char* shrt);

static bool drm_init(drm_state_t* drm); 

static const char* _egl_err_str(EGLint error);

static bool egl_gbm_init(int32_t device_fd, uint32_t dsp_w, uint32_t dsp_h, gbm_state_t* drm, egl_state_t* egl); 

static bool egl_nested_init(vt_compositor_t *c, int width, int height);

static bool comp_init(int argc, char** argv);

static void comp_run();

static void comp_shutdown();

static void comp_handle_cmd_flags(int argc, char** argv);


static void surface_attach(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *buffer,
  int32_t x,
  int32_t y);

static void surface_commit(
  struct wl_client *client,
  struct wl_resource *resource);

static void surface_frame(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t callback);

static void surface_damage(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);  

static void surface_set_opaque_region(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *region);

static void surface_set_input_region(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *region);

static void surface_set_buffer_transform(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t transform);

static void surface_set_buffer_scale(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t scale);

static void surface_damage_buffer(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

static void surface_offset(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x,
                           int32_t y);

static void surface_destroy(struct wl_client *client,
                            struct wl_resource *resource);

static void surface_handle_resource_destroy(struct wl_resource* resource);

static void comp_surface_create(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

static void release_all_scanout(vt_compositor_t *c);

static uint32_t get_time_msec(void);

static void vt_suspend(vt_compositor_t *c);


static bool vt_resume(vt_compositor_t *c);


static void send_frame_callbacks(vt_compositor_t *c, uint32_t t) ;

static void page_flip_handler(
  int fd, 
  unsigned int frame,
  unsigned int sec, 
  unsigned int usec, 
  void *data);

static void schedule_repaint(vt_compositor_t *c);

static void render_frame(vt_compositor_t *c);

static void drm_handle_frame(vt_compositor_t* c);

static void frame_handler(void *data);

static void vt_xdg_wm_base_bind(
  struct wl_client *client,
  void *data,
  uint32_t version,
  uint32_t id);

static void vt_xdg_wm_base_destroy(struct wl_client *client, struct wl_resource *resource);

static void vt_xdg_wm_base_create_positioner(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

static void vt_xdg_wm_base_get_xdg_surface(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id,
  struct wl_resource *surface_res);

static void vt_xdg_wm_base_pong(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t serial);

static void vt_xdg_positioner_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

static void vt_xdg_positioner_set_size(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t width,
  int32_t height);

static void vt_xdg_positioner_set_anchor_rect(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

static void vt_xdg_positioner_set_anchor(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t anchor);

static void vt_xdg_positioner_set_gravity(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t gravity);

static void vt_xdg_positioner_set_constraint_adjustment(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t constraint_adjustment);

static void vt_xdg_positioner_set_offset(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y);

static void vt_xdg_surface_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

static void vt_xdg_surface_get_toplevel(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

static void vt_xdg_surface_get_popup(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id,
  struct wl_resource *parent_surface,
  struct wl_resource *positioner);

static void vt_xdg_surface_ack_configure(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t serial);

static void vt_xdg_surface_set_window_geometry(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height);

static void vt_xdg_toplevel_destroy(
  struct wl_client *client,
  struct wl_resource *resource);

static void vt_xdg_toplevel_set_parent(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *parent_resource);

static void vt_xdg_toplevel_set_title(
  struct wl_client *client,
  struct wl_resource *resource,
  const char *title);

static void vt_xdg_toplevel_set_app_id(
  struct wl_client *client,
  struct wl_resource *resource,
  const char *app_id);

static void vt_xdg_toplevel_show_window_menu(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial,
  int32_t x,
  int32_t y);

static void vt_xdg_toplevel_move(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial);

static void vt_xdg_toplevel_resize(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *seat,
  uint32_t serial,
  uint32_t edges);

static void vt_xdg_toplevel_set_max_size(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t width,
  int32_t height);

static void vt_xdg_toplevel_set_min_size(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t width,
  int32_t height);

static void vt_xdg_toplevel_set_maximized(
  struct wl_client *client,
  struct wl_resource *resource);

static void vt_xdg_toplevel_unset_maximized(
  struct wl_client *client,
  struct wl_resource *resource);

static void vt_xdg_toplevel_set_fullscreen(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *output);

static void vt_xdg_toplevel_unset_fullscreen(struct wl_client *client, struct wl_resource *resource);

static void vt_xdg_toplevel_set_minimized(struct wl_client *client, struct wl_resource *resource);

static void parent_xdg_wm_base_ping(void *data, struct xdg_wm_base *wm, uint32_t serial);

static void parent_registry_add(void *data, struct wl_registry *reg,
                                uint32_t id, const char *iface, uint32_t ver);

static void parent_registry_remove(void *data, struct wl_registry *reg, uint32_t id);

static void parent_xdg_surface_configure(void *data,
                                         struct xdg_surface *surf, uint32_t serial);

static void parent_xdg_toplevel_configure(void *data,
                                          struct xdg_toplevel *toplevel, int32_t w, int32_t h, struct wl_array *states);
static void parent_xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel);

static int parent_display_ready(int fd, uint32_t mask, void *data);

static int drm_fd_ready(int fd, uint32_t mask, void *data);

static int libinput_fd_ready(int fd, uint32_t mask, void *data);

static void comp_bind(struct wl_client *client, void *data,
               uint32_t version, uint32_t id);

static void handle_vt_release(int sig);

static void handle_vt_acquire(int sig);


static const struct xdg_toplevel_interface xdg_toplevel_impl = {
  .destroy = vt_xdg_toplevel_destroy,
  .set_parent = vt_xdg_toplevel_set_parent,
  .set_title = vt_xdg_toplevel_set_title,
  .set_app_id = vt_xdg_toplevel_set_app_id,
  .show_window_menu = vt_xdg_toplevel_show_window_menu,
  .move = vt_xdg_toplevel_move,
  .resize = vt_xdg_toplevel_resize,
  .set_max_size = vt_xdg_toplevel_set_max_size,
  .set_min_size = vt_xdg_toplevel_set_min_size,
  .set_maximized = vt_xdg_toplevel_set_maximized,
  .unset_maximized = vt_xdg_toplevel_unset_maximized,
  .set_fullscreen = vt_xdg_toplevel_set_fullscreen,
  .unset_fullscreen = vt_xdg_toplevel_unset_fullscreen,
  .set_minimized = vt_xdg_toplevel_set_minimized,
};


static const struct xdg_surface_interface xdg_surface_impl = {
  .destroy = vt_xdg_surface_destroy,
  .get_toplevel = vt_xdg_surface_get_toplevel,
  .get_popup = vt_xdg_surface_get_popup,
  .ack_configure = vt_xdg_surface_ack_configure,
  .set_window_geometry = vt_xdg_surface_set_window_geometry,
};

static const struct wl_compositor_interface compositor_impl = {
  .create_surface = comp_surface_create,
  .create_region = NULL
};

static const struct xdg_positioner_interface xdg_positioner_impl = {
  .destroy = vt_xdg_positioner_destroy,
  .set_size = vt_xdg_positioner_set_size,
  .set_anchor_rect = vt_xdg_positioner_set_anchor_rect,
  .set_anchor = vt_xdg_positioner_set_anchor,
  .set_gravity = vt_xdg_positioner_set_gravity,
  .set_constraint_adjustment = vt_xdg_positioner_set_constraint_adjustment,
  .set_offset = vt_xdg_positioner_set_offset,
};

static const struct xdg_wm_base_listener parent_wm_listener = {
  .ping = parent_xdg_wm_base_ping,
};

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
static const struct xdg_toplevel_listener parent_toplevel_listener = {
  .configure = parent_xdg_toplevel_configure,
  .close = parent_xdg_toplevel_close,
};
static const struct xdg_surface_listener parent_xdg_surface_listener = {
  .configure = parent_xdg_surface_configure,
};


static const struct wl_surface_interface surface_impl = {
  .attach = surface_attach,
  .commit = surface_commit,
  .damage = surface_damage, 
  .frame = surface_frame,
  .set_opaque_region = surface_set_opaque_region,
  .set_input_region = surface_set_input_region,
  .set_buffer_scale = surface_set_buffer_scale,
  .set_buffer_transform = surface_set_buffer_transform,
  .offset = surface_offset, 
  .destroy = surface_destroy,
  .damage_buffer = surface_damage_buffer, 
};

static const struct xdg_wm_base_interface xdg_wm_base_impl = {
  .destroy = vt_xdg_wm_base_destroy,
  .create_positioner = vt_xdg_wm_base_create_positioner,
  .get_xdg_surface = vt_xdg_wm_base_get_xdg_surface,
  .pong = vt_xdg_wm_base_pong,
};

static const struct wl_registry_listener parent_registry_listener = {
  .global = parent_registry_add,
  .global_remove = parent_registry_remove,
};

char* 
_log_get_filepath() {
  static char path[PATH_MAX];
  char *state_home = getenv("XDG_STATE_HOME");
  const char *home = getenv("HOME");
  time_t now = time(NULL);
  struct tm t;
  pid_t pid = getpid();

  // Use XDG_STATE_HOME if set, otherwise fallback to ~/.local/state
  if (!state_home && home) {
    snprintf(path, sizeof(path), "%s/.local/state", home);
    state_home = path;
  }

  char log_dir[PATH_MAX];
  snprintf(log_dir, sizeof(log_dir), "%s/%s/logs", state_home, _BRAND_NAME);

  // create directories if they don't exist
  mkdir(state_home, 0755); 
  char app_dir[PATH_MAX];
  snprintf(app_dir, sizeof(app_dir), "%s/%s", state_home, _BRAND_NAME);
  mkdir(app_dir, 0755);
  mkdir(log_dir, 0755);

  localtime_r(&now, &t);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d-%H%M", &t);

  // scheme: <log_dir>/<appname>-<timestamp>-<pid>.log
  static char logfile[PATH_MAX];
  snprintf(logfile, sizeof(logfile), "%s/%s-%s-%d.log",
           log_dir, _BRAND_NAME, timestamp, pid);

  return logfile;
}


void 
_log_header(FILE* stream, log_level_t lvl) {
  static const char* lvl_str[LL_COUNT] = { "TRACE", "WARNING", "ERROR" };
  static const char* lvl_clr[LL_COUNT] = { "\033[1;32m", "\033[1;33m", "\033[1;31m" };
  const char* fnt_bold  = "\033[1m";
  const char* clr_reset = "\033[0m";
  const char* clr_blue  = "\033[1;34m";

  time_t rawtime;
  struct tm timeinfo;
  // 9 = HH:MM:SS + null terminator
  char timebuf[9];  
  time(&rawtime);
  localtime_r(&rawtime, &timeinfo);
  strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &timeinfo);

  bool colorize = (stream == stderr || stream == stdout); 
  fprintf(
    stream, "["_BRAND_NAME"]: %s%s%s%s: %s%s%s%s: ", 
    // log level 
    colorize ? lvl_clr[lvl] : "", colorize ?  fnt_bold : "",
    lvl_str[lvl], colorize ? clr_reset : "",

    // time (blue, bold)
    colorize ? fnt_bold : "", colorize ? clr_blue : "",
    timebuf, colorize ? clr_reset : ""
  );
}

void 
_log_help() {
  printf("Usage: vortex [option:s] (value:s)\n");
  printf("Options: \n");
  printf("%-20s %s\n", "-h, --help", "Show this help message and exit");
  printf("%-20s %s\n", "-v, --version", "Show version information");
  printf("%-20s %s\n", "-vb, --verbose", "Log verbose (trace) output");
  printf("%-20s %s\n", "-lf, --logfile", 
         "Write logs to a logfile (~/.local/state/vortex/logs/ or if available $XDG_STATE_HOME/vortex/logs)");
  printf("%-20s %s\n", "-q, --quiet", "Run in quiet mode");
}

bool 
flag_cmp(const char* flag, const char* lng, const char* shrt) {
  return strcmp(flag, lng) == 0 || strcmp(flag, shrt) == 0;
}

bool 
drm_init(drm_state_t* drm) {
  drm->drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
  if (drm->drm_fd < 0) {
    log_error(comp.log, "cannot open DRM: %s", strerror(errno));
    return false;
  }

  drmModeRes *res = drmModeGetResources(drm->drm_fd);
  if (!res) {
    log_error(comp.log, "drmModeGetResources() failed: %s", strerror(errno));
    return false;
  }

  drmModeConnector *conn = NULL;
  drmModeEncoder *enc = NULL;

  // Pick the first connected connector
  for (int i = 0; i < res->count_connectors; i++) {
    conn = drmModeGetConnector(drm->drm_fd, res->connectors[i]);
    if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
      drm->conn_id = conn->connector_id;
      drm->mode = conn->modes[0]; // pick preferred mode
      break;
    }
    drmModeFreeConnector(conn);
    conn = NULL;
  }

  if (!conn) {
    log_error(comp.log, "No connected DRM connector found");
    drmModeFreeResources(res);
    return false;
  }

  // Try to find a usable encoder
  if (conn->encoder_id)
    enc = drmModeGetEncoder(drm->drm_fd, conn->encoder_id);

  if (enc) {
    drm->crtc_id = enc->crtc_id;
  } else if (res->count_encoders > 0) {
    // fallback: try all encoders for this connector
    for (int i = 0; i < conn->count_encoders; i++) {
      enc = drmModeGetEncoder(drm->drm_fd, conn->encoders[i]);
      if (!enc)
        continue;

      for (int j = 0; j < res->count_crtcs; j++) {
        if (enc->possible_crtcs & (1 << j)) {
          drm->crtc_id = res->crtcs[j];
          break;
        }
      }
      drmModeFreeEncoder(enc);
      if (drm->crtc_id)
        break;
    }
  }

  if (!drm->crtc_id) {
    log_error(comp.log, "Failed to find CRTC for connector %u", drm->conn_id);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    return false;
  }

  drmModeFreeConnector(conn);
  drmModeFreeResources(res);

  if (drmSetMaster(drm->drm_fd) != 0) {
    log_error(comp.log, "drmSetMaster failed: %s", strerror(errno));
    exit(1);
  }

  log_trace(comp.log, "Using DRM connector %u, CRTC %u, mode %ux%u@%u",
            drm->conn_id, drm->crtc_id,
            drm->mode.hdisplay, drm->mode.vdisplay, drm->mode.vrefresh);

  comp.drm.evctx = (drmEventContext){
    .version = DRM_EVENT_CONTEXT_VERSION,
    .page_flip_handler = page_flip_handler,
  };

  return true;
}

const char*
_egl_err_str(EGLint error) {
  switch (error) {
    case EGL_SUCCESS:                return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:         return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:              return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:               return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:           return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG:              return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT:             return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE:     return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:             return "EGL_BAD_DISPLAY";
    case EGL_BAD_SURFACE:             return "EGL_BAD_SURFACE";
    case EGL_BAD_MATCH:               return "EGL_BAD_MATCH";
    case EGL_BAD_PARAMETER:           return "EGL_BAD_PARAMETER";
    case EGL_BAD_NATIVE_PIXMAP:       return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:       return "EGL_BAD_NATIVE_WINDOW";
    case EGL_CONTEXT_LOST:            return "EGL_CONTEXT_LOST";
    default:                          return "Unknown EGL error";
  }
}


/*
 * Die Qual der Mühe vergeht, doch die Reue des Versagens wird dich für immer begleiten.
 * */
bool 
egl_gbm_init(int32_t device_fd, uint32_t dsp_w, uint32_t dsp_h, gbm_state_t* gbm, egl_state_t* egl) {
  if(!(gbm->gbm_dev = gbm_create_device(device_fd))) {
    log_error(comp.log, "cannot create GBM device (fd: %i)", device_fd);
    return false;
  }
  const uint32_t desired_format = GBM_FORMAT_XRGB8888;
  if(!(gbm->gbm_surf = gbm_surface_create(
    gbm->gbm_dev,
    dsp_w, dsp_h,
    desired_format,
    GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
  ))) {
    log_error(comp.log, "cannot create GBM surface (%ix%i) for rendering.", dsp_w, dsp_h);
    return false;
  }


  PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
    (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (!eglGetPlatformDisplayEXT) {
    int32_t err = eglGetError();
    log_error(comp.log, "EGL_EXT_platform_base not supported: 0x%04x (%s)", err, _egl_err_str(err));
    return false;
  }

  egl->egl_dsp = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR, gbm->gbm_dev, NULL);
  if(egl->egl_dsp == EGL_NO_DISPLAY) {
    int32_t err = eglGetError();
    log_error(comp.log, "cannot get EGL display: 0x%04x (%s)", err,_egl_err_str(err));
    return false;
  }
  if (!eglInitialize(egl->egl_dsp, NULL, NULL)) {
    int32_t err = eglGetError();
    log_error(comp.log, "failed to initialize EGL: 0x%04x (%s)", err, _egl_err_str(err));
    return false;
  }

  log_trace(comp.log, "successfully initialized EGL.");

  eglBindAPI(EGL_OPENGL_API);

  EGLint cfg_attr[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, 
    EGL_NONE
  };

  EGLConfig config;
  EGLint num;
  eglChooseConfig(egl->egl_dsp, cfg_attr, &config, 1, &num);
  egl->egl_conf = config;

  EGLint ctx_attr[] = {
    EGL_CONTEXT_MAJOR_VERSION, 4,
    EGL_CONTEXT_MINOR_VERSION, 5,
    EGL_NONE
  };

  egl->egl_ctx = eglCreateContext(egl->egl_dsp, config, EGL_NO_CONTEXT, ctx_attr);
  if (egl->egl_ctx == EGL_NO_CONTEXT) {
    int err = eglGetError();
    log_error(comp.log, "eglCreateContext() failed: 0x%04x (%s)", err, _egl_err_str(err));
    return false;
  }

  if(egl->egl_ctx == EGL_NO_CONTEXT) {
    int32_t err = eglGetError();
    log_error(comp.log, "eglCreateContext() failed: 0x%04x (%s)",  err, _egl_err_str(err));
    return false;
  }
  EGLint visual_id;
  eglGetConfigAttrib(egl->egl_dsp, config, EGL_NATIVE_VISUAL_ID, &visual_id);
  egl->egl_native_vis = visual_id;

  // resolve the case where the native visual has a different format then we desire
  if (visual_id != desired_format) {
    gbm_surface_destroy(gbm->gbm_surf);
    gbm->gbm_surf = gbm_surface_create(
      gbm->gbm_dev, dsp_w, dsp_h,
      visual_id, 
      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
  }


  egl->egl_surf = eglCreateWindowSurface(egl->egl_dsp, config, (EGLNativeWindowType)gbm->gbm_surf, NULL);
  if(egl->egl_surf == EGL_NO_SURFACE) {
    int32_t err = eglGetError();
    log_error(comp.log, "eglCreateWindowSurface() failed: 0x%04x (%s)\n", err, _egl_err_str(err));
  } 
  if(!eglMakeCurrent(egl->egl_dsp, egl->egl_surf, egl->egl_surf, egl->egl_ctx)) {
    int32_t err = eglGetError();
    log_error(comp.log, "eglMakeCurrent() failed:  0x%04x (%s)", err, _egl_err_str(err));
    return false;
  }

  log_trace(comp.log, "successfully created and set EGL context to surface.");

  return true;
}

bool 
egl_nested_init(vt_compositor_t *c, int width, int height) {
  c->nested.parent_display = wl_display_connect(NULL);
  if (!c->nested.parent_display) {
    log_error(c->log, "VORTEX_NESTED: failed to connect to parent Wayland compositor.");
    return false;
  }

  // Bind globals
  struct wl_registry *reg = wl_display_get_registry(c->nested.parent_display);
  wl_registry_add_listener(reg, &parent_registry_listener, c);
  wl_display_roundtrip(c->nested.parent_display);
  if (!c->nested.parent_compositor || !c->nested.parent_xdg_wm_base) {
    log_error(c->log, "VORTEX_NESTED: required globals not found.");
    return false;
  }

  // Add wm_base ping listener
  xdg_wm_base_add_listener(c->nested.parent_xdg_wm_base, &parent_wm_listener, c);

  // Create surface + xdg objects and add listeners
  c->nested.parent_surface = wl_compositor_create_surface(c->nested.parent_compositor);
  c->nested.parent_xdg_surface =
    xdg_wm_base_get_xdg_surface(c->nested.parent_xdg_wm_base, c->nested.parent_surface);
  xdg_surface_add_listener(c->nested.parent_xdg_surface, &parent_xdg_surface_listener, c);

  c->nested.parent_xdg_toplevel = xdg_surface_get_toplevel(c->nested.parent_xdg_surface);
  xdg_toplevel_add_listener(c->nested.parent_xdg_toplevel, &parent_toplevel_listener, c);
  xdg_toplevel_set_title(c->nested.parent_xdg_toplevel, "Vortex Nested");

  // Trigger initial configure
  wl_surface_commit(c->nested.parent_surface);
  // Get the initial configure immidiately
  wl_display_roundtrip(c->nested.parent_display); 

  // Create EGL
  EGLDisplay egl_dsp = eglGetDisplay((EGLNativeDisplayType)c->nested.parent_display);
  if (egl_dsp == EGL_NO_DISPLAY) {
    log_error(c->log, "eglGetDisplay failed for nested mode");
    return false;
  }
  eglInitialize(egl_dsp, NULL, NULL);
  eglBindAPI(EGL_OPENGL_API);

  EGLint cfg_attr[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_NONE
  };
  EGLConfig config; EGLint n;
  eglChooseConfig(egl_dsp, cfg_attr, &config, 1, &n);

  // Size from configure if present
  int w = c->nested.conf_w > 0 ? c->nested.conf_w : width;
  int h = c->nested.conf_h > 0 ? c->nested.conf_h : height;

  c->nested.egl_window = wl_egl_window_create(c->nested.parent_surface, w, h);
  EGLSurface surf = eglCreateWindowSurface(egl_dsp, config, c->nested.egl_window, NULL);
  EGLContext ctx = eglCreateContext(egl_dsp, config, EGL_NO_CONTEXT, NULL);
  eglMakeCurrent(egl_dsp, surf, surf, ctx);

  c->egl.egl_dsp = egl_dsp;
  c->egl.egl_ctx = ctx;
  c->egl.egl_surf = surf;
  c->egl.egl_conf = config;
  c->egl.egl_native_vis = 0;

  log_trace(c->log, "Nested EGL context ready (%dx%d)", w, h);
  return true;
}


bool
comp_init(int argc, char** argv) {
  comp.log.stream = stdout;
  comp.log.verbose = false;
  comp.log.quiet = false;

  comp.needs_modeset   = true;
  comp.flip_inflight   = false;
  comp.repaint_pending = false;
  comp.wants_repaint   = true; 
  comp.bootstrapped    = false;

  comp.current_bo = NULL;
  comp.pending_bo = NULL;
  comp.prev_bo = NULL;
  comp.current_fb = 0;
  comp.pending_fb = 0;
  comp.prev_fb = 0;

  comp_handle_cmd_flags(argc, argv);

  if(!comp.nested.nested) {
    signal(SIGUSR1, handle_vt_release);
    signal(SIGUSR2, handle_vt_acquire);

    comp.tty_fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
    if(comp.tty_fd < 0) {
      log_error(comp.log, "failed to open /dev/tty");
      return false;
    }

    if(ioctl(comp.tty_fd, KDSETMODE, KD_GRAPHICS) < 0) {
      log_warn(comp.log, "failed to enter TTY graphics mode.");
    }
    struct vt_mode tty_mode = {
      .mode = VT_PROCESS,
      .waitv = 1,
      .relsig = SIGUSR1, 
      .acqsig = SIGUSR2 
    };
    if(ioctl(comp.tty_fd, VT_SETMODE, &tty_mode) < 0) {
      log_warn(comp.log, "failed to set TTY switching mode.");
    }
  }
  if(!(comp.wl.dsp = wl_display_create())) {
    log_error(comp.log, "cannot create wayland display.");
    return false;
  }

  log_trace(comp.log, "sucessfully created wayland display.");

  wl_list_init(&comp.surfaces);

  comp.wl.evloop = wl_display_get_event_loop(comp.wl.dsp);

  if (comp.nested.nested) {
    log_trace(comp.log, "Running in nested mode (Wayland client)");
    if (!egl_nested_init(&comp, 1920, 1080)) {
      log_error(comp.log, "Nested EGL init failed");
      return false;
    }
    int pfd = wl_display_get_fd(comp.nested.parent_display);
    wl_event_loop_add_fd(comp.wl.evloop, pfd,
                         WL_EVENT_READABLE, parent_display_ready, &comp);
  } else {
    log_trace(comp.log, "Running in DRM/KMS mode.");
    if(!drm_init(&comp.drm) ||
      !egl_gbm_init(comp.drm.drm_fd, comp.drm.mode.hdisplay, comp.drm.mode.vdisplay,
                    &comp.gbm, &comp.egl)) {
      log_error(comp.log, "failed to initialize DRM/GBM/EGL backend.");

      return false;
    }
  }


  wl_display_init_shm(comp.wl.dsp);

  uint32_t w = comp.nested.nested ? (comp.nested.conf_w > 0 ? comp.nested.conf_w : 1280)
    : comp.drm.mode.hdisplay;
  uint32_t h = comp.nested.nested ? (comp.nested.conf_h > 0 ? comp.nested.conf_h : 720)
    : comp.drm.mode.vdisplay;
  comp.render = rn_init(w, h, (RnGLLoader)eglGetProcAddress);

  wl_global_create(comp.wl.dsp, &wl_compositor_interface, 4, &comp, comp_bind); 

  comp.wl.xdg_wm_base = wl_global_create(comp.wl.dsp, &xdg_wm_base_interface, 1, NULL, vt_xdg_wm_base_bind);

  log_trace(comp.log, "entering event loop!");

  const char *socket_name = wl_display_add_socket_auto(comp.wl.dsp);
  if (!socket_name) {
    log_error(comp.log, "failed to create Wayland socket: no clients will be able to connect.");
    return false; 
  } else {
    log_trace(comp.log, "wayland display ready on socket '%s'.", socket_name);
  }

  if(!comp.nested.nested) {
    struct udev* udev = udev_new();
    if(!udev) {
      log_error(comp.log, "failed to create udev context.");
      return false;
    } else {
      log_trace(comp.log, "successfully created udev context.");
    }
    comp.input = libinput_udev_create_context(&input_interface, NULL, udev);
    if(!comp.input) {
      log_error(comp.log, "failed to create libinput context.");
      return false;
    } else {
      log_trace(comp.log, "successfully created libinput context.");
    }
    libinput_udev_assign_seat(comp.input, "seat0");
    libinput_dispatch(comp.input);
  }


  if(!comp.nested.nested) {
    wl_event_loop_add_fd(comp.wl.evloop, comp.drm.drm_fd,
                         WL_EVENT_READABLE, drm_fd_ready, &comp);

    int li_fd = libinput_get_fd(comp.input);
    wl_event_loop_add_fd(comp.wl.evloop, li_fd,
                         WL_EVENT_READABLE, libinput_fd_ready, &comp);
  }

  comp.needs_modeset = true;
  wl_event_loop_add_idle(comp.wl.evloop, frame_handler, &comp);

  return true;
}

void 
comp_run() {
  comp.running = true;
  while (comp.running) {
    wl_event_loop_dispatch(comp.wl.evloop, -1);
    wl_display_flush_clients(comp.wl.dsp);

    if(!comp.nested.nested) {
      if (comp.vt_release_pending) {
        comp.vt_release_pending = 0;
        vt_suspend(&comp);
      }
      if (comp.vt_acquire_pending) {
        comp.vt_acquire_pending = 0;
        if (!vt_resume(&comp)) {
          // If resume failed, stop to avoid tight error loop
          comp.running = false;
        }
      }
    }
  }
}


void
comp_shutdown() {
  log_trace(comp.log, "shutting down...");

  comp.running = false;

  if (!comp.nested.nested && !comp.suspended) {
    while (comp.flip_inflight) {
      drmHandleEvent(comp.drm.drm_fd, &comp.drm.evctx);
    }
  }

  if (comp.egl.egl_dsp != EGL_NO_DISPLAY) {
    eglMakeCurrent(comp.egl.egl_dsp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if (comp.egl.egl_surf != EGL_NO_SURFACE) {
      eglDestroySurface(comp.egl.egl_dsp, comp.egl.egl_surf);
      comp.egl.egl_surf = EGL_NO_SURFACE;
    }

    if (comp.egl.egl_ctx != EGL_NO_CONTEXT) {
      eglDestroyContext(comp.egl.egl_dsp, comp.egl.egl_ctx);
      comp.egl.egl_ctx = EGL_NO_CONTEXT;
    }

    eglTerminate(comp.egl.egl_dsp);
    comp.egl.egl_dsp = EGL_NO_DISPLAY;
  }

  if (!comp.nested.nested) {
    release_all_scanout(&comp);

    if (comp.gbm.gbm_surf) {
      gbm_surface_destroy(comp.gbm.gbm_surf);
      comp.gbm.gbm_surf = NULL;
    }
    if (comp.gbm.gbm_dev) {
      gbm_device_destroy(comp.gbm.gbm_dev);
      comp.gbm.gbm_dev = NULL;
    }

    if (comp.drm.drm_fd > 0) {
      drmDropMaster(comp.drm.drm_fd);
    }

    if (comp.tty_fd > 0) {
      ioctl(comp.tty_fd, KDSETMODE, KD_TEXT);

      struct vt_mode mode = { .mode = VT_AUTO };
      ioctl(comp.tty_fd, VT_SETMODE, &mode);

      close(comp.tty_fd);
      comp.tty_fd = -1;
    }

    if (comp.input) {
      libinput_unref(comp.input);
      comp.input = NULL;
    }

    if (comp.drm.drm_fd > 0) {
      close(comp.drm.drm_fd);
      comp.drm.drm_fd = -1;
    }
  }

  if (comp.nested.nested) {
    if (comp.nested.egl_window) {
      wl_egl_window_destroy(comp.nested.egl_window);
      comp.nested.egl_window = NULL;
    }

    if (comp.nested.parent_surface) {
      wl_surface_destroy(comp.nested.parent_surface);
      comp.nested.parent_surface = NULL;
    }

    if (comp.nested.parent_xdg_surface) {
      xdg_surface_destroy(comp.nested.parent_xdg_surface);
      comp.nested.parent_xdg_surface = NULL;
    }

    if (comp.nested.parent_xdg_toplevel) {
      xdg_toplevel_destroy(comp.nested.parent_xdg_toplevel);
      comp.nested.parent_xdg_toplevel = NULL;
    }

    if (comp.nested.parent_xdg_wm_base) {
      xdg_wm_base_destroy(comp.nested.parent_xdg_wm_base);
      comp.nested.parent_xdg_wm_base = NULL;
    }

    if (comp.nested.parent_display) {
      wl_display_disconnect(comp.nested.parent_display);
      comp.nested.parent_display = NULL;
    }
  }

  if (comp.wl.dsp) {
    wl_display_destroy_clients(comp.wl.dsp);
    wl_display_destroy(comp.wl.dsp);
    comp.wl.dsp = NULL;
  }

  if (comp.render) {
    rn_terminate(comp.render);
    comp.render = NULL;
  }

  if (comp.log.stream && comp.log.stream != stdout && comp.log.stream != stderr) {
    fclose(comp.log.stream);
    comp.log.stream = NULL;
  }

  log_trace(comp.log, "shutdown complete.");
}

void
comp_handle_cmd_flags(int argc, char** argv) {
  if(argc > 1) {
    for(uint32_t i = 1; i < argc; i++) {
      char* flag = argv[i];
      if(flag_cmp(flag, "--logfile", "-lf")) {
        comp.log.stream = fopen(_log_get_filepath(), "w"); 
        if (comp.log.stream)
          setvbuf(comp.log.stream, NULL, _IONBF, 0);
      } else if(flag_cmp(flag, "--verbose", "-vb")) {
        comp.log.verbose = true;
      } else if(flag_cmp(flag, "--quiet", "-q")) {
        comp.log.quiet = true;
      } else if(flag_cmp(flag, "-h", "--help")) {
        _log_help();
      } else if(flag_cmp(flag, "-v", "--version")) {
        printf(_VERSION"\n");
        exit(0);
      } else if(flag_cmp(flag, "-n", "--nested")) {
        comp.nested.nested = true;
      } else {
        log_error(comp.log, "invalid option -- '%s'", flag);
        exit(1);
      }
    }
  }

}

void 
surface_attach(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *buffer,
  int32_t x,
  int32_t y) {
  // When a client attaches an allocated buffer, store the resource handle 
  // in the surface struct
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  surf->buf_res = buffer;
}

void 
surface_commit(
  struct wl_client *client,
  struct wl_resource *resource) {
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  if (!surf) { log_error(comp.log, "surface_commit: NULL user_data"); return; }

  // No buffer attached, this commit is illegal  
  if (!surf->buf_res) {
    return;
  }

  // Retrieve the shared data inside the buffer and upload it to a GPU 
  // texture, after we finished uploading, send a buffer release to the client. 
  struct wl_shm_buffer* shm_buf = wl_shm_buffer_get(surf->buf_res);
  if (!shm_buf) {
    log_error(comp.log, "surface_commit: wl_shm_buffer_get failed");
    return;
  }

  wl_shm_buffer_begin_access(shm_buf);

  int width  = wl_shm_buffer_get_width(shm_buf);
  int height = wl_shm_buffer_get_height(shm_buf);
  int stride = wl_shm_buffer_get_stride(shm_buf);
  enum wl_shm_format fmt = wl_shm_buffer_get_format(shm_buf);
  const void* data = wl_shm_buffer_get_data(shm_buf);

  if (width <= 0 || height <= 0 || !data) {
    log_error(comp.log, "surface_commit: invalid shm buffer: %dx%d data=%p", width, height, data);
    wl_shm_buffer_end_access(shm_buf);
    return;
  }

  if (!surf->tex.id) glGenTextures(1, &surf->tex.id);

  glBindTexture(GL_TEXTURE_2D, surf->tex.id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);

  GLenum format = GL_BGRA;
  GLenum type   = GL_UNSIGNED_INT_8_8_8_8_REV;
  GLenum internal_format = GL_RGBA8;

  switch (fmt) {
    case WL_SHM_FORMAT_XRGB8888:
      internal_format = GL_RGB8;
      break;
    case WL_SHM_FORMAT_ARGB8888:
    default:
      break;
  }

  glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height,
               0, format, type, data);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);


  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);

  wl_shm_buffer_end_access(shm_buf);

  if (fmt == WL_SHM_FORMAT_XRGB8888) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
  }

  // Tell the client we're finsied uploading its buffer
  wl_buffer_send_release(surf->buf_res);

  // If we are not in the middle of a page flip, schedule a repaint, 
  // as the new contents of the clients need to be presented.
  surf->buf_res = NULL;   
  if(!comp.flip_inflight) {
    schedule_repaint(&comp);
  }

  surf->tex.width = width;
  surf->tex.height = height;

  // Setting logical dimensions inside the compositor to the commited buffer's
  // dimensions.
  surf->width = width;
  surf->height = height;

}


struct vt_frame_cb {
  struct wl_resource *res;
  struct wl_list link;
};
void
surface_frame(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t callback) {
  struct vt_frame_cb *node = calloc(1, sizeof *node);
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  if (!surf) return;
  node->res = wl_resource_create(client, &wl_callback_interface, 1, callback);

  // Store the frame callback in the list of pending frame callbacks.
  // wl_callback_send_done must be called for each of the pending callbacks
  // after the next page flip event completes in order to correctly handle 
  // frame pacing ( see send_frame_callbacks() ).
  wl_list_insert(&surf->frame_cbs, &node->link);
}

void 
surface_damage(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height) {

}

void
surface_set_opaque_region(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *region) {

}

void 
surface_set_input_region(struct wl_client *client,
                              struct wl_resource *resource,
                              struct wl_resource *region) {

}

void
surface_set_buffer_transform(struct wl_client *client,
                                  struct wl_resource *resource,
                                  int32_t transform) {

}

void
surface_set_buffer_scale(struct wl_client *client,
                              struct wl_resource *resource,
                              int32_t scale) {

}

void 
surface_damage_buffer(struct wl_client *client,
                           struct wl_resource *resource,
                           int32_t x,
                           int32_t y,
                           int32_t width,
                           int32_t height) {

}

void
surface_offset(struct wl_client *client,
                    struct wl_resource *resource,
                    int32_t x,
                    int32_t y) {

}

void
surface_destroy(struct wl_client *client,
                     struct wl_resource *resource) {
  wl_resource_destroy(resource);
  
  log_trace(comp.log, 
            "Got surface.destroy: Destroying surface resource.")
}

void 
surface_handle_resource_destroy(struct wl_resource* resource) {
  vt_surface_t* surf = wl_resource_get_user_data(resource);

  wl_list_remove(&surf->link);
  free(surf);

  schedule_repaint(&comp);
  
  log_trace(comp.log, 
            "Got surface.destroy handler: Unmanaging client.")
}

void
comp_surface_create(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id) {
  // Allocate the struct to store compositor information about the surface
  vt_surface_t* surf = calloc(1, sizeof(*surf));
  surf->title  = NULL;
  surf->app_id = NULL;

  wl_list_init(&surf->frame_cbs);

  // Add the surface to list of surfaces in the compositor
  wl_list_insert(&comp.surfaces, &surf->link);

  // Get the surface's wayland resource
  struct wl_resource* res = wl_resource_create(client, &wl_surface_interface, 4, id);
  wl_resource_set_implementation(res, &surface_impl, surf, surface_handle_resource_destroy);
  surf->surf_res = res;

  log_trace(comp.log, "Got compositor.create_surface: Started managing surface.")
}

void 
release_all_scanout(vt_compositor_t *c) {
  // Basically release all used DRM scanout buffers  
  if (c->pending_bo) {
    drmModeRmFB(c->drm.drm_fd, c->pending_fb);
    gbm_surface_release_buffer(c->gbm.gbm_surf, c->pending_bo);
    c->pending_bo = NULL; c->pending_fb = 0;
  }
  if (c->current_bo) {
    drmModeRmFB(c->drm.drm_fd, c->current_fb);
    gbm_surface_release_buffer(c->gbm.gbm_surf, c->current_bo);
    c->current_bo = NULL; c->current_fb = 0;
  }
  if (c->prev_bo) {
    drmModeRmFB(c->drm.drm_fd, c->prev_fb);
    gbm_surface_release_buffer(c->gbm.gbm_surf, c->prev_bo);
    c->prev_bo = NULL; c->prev_fb = 0;
  }
}

uint32_t
get_time_msec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}


void 
vt_suspend(vt_compositor_t *c) {
  if (c->suspended) return;
  c->suspended = true;

  // Drain any pending flips
  while (c->flip_inflight)
    drmHandleEvent(c->drm.drm_fd, &comp.drm.evctx);

  // Release DRM scanout buffers 
  release_all_scanout(c);

  // Unbind context
  eglMakeCurrent(c->egl.egl_dsp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

  // Destroy EGLSurface + GBM surface
  if (c->egl.egl_surf != EGL_NO_SURFACE) {
    eglDestroySurface(c->egl.egl_dsp, c->egl.egl_surf);
    c->egl.egl_surf = EGL_NO_SURFACE;
  }
  if (c->gbm.gbm_surf) {
    gbm_surface_destroy(c->gbm.gbm_surf);
    c->gbm.gbm_surf = NULL;
  }

  // Set the tty back to text mode
  ioctl(c->tty_fd, KDSETMODE, KD_TEXT);
  // Drop the DRM Master
  drmDropMaster(c->drm.drm_fd);
  ioctl(c->tty_fd, VT_RELDISP, 1);
}

bool 
vt_resume(vt_compositor_t *c) {
  if (!c->suspended) return true;

  // Try everything to become the master (deep)
  for (int i = 0; i < 50; i++) {
    if (drmSetMaster(c->drm.drm_fd) == 0)
      break;
    usleep(20000); 
  }
  if (drmSetMaster(c->drm.drm_fd) != 0) {
    log_error(c->log, "we cannot become the master, drmSetMaster still failed: %s", strerror(errno));
    ioctl(c->tty_fd, VT_RELDISP, VT_ACKACQ);
    return false;
  }

  // Recreate GBM surface
  c->gbm.gbm_surf = gbm_surface_create(
    c->gbm.gbm_dev,
    c->drm.mode.hdisplay, c->drm.mode.vdisplay,
    c->egl.egl_native_vis,
    GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
  );

  // Recreate EGLSurface on top of it
  c->egl.egl_surf = eglCreateWindowSurface(
    c->egl.egl_dsp, c->egl.egl_conf,
    (EGLNativeWindowType)c->gbm.gbm_surf, NULL);

  if (c->egl.egl_surf == EGL_NO_SURFACE) {
    log_error(c->log, "eglCreateWindowSurface failed: 0x%04x (%s)", eglGetError(), _egl_err_str(eglGetError()));
    ioctl(c->tty_fd, VT_RELDISP, VT_ACKACQ);
    return false;
  }

  if (!eglMakeCurrent(c->egl.egl_dsp, c->egl.egl_surf, c->egl.egl_surf, c->egl.egl_ctx)) {
    log_error(c->log, "eglMakeCurrent failed: 0x%04x (%s)", eglGetError(), _egl_err_str(eglGetError()));
    ioctl(c->tty_fd, VT_RELDISP, VT_ACKACQ);
    return false;
  }

  // Ack acquire
  ioctl(c->tty_fd, VT_RELDISP, VT_ACKACQ);

  c->needs_modeset = true;
  c->repaint_pending = true;
  c->bootstrapped = false;
  c->suspended = false;
  // Kick off the frame rendering loop
  wl_event_loop_add_idle(c->wl.evloop, frame_handler, c);

  return true;
}

void 
send_frame_callbacks(vt_compositor_t *c, uint32_t t) {
  // Basically iterate each surface and for each surface iterate 
  // each frame pending callback (since the last page flip) and 
  // let the client know we're done rendering their frames by calling 
  // wl_callback_send_done
  vt_surface_t *surf;
  wl_list_for_each(surf, &c->surfaces, link) {
    struct vt_frame_cb *cb, *tmp;
    wl_list_for_each_safe(cb, tmp, &surf->frame_cbs, link) {
      wl_callback_send_done(cb->res, t);
      wl_resource_destroy(cb->res);
      wl_list_remove(&cb->link);
      free(cb);
    }
  }
  wl_display_flush_clients(c->wl.dsp);
}

void 
page_flip_handler(int fd, unsigned int frame,
                              unsigned int sec, unsigned int usec, void *data) {
  vt_compositor_t *c = data;

  // Release the old, unused backbuffer
  if (c->prev_bo) {
    drmModeRmFB(fd, c->prev_fb);
    gbm_surface_release_buffer(c->gbm.gbm_surf, c->prev_bo);
  }

  // Swap buffers, the previous buffer becomes the currently displayed buffer 
  // and the currently displayed buffer becomes the new, pending frame buffer 
  // (that we got from eglSwapBuffers and is rendered to with OpenGL)
  c->prev_bo = c->current_bo;
  c->prev_fb = c->current_fb;
  c->current_bo = c->pending_bo;
  c->current_fb = c->pending_fb;

  // Reset pending buffer (so we can set it in the next frame)
  c->pending_bo = NULL;
  c->pending_fb = 0;

  c->flip_inflight = false;
  uint32_t t = sec * 1000u + usec / 1000u;

  // Send the frame callbacks to all clients, establishing correct frame pacing
  send_frame_callbacks(c, t);

  // Flush the clients (just in case ;))
  wl_display_flush_clients(c->wl.dsp);

  // If a client requested a repaint during the waiting perioid between 
  // drmModePageFlip() in render_frame and the execution of this handler, 
  // schedule a repaint 
  if(c->wants_repaint)
    schedule_repaint(c);
}


void 
schedule_repaint(vt_compositor_t *c) {
  if (c->suspended) return;
  if (c->repaint_pending) return;
  c->wants_repaint = true;
  c->repaint_pending = true;
  wl_event_loop_add_idle(c->wl.evloop, frame_handler, c);
}


void 
render_frame(vt_compositor_t *c) {
  int vpw = c->nested.nested ? (c->nested.conf_w > 0 ? c->nested.conf_w : 1920)
    : c->drm.mode.hdisplay;
  int vph = c->nested.nested ? (c->nested.conf_h > 0 ? c->nested.conf_h : 1080)
    : c->drm.mode.vdisplay;

  // Resize the runara display if the display size changed
  if(comp.render->render.render_w != vpw || comp.render->render.render_h != vph) {
    rn_resize_display(comp.render, vpw, vph);
  }

  // OpenGL basic frame setup 
  glViewport(0, 0, vpw, vph);
  glClearColor(1,1,1,1);
  glClear(GL_COLOR_BUFFER_BIT);

  // Render all the clients as images with runara 
  rn_begin(c->render);

  vt_surface_t *surf;
  wl_list_for_each_reverse(surf, &c->surfaces, link) {
    if (surf->tex.id)
      rn_image_render(c->render, (vec2s){0,0}, RN_WHITE, surf->tex);
  }
  rn_end(c->render);

  if (!eglSwapBuffers(c->egl.egl_dsp, c->egl.egl_surf)) {
    log_error(c->log, "eglSwapBuffers() failed: %s", _egl_err_str(eglGetError()));
    return;
  }

  // If running in nested mode, flush clients and call frame callbacks
  if (c->nested.nested) {
    wl_display_flush(c->nested.parent_display);
    uint32_t t = get_time_msec();
    send_frame_callbacks(c, t);
    wl_display_flush_clients(c->wl.dsp);
    c->wants_repaint = false;
    c->bootstrapped = true;
    return;
  } 
  // If running in "real KMS/TTY/DRM mode", handle the frame with DRM 
  else {
    drm_handle_frame(c);
  }
}

void
drm_handle_frame(vt_compositor_t* c) {
  // Retrieve the front buffer that we rendered to with EGL
  struct gbm_bo *bo = gbm_surface_lock_front_buffer(c->gbm.gbm_surf);
  if (!bo) {
    c->wants_repaint = true; 
    return;
  }

  uint32_t w = gbm_bo_get_width(bo);
  uint32_t h = gbm_bo_get_height(bo);
  uint32_t fmt = gbm_bo_get_format(bo);
  uint32_t handle = gbm_bo_get_handle(bo).u32;
  uint32_t stride = gbm_bo_get_stride(bo);
  uint32_t fb;
  uint32_t handles[4] = { gbm_bo_get_handle(bo).u32 };
  uint32_t strides[4] = { gbm_bo_get_stride(bo) };
  uint32_t offsets[4] = { 0 };

  uint64_t modifier = gbm_bo_get_modifier(bo);

  int ret;
  // If the BO wants modifiers, add the FB with modifier arguments
  if (modifier != DRM_FORMAT_MOD_INVALID) {
    uint64_t mods[4] = { modifier, 0, 0, 0 };
    ret = drmModeAddFB2WithModifiers(c->drm.drm_fd, w, h, fmt,
                                     handles, strides, offsets, mods, &fb,
                                     DRM_MODE_FB_MODIFIERS);
  } else {
    ret = drmModeAddFB2(c->drm.drm_fd, w, h, fmt,
                        handles, strides, offsets, &fb, 0);
  }

  if (ret != 0) {
    gbm_surface_release_buffer(c->gbm.gbm_surf, bo);
    c->wants_repaint = true;
    log_error(c->log, "canot create DRM frame buffer: drmModeAddFB2(%ux%u, fmt=0x%08x) failed: %s",
              w, h, fmt, strerror(errno));
    return;
  }

  if (c->needs_modeset) {
    if (drmModeSetCrtc(c->drm.drm_fd, c->drm.crtc_id, fb,
                       0, 0, &c->drm.conn_id, 1, &c->drm.mode) != 0) {
      drmModeRmFB(c->drm.drm_fd, fb);
      gbm_surface_release_buffer(c->gbm.gbm_surf, bo);
      c->wants_repaint = true;
      log_error(c->log, "cannot set CRTC mode: drmModeSetCrtc() failed: %s",
                strerror(errno));
      return;
    }

    // Release the old buffer 
    if (c->current_bo) {
      drmModeRmFB(c->drm.drm_fd, c->current_fb);
      gbm_surface_release_buffer(c->gbm.gbm_surf, c->current_bo);
    }

    // Assign the newly rendered buffer
    c->current_bo = bo;
    c->current_fb = fb;
    c->needs_modeset = false;

    // If we did not yet bootstrap (we just applied the CRTC), we need to 
    // manually send the frame callbacks to the clients to kick off
    // the client rendering loop, because this path does not invoke page_flip_handler(),
    // which would normally send the frame callbacks.
    if(!c->bootstrapped) {
      uint32_t t = get_time_msec();
      send_frame_callbacks(c, t);
      wl_display_flush_clients(c->wl.dsp);
      c->wants_repaint = false;
      c->bootstrapped = true;
    }
    return;
  }

  // Set pending buffer and eventually assign to 
  // current in page_flip_handler() (after flip completes)
  c->pending_bo = bo;
  c->pending_fb = fb;

  if( 
    drmModePageFlip(c->drm.drm_fd, c->drm.crtc_id, fb,
                    DRM_MODE_PAGE_FLIP_EVENT, c) != 0) {
    // Flip refused; free pending and try again later
    drmModeRmFB(c->drm.drm_fd, c->pending_fb);
    gbm_surface_release_buffer(c->gbm.gbm_surf, c->pending_bo);
    c->pending_bo = NULL; c->pending_fb = 0;
    c->wants_repaint = true;
      log_error(c->log, "cannot do a page flip: drmModePageFlip() failed: %s",
                strerror(errno));
    return;
  }

  c->flip_inflight = true;

  // we’ve submitted a frame so clear the desire until something else changes
  c->wants_repaint = false;
}

void 
frame_handler(void *data) {
  vt_compositor_t *c = data;
  c->repaint_pending = false;

  if (c->suspended) return;

  // If a flip is inflight, wait for the page-flip event to decide.
  if (c->flip_inflight)
    return;

  // If nothing changed and we already bootstrapped, don’t redraw.
  if (!c->wants_repaint && c->bootstrapped)
    return;

  render_frame(c);
}

void 
vt_xdg_positioner_destroy(struct wl_client *client,
                                      struct wl_resource *resource)
{
  wl_resource_destroy(resource);
}

void 
vt_xdg_positioner_set_size(struct wl_client *client,
                                       struct wl_resource *resource,
                                       int32_t width, int32_t height)
{
}

void 
vt_xdg_positioner_set_anchor_rect(struct wl_client *client,
                                              struct wl_resource *resource,
                                              int32_t x, int32_t y,
                                              int32_t width, int32_t height)
{
}

void 
vt_xdg_positioner_set_anchor(struct wl_client *client,
                                         struct wl_resource *resource,
                                         uint32_t anchor)
{
}

void 
vt_xdg_positioner_set_gravity(struct wl_client *client,
                                          struct wl_resource *resource,
                                          uint32_t gravity)
{
}

void 
vt_xdg_positioner_set_constraint_adjustment(struct wl_client *client,
                                                        struct wl_resource *resource,
                                                        uint32_t constraint_adjustment)
{
}

void 
vt_xdg_positioner_set_offset(struct wl_client *client,
                                         struct wl_resource *resource,
                                         int32_t x, int32_t y)
{
}

void 
vt_xdg_surface_destroy(struct wl_client *client,
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
                             wl_display_next_serial(comp.wl.dsp));

  wl_array_release(&states);
}

void 
vt_xdg_surface_get_toplevel(struct wl_client *client,
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
vt_xdg_surface_get_popup(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id,
                                     struct wl_resource *parent_surface,
                                     struct wl_resource *positioner)
{
}

void 
vt_xdg_surface_ack_configure(struct wl_client *client,
                                         struct wl_resource *resource,
                                         uint32_t serial)
{
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  surf->last_configure_serial = serial;
}

void 
vt_xdg_surface_set_window_geometry(struct wl_client *client,
                                               struct wl_resource *resource,
                                               int32_t x, int32_t y,
                                               int32_t width, int32_t height)
{
}

void 
vt_xdg_wm_base_bind(
  struct wl_client *client, void *data,
  uint32_t version, uint32_t id) {
  struct wl_resource* res = wl_resource_create(client, &xdg_wm_base_interface, version, id);
  wl_resource_set_implementation(res, &xdg_wm_base_impl, data, NULL);
}

void 
vt_xdg_wm_base_destroy(struct wl_client *client, struct wl_resource *resource)
{
  wl_resource_destroy(resource);
}

void 
vt_xdg_wm_base_create_positioner(struct wl_client *client,
                                             struct wl_resource *resource, uint32_t id)
{
  struct wl_resource *pos = wl_resource_create(client, &xdg_positioner_interface,
                                               wl_resource_get_version(resource), id);
  wl_resource_set_implementation(pos, &xdg_positioner_impl, NULL, NULL);
}

void 
vt_xdg_wm_base_get_xdg_surface(struct wl_client *client,
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
vt_xdg_wm_base_pong(struct wl_client *client,
                                struct wl_resource *resource, uint32_t serial)
{
}


void
vt_xdg_toplevel_destroy(struct wl_client *client, struct wl_resource *resource)
{
  wl_resource_destroy(resource);
}

void
vt_xdg_toplevel_set_parent(struct wl_client *client,
                           struct wl_resource *resource,
                           struct wl_resource *parent_resource)
{
}

void
vt_xdg_toplevel_set_title(struct wl_client *client,
                          struct wl_resource *resource,
                          const char *title)
{
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  if (surf->title)
    free(surf->title);
  surf->title = strdup(title ? title : "");
}

void
vt_xdg_toplevel_set_app_id(struct wl_client *client,
                           struct wl_resource *resource,
                           const char *app_id)
{
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  if (surf->app_id)
    free(surf->app_id);
  surf->app_id = strdup(app_id ? app_id : "");
}

void
vt_xdg_toplevel_show_window_menu(struct wl_client *client,
                                 struct wl_resource *resource,
                                 struct wl_resource *seat,
                                 uint32_t serial,
                                 int32_t x, int32_t y)
{
  // optional: ignore
}

void
vt_xdg_toplevel_move(struct wl_client *client,
                     struct wl_resource *resource,
                     struct wl_resource *seat,
                     uint32_t serial)
{
  // optional: ignore
}

void
vt_xdg_toplevel_resize(struct wl_client *client,
                       struct wl_resource *resource,
                       struct wl_resource *seat,
                       uint32_t serial,
                       uint32_t edges)
{
  // optional: ignore
}

void
vt_xdg_toplevel_set_max_size(struct wl_client *client,
                             struct wl_resource *resource,
                             int32_t width, int32_t height)
{
  // optional: ignore
}

void
vt_xdg_toplevel_set_min_size(struct wl_client *client,
                             struct wl_resource *resource,
                             int32_t width, int32_t height)
{
  // optional: ignore
}

void
vt_xdg_toplevel_set_maximized(struct wl_client *client,
                              struct wl_resource *resource)
{
  // optional: ignore
}

void
vt_xdg_toplevel_unset_maximized(struct wl_client *client,
                                struct wl_resource *resource)
{
}

void
vt_xdg_toplevel_set_fullscreen(struct wl_client *client,
                               struct wl_resource *resource,
                               struct wl_resource *output)
{
}

void
vt_xdg_toplevel_unset_fullscreen(struct wl_client *client,
                                 struct wl_resource *resource)
{
}

void
vt_xdg_toplevel_set_minimized(struct wl_client *client,
                              struct wl_resource *resource)
{
}

void 
parent_xdg_wm_base_ping(void *data, struct xdg_wm_base *wm, uint32_t serial) {
  (void)data;
  xdg_wm_base_pong(wm, serial);
}

void 
parent_registry_add(void *data, struct wl_registry *reg,
                                uint32_t id, const char *iface, uint32_t ver) {
  vt_compositor_t *c = data;
  if (strcmp(iface, "wl_compositor") == 0) {
    c->nested.parent_compositor = wl_registry_bind(reg, id, &wl_compositor_interface, 4);
  } else if (strcmp(iface, "xdg_wm_base") == 0) {
    c->nested.parent_xdg_wm_base = wl_registry_bind(reg, id, &xdg_wm_base_interface, 1);
  }
}

void 
parent_registry_remove(void *data, struct wl_registry *reg, uint32_t id) {
  (void)data; (void)reg; (void)id;
}


void 
parent_xdg_surface_configure(void *data,
                                         struct xdg_surface *surf, uint32_t serial) {
  vt_compositor_t *c = data;
  // We must acknowledge the size from the request 
  xdg_surface_ack_configure(surf, serial);

  // If we already got a toplevel size, resize wl_egl_window now.
  int w = c->nested.conf_w > 0 ? c->nested.conf_w : 1920;
  int h = c->nested.conf_h > 0 ? c->nested.conf_h : 1080;
  if (c->nested.egl_window) wl_egl_window_resize(c->nested.egl_window, w, h, 0, 0);

  c->nested.configured = true;
}


void 
parent_xdg_toplevel_configure(void *data,
  struct xdg_toplevel *toplevel, int32_t w, int32_t h, struct wl_array *states) {
  vt_compositor_t *c = data;
  // Set the size of our nested window to the size the parent comp wants
  if (w > 0 && h > 0) {
    c->nested.conf_w = w;
    c->nested.conf_h = h;
    if (c->nested.egl_window) wl_egl_window_resize(c->nested.egl_window, w, h, 0, 0);
  }
}
void 
parent_xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
  vt_compositor_t *c = data;
  c->running = false;
}

static int parent_display_ready(int fd, uint32_t mask, void *data) {
  vt_compositor_t *c = data;
  // process incoming events from parent 
  if (wl_display_dispatch(c->nested.parent_display) < 0) {
    c->running = false; // parent died
  }
  return 0;
}

int 
drm_fd_ready(int fd, uint32_t mask, void *data) {
  drmHandleEvent(fd, &comp.drm.evctx);
  return 0;
}

int 
libinput_fd_ready(int fd, uint32_t mask, void *data) {
  vt_compositor_t *c = data;
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
          comp.running = false;
        }

        bool mods = (alt_down && ctrl_down &&
          state == LIBINPUT_KEY_STATE_PRESSED);
        if (mods) {
          if (key == KEY_F1) ioctl(c->tty_fd, VT_ACTIVATE, 1);
          if (key == KEY_F2) ioctl(c->tty_fd, VT_ACTIVATE, 2);
          if (key == KEY_F3) ioctl(c->tty_fd, VT_ACTIVATE, 3);
          if (key == KEY_F4) ioctl(c->tty_fd, VT_ACTIVATE, 4);
          if (key == KEY_F5) ioctl(c->tty_fd, VT_ACTIVATE, 5);
          if (key == KEY_F6) ioctl(c->tty_fd, VT_ACTIVATE, 6);
          if (key == KEY_F7) ioctl(c->tty_fd, VT_ACTIVATE, 7);
          if (key == KEY_F8) ioctl(c->tty_fd, VT_ACTIVATE, 8);
          if (key == KEY_F9) ioctl(c->tty_fd, VT_ACTIVATE, 9);
        }
        break;
      }

      default:
        break;
    }

    libinput_event_destroy(event);
  }

  return 0;
}


void 
comp_bind(struct wl_client *client, void *data,
               uint32_t version, uint32_t id) {
  struct wl_resource *res = wl_resource_create(client,&wl_compositor_interface,version,id);
  wl_resource_set_implementation(res,&compositor_impl,data,NULL);
}

void 
handle_vt_release(int sig) {
  comp.vt_release_pending = true;
}
void 
handle_vt_acquire(int sig) {
  comp.vt_acquire_pending = true;
}


int main(int argc, char** argv) {
  if(!comp_init(argc, argv)) { return 1; }
  comp_run();
  comp_shutdown();

  return 0;
}
