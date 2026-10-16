#!/usr/bin/env bash
# stupid helper to compile versionned, then bench with glmark2-wayland
# usage: compbench.sh [wl|drm] [glmark2 args...]

set -euo pipefail

MODE="${1:-wl}"
shift || true

# git rev parse branch
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
# git rev parse commit
COMMIT="$(git rev-parse --short HEAD)"
BUILD="build-$BRANCH-$COMMIT"

# meson conf
meson setup --reconfigure "$BUILD"
# meson comp
meson compile -C "$BUILD"

# -b only scans installed backends, scripts pass the build tree .so
case "$MODE" in
wl)
	scripts/bench-run.sh "$BUILD" wl "$@"
	;;
drm)
	# build stays user-owned, only the VT session needs root
	sudo --preserve-env=VERBOSE,TIMEOUT,VT,VORTEX_ARGS,VT_DRM_NO_DIRECT_SCANOUT scripts/bench-drm.sh "$BUILD" "$@"
	;;
*)
	echo "unknown mode '$MODE' (wl|drm)" >&2
	exit 1
	;;
esac
