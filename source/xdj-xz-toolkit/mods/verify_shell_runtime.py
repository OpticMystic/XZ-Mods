"""Compile and run the FLAGSHIP shell runtime's host test (pure core and library lists) with live assertions.

Usage: python mods/verify_shell_runtime.py --zig PATH/zig.exe
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
args = parser.parse_args()
common = [str(Path(args.zig).resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror']
sources = [ROOT / 'shell' / name for name in SHELL_SOURCES] + [ROOT / name for name in
           ('shell_runtime_core.c', 'shell_library.c', 'pdb/pdb.c', 'test_shell_runtime.c')]
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
print('PASS assertion rejection')
