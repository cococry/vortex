./build/vortex &

sleep 1s
WAYLAND_DISPLAY=wayland-1 weston-simple-damage &
sleep 3s
WAYLAND_DISPLAY=wayland-1 weston-simple-damage &
