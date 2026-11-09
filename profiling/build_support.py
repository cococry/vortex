"""Incremental build helpers shared by the comparison builders."""
import configparser
import json
from pathlib import Path
import subprocess
import time


def run_build(command, label):
    started = time.monotonic()
    subprocess.run(command, check=True)
    print(f'{label}: finished in {time.monotonic() - started:.2f}s', flush=True)


def configure(build, source, options):
    build = Path(build)
    try:
        actual = {row['name']: row['value'] for row in json.loads(
            (build / 'meson-info/intro-buildoptions.json').read_text())}
    except (OSError, ValueError, KeyError):
        actual = {}
    # Meson omits some per-subproject built-in options from introspection.
    recorded = configparser.ConfigParser(interpolation=None, delimiters=('=',))
    recorded.read(build / 'meson-private/cmd_line.txt')
    def encode(value):
        if isinstance(value, list):
            return ','.join(value) if value else '[]'
        return str(value).lower() if isinstance(value, bool) else str(value)
    def matches(key, value):
        if key in actual:
            return actual[key] == value
        return ':' in key and recorded.get('options', key, fallback=None) == encode(value)
    if (build / 'build.ninja').exists() and all(matches(k, v) for k, v in options.items()):
        print(f'{build.name}: configuration unchanged', flush=True)
        # Ninja still checks Meson inputs and regenerates when they change.
        return
    command = ['meson', 'setup']
    if (build / 'build.ninja').exists():
        command.append('--reconfigure')
    command += [str(build), str(source)]
    for key, value in options.items():
        command.append(f'-D{key}={encode(value)}')
    subprocess.run(command, check=True)


def write_changed(path, content):
    path = Path(path)
    if not path.exists() or path.read_text() != content:
        path.write_text(content)
