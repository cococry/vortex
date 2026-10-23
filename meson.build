project(
  'vortex',
  'c',
  version: '0.1.0',
  meson_version: '>=1.1',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'default_library=static',
    'werror=false',
  ],
)

# ============================================================
# Build options
# ============================================================

sink_backends = get_option('backends')
renderers = get_option('renderers')

build_drm = sink_backends in ['drm', 'all']
build_wayland = sink_backends in ['wayland', 'all']

build_opengl = renderers in ['opengl', 'all']
build_vulkan = renderers in ['vulkan', 'all']

# ============================================================
# Runara
# ============================================================

runara_backends = renderers
if get_option('background-client') and renderers == 'opengl'
  runara_backends = 'all'
endif

runara_sp = subproject(
  'runara',
  default_options: [
    'backends=' + runara_backends,
    'examples=false',
    'default_library=static',
  ],
)

runara_dep = runara_sp.get_variable('runara_dep')

# ============================================================
# Core dependencies
# ============================================================

dep_wayland_server = dependency(
  'wayland-server',
  required: true,
)

dep_wayland_protos = dependency(
  'wayland-protocols',
  required: true,
)

dep_pixman = dependency(
  'pixman-1',
  required: true,
)

dep_xcursor = dependency(
  'xcursor',
  required: true,
)

dep_xkbcommon = dependency(
  'xkbcommon',
  required: false,
)

# Needed by the shared DRM format code
dep_libdrm = dependency(
  'libdrm',
  required: true,
)

deps = [
  dep_wayland_server,
  dep_pixman,
  dep_xcursor,
  dep_libdrm,
  runara_dep,
]

if dep_xkbcommon.found()
  deps += dep_xkbcommon
endif

# ============================================================
# Renderer dependencies
# ============================================================

if build_opengl
  deps += [
    dependency('gl'),
    dependency('egl'),
    dependency('wayland-egl'),
  ]
endif

if build_vulkan
  deps += dependency('vulkan')
endif

# ============================================================
# Wayland protocol generation
# ============================================================

wayland_scanner = find_program(
  'wayland-scanner',
  required: true,
)

protocols_dir = dep_wayland_protos.get_variable(
  pkgconfig: 'pkgdatadir',
)

xdg_shell_xml = join_paths(
  protocols_dir,
  'stable',
  'xdg-shell',
  'xdg-shell.xml',
)

dmabuf_xml = join_paths(
  protocols_dir,
  'stable',
  'linux-dmabuf',
  'linux-dmabuf-v1.xml',
)

explicit_sync_xml = join_paths(
  protocols_dir,
  'unstable',
  'linux-explicit-synchronization',
  'linux-explicit-synchronization-unstable-v1.xml',
)

drm_syncobj_xml = join_paths(
  protocols_dir,
  'staging',
  'linux-drm-syncobj',
  'linux-drm-syncobj-v1.xml',
)

vt_background_xml = 'protocols/vt-background-v1.xml'

protocol_sources = []

protocols = {
  'xdg-shell': [
    xdg_shell_xml,
    'xdg-shell-protocol.h',
    'xdg-shell-protocol.c',
  ],
  'linux-dmabuf': [
    dmabuf_xml,
    'linux-dmabuf-v1-server-protocol.h',
    'linux-dmabuf-v1-protocol.c',
  ],
  'linux-explicit-sync': [
    explicit_sync_xml,
    'linux-explicit-synchronization-v1-server-protocol.h',
    'linux-explicit-synchronization-v1-protocol.c',
  ],
  'linux-drm-syncobj': [
    drm_syncobj_xml,
    'linux-drm-syncobj-v1-server-protocol.h',
    'linux-drm-syncobj-v1-protocol.c',
  ],
  'vt-background': [
    vt_background_xml,
    'vt-background-v1-server-protocol.h',
    'vt-background-v1-protocol.c',
  ]
}

