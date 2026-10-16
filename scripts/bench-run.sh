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

# line-buffered so the readiness lines show up while vortex runs
timeout -s ABRT -k 5 "$TIMEOUT" stdbuf -oL -eL \
	"$BUILD/vortex" --verbose --backend-path "$BACKEND" >"$LOG" 2>&1 &
VPID=$!
trap 'kill "$VPID" || true; wait "$VPID" || true' EXIT

# socket is bound before session/backend init, seat init comes last
SOCK=""
for _ in $(seq 100); do
	if ! kill -0 "$VPID"; then
		echo "vortex exited early, log: $LOG" >&2
		cat "$LOG"
		exit 1
	fi
	if grep -q "Initialized wayland seat." "$LOG"; then
		SOCK="$(grep -oP "ready on socket '\K[^']+" "$LOG")"
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
