"""Build orchestration tests; these never run a compiler or compositor."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('profile_compare', Path(__file__).parents[1]/'profile_compare.py')
compare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compare)
import build_support
import build_dwl


class BuildTests(unittest.TestCase):
    def test_warm_configuration_skips_meson_but_changed_options_reconfigure(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            (build/'meson-info').mkdir()
            (build/'meson-private').mkdir()
            (build/'build.ninja').touch()
            (build/'meson-info/intro-buildoptions.json').write_text(json.dumps([
                dict(name='profiling', value=True), dict(name='renderers', value=['gles2', 'vulkan'])]))
            (build/'meson-private/cmd_line.txt').write_text('[options]\nrunara:c_args = []\n')
            options = {'profiling': True, 'renderers': ['gles2', 'vulkan'], 'runara:c_args': []}
            with patch.object(build_support.subprocess, 'run') as run:
                build_support.configure(build, build, options)
                run.assert_not_called()
                build_support.configure(build, build, dict(options, profiling=False))
                self.assertIn('--reconfigure', run.call_args.args[0])
                self.assertIn('-Dprofiling=false', run.call_args.args[0])

    def test_identical_generated_build_file_preserves_mtime(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'build.ninja'
            build_support.write_changed(path, 'rules\n')
            before = path.stat().st_mtime_ns
            build_support.write_changed(path, 'rules\n')
            self.assertEqual(before, path.stat().st_mtime_ns)

    def test_dwl_uses_ninja_with_header_and_library_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dwl = root/'profiling/dwl/dwl'
            dwl.mkdir(parents=True)
            (dwl/'config.def.h').write_text('configuration')
            with patch.object(build_dwl, 'ROOT', root), \
                 patch.object(build_dwl, 'configure'), \
                 patch.dict(build_dwl.os.environ, {'PROFILE_ENABLED': 'true', 'PROFILE_COMPARISON': 'true', 'PROFILE_BUILD_JOBS': '8'}), \
                 patch.object(build_dwl.subprocess, 'check_output', side_effect=[str(root/'protocols'), '', '']), \
                 patch.object(build_dwl.subprocess, 'run') as run:
                build_dwl.main()
            graph = (dwl/'build-comparison/build.ninja').read_text()
            for dependency in ['depfile = dwl.d', 'deps = gcc', 'libwlroots-0.21.so',
                               'libtracy-client.so', 'config.h', 'xdg-shell-protocol.h']:
                self.assertIn(dependency, graph)
            self.assertIn('-MMD', graph)
            self.assertIn('-DWLR_PROFILE_COMPARISON', graph)
            self.assertEqual(len(run.call_args_list), 2)
            for call in run.call_args_list:
                self.assertEqual(call.args[0][0], 'ninja')
                self.assertEqual(call.args[0][-2:], ['-j', '8'])
            # Parse the real generated graph without executing any build rules.
            targets = subprocess.check_output(
                ['ninja', '-C', str(dwl/'build-comparison'), '-t', 'targets', 'all'], text=True)
            self.assertIn('dwl: compile', targets)

    def test_progress_is_logged_and_visible_and_failure_propagates(self):
        import subprocess
        import sys
        terminal, log = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(terminal):
            compare.run_logged([sys.executable, '-c', 'print("build output")'], log, 'Test phase')
        self.assertIn('build output', terminal.getvalue())
        self.assertIn('build output', log.getvalue())
        self.assertIn('[Test phase:', terminal.getvalue())
        with contextlib.redirect_stdout(terminal):
            with self.assertRaises(subprocess.CalledProcessError):
                compare.run_logged([sys.executable, '-c', 'raise SystemExit(7)'], log, 'Failure')

    def test_successful_mode_checks_are_cached_and_invalidated(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ['profiling/check_modes.py', 'src/core/profiling.h',
                         'profiling/dwl/include/util/profiling.h', 'profiling/dwl/dwl/profiling.h',
                         'subprojects/runara/src/profiling.h', 'subprojects/runara/src/vulkan/backend.c']:
                path = root/name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('test input')
            binary = root/'binary'
            binary.write_bytes(b'binary')
            with patch.object(compare, 'ROOT', root), patch.object(compare, 'binary', return_value=binary), \
                 patch.object(compare, 'run_logged') as run:
                compare.check_modes_cached(io.StringIO())
                self.assertEqual(run.call_count, 1)
                compare.check_modes_cached(io.StringIO())
                self.assertEqual(run.call_count, 1)
                binary.write_bytes(b'changed binary')
                compare.check_modes_cached(io.StringIO())
                self.assertEqual(run.call_count, 2)


if __name__ == '__main__':
    unittest.main()
