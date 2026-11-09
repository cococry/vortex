"""Checks for workload isolation and coarse comparison accounting."""
import csv
import ctypes
import importlib.util
from pathlib import Path
import tempfile
import subprocess
import sys
import time
import unittest

SPEC = importlib.util.spec_from_file_location('profile_compare', Path(__file__).parents[1] / 'profile_compare.py')
compare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compare)


class ComparisonTests(unittest.TestCase):
    def test_automatic_backend_priority(self):
        self.assertEqual(compare.backend_hint({'DISPLAY': ':0', 'WAYLAND_DISPLAY': 'wayland-0'}), 'wl')
        self.assertEqual(compare.backend_hint({'DISPLAY': ':0'}), 'x11')
        self.assertEqual(compare.backend_hint({}), 'drm')
        with self.assertRaisesRegex(RuntimeError, 'WAYLAND_SOCKET'):
            compare.backend_hint({'WAYLAND_SOCKET': '5'})
        command = compare.compositor_command('vortex', Path('/tmp/build/vortex'), 'vulkan', 'wl')
        self.assertNotIn('-b', command)
        self.assertNotIn('--backend', command)
        self.assertIn('/tmp/build/src/backends/wayland/libwl-backend.so', command)
        self.assertEqual(compare.compositor_command('dwl', Path('/tmp/dwl'), 'opengl', 'drm'), ['/tmp/dwl'])

    def test_workload_environment_is_not_host_display(self):
        original = {'DISPLAY': ':0', 'WAYLAND_DISPLAY': 'host', 'XDG_RUNTIME_DIR': '/run/user/test',
                    'WAYLAND_SOCKET': '7', 'TRACY_PORT': '8086', 'WLR_BACKENDS': 'x11'}
        env = compare.workload_environment(original, 'measured', Path('/tmp/work'))
        self.assertEqual(original['WAYLAND_DISPLAY'], 'host')
        self.assertEqual(env['WAYLAND_DISPLAY'], 'measured')
        self.assertEqual(env['XDG_RUNTIME_DIR'], original['XDG_RUNTIME_DIR'])
        self.assertEqual(env['PROFILE_COMPARE_WORKDIR'], '/tmp/work')
        for key in ['DISPLAY', 'WAYLAND_SOCKET', 'TRACY_PORT', 'WLR_BACKENDS']:
            self.assertNotIn(key, env)

    def test_compositor_environment_keeps_auto_selection(self):
        original = {'DISPLAY': ':0', 'WAYLAND_DISPLAY': 'host', 'WLR_BACKENDS': 'drm'}
        for renderer, expected in [('opengl', 'gles2'), ('vulkan', 'vulkan')]:
            env = compare.compositor_environment(original, renderer)
            self.assertEqual(env['DISPLAY'], ':0')
            self.assertEqual(env['WAYLAND_DISPLAY'], 'host')
            self.assertNotIn('WLR_BACKENDS', env)
            self.assertEqual(env['WLR_RENDERER'], expected)

    def test_renderer_fallback_is_rejected(self):
        log = 'Implemented Vulkan renderer\nImplemented OpenGL renderer\n'
        with self.assertRaisesRegex(RuntimeError, 'fell back'):
            compare.verify_vortex_renderer(log, 'vulkan')
        compare.verify_vortex_renderer(log, 'opengl')
        compare.verify_vortex_renderer('Implemented Vulkan renderer', 'vulkan')

    def test_script_arguments_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            script = Path(temporary) / 'with spaces.sh'
            script.write_text('sleep 1\n')
            args = compare.arguments(['--renderer', 'vulkan', '--seconds', '3', '--', str(script), 'one two', '$literal'])
            self.assertEqual(args.workload, [str(script), 'one two', '$literal'])
            self.assertEqual(args.renderer, 'vulkan')

    def events(self, directory, rows):
        path = Path(directory) / 'events.csv'
        with path.open('w') as stream:
            writer = csv.DictWriter(stream, fieldnames=['name', 'exec_time_ns', 'value'])
            writer.writeheader()
            for name, duration, stamp in rows:
                writer.writerow(dict(name=name, exec_time_ns=duration,
                                     value=f'{stamp} [0x{stamp:x}]' if stamp is not None else ''))
        return path

    def test_exact_workload_window_and_multiple_commits(self):
        with tempfile.TemporaryDirectory() as temporary:
            events = self.events(temporary, [
                ('compare.frame', 9999999, 900),
                ('compare.client_commit', 20, 1100),
                ('compare.client_commit', 20, 1150),
                ('compare.client_commit', 20, 1160),
                ('compare.frame', 100000, 1200),
                ('compare.frame', 300000, 1800),
                ('compare.frame', 9999999, 2000),
            ])
            result = compare.frame_statistics(events, 1000, 2000)
            self.assertEqual(result['frame_count'], 2)
            self.assertEqual(result['mean_frame_us'], 200)
            self.assertEqual(result['frame_total_ns'], 400000)

    def test_idle_workload_has_no_invented_frame_time(self):
        with tempfile.TemporaryDirectory() as temporary:
            events = self.events(temporary, [('compare.frame', 500000, 900)])
            self.assertIsNone(compare.frame_statistics(events, 1000, 2000)['mean_frame_us'])

    def test_detailed_leaks_and_missing_clock_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            events = self.events(temporary, [('internal.draw', 100, 1000)])
            with self.assertRaisesRegex(RuntimeError, 'unexpected scope'):
                compare.frame_statistics(events, 1000, 2000)
            events = self.events(temporary, [('compare.frame', 100, None)])
            with self.assertRaisesRegex(RuntimeError, 'timestamp missing'):
                compare.frame_statistics(events, 1000, 2000)

    def test_output_matching_including_unknown_refresh(self):
        before = [dict(width=1024, height=768, scale=1, transform=0, refresh_mhz=60000)]
        after = [dict(before[0], refresh_mhz=0)]
        compare.compare_outputs(before, after)
        with self.assertRaisesRegex(RuntimeError, 'dimensions'):
            compare.compare_outputs(before, [dict(after[0], width=1280)])
        with self.assertRaisesRegex(RuntimeError, 'refresh'):
            compare.compare_outputs(before, [dict(after[0], refresh_mhz=50000)])

    def test_cleanup_reaps_daemonized_workload_children(self):
        libc = ctypes.CDLL(None)
        previous = ctypes.c_int()
        self.assertEqual(libc.prctl(37, ctypes.byref(previous), 0, 0, 0), 0)
        self.assertEqual(libc.prctl(36, 1, 0, 0, 0), 0)
        processes = []
        try:
            with tempfile.TemporaryDirectory() as temporary:
                marker = Path(temporary) / 'pid'
                code = ('import os,time,pathlib\n'
                        'if os.fork()==0:\n'
                        ' os.setsid()\n'
                        f' pathlib.Path({str(marker)!r}).write_text(str(os.getpid()))\n'
                        ' time.sleep(60)\n')
                parent = subprocess.Popen([sys.executable, '-c', code], start_new_session=True)
                processes.append(parent)
                limit = time.monotonic() + 5
                while not marker.exists() and time.monotonic() < limit:
                    time.sleep(.02)
                self.assertTrue(marker.exists())
                child = int(marker.read_text())
                parent.wait(timeout=5)
                compare.cleanup(processes)
                self.assertFalse(Path(f'/proc/{child}').exists())
        finally:
            compare.cleanup(processes)
            libc.prctl(36, previous.value, 0, 0, 0)


if __name__ == '__main__':
    unittest.main()
