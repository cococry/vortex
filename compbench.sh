#!/usr/bin/env bash
# stupid helper to compile versionned

set -euo pipefail

# git rev parse branch
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
# git rev parse commit
COMMIT="$(git rev-parse --short HEAD)"
BUILD="build-$BRANCH-$COMMIT"

# meson conf
meson setup --reconfigure "$BUILD"
# meson comp
meson compile -C "$BUILD"

# run nested smoke test
# -b only scans installed backends, build tree needs explicit path
# wl is picked from WAYLAND_DISPLAY
WL_BACKEND="$BUILD/src/backends/wayland/libwl-backend.so"
timeout 5 "$BUILD/vortex" --verbose --backend-path "$WL_BACKEND" \
	|| [[ $? -eq 124 ]]
