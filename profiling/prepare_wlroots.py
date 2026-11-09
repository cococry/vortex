#!/usr/bin/env python3
"""Fetch the pinned, already-profiled wlroots/dwl reference checkout."""
import argparse
import fcntl
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = 'git@github.com:cococry/dwl-profiled'
REVISION = 'ffc51f8c0e019f9974476ad356b3b667a1af8fc1'
CHECKOUT = ROOT / 'profiling/dwl'


def git(checkout, *arguments, check=True):
    env = os.environ.copy()
    env['GIT_TERMINAL_PROMPT'] = '0'
    env.setdefault('GIT_SSH_COMMAND', 'ssh -o BatchMode=yes -o ConnectTimeout=20')
    return subprocess.run(['git', '-c', 'pack.threads=1', '-c', 'index.threads=1',
                           '-C', str(checkout), *arguments], env=env,
                          text=True, capture_output=True, check=check)


def prepare_checkout(checkout=CHECKOUT, *, download=True,
                     repository=REPOSITORY, revision=REVISION):
    checkout = Path(checkout)
    if not checkout.exists():
        if not download:
            raise RuntimeError('wlroots is not downloaded; configure once without --wrap-mode=nodownload')
        checkout.parent.mkdir(parents=True, exist_ok=True)
        # Publish only a complete checkout; a failed fetch leaves no partial tree.
        with tempfile.TemporaryDirectory(prefix='.dwl-fetch-', dir=checkout.parent) as temporary:
            staged = Path(temporary) / 'checkout'
            staged.mkdir()
            git(staged, 'init', '--quiet')
            git(staged, 'remote', 'add', 'origin', repository)
            print(f'Fetching wlroots/dwl at {revision}', flush=True)
            git(staged, 'fetch', '--quiet', '--no-tags', '--depth=1', 'origin', revision)
            git(staged, 'checkout', '--quiet', '--detach', revision)
            staged.rename(checkout)
    if not (checkout / '.git').exists():
        raise RuntimeError(f'{checkout} exists without .git; move it aside rather than overwriting its contents')
    head = git(checkout, 'rev-parse', 'HEAD').stdout.strip()
    if head != revision:
        raise RuntimeError(f'wlroots HEAD is {head}, expected {revision}; existing checkout was left unchanged')
    print(f'Pinned wlroots/dwl is ready at {revision}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-download', action='store_true')
    args = parser.parse_args()
    cache = ROOT / 'profiling/.cache'
    cache.mkdir(parents=True, exist_ok=True)
    with (cache / 'wlroots-prepare.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        prepare_checkout(download=not args.no_download)


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit('wlroots Git operation failed (check SSH access to the repository):\n' + error.stderr)
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit(str(error))
