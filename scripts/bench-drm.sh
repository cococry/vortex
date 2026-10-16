#!/usr/bin/env bash
# run bench-run.sh in drm mode on a spare VT, from any terminal or ssh.
# logind only hands out DRM master to an active session bound to a VT,
# PAMName=login + TTYPath create exactly that for the transient unit.
# usage: sudo bench-drm.sh <build-dir> [glmark2 args...]

set -euo pipefail

if [[ "$EUID" -ne 0 || -z "${SUDO_USER:-}" ]]; then
	echo "run through sudo: sudo $0 <build-dir> [glmark2 args...]" >&2
	exit 1
fi

BUILD="$(realpath "$1")"
shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VT="${VT:-8}"
TIMEOUT="${TIMEOUT:-600}"
UNIT="vortex-bench"

ORIG_VT="$(fgconsole)"
trap 'chvt "$ORIG_VT"' EXIT

echo "switching to VT $VT, back to VT $ORIG_VT when done"
chvt "$VT"

# RuntimeMaxSec backs up the vortex watchdog: the unit dying frees the VT
status=0
systemd-run --unit="$UNIT" --wait --collect \
	-p PAMName=login -p User="$SUDO_USER" \
	-p TTYPath="/dev/tty$VT" -p StandardInput=tty-force \
	-p StandardOutput=journal -p StandardError=journal \
	-p TTYReset=yes -p TTYVTDisallocate=yes \
	-p RuntimeMaxSec="$((TIMEOUT + 30))" \
	-p WorkingDirectory="$ROOT" \
	--setenv=TIMEOUT="$TIMEOUT" \
	"$ROOT/scripts/bench-run.sh" "$BUILD" drm "$@" || status=$?

# unit stdout did not reach the journal in testing, read results back
if [[ -f "$BUILD/glmark2-drm.txt" ]]; then
	cat "$BUILD/glmark2-drm.txt"
fi
echo "unit exited with status $status. vortex log: $BUILD/vortex-drm.log"
exit "$status"
