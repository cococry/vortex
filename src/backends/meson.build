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

summary(
  {
    'DRM backend': build_drm,
    'Wayland backend': build_wayland,
  },
  section: 'Sink backends',
  bool_yn: true,
)
