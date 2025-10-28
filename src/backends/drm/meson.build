dep_gbm             = dependency('gbm',           required: true)
dep_drm             = dependency('libdrm',        required: true)
dep_wayland_server  = dependency('wayland-server', required: true)
dep_seat            = dependency('libseat', required: true)
dep_udev            = dependency('libudev', required: true)
dep_libinput        = dependency('libinput', required: true)

vortex_inc = include_directories('../../', '/usr/include/pixman-1/')

shared_module(
  'drm-backend',
  ['drm.c', 'session_drm.c'],
  dependencies: [dep_gbm, dep_drm, dep_wayland_server, dep_seat, dep_udev, dep_libinput],
  install: true,
  include_directories: vortex_inc, 
  install_dir: join_paths(get_option('libdir'), 'vortex/backends'),
)
