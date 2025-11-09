#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#define _BRAND_NAME "vortex"  
#define _VERSION "alpha 0.1"

#define VT_TRACE(logstate, ...)                                                     \
  if (!(logstate).quiet) {                                                          \
    if ((logstate).verbose) {                                                       \
      do {                                                                          \
        vt_util_log_header((logstate).stream, VT_LL_TRACE);                         \
        fprintf((logstate).stream, "%s: %s: ", _SUBSYS_NAME, __func__);             \
        fprintf((logstate).stream, __VA_ARGS__);                                    \
        fprintf((logstate).stream, "\n");                                           \
        if ((logstate).stream != stdout &&                                          \
            (logstate).stream != stderr && !(logstate).quiet) {                     \
          vt_util_log_header(stdout, VT_LL_TRACE);                                  \
          fprintf(stdout, "%s: %s: ", _SUBSYS_NAME, __func__);                      \
          fprintf(stdout, __VA_ARGS__);                                             \
          fprintf(stdout, "\n");                                                    \
        }                                                                           \
      } while (0);                                                                  \
    }                                                                               \
  }

#define VT_WARN(logstate, ...)                                                      \
  if (!(logstate).quiet) {                                                          \
    do {                                                                            \
      vt_util_log_header((logstate).stream, VT_LL_WARN);                            \
      fprintf((logstate).stream, "%s: %s (%s:%d): ",                                \
              _SUBSYS_NAME, __func__, __FILE__, __LINE__);                          \
      fprintf((logstate).stream, __VA_ARGS__);                                      \
      fprintf((logstate).stream, "\n");                                             \
      if ((logstate).stream != stdout &&                                            \
          (logstate).stream != stderr && !(logstate).quiet) {                       \
        vt_util_log_header(stdout, VT_LL_WARN);                                     \
        fprintf(stdout, "%s: %s (%s:%d): ",                                         \
                _SUBSYS_NAME, __func__, __FILE__, __LINE__);                        \
        fprintf(stdout, __VA_ARGS__);                                               \
        fprintf(stdout, "\n");                                                      \
      }                                                                             \
    } while (0);                                                                    \
  }

#define VT_ERROR(logstate, ...)                                                     \
  if (!(logstate).quiet) {                                                          \
    do {                                                                            \
      FILE* _stream = ((logstate).stream == stdout) ? stderr : (logstate).stream;   \
      vt_util_log_header(_stream, VT_LL_ERR);                                       \
      fprintf(_stream, "%s: %s (%s:%d): ",                                          \
              _SUBSYS_NAME, __func__, __FILE__, __LINE__);                          \
      fprintf(_stream, __VA_ARGS__);                                                \
      fprintf(_stream, "\n");                                                       \
      if ((logstate).stream != stdout &&                                            \
          (logstate).stream != stderr && !(logstate).quiet) {                       \
        vt_util_log_header(stderr, VT_LL_ERR);                                      \
        fprintf(stderr, "%s: %s (%s:%d): ",                                         \
                _SUBSYS_NAME, __func__, __FILE__, __LINE__);                        \
        fprintf(stderr, __VA_ARGS__);                                               \
        fprintf(stderr, "\n");                                                      \
      }                                                                             \
    } while (0);                                                                    \
  }

#define log_fatal(logstate, ...)                                                    \
  if (!(logstate).quiet) {                                                          \
    do {                                                                            \
      FILE* _stream = ((logstate).stream == stdout) ? stderr : (logstate).stream;   \
      vt_util_log_header(_stream, VT_LL_FATAL);                                     \
      fprintf(_stream, "%s: %s (%s:%d): ",                                          \
              _SUBSYS_NAME, __func__, __FILE__, __LINE__);                          \
      fprintf(_stream, __VA_ARGS__);                                                \
      fprintf(_stream, "\n");                                                       \
      if ((logstate).stream != stdout &&                                            \
          (logstate).stream != stderr && !(logstate).quiet) {                       \
        vt_util_log_header(stderr, VT_LL_FATAL);                                    \
        fprintf(stderr, "%s: %s (%s:%d): ",                                         \
                _SUBSYS_NAME, __func__, __FILE__, __LINE__);                        \
        fprintf(stderr, __VA_ARGS__);                                               \
        fprintf(stderr, "\n");                                                      \
      }                                                                             \
      exit(1);                                                                      \
    } while (0);                                                                    \
  }

#define VT_PARAM_CHECK_FAIL(comp)                                                   \
  do {                                                                              \
    VT_ERROR((comp)->log, "Did not pass parameter check.");                         \
  } while (0)

#define VT_WL_OUT_OF_MEMORY(comp, client)                                           \
  do {                                                                              \
    VT_ERROR((comp)->log, "Out of memory.");                                        \
    wl_client_post_no_memory((client));                                             \
  } while (0)

#define VT_MAX(a, b) a > b ? a : b
#define VT_MIN(a, b) a < b ? a : b

typedef enum {
  VT_LL_TRACE = 0,
  VT_LL_WARN,
  VT_LL_ERR,
  VT_LL_FATAL,
  VT_LL_COUNT
} log_level_t;

void vt_util_log_header(FILE* stream, log_level_t lvl);

char* vt_util_log_get_filepath();

struct vt_arena_t {
    uint8_t *base;
    size_t   offset;
    size_t   capacity;
};

uint64_t vt_util_get_time_msec(void);

void vt_util_arena_init(struct vt_arena_t *a, size_t capacity);

void* vt_util_alloc(struct vt_arena_t *a, size_t size);

void vt_util_arena_reset(struct vt_arena_t *a);

void vt_util_arena_destroy(struct vt_arena_t *a);

void vt_util_emit_signal(struct wl_signal *signal, void *data);

int vt_util_allocate_shm_file(struct vt_compositor_t* comp, size_t size);

struct vt_compositor_t;
bool vt_util_allocate_shm_rwro_pair(struct vt_compositor_t* comp, size_t size, int* rw_fd, int* ro_fd);

uint32_t vt_util_convert_wl_shm_format_to_drm(enum wl_shm_format fmt);

enum wl_shm_format vt_util_convert_drm_format_to_wl_shm(uint32_t fmt);
