./build/vortex -lf &
sleep 1s 
WAYLAND_DISPLAY=wayland-1 weston-simple-egl & 
