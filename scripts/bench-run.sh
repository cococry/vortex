#!/usr/bin/env bash
# run vortex from a build dir, attach glmark2-wayland, save score.
# usage: bench-run.sh <build-dir> <wl|drm> [glmark2 args...]

set -euo pipefail

BUILD="$1"
MODE="$2"
shift 2

# watchdog: a hung vortex on drm keeps the VT. SIGABRT leaves a core.
TIMEOUT="${TIMEOUT:-600}"
LOG="$BUILD/vortex-$MODE.log"
OUT="$BUILD/glmark2-$MODE.txt"
# a failed run must not leave the previous score looking current
rm -f "$OUT"

case "$MODE" in
wl)
	BACKEND="$BUILD/src/backends/wayland/libwl-backend.so"
	;;
drm)
	BACKEND="$BUILD/src/backends/drm/libdrm-backend.so"
	if [[ "$(tty)" != /dev/tty[0-9]* ]]; then
		echo "drm mode needs a VT, got $(tty). Use bench-drm.sh" >&2
		exit 1
	fi
	# vortex picks the wl backend whenever this is set
	unset WAYLAND_DISPLAY
	;;
*)
	echo "unknown mode '$MODE' (wl|drm)" >&2
	exit 1
	;;
esac

# trace logging costs frames, only turn it on when debugging
LOG_ARGS=()
if [[ "${VERBOSE:-0}" == 1 ]]; then
	LOG_ARGS=(--verbose)
fi
# extra vortex flags for A/B runs, e.g. "-expt linux-dmabuf-explicit-sync"
read -ra VT_ARGS <<<"${VORTEX_ARGS:-}"

# a killed vortex leaves its socket behind and the next one reuses the
# name, so match on creation after launch rather than on a new name
MARK="$(mktemp)"
new_socket() {
	find "$XDG_RUNTIME_DIR" -maxdepth 1 -type s -name 'wayland-*' \
		-newer "$MARK" -printf '%f\n'
}

# line-buffered so the last lines before a hang/kill reach the log
timeout -s ABRT -k 5 "$TIMEOUT" stdbuf -oL -eL \
	"$BUILD/vortex" "${LOG_ARGS[@]}" "${VT_ARGS[@]}" --backend-path "$BACKEND" \
	>"$LOG" 2>&1 &
VPID=$!
trap 'kill "$VPID" || true; wait "$VPID" || true; rm -f "$MARK"' EXIT

# the new socket is vortex's. connecting before init finishes is fine:
# the first roundtrip is only answered once the event loop runs.
SOCK=""
for _ in $(seq 100); do
	if ! kill -0 "$VPID"; then
		echo "vortex exited early, log: $LOG" >&2
		cat "$LOG"
		exit 1
	fi
	SOCK="$(new_socket)"
	if [[ -n "$SOCK" ]]; then
		break
	fi
	sleep 0.1
done

if [[ -z "$SOCK" ]]; then
	echo "vortex not ready after 10s, log: $LOG" >&2
	cat "$LOG"
	exit 1
fi

echo "vortex ready on $SOCK ($MODE), running glmark2"
# stderr too: a glmark2 init failure is otherwise invisible on drm
WAYLAND_DISPLAY="$SOCK" glmark2-wayland "$@" 2>&1 | tee "$OUT"
echo "score saved: $OUT, vortex log: $LOG"
