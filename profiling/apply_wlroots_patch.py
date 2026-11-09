#!/usr/bin/env python3
"""Fetch the pinned profiling checkout and apply its comparison changes once."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = 'git@github.com:cococry/dwl-profiled'
REVISION = '0818d073a7c55cb11c1c2d70d3ecd18b8f6bf4cb'
CHECKOUT = ROOT / 'profiling/dwl'
PATCH = ROOT / 'profiling/tools/wlroots-comparison.patch'


def git(checkout, *arguments, check=True):
    env = os.environ.copy()
    env['GIT_TERMINAL_PROMPT'] = '0'
    env.setdefault('GIT_SSH_COMMAND', 'ssh -o BatchMode=yes -o ConnectTimeout=20')
    return subprocess.run(['git', '-c', 'pack.threads=1', '-c', 'index.threads=1',
                           '-C', str(checkout), *arguments], env=env,
                          text=True, capture_output=True, check=check)


def prepare_checkout(checkout=CHECKOUT, patch=PATCH, *, download=True,
                     repository=REPOSITORY, revision=REVISION):
    checkout, patch = Path(checkout), Path(patch).resolve()
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
    state = dict(revision=revision, patch_sha256=hashlib.sha256(patch.read_bytes()).hexdigest())
    gitdir = Path(git(checkout, 'rev-parse', '--absolute-git-dir').stdout.strip())
    marker = gitdir / 'vortex-profiling-patch.json'
    if marker.is_file() and json.loads(marker.read_text()) == state:
        # Preserve subsequent local experiments rather than reapplying/resetting.
        # A hard reset can leave the marker behind, so detect an unpatched tree.
        if git(checkout, 'apply', '--check', str(patch), check=False).returncode != 0:
            print('Pinned wlroots/dwl is already prepared')
            return
    reverse = git(checkout, 'apply', '--reverse', '--check', str(patch), check=False)
    if reverse.returncode != 0:
        forward = git(checkout, 'apply', '--check', str(patch), check=False)
        if forward.returncode:
            raise RuntimeError('wlroots patch conflicts with this checkout; local changes were preserved:\n' + forward.stderr)
        git(checkout, 'apply', str(patch))
    marker.write_text(json.dumps(state, indent=2) + '\n')
    print('Pinned wlroots/dwl comparison patch is applied')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-download', action='store_true')
    parser.add_argument('--patch', type=Path, default=PATCH)
    args = parser.parse_args()
    cache = ROOT / 'profiling/.cache'
    cache.mkdir(parents=True, exist_ok=True)
    with (cache / 'wlroots-prepare.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        prepare_checkout(patch=args.patch, download=not args.no_download)


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit('wlroots Git operation failed (check SSH access to the repository):\n' + error.stderr)
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit(str(error))
