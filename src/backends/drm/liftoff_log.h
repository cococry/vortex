#pragma once

#include "core/core_types.h"
#include <libliftoff.h>
#include <stdarg.h>
#include <stdio.h>

void drm_liftoff_log_init(struct vt_backend_t *backend);

void drm_liftoff_log_write(FILE *stream, log_level_t level, const char *fmt,
                           va_list args);

void drm_liftoff_log_handler(enum liftoff_log_priority priority,
                             const char *fmt, va_list args);
