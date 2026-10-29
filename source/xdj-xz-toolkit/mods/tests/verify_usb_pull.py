"""Own-source USB recovery fixture; no firmware, device or app execution."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--arch', choices=['host', 'arm'], default='host')
a = p.parse_args()
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
sys.path.insert(0, str(root / 'pdb'))
from make_fixture import make
data = out / 'usb/PIONEER/rekordbox'
data.mkdir(parents=True)
(data / 'export.pdb').write_bytes(make())

def linux(path):
    s = Path(path).resolve().as_posix()
    return '/mnt/' + s[0].lower() + s[2:] if os.name == 'nt' else s

prefix = ['wsl', '-d', 'Ubuntu', '--exec'] if os.name == 'nt' else []
rows = []
def run(label, command):
    result = subprocess.run(prefix + command, capture_output=True, timeout=600)
    (out / (label + '.log')).write_bytes(result.stdout + result.stderr)
    rows.append(dict(label=label, command=prefix + command, exit=result.returncode))
    print(label, result.returncode, flush=True)
    if result.returncode:
        print((result.stdout + result.stderr).decode(errors='replace')[-6000:])
        raise SystemExit(result.returncode)
    return result

cc = 'gcc' if a.arch == 'host' else 'arm-linux-gnueabi-gcc'
flags = ['-std=c11', '-O1', '-g', '-UNDEBUG', '-ffunction-sections', '-fdata-sections',
         '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-Wno-format-truncation']
flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie'] if a.arch == 'host' else ['-march=armv7-a']
units = ['tests/test_usb_pull.c', 'shell_runtime_core.c', 'shell_library.c', 'shell_playlists.c',
         'shell_mysettings.c', 'my_settings.c', 'shell_analysis.c', 'shell_utility.c', 'pdb/pdb.c',
         'ui/layered_wave_source.c', 'ui/layered_wave.c', 'shell/shell.c', 'shell/shell_info.c']
sys.path.insert(0, str(root / 'shell'))
from shell_sources import SHELL_SOURCES
units = units[:-2] + [str((root / 'shell' / name).relative_to(root)) for name in SHELL_SOURCES]
inputs = {s: hashlib.sha256((root / s).read_bytes()).hexdigest() for s in [*units, 'shell_runtime.c', 'shell_media.h']}
try:
    binary = out / 'test-usb-pull'
    objects = [out / ('unit-' + str(i) + '.o') for i in range(len(units))]
    def compile_unit(i):
        run('compile-' + str(i), [cc, *flags, '-c', linux(root / units[i]), '-o', linux(objects[i])])
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(compile_unit, range(len(units))))
    run('build', [cc, *flags, '-Wl,--gc-sections', *map(linux, objects),
                  '-pthread', '-lm', '-o', linux(binary)])
    runner = [] if a.arch == 'host' else ['qemu-arm', '-L', '/usr/arm-linux-gnueabi']
    result = run('run', [*runner, linux(binary), linux(out / 'usb')])
    print(result.stdout.decode(), end='')
finally:
    (out / 'receipt.json').write_text(json.dumps(dict(arch=a.arch, inputs=inputs, commands=rows,
                                                   hardware_contacted=False), indent=2) + '\n')
