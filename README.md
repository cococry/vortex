# vortex

**vortex** is an independent wayland compositor written in C. 

---

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
