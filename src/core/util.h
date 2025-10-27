#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <wayland-server-core.h>

#define _BRAND_NAME "vortex"  
#define _VERSION "alpha 0.1"

#define VT_TRACE(logstate, ...) if(!(logstate).quiet)                               \
{ if((logstate).verbose) { do {                                                     \
  vt_util_log_header((logstate).stream, VT_LL_TRACE);                               \
  fprintf((logstate).stream, __VA_ARGS__);                                          \
  fprintf((logstate).stream, "\n");                                                 \
  if((logstate).stream != stdout &&                                                 \
    (logstate).stream != stderr && !(logstate).quiet) {                             \
    vt_util_log_header(stdout, VT_LL_TRACE);                                        \
    fprintf(stdout, __VA_ARGS__);                                                   \
    fprintf(stdout, "\n");                                                          \
  }                                                                                 \
} while(0); }  }                                                                    \

#define VT_WARN(logstate, ...) if(!(logstate).quiet) {                              \
  do {                                                                              \
    vt_util_log_header((logstate).stream, VT_LL_WARN);                              \
    fprintf((logstate).stream, __VA_ARGS__);                                        \
    fprintf((logstate).stream, "\n");                                               \
    if((logstate).stream != stdout &&                                               \
      (logstate).stream != stderr && !(logstate).quiet) {                           \
      vt_util_log_header(stdout, VT_LL_WARN);                                       \
      fprintf(stdout, __VA_ARGS__);                                                 \
      fprintf(stdout, "\n");                                                        \
    }                                                                               \
  } while(0); }                                                                     \

#define VT_ERROR(logstate, ...) if(!(logstate).quiet) {                             \
  do {                                                                              \
    vt_util_log_header((logstate).stream == stdout                                  \
        ? stderr : (logstate).stream, VT_LL_ERR);                                   \
    fprintf((logstate).stream == stdout ? stderr : (logstate).stream, __VA_ARGS__); \
    fprintf((logstate).stream == stdout ? stderr : (logstate).stream, "\n");        \
    if((logstate).stream != stdout &&                                               \
      (logstate).stream != stderr && !(logstate).quiet) {                           \
      vt_util_log_header(stderr, VT_LL_ERR);                                        \
      fprintf(stderr, __VA_ARGS__);                                                 \
      fprintf(stderr, "\n");                                                        \
    }                                                                               \
  } while(0); }                                                                     \

#define log_fatal(logstate, ...) if(!(logstate).quiet) {                            \
  do {                                                                              \
    vt_util_log_header((logstate).stream == stdout                                  \
        ? stderr : (logstate).stream, VT_LL_FATAL);                                 \
    fprintf((logstate).stream == stdout ? stderr : (logstate).stream, __VA_ARGS__); \
    fprintf((logstate).stream == stdout ? stderr : (logstate).stream, "\n");        \
    if((logstate).stream != stdout &&                                               \
      (logstate).stream != stderr && !(logstate).quiet) {                           \
      vt_util_log_header(stderr, VT_LL_FATAL);                                      \
      fprintf(stderr, __VA_ARGS__);                                                 \
      fprintf(stderr, "\n");                                                        \
    }                                                                               \
    exit(1);                                                                        \
  } while(0); }                                                                     \

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
