# vortex

**Vortex** is a wayland compositor and renderer built on DRM, GBM,EGL, etc. using [Runara](https://github.com/cococry/runara) for its OpenGL abstraction and rendering layer.
The compositor does not depend on any wayland abstraction library like libweston or wlroots, everything is self-implemented. 
Vortex is the compositor of the Ragnar desktop ecosystem. 

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

Use `--nested` to run it inside another Wayland compositor:

./build/vortex --nested

---

