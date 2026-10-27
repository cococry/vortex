dep_runara_headers = runara_dep.partial_dependency(
  compile_args: true,
  includes: true,
)

if build_drm
  subdir('drm')
endif

if build_wayland
  subdir('wayland')
endif

if build_x11
  subdir('x11')
endif

summary(
  {
    'DRM backend': build_drm,
    'Wayland backend': build_wayland,
    'X11 backend': build_x11,
  },
  section: 'Sink backends',
  bool_yn: true,
)
