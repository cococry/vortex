#!/usr/bin/env python3
"""Incrementally build the pinned wlroots/dwl reference compositor."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess

from build_support import configure, run_build, write_changed

ROOT = Path(__file__).resolve().parents[1]


def ninja_path(path):
    return str(path).replace('$', '$$').replace(' ', '$ ').replace(':', '$:')


def command(words):
    return shlex.join([str(word) for word in words]).replace('$', '$$')


def main():
    enabled = os.environ.get('PROFILE_ENABLED', 'true') == 'true'
    comparison = os.environ.get('PROFILE_COMPARISON', 'false') == 'true'
    if comparison and not enabled:
        raise RuntimeError('comparison mode requires profiling')
    suffix = 'comparison' if comparison else ('profiling' if enabled else 'control')
    source = ROOT / 'profiling/dwl'
    dwl = source / 'dwl'
    tracy = Path(os.environ.get('TRACY_SOURCE', ROOT / 'subprojects/tracy'))
    build = source / f'build-{suffix}'
    target = dwl / f'build-{suffix}'
    jobs = os.environ.get('PROFILE_BUILD_JOBS', str(os.cpu_count() or 1))
    print(f'wlroots ({suffix}): checking/building with {jobs} jobs', flush=True)
    configure(build, source, dict(
        profiling=enabled, profiling_comparison=comparison, tracy_source=str(tracy),
        buildtype=os.environ.get('PROFILE_BUILD_TYPE', 'release'),
        backends=['drm', 'libinput', 'x11'], renderers=['gles2', 'vulkan'],
        examples=False, tests=False, xwayland='disabled', **{'icc-profile': 'disabled'}))
    run_build(['ninja', '-C', str(build), '-j', jobs], f'wlroots ({suffix})')
    target.mkdir(parents=True, exist_ok=True)
    if not (target / 'config.h').exists():
        shutil.copyfile(dwl / 'config.def.h', target / 'config.h')
    protocols = Path(subprocess.check_output(
        ['pkg-config', '--variable=pkgdatadir', 'wayland-protocols'], text=True).strip())
    headers = [
        ('enum-header', protocols/'staging/cursor-shape/cursor-shape-v1.xml'),
        ('enum-header', protocols/'staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml'),
        ('enum-header', protocols/'unstable/pointer-constraints/pointer-constraints-unstable-v1.xml'),
        ('enum-header', dwl/'protocols/wlr-layer-shell-unstable-v1.xml'),
        ('server-header', dwl/'protocols/wlr-output-power-management-unstable-v1.xml'),
        ('server-header', protocols/'stable/xdg-shell/xdg-shell.xml'),
    ]
    rules = ['rule scan', '  command = wayland-scanner $kind $in $out',
             '  description = Generating $out']
    generated = []
    for kind, xml in headers:
        output = xml.stem + '-protocol.h'
        generated.append(output)
        rules += [f'build {output}: scan {ninja_path(xml)}', f'  kind = {kind}']
    packages = ['wayland-server', 'xkbcommon', 'libinput', 'pixman-1', 'libdrm']
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', *packages], text=True))
    libraries = shlex.split(subprocess.check_output(['pkg-config', '--libs', *packages], text=True))
    cc = shlex.split(os.environ.get('CC', 'cc'))
    cc += ['-std=gnu11', '-O3', '-g', '-fno-omit-frame-pointer', '-DWLR_USE_UNSTABLE',
           '-D_POSIX_C_SOURCE=200809L', '-DVERSION="0.10-dev-profiling"']
    if comparison:
        cc.append('-DWLR_PROFILE_COMPARISON')
    if enabled:
        cc.append('-DTRACY_ENABLE')
    for include in [target, dwl, source/'include', build/'include', tracy/'public']:
        cc.append('-I'+str(include))
    cc += flags + ['-MMD', '-MF', 'dwl.d', '-MT', 'dwl', str(dwl/'dwl.c'),
                   '-L'+str(build), '-Wl,-rpath,'+str(build), '-lwlroots-0.21']
    dependencies = [build/'libwlroots-0.21.so', target/'config.h']
    if enabled:
        cc.append('-ltracy-client')
        dependencies.append(build/'libtracy-client.so')
    cc += libraries + ['-lm', '-o', 'dwl']
    rules += ['rule compile', '  command = '+command(cc), '  depfile = dwl.d',
              '  deps = gcc', '  description = Building dwl ('+suffix+')',
              'build dwl: compile '+ninja_path(dwl/'dwl.c')+' | '+
              ' '.join(generated + [ninja_path(path) for path in dependencies]),
              'default dwl', '']
    write_changed(target/'build.ninja', '\n'.join(rules))
    print(f'dwl ({suffix}): checking/building', flush=True)
    run_build(['ninja', '-C', str(target), '-j', jobs], f'dwl ({suffix})')


if __name__ == '__main__':
    main()
