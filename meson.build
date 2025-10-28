
project(
  'vortex',
  'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'optimization=3',
    'default_library=static',
    'werror=false',
    'c_args=-w'
  ]
)

add_project_arguments('-Ddefault_library=static', language: 'c')

subdir('src')

cc = meson.get_compiler('c')

dep_wayland_server  = dependency('wayland-server',    required: true)
dep_wayland_egl     = dependency('wayland-egl',       required: true)
dep_wayland_protos  = dependency('wayland-protocols', required: true)
dep_egl             = dependency('egl',               required: true)
dep_input           = dependency('libinput',          required: true)
dep_udev            = dependency('libudev',           required: true)
dep_gl              = dependency('gl',                required: true)
dep_xkbcommon       = dependency('xkbcommon',         required: false)
dep_pixman          = dependency('pixman-1',          required: true)
dep_seat            = dependency('libseat',           required: false)
dep_udev            = dependency('libudev',           required: false)
dep_libinput        = dependency('libinput',          required: false)

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

xdg_server_code = custom_target(
  'xdg-shell-server-code',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [wayland_scanner, 'code', '@INPUT@', '@OUTPUT@'],
  install: false
)

runara_sp  = subproject('runara')
runara_dep = runara_sp.get_variable('runara_dep')


vortex_src = files('src/vortex.c', 
'src/vortex.c', 
'src/render/renderer.c', 
'src/render/renderer.h', 
'src/render/gl/egl_gl46.c', 
'src/render/gl/egl_gl46.h', 
'src/protocols/xdg_shell.c', 
'src/protocols/xdg_shell.h', 
'src/input/input.c', 
'src/input/input.h', 
'src/input/wl_seat.c', 
'src/input/wl_seat.h', 
'src/input/backends/libinput/libinput.c', 
'src/input/backends/libinput/libinput.h', 
'src/input/backends/wayland/wayland_input.c', 
'src/input/backends/wayland/wayland_input.h', 
'src/input/input.h', 
'src/core/compositor.c', 
'src/core/compositor.h', 
'src/core/util.c',
'src/core/util.h',
'src/core/surface.c',
'src/core/surface.h',
'src/core/scene.c',
'src/core/scene.h',
)
vortex_inc = include_directories('include', '/usr/include/pixman-1/')

deps = [
  dep_wayland_server, dep_wayland_egl,
  dep_egl, dep_input, dep_udev, dep_gl,
  runara_dep, dep_pixman, dep_seat, dep_udev,
  dep_input
]
if dep_xkbcommon.found()
  deps += dep_xkbcommon
endif

conf_data = configuration_data()
conf_data.set('prefix', get_option('prefix'))
conf_data.set('libdir', get_option('libdir'))

configure_file(
  input: 'gen/config.h.in',
  output: 'config.h',
  configuration: conf_data,
)

# --- Executable ---
vortex_exe = executable(
  'vortex',
  [
    vortex_src,
    xdg_server_header,
    xdg_server_code,
  ],
  include_directories: vortex_inc,
  link_args: ['-Wl,-E'], 
  dependencies: deps,
  install: true
)

# --- Simple sanity test ---
test('binary-exists', vortex_exe)