foreach name, protocol : protocols
  protocol_sources += custom_target(
    name + '-server-header',
    input: protocol[0],
    output: protocol[1],
    command: [
      wayland_scanner,
      'server-header',
      '@INPUT@',
      '@OUTPUT@',
    ],
  )

  protocol_sources += custom_target(
    name + '-server-code',
    input: protocol[0],
    output: protocol[2],
    command: [
      wayland_scanner,
      'private-code',
      '@INPUT@',
      '@OUTPUT@',
    ],
  )
endforeach

# ============================================================
# Configuration
# ============================================================

conf_data = configuration_data()

conf_data.set(
  'prefix',
  get_option('prefix'),
)

conf_data.set(
  'libdir',
  get_option('libdir'),
)

conf_data.set_quoted(
  'VORTEX_PREFIX',
  get_option('prefix'),
)

conf_data.set_quoted(
  'VORTEX_BACKEND_DIR',
  join_paths(
    get_option('libdir'),
    'vortex',
    'backends',
  ),
)

conf_data.set10('VT_HAS_OPENGL', build_opengl)
conf_data.set10('VT_HAS_VULKAN', build_vulkan)
conf_data.set10('VT_HAS_LIBINPUT', build_drm)
conf_data.set10('VT_HAS_WAYLAND_INPUT', build_wayland)

configure_file(
  input: 'gen/config.h.in',
  output: 'config.h',
  configuration: conf_data,
)

# ============================================================
# Include directories
# ============================================================

vortex_inc = include_directories(
  'src',
)

# ============================================================
# Compositor sources
# ============================================================

vortex_src = files(
  'src/vortex.c',

  'src/render/renderer.c',
  'src/render/dmabuf.c',
  'src/render/drm_format.c',

  'src/protocols/xdg_shell.c',
  'src/protocols/linux_dmabuf.c',
  'src/protocols/linux_explicit_sync.c',
  'src/protocols/linux_drm_syncobj.c',
  'src/protocols/wl_surface.c',
  'src/protocols/vt_background.c',
  'src/protocols/wl_output.c',
  'src/protocols/wl_data_device.c',
  'src/protocols/wl_subcompositor.c',
  'src/protocols/wl_shm.c',

  'src/input/input.c',
  'src/input/wl_seat.c',
  'src/input/root_cursor.c',

  'src/core/compositor.c',
  'src/core/content_update.c',
  'src/core/util.c',
  'src/core/surface.c',
  'src/core/surface_addon.c',
  'src/core/output.c',
  'src/core/focus_policy.c',
  'src/core/buffer.c',
  'src/core/buffer_orchestrator.c',
  'src/core/scene.c',
)

# ============================================================
# Renderer-specific sources
# ============================================================

if build_opengl
  vortex_src += files(
    'src/render/gl/egl_gl.c',
  )
endif

# TODO: Vulkan
#
# ============================================================
# Input backend implementations
# ============================================================

if build_drm
  vortex_src += files(
    'src/input/backends/libinput/libinput.c',
  )

  deps += [
    dependency('libinput'),
    dependency('libudev'),
  ]
endif

if build_wayland
  vortex_src += files(
    'src/input/backends/wayland/wayland_input.c',
  )

  deps += [
    dependency('wayland-client'),
  ]
endif

# ============================================================
# Compositor executable
# ============================================================

vortex_exe = executable(
  'vortex',
  [
    vortex_src,
    protocol_sources,
  ],
  include_directories: vortex_inc,
  dependencies: deps,

  export_dynamic: true,

  install: true,
)

# ============================================================
# Dynamic sink backends
# ============================================================

subdir('src/backends')

# ============================================================
# Build summary
# ============================================================

summary(
  {
    'Sink backends': sink_backends,
    'GPU renderers': renderers,
    'OpenGL': build_opengl,
    'Vulkan': build_vulkan,
    'Vulkan background client': get_option('background-client'),
  },
  section: 'Vortex',
  bool_yn: true,
)
