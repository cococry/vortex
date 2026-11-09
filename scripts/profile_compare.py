#!/usr/bin/env python3
"""Run identical workloads in coarse-profiled and release dwl/Vortex builds."""
import argparse
import csv
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import socket
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'profiling'))
from host_display import HostDisplay
from process_cpu import ProcessCPU

TOOLS = ROOT / 'profiling/.cache/tools'
SCOPES = {'compare.frame', 'compare.client_commit'}


def arguments(argv=None):
    parser = argparse.ArgumentParser(
        prog='profile-compare.sh', description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''Examples:
  ./scripts/profile-compare.sh
  ./scripts/profile-compare.sh --renderer vulkan -- ./workload.sh
  ./scripts/profile-compare.sh --seconds 60 --repeats 3 -- ./workload.sh arg1
  ./scripts/profile-compare.sh --no-build --seconds 10 --repeats 1

The same Bash script and arguments run for --seconds in every compositor/build.
The script may start/stop applications or exit while background children continue.
Remaining children are stopped at the end of each run. A nonzero script exit fails
the comparison. The default workload is weston-simple-dmabuf-egl.

Workloads receive WAYLAND_DISPLAY and XDG_RUNTIME_DIR for the measured compositor,
Wayland toolkit defaults, and PROFILE_COMPARE_WORKDIR (a fresh private directory).
Use that directory for application profiles to avoid reusing an existing browser.
DISPLAY is unset for workloads so clients cannot fall back to the host X server.
Keep each workload deterministic and do not resize the compositor's host window.

No backend is forced: inherited WAYLAND_DISPLAY/DISPLAY select Wayland/X11;
without them the compositors select DRM (run from an active physical VT).
Both builds use release optimization. Only the profiled build has coarse Tracy
probes; release CPU is measured without Tracy. Mean frame time is from the coarse
profiled runs. CPU includes all compositor threads and excludes client processes.
Frame-stage rates describe instrumented work, not display presentation FPS.
OpenGL selects desktop GL in Vortex and GLES2 in wlroots.
Builds use one job and the capture/export tools have no GUI.
Profiling configuration fetches the pinned wlroots/dwl checkout over SSH and
applies its saved comparison patch. Release-only Meson setup does not fetch it.
''')
    parser.add_argument('--renderer', choices=['opengl', 'vulkan'], default='opengl')
    parser.add_argument('--seconds', type=float, default=30, help='Workload measurement window per run (default: 30)')
    parser.add_argument('--repeats', type=int, default=3, help='Alternating repetitions of each compositor/build (default: 3)')
    parser.add_argument('--output', type=Path, help='New output directory; default: profiling/results/compare-TIMESTAMP')
    parser.add_argument('--no-build', action='store_true', help='Use existing binaries/tools; still audit profiling configuration')
    parser.add_argument('--width', type=int, default=1024, help='X11 nested window width (default: 1024)')
    parser.add_argument('--height', type=int, default=768, help='X11 nested window height (default: 768)')
    parser.add_argument('workload', nargs=argparse.REMAINDER, help='-- SCRIPT [ARG ...]')
    args = parser.parse_args(argv)
    if args.seconds < 1 or args.repeats < 1 or args.width < 1 or args.height < 1:
        parser.error('seconds, repeats and dimensions must be positive (seconds >= 1)')
    if args.workload and args.workload[0] == '--':
        args.workload = args.workload[1:]
    args.workload = args.workload or [str(ROOT / 'scripts/profile-workload.sh')]
    script = Path(args.workload[0]).resolve()
    if not script.is_file():
        parser.error(f'workload script does not exist: {script}')
    args.workload[0] = str(script)
    return args


def backend_hint(env):
    # Used for module paths and observations only; neither compositor is given
    # a backend-selection flag or WLR_BACKENDS override.
    if 'WAYLAND_DISPLAY' in env:
        return 'wl'
    if 'WAYLAND_SOCKET' in env:
        raise RuntimeError('WAYLAND_SOCKET-only sessions have different auto-selection rules; set WAYLAND_DISPLAY')
    return 'x11' if 'DISPLAY' in env else 'drm'


def binary(kind, mode):
    directory = 'build-comparison' if mode == 'profiled' else 'build-control'
    return ROOT / directory / 'vortex' if kind == 'vortex' else ROOT / 'profiling/dwl/dwl' / directory / 'dwl'


def backend_module(executable, backend):
    folder, name = {'x11': ('x11', 'x11'), 'wl': ('wayland', 'wl'), 'drm': ('drm', 'drm')}[backend]
    return executable.parent / 'src/backends' / folder / f'lib{name}-backend.so'


def compositor_command(kind, executable, renderer, backend):
    if kind == 'dwl':
        return [str(executable)]
    # -bp selects a module artifact, not a backend. Auto-selection remains in
    # Vortex, while the installed backend cannot shadow the measured build.
    return [str(executable), '-r', renderer, '-bp', str(backend_module(executable, backend))]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def descendants():
    parents = {}
    for path in Path('/proc').glob('[0-9]*/stat'):
        try:
            parents[int(path.parent.name)] = int(path.read_text().rsplit(')', 1)[1].split()[1])
        except (OSError, ValueError, IndexError):
            pass
    found = {os.getpid()}
    while True:
        children = {pid for pid, parent in parents.items() if parent in found}
        expanded = found | children
        if expanded == found:
            return found - {os.getpid()}
        found = expanded


def cleanup(processes):
    # Subreaping also catches browsers/clients which daemonize into new groups.
    for sig, timeout in [(signal.SIGTERM, 2), (signal.SIGKILL, 1)]:
        for process in reversed(processes):
            try:
                os.killpg(process.pid, sig)
            except ProcessLookupError:
                pass
        for pid in descendants():
            try:
                os.kill(pid, sig)
            except ProcessLookupError:
                pass
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for process in processes:
                process.poll()
            try:
                while os.waitpid(-1, os.WNOHANG)[0]:
                    pass
            except ChildProcessError:
                return
            if not descendants():
                return
            time.sleep(.02)


def start(command, log, env, processes, files):
    stream = log.open('w')
    files.append(stream)
    process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                               env=env, start_new_session=True)
    processes.append(process)
    return process


def wait_until(predicate, compositor, description, timeout=20):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if compositor.poll() is not None:
            raise RuntimeError(f'compositor exited while waiting for {description}')
        result = predicate()
        if result:
            return result
        time.sleep(.05)
    raise RuntimeError(f'timed out waiting for {description}')


def sockets(runtime):
    return {p.name: p.stat().st_ino for p in runtime.glob('wayland-*') if p.is_socket()}


def x11_geometry(kind, width, height):
    tree = subprocess.check_output(['xwininfo', '-root', '-tree'], text=True)
    title = 'Vortex Nested X11' if kind == 'vortex' else 'wlroots - X11-1'
    matches = [re.search(r'0x[0-9a-f]+', line).group() for line in tree.splitlines() if title in line]
    if not matches:
        return None
    if len(matches) != 1:
        raise RuntimeError(f'expected one {kind} X11 window; close other nested {kind} instances')
    xid = int(matches[0], 16)
    x = ctypes.CDLL('libX11.so.6')
    x.XOpenDisplay.restype = ctypes.c_void_p
    x.XResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_uint, ctypes.c_uint]
    x.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x.XCloseDisplay.argtypes = [ctypes.c_void_p]
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError('cannot open host X11 display')
    try:
        x.XResizeWindow(display, xid, width, height)
        x.XSync(display, 0)
    finally:
        x.XCloseDisplay(display)
    return xid


def output_signature(outputs):
    return sorted((o['width'], o['height'], o['scale'], o.get('transform', 0)) for o in outputs)


def output_key(output):
    return output['width'], output['height'], output['scale'], output.get('transform', 0)


def compare_outputs(reference, actual):
    if output_signature(reference) != output_signature(actual):
        raise RuntimeError('output dimensions/scale/transform differ between runs; use identical host window layouts')
    for before, after in zip(sorted(reference, key=output_key), sorted(actual, key=output_key)):
        a, b = before.get('refresh_mhz', 0), after.get('refresh_mhz', 0)
        if a and b and abs(a - b) > 100:
            raise RuntimeError('output refresh rates differ between runs')


def workload_environment(parent, display, workdir):
    env = parent.copy()
    env['WAYLAND_DISPLAY'] = display
    for key in ['DISPLAY', 'WAYLAND_SOCKET', 'TRACY_PORT', 'WLR_BACKENDS', 'WLR_RENDERER']:
        env.pop(key, None)
    for key, value in {'MOZ_ENABLE_WAYLAND': '1', 'GDK_BACKEND': 'wayland',
                       'QT_QPA_PLATFORM': 'wayland', 'SDL_VIDEODRIVER': 'wayland'}.items():
        env.setdefault(key, value)
    env['PROFILE_COMPARE_WORKDIR'] = str(workdir)
    env['XDG_CACHE_HOME'] = str(workdir / 'cache')
    return env


def compositor_environment(parent, renderer):
    env = parent.copy()
    env.pop('WLR_BACKENDS', None)
    env['WLR_RENDERER'] = 'gles2' if renderer == 'opengl' else 'vulkan'
    env['WLR_X11_OUTPUTS'] = env['WLR_WL_OUTPUTS'] = '1'
    env.setdefault('XCURSOR_THEME', 'Adwaita')
    return env


def verify_vortex_renderer(log, renderer):
    choices = re.findall(r'Implemented (OpenGL|Vulkan) renderer', log)
    expected = 'OpenGL' if renderer == 'opengl' else 'Vulkan'
    if not choices or choices[-1] != expected:
        raise RuntimeError('Vortex renderer selection failed or fell back; inspect compositor.log')


def acquire(args, kind, mode, repeat, output, reference):
    directory = output / f'{repeat:02d}-{kind}-{mode}'
    directory.mkdir()
    workdir = directory / 'work'
    workdir.mkdir()
    (workdir / 'cache').mkdir()
    env = compositor_environment(os.environ, args.renderer)
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    env['TRACY_PORT'] = str(port)
    backend = backend_hint(env)
    executable = binary(kind, mode).resolve()
    expected = args.artifact_hashes[f'{kind}-{mode}']
    if sha256(executable) != expected:
        raise RuntimeError('compositor binary changed during comparison')
    if sha256(Path(args.workload[0])) != args.workload_hash:
        raise RuntimeError('workload script changed during comparison')
    command = compositor_command(kind, executable, args.renderer, backend)
    runtime = Path(env['XDG_RUNTIME_DIR'])
    previous = sockets(runtime)
    processes, files = [], []
    host = None
    try:
        if backend == 'x11':
            host = HostDisplay()
        compositor = start(command, directory / 'compositor.log', env, processes, files)
        def new_socket():
            candidates = [name for name, inode in sockets(runtime).items() if previous.get(name) != inode]
            if len(candidates) > 1:
                raise RuntimeError('multiple new Wayland sockets; another compositor started during acquisition')
            return candidates[0] if candidates else None
        display = wait_until(new_socket, compositor, 'Wayland socket')
        client_env = workload_environment(env, display, workdir)
        xid = wait_until(lambda: x11_geometry(kind, args.width, args.height), compositor,
                         'X11 window') if backend == 'x11' else None
        def query_outputs():
            try:
                data = json.loads(subprocess.check_output([str(TOOLS / 'output-info')], env=client_env,
                                                           text=True, stderr=subprocess.DEVNULL, timeout=3))
            except (subprocess.SubprocessError, ValueError):
                return None
            if not data:
                return None
            if xid and any(o['width'] != args.width or o['height'] != args.height for o in data):
                return None
            return data
        outputs = wait_until(query_outputs, compositor, 'settled output configuration')
        time.sleep(.3)
        outputs = query_outputs() or outputs
        if reference is not None:
            compare_outputs(reference, outputs)
        monitor = start([str(TOOLS / 'output-info'), '--watch'], directory / 'output-events.log',
                        client_env, processes, files)
        time.sleep(.1)
        log = (directory / 'compositor.log').read_text()
        mappings = Path(f'/proc/{compositor.pid}/maps').read_text()
        mapped = {Path(line.split()[-1]).resolve() for line in mappings.splitlines()
                  if line.split()[-1].startswith('/')}
        (directory / 'loaded-maps.txt').write_text(mappings)
        if kind == 'vortex':
            verify_vortex_renderer(log, args.renderer)
            module = backend_module(executable, backend).resolve()
            if module not in mapped:
                raise RuntimeError('Vortex did not load the matching build backend module')
        else:
            folder = 'build-comparison' if mode == 'profiled' else 'build-control'
            module = (ROOT / 'profiling/dwl' / folder / 'libwlroots-0.21.so').resolve()
            if module not in mapped:
                raise RuntimeError('dwl did not load the matching build wlroots library')
        if mode == 'release':
            names = [p.read_text().strip() for p in Path(f'/proc/{compositor.pid}/task').glob('*/comm')]
            if any(name.startswith('Tracy') for name in names):
                raise RuntimeError('release compositor still has Tracy threads')
        capture = None
        if mode == 'profiled':
            capture_log = directory / 'capture.log'
            capture = start(['stdbuf', '-oL', str(TOOLS / 'tracy-capture'), '-a', '127.0.0.1', '-p', str(port),
                             '-o', str(directory / 'capture.tracy')], capture_log, env, processes, files)
            def connected():
                if capture.poll() is not None:
                    raise RuntimeError('Tracy connection failed; inspect capture.log')
                return 'Timer resolution:' in capture_log.read_text(errors='replace')
            wait_until(connected, compositor, 'Tracy connection')
        clock = ProcessCPU(compositor.pid)
        cpu_start = clock.read()
        begin = time.monotonic_ns()
        workload = start(['bash', *args.workload], directory / 'workload.log', client_env, processes, files)
        deadline = begin + int(args.seconds * 1e9)
        while time.monotonic_ns() < deadline:
            if compositor.poll() is not None:
                raise RuntimeError('compositor exited during workload')
            if workload.poll() not in (None, 0):
                raise RuntimeError(f'workload exited with status {workload.returncode}; inspect workload.log')
            time.sleep(min(.05, max(0, (deadline - time.monotonic_ns()) / 1e9)))
        end = time.monotonic_ns()
        cpu_end = clock.read()
        if workload.poll() not in (None, 0):
            raise RuntimeError(f'workload exited with status {workload.returncode}')
        if host:
            host.check()
        if capture:
            capture.send_signal(signal.SIGINT)
            capture.wait(timeout=30)
            if capture.returncode:
                raise RuntimeError('Tracy capture failed; inspect capture.log')
        if sha256(executable) != expected or sha256(Path(args.workload[0])) != args.workload_hash:
            raise RuntimeError('workload script or compositor binary changed during acquisition')
        if monitor.poll() is not None:
            raise RuntimeError('output observer exited')
        allowed = set(output_signature(outputs))
        events = [json.loads(line) for line in (directory / 'output-events.log').read_text().splitlines() if line]
        if not events or any((e['width'], e['height'], e['scale'], e.get('transform', 0)) not in allowed for e in events):
            raise RuntimeError('output geometry changed during workload')
        metadata = dict(compositor=kind, mode=mode, repeat=repeat, backend=backend, renderer=args.renderer,
                        command=command, workload=args.workload, workload_sha256=args.workload_hash,
                        binary_sha256=expected, outputs=outputs, begin_ns=begin, end_ns=end,
                        wall_seconds=(end - begin) / 1e9, cpu_seconds=(cpu_end - cpu_start) / 1e9,
                        cpu_clock_resolution=clock.resolution, wayland_display=display,
                        workload_exit_status=workload.poll())
        metadata['cpu_percent'] = 100 * metadata['cpu_seconds'] / metadata['wall_seconds']
        metadata['backend_library'] = str(module)
        metadata['backend_library_sha256'] = sha256(module)
        (directory / 'run.json').write_text(json.dumps(metadata, indent=2) + '\n')
        print(f"  {kind:6} {mode:8}: CPU {metadata['cpu_percent']:.2f}%", flush=True)
        return directory, metadata
    finally:
        cleanup(processes)
        for stream in files:
            stream.close()
        if host:
            host.close()


def frame_statistics(events, begin, end):
    frames = []
    with events.open() as stream:
        for row in csv.DictReader(stream):
            if row['name'] not in SCOPES:
                raise RuntimeError(f'coarse capture contains unexpected scope: {row["name"]}')
            duration = int(row['exec_time_ns'])
            if duration <= 0:
                continue  # An open zone can be cut off by capture shutdown.
            match = re.match(r'^(\d+)\s+\[0x[0-9a-fA-F]+\]$', row.get('value', ''))
            if not match:
                raise RuntimeError('coarse timestamp missing; rebuild both profiled compositors')
            stamp = int(match.group(1))
            if row['name'] == 'compare.frame' and begin <= stamp < end and duration > 0:
                frames.append(duration)
    return dict(frame_count=len(frames), frame_total_ns=sum(frames),
                mean_frame_us=statistics.mean(frames) / 1000 if frames else None)


def summarize(runs, output):
    for directory, result in runs:
        if result['mode'] != 'profiled':
            continue
        events = directory / 'events.csv'
        with events.open('w') as stream:
            subprocess.run([str(TOOLS / 'tracy-csvexport'), '-u', str(directory / 'capture.tracy')],
                           stdout=stream, check=True)
        result.update(frame_statistics(events, result['begin_ns'], result['end_ns']))
        (directory / 'run.json').write_text(json.dumps(result, indent=2) + '\n')
    rows = []
    for kind in ['dwl', 'vortex']:
        profiled = [r for _, r in runs if r['compositor'] == kind and r['mode'] == 'profiled']
        release = [r for _, r in runs if r['compositor'] == kind and r['mode'] == 'release']
        count = sum(r['frame_count'] for r in profiled)
        rows.append(dict(compositor=kind,
                         mean_frame_us=sum(r['frame_total_ns'] for r in profiled) / count / 1000 if count else None,
                         profiled_cpu_percent=100 * sum(r['cpu_seconds'] for r in profiled) / sum(r['wall_seconds'] for r in profiled),
                         release_cpu_percent=100 * sum(r['cpu_seconds'] for r in release) / sum(r['wall_seconds'] for r in release),
                         profiled_frame_stages_per_second=count / sum(r['wall_seconds'] for r in profiled), frame_samples=count))
    table = ['| Compositor | Mean frame (profiled, us) | CPU profiled (%) | CPU release (%) | Frame stages/s |',
             '| --- | ---: | ---: | ---: | ---: |']
    for row in rows:
        mean = f"{row['mean_frame_us']:.2f}" if row['mean_frame_us'] is not None else 'n/a'
        table.append(f"| {row['compositor']} | {mean} | {row['profiled_cpu_percent']:.2f} | {row['release_cpu_percent']:.2f} | {row['profiled_frame_stages_per_second']:.2f} |")
    text = '\n'.join(table) + '\n\nCPU is percent of one logical core; compositor processes only.\n'
    text += 'Mean frame time uses coarse profiled samples inside the workload window. Release builds contain no Tracy.\n'
    (output / 'summary.md').write_text(text)
    (output / 'summary.json').write_text(json.dumps(dict(results=rows, runs=[r for _, r in runs]), indent=2) + '\n')
    with (output / 'summary.csv').open('w') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print('\n' + text, flush=True)
    print(f'Results: {output}', flush=True)


def audit_release():
    dwl = ROOT / 'profiling/dwl/dwl'
    if (dwl / 'build-comparison/config.h').read_bytes() != (dwl / 'build-control/config.h').read_bytes():
        raise RuntimeError('dwl profiled/release config.h files differ; use the same configuration in both builds')
    for kind in ['vortex', 'dwl']:
        executable = binary(kind, 'release')
        dependencies = subprocess.check_output(['ldd', str(executable)], text=True)
        if 'tracy' in dependencies.lower():
            raise RuntimeError(f'{kind} release build links Tracy')


def main(argv=None):
    args = arguments(argv)
    if not os.environ.get('XDG_RUNTIME_DIR'):
        raise RuntimeError('XDG_RUNTIME_DIR is required')
    backend = backend_hint(os.environ)
    if backend == 'drm' and (not os.isatty(0) or not re.fullmatch(r'/dev/tty\d+', os.ttyname(0))):
        raise RuntimeError('DRM auto-selection requires an active physical VT; X11/Wayland sessions use their inherited display')
    # Adopt orphaned workload descendants so daemonized clients cannot leak into
    # subsequent runs. No unrelated desktop processes are included in cleanup.
    if ctypes.CDLL(None).prctl(36, 1, 0, 0, 0) != 0:
        raise RuntimeError('cannot enable child subreaping for workload cleanup')
    output = (args.output or ROOT / 'profiling/results' / time.strftime('compare-%Y%m%d-%H%M%S')).resolve()
    output.mkdir(parents=True, exist_ok=False)
    print(f'Backend: automatic ({backend}); renderer: {args.renderer}; results: {output}', flush=True)
    try:
        with (output / 'build.log').open('w') as log:
            if not args.no_build:
                print('Building coarse-profiled/release compositors and minimal tools with one job...', flush=True)
                for mode in ['comparison', 'off']:
                    subprocess.run([sys.executable, str(ROOT / 'profiling/build.py'), '--mode', mode],
                                   cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
                subprocess.run([sys.executable, str(ROOT / 'profiling/tools/build.py')],
                               cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
            subprocess.run([sys.executable, str(ROOT / 'profiling/check_builds.py')],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            subprocess.run([sys.executable, str(ROOT / 'profiling/check_modes.py')],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            audit_release()
            for tool in ['tracy-capture', 'tracy-csvexport', 'output-info']:
                if not (TOOLS / tool).is_file():
                    raise RuntimeError(f'missing tool {tool}; run without --no-build')
        (output / 'workload.sh').write_bytes(Path(args.workload[0]).read_bytes())
        args.workload_hash = sha256(output / 'workload.sh')
        args.artifact_hashes = {f'{kind}-{mode}': sha256(binary(kind, mode))
                                for kind in ['dwl', 'vortex'] for mode in ['profiled', 'release']}
        source = subprocess.run(['git', '-C', str(ROOT), 'diff', 'HEAD'], text=True, capture_output=True, check=True)
        (output / 'source.diff').write_text(source.stdout)
        for name, checkout in [('runara', ROOT / 'subprojects/runara'), ('wlroots', ROOT / 'profiling/dwl')]:
            if (checkout / '.git').exists():
                diff = subprocess.check_output(['git', '-C', str(checkout), 'diff', 'HEAD'], text=True)
                (output / f'{name}-source.diff').write_text(diff)
        (output / 'configuration.json').write_text(json.dumps(dict(
            backend=backend, renderer=args.renderer, seconds=args.seconds, repeats=args.repeats,
            workload=args.workload, workload_sha256=args.workload_hash, binary_sha256=args.artifact_hashes,
            git_head=subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
            parent_display=os.environ.get('DISPLAY'), parent_wayland_display=os.environ.get('WAYLAND_DISPLAY'),
            runara_wrap=(ROOT / 'subprojects/runara.wrap').read_text()), indent=2) + '\n')
        runs, reference = [], None
        for repeat in range(1, args.repeats + 1):
            order = ['dwl', 'vortex'] if repeat % 2 else ['vortex', 'dwl']
            modes = ['profiled', 'release'] if repeat % 2 else ['release', 'profiled']
            for mode in modes:
                for kind in order:
                    print(f'Run {repeat}/{args.repeats}: {kind} {mode}, {args.seconds:g}s workload', flush=True)
                    directory, result = acquire(args, kind, mode, repeat, output, reference)
                    if reference is None:
                        reference = [dict(o) for o in result['outputs']]
                    for before, actual in zip(sorted(reference, key=output_key), sorted(result['outputs'], key=output_key)):
                        if not before.get('refresh_mhz'):
                            before['refresh_mhz'] = actual.get('refresh_mhz', 0)
                    runs.append((directory, result))
        # Export only after all acquisition ends: analysis cannot affect CPU results.
        print('Exporting coarse captures...', flush=True)
        summarize(runs, output)
    except BaseException as error:
        (output / 'FAILED.txt').write_text(str(error) + '\n')
        raise
    finally:
        cleanup([])


def interrupted(*_):
    raise KeyboardInterrupt()


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, interrupted)
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        print(f'profile-compare: {error}', file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print('profile-compare: interrupted; child processes stopped', file=sys.stderr)
        sys.exit(130)
