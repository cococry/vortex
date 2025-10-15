#define _GNU_SOURCE
#include "log.h"

#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>

char* 
log_get_filepath() {
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
log_header(FILE* stream, log_level_t lvl) {
  static const char* lvl_str[LL_COUNT] = { "TRACE", "WARNING", "ERROR", "FATAL" };
  static const char* lvl_clr[LL_COUNT] = {
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
