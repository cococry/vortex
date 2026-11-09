#!/bin/sh
set -eu
PROFILE_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
DWL_SOURCE="$PROFILE_ROOT/dwl/dwl"
WLR_SOURCE=$(CDPATH= cd -- "$DWL_SOURCE/.." && pwd -P)
TRACY_SOURCE=${TRACY_SOURCE:-"$WLR_SOURCE/../../subprojects/tracy"}
BUILD_TYPE=${PROFILE_BUILD_TYPE:-release}
MODE=${PROFILE_COMPARISON:-false}
case "$MODE" in
  true) SUFFIX=comparison; COMPARISON_CFLAGS=-DWLR_PROFILE_COMPARISON ;;
  false) SUFFIX=profiling; COMPARISON_CFLAGS= ;;
  *) echo 'PROFILE_COMPARISON must be true or false' >&2; exit 1 ;;
esac
ENABLED=${PROFILE_ENABLED:-true}
case "$ENABLED" in
 true) TRACY_CFLAGS=-DTRACY_ENABLE; TRACY_LIBS=-ltracy-client ;;
 false)
   if [ "$MODE" = true ]; then echo 'comparison mode requires profiling' >&2; exit 1; fi
   SUFFIX=control; TRACY_CFLAGS=; TRACY_LIBS= ;;
 *) echo 'PROFILE_ENABLED must be true or false' >&2; exit 1 ;;
esac
WLR_BUILD="$WLR_SOURCE/build-$SUFFIX"
DWL_BUILD="$DWL_SOURCE/build-$SUFFIX"
if [ -f "$WLR_BUILD/build.ninja" ]; then
    meson setup --reconfigure "$WLR_BUILD" "$WLR_SOURCE" -Dprofiling="$ENABLED" -Dprofiling_comparison="$MODE" -Dtracy_source="$TRACY_SOURCE" -Dbuildtype="$BUILD_TYPE" -Dbackends=drm,libinput,x11 -Drenderers=gles2,vulkan -Dexamples=false -Dtests=false -Dxwayland=disabled -Dicc-profile=disabled
else
    meson setup "$WLR_BUILD" "$WLR_SOURCE" -Dprofiling="$ENABLED" -Dprofiling_comparison="$MODE" -Dtracy_source="$TRACY_SOURCE" -Dbuildtype="$BUILD_TYPE" -Dbackends=drm,libinput,x11 -Drenderers=gles2,vulkan -Dexamples=false -Dtests=false -Dxwayland=disabled -Dicc-profile=disabled
fi
ninja -C "$WLR_BUILD" -j "${PROFILE_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}"
mkdir -p "$DWL_BUILD"
if [ ! -f "$DWL_BUILD/config.h" ]; then
    cp "$DWL_SOURCE/config.def.h" "$DWL_BUILD/config.h"
fi
PROTOCOLS=$(pkg-config --variable=pkgdatadir wayland-protocols)
wayland-scanner enum-header "$PROTOCOLS/staging/cursor-shape/cursor-shape-v1.xml" "$DWL_BUILD/cursor-shape-v1-protocol.h"
wayland-scanner enum-header "$PROTOCOLS/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml" "$DWL_BUILD/ext-image-copy-capture-v1-protocol.h"
wayland-scanner enum-header "$PROTOCOLS/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml" "$DWL_BUILD/pointer-constraints-unstable-v1-protocol.h"
wayland-scanner enum-header "$DWL_SOURCE/protocols/wlr-layer-shell-unstable-v1.xml" "$DWL_BUILD/wlr-layer-shell-unstable-v1-protocol.h"
wayland-scanner server-header "$DWL_SOURCE/protocols/wlr-output-power-management-unstable-v1.xml" "$DWL_BUILD/wlr-output-power-management-unstable-v1-protocol.h"
wayland-scanner server-header "$PROTOCOLS/stable/xdg-shell/xdg-shell.xml" "$DWL_BUILD/xdg-shell-protocol.h"
# we use the local library and the same client as wlroots
${CC:-cc} -std=gnu11 -O3 -g -fno-omit-frame-pointer -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L $COMPARISON_CFLAGS $TRACY_CFLAGS '-DVERSION="0.10-dev-profiling"' \
    -I"$DWL_BUILD" -I"$DWL_SOURCE" -I"$WLR_SOURCE/include" -I"$WLR_BUILD/include" \
    -I"$TRACY_SOURCE/public" \
    $(pkg-config --cflags wayland-server xkbcommon libinput pixman-1 libdrm) \
    "$DWL_SOURCE/dwl.c" -L"$WLR_BUILD" -Wl,-rpath,"$WLR_BUILD" \
    -lwlroots-0.21 $TRACY_LIBS $(pkg-config --libs wayland-server xkbcommon libinput pixman-1 libdrm) -lm \
    -o "$DWL_BUILD/dwl"
printf '%s\n' "$DWL_BUILD/dwl"
