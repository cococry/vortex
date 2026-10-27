shared_module(
  'x11-backend',
  'x11.c',
  dependencies: [
    dependency('x11'),
    dependency('x11-xcb'),
    dependency('xcb-present'),
    dependency('egl'),
    dep_pixman,
    dep_libdrm,
    dep_runara_headers,
  ],
  include_directories: [vortex_inc, include_directories('../../..')],
  install: true,
  install_dir: join_paths(get_option('libdir'), 'vortex', 'backends'),
)
