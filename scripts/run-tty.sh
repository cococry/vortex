#!/usr/bin/env bash
# tty testing helper.

set -euo pipefail

# Alt+E spawns $TERMINAL through vortex's child shell, so it must be exported
export TERMINAL="${TERMINAL:-foot}"
# watchdog: a hung vortex keeps DRM + keyboard, killing it hands the VT back
# to logind. SIGABRT leaves a core for `coredumpctl gdb vortex`. 0 disables.
TIMEOUT="${TIMEOUT:-20}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BACKEND="$ROOT/build/src/backends/drm/libdrm-backend.so"
LOG="$ROOT/build/vortex-tty.log"

if [[ "$(tty)" != /dev/tty[0-9]* ]]; then
  echo "Not on a VT ($(tty)). Switch with Ctrl+Alt+F3, log in, rerun." >&2
  exit 1
fi

if [[ ! -x "$ROOT/build/vortex" || ! -f "$BACKEND" ]]; then
  echo "No build found. Run: meson compile -C build" >&2
  exit 1
fi

# vortex picks the wl backend whenever this is set
unset WAYLAND_DISPLAY

# DRM owns the screen while running, so the log is the only readable output
echo "Logging to $LOG (killed after ${TIMEOUT}s)"
status=0
# line-buffered so the last lines before a hang/kill reach the log
timeout -s ABRT -k 5 "$TIMEOUT" stdbuf -oL -eL \
  "$ROOT/build/vortex" --verbose --backend-path "$BACKEND" "$@" \
  >"$LOG" 2>&1 || status=$?

echo "vortex exited with status $status. Log: $LOG"
exit "$status"
