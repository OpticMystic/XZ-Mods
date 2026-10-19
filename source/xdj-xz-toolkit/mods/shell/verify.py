"""Compile and run the shell's host tests with live assertions."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from shell_sources import SHELL_SOURCES

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent
common = [str(Path(args.zig).resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror']
with tempfile.TemporaryDirectory(prefix='xz-shell-test-') as folder:
    # Every test_*.c is its own program over the whole shell, so a screen family adds a test without editing this file.
    for name, sources in {test.stem: [*SHELL_SOURCES, test.name] for test in sorted(root.glob('test_*.c'))}.items():
        output = Path(folder) / (name + '.exe')
        subprocess.run(common + ['-UNDEBUG'] + [str(root / p) for p in sources] + ['-o', str(output)], check=True)
        subprocess.run([str(output)], check=True, cwd=folder)
    negative = subprocess.run(common + ['-DNDEBUG', '-c', str(root / 'test_shell.c'), '-o', str(Path(folder) / 'negative.o')], capture_output=True, text=True)
    if negative.returncode == 0 or 'Shell acceptance requires active assertions' not in negative.stderr:
        raise RuntimeError('Missing assertion rejection')
print('PASS assertion rejection')

# The screens describe what a setting does; they never tell the DJ how to get somewhere.
NAVIGATION = ['HOLD MENU', 'STOCK SCREEN', 'TO CHANGE IT', 'PUSH, THEN', 'TURN TO MOVE', 'SHORTCUT TO CLOSE',
              'THIS BUILD CANNOT', 'TOUCH AGAIN OR PUSH', 'GO TO ']
for path in sorted([*root.glob('shell*.c'), *root.glob('shell*.h'), *root.glob('*fixtures.h'), *root.glob('preview*.c')]):
    text = path.read_text(encoding='utf-8').upper()
    found = [phrase for phrase in NAVIGATION if '"' in text and any(phrase in line and '"' in line for line in text.splitlines())]
    if found:
        raise RuntimeError(f'{path.name}: navigation wording on screen: {found}')
print('PASS no navigation instructions in screen text')
