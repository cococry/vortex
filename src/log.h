#pragma once

#include <stdio.h>

#define _BRAND_NAME "vortex"  
#define _VERSION "alpha 0.1"

#define log_trace(logstate, ...) if(!(logstate).quiet)                              \
{ if((logstate).verbose) { do {                                                     \
  log_header((logstate).stream, LL_TRACE);                                         \
  fprintf((logstate).stream, __VA_ARGS__);                                          \
  fprintf((logstate).stream, "\n");                                                 \
  if((logstate).stream != stdout &&                                                 \
    (logstate).stream != stderr && !(logstate).quiet) {                             \
    log_header(stdout, LL_TRACE);                                                  \
    fprintf(stdout, __VA_ARGS__);                                                   \
    fprintf(stdout, "\n");                                                          \
  }                                                                                 \
} while(0); }  }                                                                    \

#define log_warn(logstate, ...) if(!(logstate).quiet) {                             \
  do {                                                                              \
    log_header((logstate).stream, LL_WARN);                                        \
    fprintf((logstate).stream, __VA_ARGS__);                                        \
    fprintf((logstate).stream, "\n");                                               \
    if((logstate).stream != stdout &&                                               \
      (logstate).stream != stderr && !(logstate).quiet) {                           \
      log_header(stdout, LL_WARN);                                                 \
      fprintf(stdout, __VA_ARGS__);                                                 \
      fprintf(stdout, "\n");                                                        \
    }                                                                               \
  } while(0); }                                                                     \

#define log_error(logstate, ...) if(!(logstate).quiet) {                            \
  do {                                                                              \
    log_header((logstate).stream == stdout ? stderr : (logstate).stream, LL_ERR);  \
    fprintf((logstate).stream == stdout ? stderr : (logstate).stream, __VA_ARGS__); \
    fprintf((logstate).stream == stdout ? stderr : (logstate).stream, "\n");        \
    if((logstate).stream != stdout &&                                               \
      (logstate).stream != stderr && !(logstate).quiet) {                           \
      log_header(stderr, LL_ERR);                                                  \
      fprintf(stderr, __VA_ARGS__);                                                 \
      fprintf(stderr, "\n");                                                        \
    }                                                                               \
  } while(0); }                                                                     \

#define log_fatal(logstate, ...) if(!(logstate).quiet) {                            \
  do {                                                                              \
    log_header((logstate).stream == stdout ? stderr : (logstate).stream, LL_FATAL);\
    fprintf((logstate).stream == stdout ? stderr : (logstate).stream, __VA_ARGS__); \
    fprintf((logstate).stream == stdout ? stderr : (logstate).stream, "\n");        \
    if((logstate).stream != stdout &&                                               \
      (logstate).stream != stderr && !(logstate).quiet) {                           \
      log_header(stderr, LL_FATAL);                                                 \
      fprintf(stderr, __VA_ARGS__);                                                 \
      fprintf(stderr, "\n");                                                        \
    }                                                                               \
    exit(1);                                                                        \
  } while(0); }                                                                     \

typedef enum {
  LL_TRACE = 0,
  LL_WARN,
  LL_ERR,
  LL_FATAL,
  LL_COUNT
} log_level_t;

void log_header(FILE* stream, log_level_t lvl);

char* log_get_filepath();
