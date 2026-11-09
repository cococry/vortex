#!/usr/bin/env python3
"""Build GUI-free Tracy tools, serially, from the repository's Tracy source."""
import argparse
import hashlib
import pathlib
import shutil
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configure-only', action='store_true')
    args = parser.parse_args()
    original = ROOT / 'subprojects/tracy'
    source = HERE.parent / '.cache/tracy-source'
    build = HERE.parent / '.cache/tools'
    if not (original / 'server/TracyWorker.cpp').exists():
        parser.error('subprojects/tracy is missing; configure the profiling-enabled Vortex build first')
    directories = ['server', 'public', 'getopt', 'csvexport', 'capture', 'cmake']
    digest = hashlib.sha256()
    inputs = [HERE/'build.py', HERE/'CMakeLists.txt', HERE/'tracy-minimal.patch',
              HERE/'ppqsort.h', HERE/'output-info.c']
    for directory in directories:
        inputs.extend(p for p in sorted((original/directory).rglob('*')) if p.is_file())
    for path in inputs:
        digest.update(str(path.relative_to(ROOT)).encode())
        digest.update(path.read_bytes())
    fingerprint = digest.hexdigest()
    stamp = build/'source.sha256'
    if not args.configure_only and stamp.is_file() and stamp.read_text() == fingerprint and all(
            (build/name).is_file() for name in ['tracy-capture', 'tracy-csvexport', 'output-info']):
        print('Minimal Tracy tools are up to date')
        return
    # Refresh only the small set of source trees needed by these tools.
    if source.exists():
        shutil.rmtree(source)
    for directory in ['server', 'public', 'getopt', 'csvexport', 'capture', 'cmake']:
        shutil.copytree(original / directory, source / directory, dirs_exist_ok=True)
    subprocess.run(['patch', '--batch', '-p1', '-i', str(HERE / 'tracy-minimal.patch')], cwd=source, check=True)
    shutil.copyfile(HERE / 'ppqsort.h', source / 'server/ppqsort.h')
    capture = source / 'capture/src/capture.cpp'
    text = capture.read_text()
    old = 'tracy::FileCompression::Zstd, 3, 4'
    if old not in text:
        raise RuntimeError('Tracy capture source changed: review the one-stream compression patch')
    capture.write_text(text.replace(old, 'tracy::FileCompression::Zstd, 3, 1'))
    subprocess.run(['cmake', '-S', str(HERE), '-B', str(build), '-DTRACY_SOURCE=' + str(source)], check=True)
    flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'wayland-client'], text=True).split()
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', str(HERE/'output-info.c'), '-o', str(build/'output-info'), *flags], check=True)
    if not args.configure_only:
        subprocess.run(['cmake', '--build', str(build), '--parallel', '1'], check=True)
        stamp.write_text(fingerprint)

if __name__ == '__main__':
    main()
