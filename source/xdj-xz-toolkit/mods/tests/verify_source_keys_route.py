"""The physical source keys (USB 1, USB 2, LINK) in MODERN through the real shell runtime, on host, ARM and ASan.

Usage: python mods/tests/verify_source_keys_route.py --out FOLDER [--arch host|arm|all]
Needs WSL Ubuntu with gcc, arm-linux-gnueabi-gcc and qemu-arm on Windows. Nothing touches a device.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--arch', choices=('host', 'arm', 'all'), default='all')
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)


def linux(path):
    value = str(Path(path).resolve()).replace('\\', '/')
    return '/mnt/' + value[0].lower() + value[2:] if os.name == 'nt' else value


def run(command, check=True):
    prefix = ['wsl', '-d', 'Ubuntu', '--exec', 'env', 'TMPDIR=' + linux(args.out)] if os.name == 'nt' else []
    result = subprocess.run(prefix + command, text=True, capture_output=True)
    if check and result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    return result


sys.path.insert(0, str(root / 'shell'))
from shell_sources import SHELL_SOURCES  # noqa: E402
sources = [root / 'tests/test_source_keys_route.c', *(root / name for name in (
    'shell_runtime_core.c', 'shell_library.c', 'shell_playlists.c', 'shell_mysettings.c', 'my_settings.c',
    'shell_analysis.c', 'shell_utility.c', 'pdb/pdb.c', 'ui/layered_wave_source.c', 'ui/layered_wave.c')),
           *(root / 'shell' / path for path in SHELL_SOURCES)]
builds = [('host', 'gcc', 'objcopy', [], []),
          ('arm', 'arm-linux-gnueabi-gcc', 'arm-linux-gnueabi-objcopy', ['qemu-arm', '-L', '/usr/arm-linux-gnueabi'], []),
          ('asan', 'gcc', 'objcopy', [], ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie'])]
if args.arch == 'host':
    builds = [b for b in builds if b[0] != 'arm']
elif args.arch == 'arm':
    builds = [b for b in builds if b[0] == 'arm']
failed = 0
for name, compiler, objcopy, runner, extra in builds:
    common = [compiler, '-std=c11', '-O1', '-g', '-UNDEBUG', '-ffunction-sections', '-fdata-sections',
              '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-Wno-format-truncation', *extra]
    obj = linux(args.out / ('source-keys-' + name + '.o'))
    binary = linux(args.out / ('source-keys-' + name))
    run([*common, '-c', linux(sources[0]), '-o', obj])
    run([objcopy, '--remove-section=.init_array', '--remove-section=.fini_array', obj])
    run([*common, '-Wl,--gc-sections', obj, *map(linux, sources[1:]), '-pthread', '-ldl', '-lm', '-o', binary])
    result = run([*runner, binary], check=False)
    print(name + ': ' + (result.stdout + result.stderr).strip(), flush=True)
    failed |= result.returncode != 0
sys.exit(1 if failed else 0)
