dep_gbm             = dependency('gbm',           required: true)
dep_drm             = dependency('libdrm',        required: true)
dep_wayland_server  = dependency('wayland-server', required: true)

vortex_inc = include_directories('/usr/include/pixman-1/')

shared_module(
  'drm-backend',
  'drm.c',
  dependencies: [dep_gbm, dep_drm, dep_wayland_server],
  install: true,
  include_directories: vortex_inc, 
  install_dir: join_paths(get_option('libdir'), 'vortex/backends'),
)
