# vortex

**vortex** is an independent wayland compositor written in C. 

---

## Overview

Vortex is currently under active development as a high-performance, modern Wayland compositor
focused on creating a visually appealing Wayland desktop experience. 
The project's goal is to create a self-contained, independent platform dedicated entirely to desktop compositing. 
In other words, the compositor is responsible only for compositing and its related aspects (such as window animations and IPC), 
while components like the desktop shell are intentionally abstracted away from it.

## Beta?

Given that Vortex is only about a month old, it’s still far too 
early to consider it a Beta release. The project currently remains in its alpha stage of development.

**The first beta release is planned for around January 2026.**

## Build & Run

Make sure you have the required dependencies installed (debian packages shown):
```
meson ninja-build libwayland-dev libdrm-dev libgbm-dev libegl1-mesa-dev libinput-dev libudev-dev libxkbcommon-dev libglfw3-dev libcglm-dev libfreetype-dev libharfbuzz-dev
```

Clone & build:
```
git clone --recurse-submodules https://github.com/cococry/vortex.git
cd vortex

meson setup build
meson compile -C build
sudo meson install -C build
```

To run Vortex:
```
./build/vortex # Start the compositor
WAYLAND_DISPLAY=wayland-1 weston-simple-shm # Start a simple client 
```

Note: Use the wayland display that is logged by the output of **./vortex --verbose** as the WAYLAND_DISPLAY variable 
when starting clients.


Note: The sink backend is automatically chosen at runtime, depending on context. 

Supported sink backends are:
- *DRM/KMS* (hardware scanout)
- *Wayland* (nested session)

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
| `-b, --backend [val]` | Specify the compositor’s sink backend.<br>Valid options: `drm`, `wl`. Example:<br>`vortex -b drm` |
| `-bp, --backend-path [val]` | Specify the **path to a `.so` file** to load as a custom sink backend |
| `-expt,  --exclude-protocol [val(s)]` | Specify the optional protocols (space seperated) you wish to not support during runtime |

## Sink Backend Configuration

The compositor supports **pluggable sink backends** that can be discovered at runtime.

### Valid Backends

Valid options for backends are: 
  - drm
  -  wl 

You can specify one directly:

```bash
vortex -b drm
```

Or load a custom shared object backend manually:

```bash
vortex -bp /usr/lib/vortex/backends/libcustom.so
```

---
