# Copyright (c) 2026 Luca Machiedo
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

drm_opt             = get_option('vt_backend_drm')
dep_gbm             = dependency('gbm',           required: drm_opt)
dep_drm             = dependency('libdrm',        required: drm_opt)
dep_seat            = dependency('libseat',       required: drm_opt)

# auto: skip the DRM backend when its deps are missing (e.g. no seatd)
if not (dep_gbm.found() and dep_drm.found() and dep_seat.found())
  subdir_done()
endif
vt_build_drm = true

dep_wayland_server  = dependency('wayland-server', required: true)
dep_udev            = dependency('libudev', required: true)
dep_libinput        = dependency('libinput', required: true)
dep_libliftoff = dependency('libliftoff',version: '>=0.5.0', fallback: 'libliftoff')

vortex_inc = include_directories('../../', '/usr/include/pixman-1/')

shared_module(
  'drm-backend',
  ['drm_backend.c', 'session_drm.c', 'props.c', 'prime.c', 'fb.c', 'atomic.c', 'legacy.c', 'cursor.c', 'kms.c'],
  dependencies: [dep_gbm, dep_drm, dep_wayland_server, dep_seat, dep_udev, dep_libinput,
    dep_runara_headers, dep_libliftoff],
  install: true,
  include_directories: vortex_inc,
  install_dir: join_paths(get_option('libdir'), 'vortex/backends'),
)


