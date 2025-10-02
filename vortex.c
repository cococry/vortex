#include <errno.h>
#include <linux/vt.h>
#define _GNU_SOURCE
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

#include <linux/input-event-codes.h>

#include <runara/runara.h>
#include "glad.h" 


#define _BRAND_NAME "vortex"
#define _VERSION "alpha 0.1"

typedef struct {
  FILE* stream;
  bool verbose, quiet;
} log_state_t;

typedef struct {
  int drm_fd;
  drmModeModeInfo mode;
  uint32_t conn_id;
  uint32_t crtc_id;
} drm_state_t;

typedef struct {
  struct gbm_device* gbm_dev;
  struct gbm_surface* gbm_surf;
} gbm_state_t;

typedef struct {
  EGLDisplay egl_dsp;
  EGLContext egl_ctx;
  EGLSurface egl_surf;
} egl_state_t;

typedef struct {
  struct wl_display* dsp;
  struct wl_event_loop* evloop;
  struct wl_compositor* compositor;
} wl_state_t;

typedef struct {
  struct wl_resource* surf_res, *buf_res, *frame_cb; 
  RnTexture tex; 
  struct wl_list link;
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
  bool needs_modeset;

  bool running;

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

static bool gbm_egl_init(int32_t device_fd, uint32_t dsp_w, uint32_t dsp_h, gbm_state_t* drm, egl_state_t* egl); 

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

static void comp_surface_create(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id);

static void surface_set_input_region(struct wl_client *client,
				 struct wl_resource *resource,
				 struct wl_resource *region);
	
static void surface_set_buffer_transform(struct wl_client *client,
				     struct wl_resource *resource,
				     int32_t transform);
	
static void surface_set_buffer_scale(struct wl_client *client,
				 struct wl_resource *resource,
				 int32_t scale);
	
static void surface_damage_buffer(struct wl_client *client,
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

static const struct wl_compositor_interface compositor_impl = {
    .create_surface = comp_surface_create,
    .create_region = NULL
};

int input_open_restricted(const char* path, int32_t flags, void* user_data) {
  int fd = open(path, flags);
  return fd < 0 ? -errno : fd;
}

void input_close_restricted(int32_t fd, void* user_data) {
  close(fd);
}

static const struct libinput_interface input_interface = {
  .close_restricted = input_close_restricted,
  .open_restricted = input_open_restricted
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

#define log_trace(logstate, ...) if(!(logstate).quiet)                      \
{ if((logstate).verbose) { do {                                             \
  _log_header((logstate).stream, LL_TRACE);                                 \
  fprintf((logstate).stream, __VA_ARGS__);                                  \
  fprintf((logstate).stream, "\n");                                         \
if((logstate).stream != stdout &&                                           \
  (logstate).stream != stderr && !(logstate).quiet) {                       \
  _log_header(stdout, LL_WARN);                                             \
  fprintf(stdout, __VA_ARGS__);                                             \
  fprintf(stdout, "\n");                                                    \
}                                                                           \
} while(0); }  }                                                            \

#define log_warn(logstate, ...) if(!(logstate).quiet) {                     \
do {                                                                        \
_log_header((logstate).stream, LL_WARN);                                    \
fprintf((logstate).stream, __VA_ARGS__);                                    \
fprintf((logstate).stream, "\n");                                           \
if((logstate).stream != stdout &&                                           \
  (logstate).stream != stderr && !(logstate).quiet) {                       \
  _log_header(stdout, LL_WARN);                                             \
  fprintf(stdout, __VA_ARGS__);                                             \
  fprintf(stdout, "\n");                                                    \
}                                                                           \
} while(0); }                                                               \

#define log_error(logstate, ...) if(!(logstate).quiet) {                          \
do {                                                                              \
  _log_header((logstate).stream == stdout ? stderr : (logstate).stream, LL_ERR);  \
  fprintf((logstate).stream == stdout ? stderr : (logstate).stream, __VA_ARGS__); \
  fprintf((logstate).stream == stdout ? stderr : (logstate).stream, "\n");        \
  if((logstate).stream != stdout &&                                               \
    (logstate).stream != stderr && !(logstate).quiet) {                           \
  _log_header(stderr, LL_WARN);                                                   \
  fprintf(stderr, __VA_ARGS__);                                                   \
  fprintf(stderr, "\n");                                                          \
}                                                                                 \
} while(0); }                                                                     \

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
  if(drm->drm_fd < 0) {
    log_error(comp.log, "cannot open DRM: %s", strerror(errno));
    return false;
  }

  log_trace(comp.log, "connected to DRM (fd: %i)", drm->drm_fd);

  drmModeRes* res = drmModeGetResources(drm->drm_fd);
  if (!res) {
    log_error(comp.log, "drmModeGetResources() failed: %s\n", strerror(errno));
    return false;
  }

  drmModeConnector* conn = NULL;
  // iterate all avaiable monitors
  for (int i = 0; i < res->count_connectors; i++) {
    conn = drmModeGetConnector(drm->drm_fd, res->connectors[i]);
    // check if the monitor is connected and has atleast one valid mode 
    if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
      drm->conn_id = conn->connector_id;
      drm->mode = conn->modes[0];
      log_trace(comp.log, "found valid DRM monitor: %i (%ix%i)", 
                drm->conn_id, drm->mode.hdisplay, drm->mode.vdisplay);
      break;
    }
    drmModeFreeConnector(conn);
    conn = NULL;
  }

  if (!conn) {
    log_error(comp.log, "no connected DRM monitor, cannot procede.\n");
    return false;
  }

  drm->crtc_id = res->crtcs[0];

  drmModeFreeConnector(conn);
  drmModeFreeResources(res);

  if (drmSetMaster(drm->drm_fd) != 0)
    log_error(comp.log, "drmSetMaster failed: %s", strerror(errno));

  return true;
}

