project(
  'vortex',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'optimization=3',
    'werror=false',
    'c_args=-w'
  ]
)

cc = meson.get_compiler('c')

dep_wayland_server  = dependency('wayland-server', required: true)
dep_wayland_client  = dependency('wayland-client', required: true)
dep_wayland_egl     = dependency('wayland-egl',    required: true)
dep_wayland_protos  = dependency('wayland-protocols', required: true)
dep_egl             = dependency('egl',           required: true)
dep_gbm             = dependency('gbm',           required: true)
dep_drm             = dependency('libdrm',        required: true)
dep_input           = dependency('libinput',      required: true)
dep_udev            = dependency('libudev',       required: true)
dep_gl              = dependency('gl',            required: true)
dep_xkbcommon       = dependency('xkbcommon',     required: false)

wayland_scanner = find_program('wayland-scanner', required: true)
protocols_dir = dep_wayland_protos.get_variable(pkgconfig: 'pkgdatadir')
xdg_shell_xml = join_paths(protocols_dir, 'stable', 'xdg-shell', 'xdg-shell.xml')

xdg_server_header = custom_target(
  'xdg-shell-server-header',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  install: false
)

xdg_client_header = custom_target(
  'xdg-shell-client-header',
  input: xdg_shell_xml,
  output: 'xdg-shell-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
  install: false
)

xdg_server_code = custom_target(
  'xdg-shell-server-code',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [wayland_scanner, 'code', '@INPUT@', '@OUTPUT@'],
  install: false
)

runara_sp  = subproject('runara')
runara_dep = runara_sp.get_variable('runara_dep')

vortex_src = files('src/vortex.c', 'src/compositor.c', 'src/backends/drm.c', 'src/backends/wayland.c', 'src/renderers/egl_gl46.c', 'src/log.c')
vortex_inc = include_directories('include')

deps = [
  dep_wayland_server, dep_wayland_client, dep_wayland_egl,
  dep_egl, dep_gbm, dep_drm, dep_input, dep_udev, dep_gl,
  runara_dep
]
if dep_xkbcommon.found()
  deps += dep_xkbcommon
endif

# --- Executable ---
vortex_exe = executable(
  'vortex',
  [
    vortex_src,
    xdg_server_header,
    xdg_client_header,
    xdg_server_code,
  ],
  include_directories: vortex_inc,
  dependencies: deps,
  install: true
)

# --- Simple sanity test ---
test('binary-exists', vortex_exe)

