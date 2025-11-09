#define _GNU_SOURCE

#include <time.h>
#include <stdio.h>
#include <string.h>

#include "../core/core_types.h"
#include "util.h"
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/mman.h>

#define _SUBSYS_NAME "UTIL"


// ===================================================
// =================== PUBLIC API ====================
// ===================================================
uint64_t
vt_util_get_time_msec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void 
vt_util_arena_init(struct vt_arena_t* a, size_t capacity) {
  a->base = (uint8_t*)malloc(capacity);
  a->offset = 0;
  a->capacity = capacity;
}

void*
vt_util_alloc(struct vt_arena_t* a, size_t size)  {
  size = (size + 7u) & ~7u;
  if (a->offset + size > a->capacity) {
    fprintf(stderr, "[vortex]: warning: arena violated.\n");
    a->capacity += size;
    a->base = realloc(a->base, a->capacity);
  }
  void* ptr = a->base + a->offset;
  a->offset += size;
  memset(ptr, 0, size);
  return ptr;
}

void
vt_util_arena_reset(struct vt_arena_t* a) {
  a->offset = 0;
}

void 
vt_util_arena_destroy(struct vt_arena_t* a) {
  free(a->base);
  a->base = NULL;
  a->capacity = a->offset = 0;
}

char* 
vt_util_log_get_filepath() {
  static char path[PATH_MAX];
  char* state_home = getenv("XDG_STATE_HOME");
  const char* home = getenv("HOME");
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
vt_util_log_header(FILE* stream, log_level_t lvl) {
  static const char* lvl_str[VT_LL_COUNT] = { "TRACE", "WARNING", "ERROR", "FATAL" };
  static const char* lvl_clr[VT_LL_COUNT] = {
    "\033[1;32m",   // TRACE - bright green
    "\033[1;33m",   // WARNING - yellow
    "\033[1;31m",   // ERROR - bright red
    "\033[38;5;88m" // FATAL - dark red
  };
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


// handle_noop + vt_util_emit_signal => https://github.com/swaywm/wlroots/blob/master/util/signal.c#L7
static void handle_noop(struct wl_listener* listener, void* data) {
  (void)listener;
  (void)data;
}
void 
vt_util_emit_signal(struct wl_signal* signal, void* data) {

	struct wl_listener cursor;
	struct wl_listener end;

	/* Add two special markers: one cursor and one end marker. This way, we know
	 * that we've already called listeners on the left of the cursor and that we
	 * don't want to call listeners on the right of the end marker. The 'it'
	 * function can remove any element it wants from the list without troubles.
	 * wl_list_for_each_safe tries to be safe but it fails: it works fine
	 * if the current item is removed, but not if the next one is. */
	wl_list_insert(&signal->listener_list, &cursor.link);
	cursor.notify = handle_noop;
	wl_list_insert(signal->listener_list.prev, &end.link);
	end.notify = handle_noop;

	while (cursor.link.next != &end.link) {
		struct wl_list* pos = cursor.link.next;
		struct wl_listener* l = wl_container_of(pos, l, link);

		wl_list_remove(&cursor.link);
		wl_list_insert(pos, &cursor.link);

		l->notify(l, data);
	}

	wl_list_remove(&cursor.link);
	wl_list_remove(&end.link);
}

int 
vt_util_allocate_shm_file(struct vt_compositor_t* comp, size_t size) {
  int fd;
  if ((fd = memfd_create("vortex-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING)) < 0) {
    VT_ERROR(comp->log, "memfd_create() failed: %s", strerror(errno));
    return -1;
  }
  if (ftruncate(fd, size) < 0) {
    VT_ERROR(comp->log, "ftruncate() failed: %s", strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

bool 
vt_util_allocate_shm_rwro_pair(struct vt_compositor_t* comp, size_t size, int* rw_fd, int* ro_fd) {
  *rw_fd = -1;
  *ro_fd = -1;

  *rw_fd = vt_util_allocate_shm_file(comp, size);
  if(*rw_fd < 0) {
    VT_ERROR(comp->log, "failed to allocate read-write shm file."); 
    return false;
  }

  *ro_fd = dup(*rw_fd);
  if (*ro_fd < 0) {
    perror("dup failed");
    close(*rw_fd);
    return false;
  }

  return true;
}

#define fourcc_code(a, b, c, d) ((__u32)(a) | ((__u32)(b) << 8) | \
  ((__u32)(c) << 16) | ((__u32)(d) << 24))

#define DRM_FORMAT_XRGB8888	fourcc_code('X', 'R', '2', '4') /* [31:0] x:R:G:B 8:8:8:8 little endian */
#define DRM_FORMAT_ARGB8888	fourcc_code('A', 'R', '2', '4') /* [31:0] A:R:G:B 8:8:8:8 little endian */

uint32_t 
vt_util_convert_wl_shm_format_to_drm(enum wl_shm_format fmt) {
  switch (fmt) {
    case WL_SHM_FORMAT_XRGB8888:
      return DRM_FORMAT_XRGB8888;
    case WL_SHM_FORMAT_ARGB8888:
      return DRM_FORMAT_ARGB8888;
    default:
      return (uint32_t)fmt;
  }
}

enum wl_shm_format 
vt_util_convert_drm_format_to_wl_shm(uint32_t fmt) {
  switch (fmt) {
    case DRM_FORMAT_XRGB8888:
      return WL_SHM_FORMAT_XRGB8888;
    case DRM_FORMAT_ARGB8888:
      return WL_SHM_FORMAT_ARGB8888;
    default:
      return (enum wl_shm_format)fmt;
  }
}
