background_client_header = custom_target(
  'vt-background-client-header',
  input: '../protocols/vt-background-v1.xml',
  output: 'vt-background-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)
background_client_code = custom_target(
  'vt-background-client-code',
  input: '../protocols/vt-background-v1.xml',
  output: 'vt-background-v1-client-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

executable(
  'vortex-background-vk',
  ['background-vk.c', background_client_header, background_client_code],
  dependencies: [runara_dep, dependency('vulkan'), dependency('wayland-client')],
  c_args: ['-DBACKGROUND_IMAGE_DIR="' + meson.project_source_root() / 'subprojects/runara/examples' + '"'],
  install: false,
)
