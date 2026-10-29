# vortex

`vortex` is an independent wayland compositor written in C.

---

## Overview

Currently under active development as a high-performance, independent Wayland compositor
focused on creating a notoriously fast and visually appealing Wayland desktop experience.

The project's goal is to create a self-contained platform dedicated entirely to desktop compositing.
In other words, the compositor is responsible only for compositing and its related aspects (such as window animations and IPC),
while components like the desktop shell are intentionally abstracted away from it.

## Build & Run

Make sure you have the required dependencies installed (debian packages shown):

```bash
sudo apt install meson ninja-build libwayland-dev wayland-protocols libdrm-dev libgbm-dev libegl-dev libgl-dev libwayland-egl-backend-dev libinput-dev libudev-dev libxcursor-dev xcursor-themes libxkbcommon-dev libpixman-1-dev libseat-dev libliftoff-dev libcglm-dev libx11-dev libx11-xcb-dev libxkbcommon-x11-dev libxcb-present-dev
```

For Vulkan rendering support, additionally install:

```bash
sudo apt install libvulkan-dev
```

Clone & build:
```bash
git clone https://github.com/cococry/vortex.git
cd vortex

# needs to be in $PATH
meson setup build --prefix="$HOME/.local"
meson compile -C build
meson install -C build
```

### Build Options

Vortex supports configuring which sink backends and rendering backends are built through Meson options.

| Option | Values | Default | Description |
|--------|--------|---------|-------------|
| `backends` | `drm`, `wayland`, `x11`, `all` | `all` | Specify which sink backends to build |
| `renderers` | `opengl`, `vulkan`, `all` | `opengl` | Specify which rendering backends to build |
| `profiling` | `true`, `false` | `false` | Build and link the Tracy client for CPU profiling |

Simple test of vortex:
```bash
./build/vortex &
sleep 1
WAYLAND_DISPLAY=wayland-1 weston-simple-shm
```

### Running 
> [!NOTE]
> Use the wayland display that is logged by the output of **./vortex --verbose** as the WAYLAND_DISPLAY variable
> when starting clients.

Supported sink backends are:
- *DRM/KMS* (hardware scanout)
- *Wayland* (nested session)
- *X11* (nested session)

---

## CLI options

### Usage
```bash
vortex [option:s] (value:s)
```


### Options

| Option | Description |
|--------|--------------|
| `-h, --help` | Show this help message and exit |
| `-v, --version` | Show version information |
| `-vb, --verbose` | Log verbose (trace) output for debugging |
| `-lf, --logfile` | Write logs to a logfile located in:<br>`~/.local/state/vortex/logs/` or `$XDG_STATE_HOME/vortex/logs` |
| `-q, --quiet` | Run in quiet mode (no logging) |
| `-vo, --virtual-outputs [val]` | Specify the number of virtual outputs (windows) in nested mode |
| `-b, --backend [val]` | Specify the compositor’s sink backend.<br>Valid options: `drm`, `wl`, `x11`. Example:<br>`vortex -b drm` |
| `-r, --renderer [val]` | Specify the compositor’s renderer.<br>Valid options: `opengl`, `vulkan`. Example:<br>`vortex -r vulkan` |
| `-bp, --backend-path [val]` | Specify the **path to a `.so` file** to load as a custom sink backend |
| `-expt,  --exclude-protocol [val(s)]` | Specify the optional protocols (space seperated) you wish to not support during runtime |

> [!NOTE]
> When no renderer is specified, OpenGL (3.3) is used by default.  

## Sink Backend Configuration

The compositor supports **pluggable sink backends** that can be discovered at runtime.

### Valid Backends

Valid options for backends are:
  - drm
  - wl
  - x11

You can specify one directly:

```bash
vortex -b drm
```

Or load a custom shared object backend manually:

```bash
vortex -bp /usr/lib/vortex/backends/libcustom.so
```

### Environment Variables
Vortex handles the following environment variables:

| Env | Description |
|--------|--------------|
| `VT_DRM_NO_ATOMIC` | Force legacy DRM/KMS implementation |
| `VT_DRM_NO_HARDWARE_CURSOR` | Force cursor compositing |
| `VT_DRM_NO_DIRECT_SCANOUT` | Composite every surface, never try to put client buffers on hardware planes. |

---
