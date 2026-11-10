#!/usr/bin/env python3
"""Build and run the offscreen Vulkan SHM pixel/lifetime regression check."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--validation', action='store_true',
                    help='Enable Khronos Vulkan synchronization validation')
args = parser.parse_args()
subprocess.run(['meson', 'compile', '-C', str(ROOT / 'build-control')], check=True)

with tempfile.TemporaryDirectory(prefix='vortex-shm-check-') as directory:
    directory = Path(directory)
    obj = directory / 'check.o'
    executable = directory / 'check'
    cflags = shlex.split(subprocess.check_output(
        ['pkg-config', '--cflags', 'vulkan'], text=True))
    libraries = shlex.split(subprocess.check_output(
        ['pkg-config', '--libs', 'cglm', 'gl', 'vulkan', 'shaderc'], text=True))
    subprocess.run(['cc', '-std=c11', '-O2', '-UNDEBUG', *cflags,
                    '-I' + str(ROOT / 'subprojects/runara/include'),
                    '-c', str(ROOT / 'profiling/tools/vulkan-shm-check.c'),
                    '-o', str(obj)], check=True)
    subprocess.run(['c++', str(obj),
                    str(ROOT / 'build-control/subprojects/runara/librunara.a'),
                    *libraries, '-lm', '-Wl,--wrap=vkQueueSubmit', '-o', str(executable)], check=True)
    env = os.environ.copy()
    if args.validation:
        env['VK_INSTANCE_LAYERS'] = 'VK_LAYER_KHRONOS_validation'
        env['VK_LAYER_VALIDATE_SYNC'] = '1'
    result = subprocess.run([str(executable)], env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(result.stdout, end='')
    if result.returncode or 'Validation Error:' in result.stdout or 'SYNC-HAZARD' in result.stdout:
        raise SystemExit('Vulkan SHM regression check failed')