static inline const char* _egl_err_str(EGLint error) {
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
gbm_egl_init(int32_t device_fd, uint32_t dsp_w, uint32_t dsp_h, gbm_state_t* gbm, egl_state_t* egl) {
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


void surface_attach(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *buffer,
  int32_t x,
  int32_t y) {
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  surf->buf_res = buffer;
}

void 
surface_commit(
  struct wl_client *client,
  struct wl_resource *resource) {
  vt_surface_t* surf = wl_resource_get_user_data(resource);
  if (!surf) { log_error(comp.log, "surface_commit: NULL user_data"); return; }

  if (!surf->buf_res) {
    log_error(comp.log, "surface_commit: no buffer attached");
    return;
  }

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

  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / 4);

  // Desktop GL upload (ARGB/XRGB little-endian → GL_BGRA + REV type)
  GLenum ext_format = GL_BGRA;
  GLenum ext_type   = GL_UNSIGNED_INT_8_8_8_8_REV;

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, ext_format, ext_type, data);

  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  wl_shm_buffer_end_access(shm_buf);

  if (fmt == WL_SHM_FORMAT_XRGB8888) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
  }

  wl_buffer_send_release(surf->buf_res);
  surf->buf_res = NULL;

  surf->tex.width = width;
  surf->tex.height = height;
}

void surface_frame(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t callback) {
  struct wl_resource* cb =
    wl_resource_create(client, &wl_callback_interface, 1, callback);
  vt_surface_t *surf = wl_resource_get_user_data(resource);
  surf->frame_cb = cb;
}

void surface_damage(
  struct wl_client *client,
  struct wl_resource *resource,
  int32_t x,
  int32_t y,
  int32_t width,
  int32_t height) {

}

void surface_set_opaque_region(
  struct wl_client *client,
  struct wl_resource *resource,
  struct wl_resource *region) {

}

void surface_set_input_region(struct wl_client *client,
				 struct wl_resource *resource,
				 struct wl_resource *region) {

}
	
void surface_set_buffer_transform(struct wl_client *client,
				     struct wl_resource *resource,
				     int32_t transform) {

}
	
void surface_set_buffer_scale(struct wl_client *client,
				 struct wl_resource *resource,
				 int32_t scale) {

}
	
void surface_damage_buffer(struct wl_client *client,
			      struct wl_resource *resource,
			      int32_t x,
			      int32_t y,
			      int32_t width,
			      int32_t height) {

}
	
void surface_offset(struct wl_client *client,
		       struct wl_resource *resource,
		       int32_t x,
		       int32_t y) {

}

void surface_destroy(struct wl_client *client,
			struct wl_resource *resource) {
}

void comp_surface_create(
  struct wl_client *client,
  struct wl_resource *resource,
  uint32_t id) {
  vt_surface_t* surf = calloc(1, sizeof(*surf));
  wl_list_insert(&comp.surfaces, &surf->link);
  struct wl_resource* res = wl_resource_create(client, &wl_surface_interface, 4, id);
  wl_resource_set_implementation(res, &surface_impl, surf, NULL);
  surf->surf_res = res;
}


