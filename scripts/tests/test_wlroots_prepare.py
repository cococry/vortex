"""Pinned checkout bootstrap and local-change preservation."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('wlroots_prepare', Path(__file__).parents[2] / 'profiling/prepare_wlroots.py')
prepare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prepare)


def git(path, *args):
    return subprocess.check_output(['git', '-c', 'user.name=Bootstrap test', '-c',
                                    'user.email=bootstrap@example.invalid', '-C', str(path), *args], text=True).strip()


class PrepareTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        git(self.source, 'init', '--quiet')
        (self.source / 'test.txt').write_text('before\n')
        git(self.source, 'add', 'test.txt')
        git(self.source, 'commit', '--quiet', '-m', 'base')
        self.revision = git(self.source, 'rev-parse', 'HEAD')
        self.checkout = self.root / 'checkout'

    def run_prepare(self, download=True):
        prepare.prepare_checkout(self.checkout, repository=str(self.source),
                                 revision=self.revision, download=download)

    def test_clone_exact_commit_and_preserve_edits(self):
        self.run_prepare()
        self.assertEqual(git(self.checkout, 'rev-parse', 'HEAD'), self.revision)
        self.assertEqual((self.checkout / 'test.txt').read_text(), 'before\n')
        (self.checkout / 'test.txt').write_text('after\nlocal experiment\n')
        self.run_prepare(download=False)
        self.assertEqual((self.checkout / 'test.txt').read_text(), 'after\nlocal experiment\n')

    def test_download_disabled_does_not_create_checkout(self):
        with self.assertRaisesRegex(RuntimeError, 'not downloaded'):
            self.run_prepare(download=False)
        self.assertFalse(self.checkout.exists())

    def test_wrong_commit_is_rejected_without_reset(self):
        self.run_prepare()
        (self.checkout / 'test.txt').write_text('local commit\n')
        git(self.checkout, 'add', 'test.txt')
        git(self.checkout, 'commit', '--quiet', '-m', 'local commit')
        changed = git(self.checkout, 'rev-parse', 'HEAD')
        with self.assertRaisesRegex(RuntimeError, 'expected'):
            self.run_prepare()
        self.assertEqual(git(self.checkout, 'rev-parse', 'HEAD'), changed)

    def test_directory_without_git_is_not_overwritten(self):
        self.checkout.mkdir()
        (self.checkout / 'keep.txt').write_text('local work')
        with self.assertRaisesRegex(RuntimeError, 'without .git'):
            self.run_prepare()
        self.assertEqual((self.checkout / 'keep.txt').read_text(), 'local work')


if __name__ == '__main__':
    unittest.main()
