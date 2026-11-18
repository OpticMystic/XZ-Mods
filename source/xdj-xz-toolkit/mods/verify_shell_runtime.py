"""Compile and run the FLAGSHIP shell runtime's host test (pure core and library lists) with live assertions.

Usage: python mods/verify_shell_runtime.py --zig PATH/zig.exe [--bench]
--bench also builds and runs bench_shell_frame.c (PLAY render cost and playhead smoothness, host -O2).
The pdb fixture comes from mods/pdb/make_fixture.py; nothing touches a device.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'shell'))
sys.path.insert(0, str(ROOT / 'pdb'))
from shell_sources import SHELL_SOURCES  # noqa: E402
from make_fixture import make  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
parser.add_argument('--bench', action='store_true')
args = parser.parse_args()
common = [str(Path(args.zig).resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror']
sources = [ROOT / 'shell' / name for name in SHELL_SOURCES] + [ROOT / name for name in
           ('shell_runtime_core.c', 'shell_library.c', 'shell_playlists.c', 'pdb/pdb.c', 'test_shell_runtime.c')]
with tempfile.TemporaryDirectory(prefix='xz-shell-runtime-') as folder:
    folder = Path(folder)
    fixture = folder / 'fixture.pdb'
    fixture.write_bytes(make())
    binary = folder / 'test-shell-runtime.exe'
    subprocess.run(common + ['-UNDEBUG'] + [str(p) for p in sources] + ['-o', str(binary)], check=True)
    subprocess.run([str(binary), str(fixture)], check=True, cwd=folder)
    negative = subprocess.run(common + ['-DNDEBUG', '-c', str(ROOT / 'test_shell_runtime.c'), '-o', str(folder / 'negative.o')],
                              capture_output=True, text=True)
    if negative.returncode == 0 or 'Shell runtime acceptance requires active assertions' not in negative.stderr:
        raise RuntimeError('Missing assertion rejection')
    utility = folder / 'test-shell-utility.exe'
    subprocess.run(common + ['-UNDEBUG'] + [str(ROOT / 'shell' / name) for name in SHELL_SOURCES] +
                   [str(ROOT / name) for name in ('shell_runtime_core.c', 'shell_utility.c', 'test_shell_utility.c')] +
                   ['-o', str(utility)], check=True)
    subprocess.run([str(utility)], check=True, cwd=folder)
    negative = subprocess.run(common + ['-DNDEBUG', '-c', str(ROOT / 'test_shell_utility.c'), '-o', str(folder / 'negative-utility.o')],
                              capture_output=True, text=True)
    if negative.returncode == 0 or 'Shell utility acceptance requires active assertions' not in negative.stderr:
        raise RuntimeError('Missing utility assertion rejection')
    analysis = folder / 'test-shell-analysis.exe'
    subprocess.run(common + ['-UNDEBUG'] + [str(ROOT / name) for name in
                   ('shell_analysis.c', 'ui/layered_wave_source.c', 'ui/layered_wave.c', 'test_shell_analysis.c')] +
                   ['-o', str(analysis)], check=True)
    subprocess.run([str(analysis)], check=True, cwd=folder)
    if args.bench:
        bench = folder / 'bench-shell-frame.exe'
        subprocess.run(common + [str(ROOT / 'shell' / name) for name in SHELL_SOURCES] +
                       [str(ROOT / name) for name in ('shell_runtime_core.c', 'shell_library.c', 'shell_playlists.c', 'pdb/pdb.c', 'bench_shell_frame.c')] +
                       ['-o', str(bench)], check=True)
        subprocess.run([str(bench), '600'], check=True)
print('PASS assertion rejection')
