"""Compile and run the deck-select host test (SHIFT twice, router, tempo pickup) with live assertions.

Usage: python mods/verify_deck_select.py --zig PATH/zig.exe
Pure host code; nothing touches a device.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
args = parser.parse_args()
common = [str(Path(args.zig).resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror']
with tempfile.TemporaryDirectory(prefix='xz-deck-select-') as folder:
    folder = Path(folder)
    binary = folder / 'test-deck-select.exe'
    subprocess.run(common + ['-UNDEBUG', str(ROOT / 'deck_select.c'), str(ROOT / 'test_deck_select.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, cwd=folder)
    idle = folder / 'test-idle-jog-shift.exe'
    subprocess.run(common + ['-UNDEBUG', str(ROOT / 'deck_select.c'), str(ROOT / 'tests/test_idle_jog_shift.c'), '-o', str(idle)], check=True)
    subprocess.run([str(idle)], check=True, cwd=folder)
    negative = subprocess.run(common + ['-DNDEBUG', '-c', str(ROOT / 'test_deck_select.c'), '-o', str(folder / 'negative.o')],
                              capture_output=True, text=True)
    if negative.returncode == 0 or 'Deck select acceptance requires active assertions' not in negative.stderr:
        raise RuntimeError('Missing assertion rejection')
print('PASS deck select; assertion rejection')