static void render_frame(vt_compositor_t *c) {
    glViewport(0, 0, c->drm.mode.hdisplay, c->drm.mode.vdisplay);
    glClearColor(0.3, 0.2, 0.5, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    rn_begin(c->render);
    vt_surface_t *surf;
    wl_list_for_each(surf, &c->surfaces, link) {
        if (surf->tex.id)
            rn_image_render(c->render, (vec2s){0, 0}, RN_WHITE, surf->tex);
    }
    rn_end(c->render);

    // Swap GL -> GBM
    if (!eglSwapBuffers(c->egl.egl_dsp, c->egl.egl_surf)) {
        log_error(c->log, "eglSwapBuffers failed: 0x%x", eglGetError());
        return;
    }

    struct gbm_bo *bo = gbm_surface_lock_front_buffer(c->gbm.gbm_surf);
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t stride = gbm_bo_get_stride(bo);

    uint32_t fb;
    if (drmModeAddFB(c->drm.drm_fd,
                     c->drm.mode.hdisplay, c->drm.mode.vdisplay,
                     24, 32, stride, handle, &fb)) {
        log_error(c->log, "drmModeAddFB failed: %s", strerror(errno));
        return;
    }

    if (c->needs_modeset) {
        drmModeSetCrtc(c->drm.drm_fd, c->drm.crtc_id, fb,
                       0, 0, &c->drm.conn_id, 1, &c->drm.mode);
        c->current_bo = bo;
        c->current_fb = fb;
        c->needs_modeset = false;
    } else {
        c->pending_bo = bo;
        c->pending_fb = fb;
        drmModePageFlip(c->drm.drm_fd, c->drm.crtc_id, fb,
                        DRM_MODE_PAGE_FLIP_EVENT, c);
    }
}

static int frame_handler(void *data) {
  vt_compositor_t* c = data;
  render_frame(c);
  // keep running
  return 0; 
}

static void
send_frame_callbacks(vt_compositor_t *c, uint32_t time_msec)
{
    vt_surface_t *surf;
    wl_list_for_each(surf, &c->surfaces, link) {
        if (surf->frame_cb) {
            wl_callback_send_done(surf->frame_cb, time_msec);
            wl_resource_destroy(surf->frame_cb);
            surf->frame_cb = NULL;
        }
    }
}

static void page_flip_handler(int fd, unsigned int frame,
                              unsigned int sec, unsigned int usec, void *data) {
    vt_compositor_t *c = data;

    if (c->prev_bo) {
        drmModeRmFB(fd, c->prev_fb);
        gbm_surface_release_buffer(c->gbm.gbm_surf, c->prev_bo);
    }

    c->prev_bo = c->current_bo;
    c->prev_fb = c->current_fb;
    c->current_bo = c->pending_bo;
    c->current_fb = c->pending_fb;
    c->pending_bo = NULL;
    c->pending_fb = 0;

    send_frame_callbacks(c, sec * 1000 + usec / 1000);

  render_frame(c);
}


static drmEventContext evctx = {
  .version = DRM_EVENT_CONTEXT_VERSION,
  .page_flip_handler = page_flip_handler,
};

static int drm_fd_ready(int fd, uint32_t mask, void *data) {
  drmHandleEvent(fd, &evctx);
  return 0;
}

static int libinput_fd_ready(int fd, uint32_t mask, void *data) {
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

        if (state == LIBINPUT_KEY_STATE_PRESSED && key == 41) // ESC
          comp.running = false;

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


void comp_bind(struct wl_client *client, void *data,
				      uint32_t version, uint32_t id) {
  struct wl_resource *res = wl_resource_create(client,&wl_compositor_interface,version,id);
  wl_resource_set_implementation(res,&compositor_impl,data,NULL);
}

void handle_vt_release(int sig) {
  // set to text mode 
  ioctl(comp.tty_fd, KDSETMODE, KD_TEXT);
  // drop DRM master 
  drmDropMaster(comp.drm.drm_fd);
  // acknowledge release 
  ioctl(comp.tty_fd, VT_RELDISP, 1);
}
void handle_vt_acquire(int sig) {
  // set back to graphics mode 
  ioctl(comp.tty_fd, KDSETMODE, KD_GRAPHICS);
  // reaquire DRM master 
  drmSetMaster(comp.drm.drm_fd);
  // acknowledge acquire
  ioctl(comp.tty_fd, VT_RELDISP, VT_ACKACQ);

  render_frame(&comp);
}


struct vt_mode tty_mode = {
  .mode = VT_PROCESS,
  .waitv = 1,
  .relsig = SIGUSR1, 
  .acqsig = SIGUSR2 
};

int main(int argc, char** argv) {
  comp.log.stream = stdout;
  comp.log.verbose = false;
  comp.log.quiet = false;
  if(argc > 1) {
    char* flag = argv[1];
    if(flag_cmp(flag, "--logfile", "-lf")) {
      comp.log.stream = fopen(_log_get_filepath(), "w"); 
    } else if(flag_cmp(flag, "--verbose", "-vb")) {
      comp.log.verbose = true;
    } else if(flag_cmp(flag, "--quiet", "-q")) {
      comp.log.quiet = true;
    } else if(flag_cmp(flag, "-h", "--help")) {
      _log_help();
      return 0;
    } else if(flag_cmp(flag, "-v", "--version")) {
      printf(_VERSION"\n");
      return 0;
    } else {
      log_error(comp.log, "invalid option -- '%s'", flag);
      return 1;
    }
  }
  
  signal(SIGUSR1, handle_vt_release);
  signal(SIGUSR2, handle_vt_acquire);

  comp.tty_fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
  if(comp.tty_fd < 0) {
    log_error(comp.log, "failed to open /dev/tty");
    return 1;
  }

  if(ioctl(comp.tty_fd, KDSETMODE, KD_GRAPHICS) < 0) {
    log_warn(comp.log, "failed to enter TTY graphics mode.");
  }
  if(ioctl(comp.tty_fd, VT_SETMODE, &tty_mode) < 0) {
    log_warn(comp.log, "failed to set TTY switching mode.");
  }
  if(!(comp.wl.dsp = wl_display_create())) {
    log_error(comp.log, "cannot create wayland display.");
    return 1;
  }

  log_trace(comp.log, "sucessfully created wayland display.");

  wl_list_init(&comp.surfaces);

  comp.wl.evloop = wl_display_get_event_loop(comp.wl.dsp);

  if(!drm_init(&comp.drm) || 
    !gbm_egl_init(
      comp.drm.drm_fd, comp.drm.mode.hdisplay, comp.drm.mode.vdisplay,
      &comp.gbm, &comp.egl)) {
    log_error(comp.log, "failed to initialize DRM/GBM/EGL backend.");
  }

  wl_display_init_shm(comp.wl.dsp);

  comp.render = rn_init(comp.drm.mode.hdisplay, comp.drm.mode.vdisplay, (RnGLLoader)eglGetProcAddress); 
                                        
  wl_global_create(comp.wl.dsp, &wl_compositor_interface, 4, &comp, comp_bind); 
  
  log_trace(comp.log, "entering event loop!");

  const char *socket_name = wl_display_add_socket_auto(comp.wl.dsp);
  if (!socket_name) {
    log_error(comp.log, "failed to create Wayland socket: no clients will be able to connect.");
    return 1; 
  } else {
    log_trace(comp.log, "wayland display ready on socket '%s'.", socket_name);
  }

  struct udev* udev = udev_new();
  if(!udev) {
    log_error(comp.log, "failed to create udev context.");
    return 1;
  } else {
    log_trace(comp.log, "successfully created udev context.");
  }
  comp.input = libinput_udev_create_context(&input_interface, NULL, udev);
  if(!comp.input) {
    log_error(comp.log, "failed to create libinput context.");
    return 1;
  } else {
    log_trace(comp.log, "successfully created libinput context.");
  }
  libinput_udev_assign_seat(comp.input, "seat0");
  libinput_dispatch(comp.input);

  comp.running = true;

  comp.needs_modeset = true;
  comp.current_bo = NULL;
  comp.pending_bo = NULL;
  comp.prev_bo = NULL;
  comp.current_fb = 0;
  comp.pending_fb = 0;
  comp.prev_fb = 0;

  wl_event_loop_add_fd(comp.wl.evloop, comp.drm.drm_fd,
                       WL_EVENT_READABLE, drm_fd_ready, &comp);

  int li_fd = libinput_get_fd(comp.input);
  wl_event_loop_add_fd(comp.wl.evloop, li_fd,
                       WL_EVENT_READABLE, libinput_fd_ready, &comp);

  comp.needs_modeset = true;
  render_frame(&comp);

  while (comp.running) {
    wl_event_loop_dispatch(comp.wl.evloop, -1); 
    wl_display_flush_clients(comp.wl.dsp);
  }


  
  log_trace(comp.log, "shutting down...");

  eglMakeCurrent(comp.egl.egl_dsp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(comp.egl.egl_dsp, comp.egl.egl_surf);
  eglDestroyContext(comp.egl.egl_dsp, comp.egl.egl_ctx);
  eglTerminate(comp.egl.egl_dsp);

  gbm_surface_destroy(comp.gbm.gbm_surf);
  gbm_device_destroy(comp.gbm.gbm_dev);

  drmDropMaster(comp.drm.drm_fd);
  close(comp.drm.drm_fd);

  ioctl(comp.tty_fd, KDSETMODE, KD_TEXT);
  close(comp.tty_fd);

  libinput_unref(comp.input);
  wl_display_destroy(comp.wl.dsp);

  if(comp.log.stream != stdout && comp.log.stream != stderr)
    fclose(comp.log.stream);
  return 0;
}
