"""The LOAD menu through the real shell runtime (shell_runtime.c with the shell), on host, ARM and ASan.

Usage: python mods/tests/verify_load_menu_route.py --out FOLDER
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
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)


def linux(path):
    value = str(Path(path).resolve()).replace('\\', '/')
    return '/mnt/' + value[0].lower() + value[2:] if os.name == 'nt' else value


def run(command):
    prefix = ['wsl', '-d', 'Ubuntu', '--exec', 'env', 'TMPDIR=' + linux(args.out)] if os.name == 'nt' else []
    result = subprocess.run(prefix + command, text=True, capture_output=True)
    if result.returncode:
        raise SystemExit(result.stdout + result.stderr)
    return result


sys.path.insert(0, str(root / 'shell'))
from shell_sources import SHELL_SOURCES  # noqa: E402
sources = [root / 'tests/test_load_menu_route.c', *(root / name for name in (
    'shell_runtime_core.c', 'shell_library.c', 'shell_playlists.c', 'shell_mysettings.c', 'my_settings.c',
    'shell_analysis.c', 'shell_utility.c', 'pdb/pdb.c', 'ui/layered_wave_source.c', 'ui/layered_wave.c')),
           *(root / 'shell' / path for path in SHELL_SOURCES)]
for name, compiler, objcopy, runner, extra in [
        ('host', 'gcc', 'objcopy', [], []),
        ('arm', 'arm-linux-gnueabi-gcc', 'arm-linux-gnueabi-objcopy', ['qemu-arm', '-L', '/usr/arm-linux-gnueabi'], []),
        ('asan', 'gcc', 'objcopy', [], ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie'])]:
    common = [compiler, '-std=c11', '-O1', '-g', '-UNDEBUG', '-ffunction-sections', '-fdata-sections',
              '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-Wno-format-truncation', *extra]
    obj = linux(args.out / ('load-menu-' + name + '.o'))
    binary = linux(args.out / ('load-menu-' + name))
    run([*common, '-c', linux(sources[0]), '-o', obj])
    run([objcopy, '--remove-section=.init_array', '--remove-section=.fini_array', obj])
    run([*common, '-Wl,--gc-sections', obj, *map(linux, sources[1:]), '-pthread', '-ldl', '-lm', '-o', binary])
    print(name + ': ' + run([*runner, binary]).stdout.strip(), flush=True)
