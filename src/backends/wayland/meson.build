# src/backends/wayland/meson.build
dep_wayland_client  = dependency('wayland-client', required: true)
dep_wayland_protos  = dependency('wayland-protocols', required: true)
wayland_scanner     = find_program('wayland-scanner', required: true)

protocols_dir = dep_wayland_protos.get_variable(pkgconfig: 'pkgdatadir')
xdg_shell_xml = join_paths(protocols_dir, 'stable', 'xdg-shell', 'xdg-shell.xml')

xdg_client_header = custom_target(
  'xdg-shell-client-header',
  input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

vortex_inc = include_directories('/usr/include/pixman-1/')

shared_module(
  'wl-backend',
  [
    'wayland.c',
    xdg_client_header,
  ],
  dependencies: [dep_wayland_client],
  install: true,
  include_directories: vortex_inc, 
  install_dir: join_paths(get_option('libdir'), 'vortex/backends'),
)
