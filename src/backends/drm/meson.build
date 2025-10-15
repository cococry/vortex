dep_gbm             = dependency('gbm',           required: true)
dep_drm             = dependency('libdrm',        required: true)
dep_wayland_server  = dependency('wayland-server', required: true)

shared_module(
  'drm-backend',
  'drm.c',
  dependencies: [dep_gbm, dep_drm, dep_wayland_server],
  install: true,
  install_dir: join_paths(get_option('libdir'), 'vortex/backends'),
)
