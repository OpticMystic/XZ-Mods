"""Tempo-first SYNC regressions, with real worker/PCM code and synthetic native inputs."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', required=True, type=Path)
parser.add_argument('--zig', required=True)
parser.add_argument('--arch', choices=['host', 'all'], default='all')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('fixtures', root / 'extra_player_fixtures.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
data = out / 'data'
data.mkdir(exist_ok=True)
fixture.fixtures(data)
sources = [root / n for n in ['extra_player.c','key/keyshift.c','key/music.c','key/control.c', 'deck_select.c', 'anlz_extra.c', 'shell_analysis.c',
    'ui/layered_wave_source.c', 'ui/layered_wave.c', 'audio/stem_media.c', 'audio/stem_mp3.c',
    'audio/stem_decode.c', 'tests/test_sync_engine.c']]
receipt = []

def linux(path):
    value = path.resolve().as_posix()
    return '/mnt/' + value[0].lower() + value[2:]

def run(label, command, cwd=out):
    result = subprocess.run(list(map(str, command)), cwd=cwd, capture_output=True, timeout=300)
    (out / (label + '.stdout')).write_bytes(result.stdout)
    (out / (label + '.stderr')).write_bytes(result.stderr)
    receipt.append({'label': label, 'command': list(map(str, command)), 'exit': result.returncode})
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2))
    print(label, result.returncode, result.stdout.decode(errors='replace')[-1500:], result.stderr.decode(errors='replace')[-1500:], flush=True)
    if result.returncode:
        raise SystemExit(result.returncode)

flags = ['-std=c11', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-DXZ_XP_TEST_RENDER_HOOK']
binary = out / 'engine.exe'
run('zig-build', [args.zig, 'cc', *flags, '-fsanitize=undefined', '-I' + str(root), *sources, '-lm', '-o', binary])
run('zig-run', [binary, data])
if args.arch == 'all':
    for arch, compiler in [('asan', 'gcc'), ('arm', 'arm-linux-gnueabi-gcc')]:
        binary = out / ('engine-' + arch)
        options = ['-Wno-misleading-indentation', '-pthread']
        options += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie'] if arch == 'asan' else ['-mcpu=cortex-a9', '-mfpu=vfpv3', '-mfloat-abi=softfp']
        run(arch + '-build', ['wsl', '-d', 'Ubuntu', '--exec', compiler, *flags, *options,
             '-I' + linux(root), *map(linux, sources), '-lm', '-o', linux(binary)])
        runner = ['qemu-arm', '-cpu', 'cortex-a9', '-L', '/usr/arm-linux-gnueabi'] if arch == 'arm' else []
        run(arch + '-run', ['wsl', '-d', 'Ubuntu', '--exec', *runner, linux(binary), linux(data)])
