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
