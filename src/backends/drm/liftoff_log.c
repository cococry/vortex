#include "src/core/profiling.h"
#include "liftoff_log.h"
#include "core/core_types.h"

static struct log_state_t *_liftoff_log_state = NULL;

void drm_liftoff_log_init(struct vt_backend_t *backend) {
  VT_PROFILE_SCOPE();
  _liftoff_log_state = &backend->comp->log;

  liftoff_log_set_handler(drm_liftoff_log_handler);

  if (backend->comp->log.quiet)
    liftoff_log_set_priority(LIFTOFF_SILENT);
  else if (backend->comp->log.verbose)
    liftoff_log_set_priority(LIFTOFF_DEBUG);
  else
    liftoff_log_set_priority(LIFTOFF_ERROR);
}

void drm_liftoff_log_write(FILE *stream, log_level_t level, const char *fmt,
                           va_list args) {
  VT_PROFILE_SCOPE();
  va_list copy;
  va_copy(copy, args);

  vt_util_log_header(stream, level);
  fprintf(stream, "LIFTOFF: ");
  vfprintf(stream, fmt, copy);
  fprintf(stream, "\n");

  va_end(copy);
}
void drm_liftoff_log_handler(enum liftoff_log_priority priority,
                             const char *fmt, va_list args) {
  VT_PROFILE_SCOPE();
  struct log_state_t *log = _liftoff_log_state;

  if (!log || log->quiet)
    return;

  log_level_t level;
  FILE       *stream;
  FILE       *mirror;

  switch (priority) {
  case LIFTOFF_DEBUG:
    if (!log->verbose)
      return;

    level = VT_LL_TRACE;
    stream = log->stream;
    mirror = stdout;
    break;

  case LIFTOFF_ERROR:
    level = VT_LL_ERR;
    stream = log->stream == stdout ? stderr : log->stream;
    mirror = stderr;
    break;

  case LIFTOFF_SILENT:
  default:
    return;
  }

  drm_liftoff_log_write(stream, level, fmt, args);

  if (log->stream != stdout && log->stream != stderr)
    drm_liftoff_log_write(mirror, level, fmt, args);
}
