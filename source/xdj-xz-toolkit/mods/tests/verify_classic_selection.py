"""Exercise the actual runtime selection functions with the portable deck router, offline."""
import argparse
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
args = parser.parse_args()
source = (root / 'runtime.c').read_text(encoding='utf-8')
start = source.index('static int deck_select_apply(')
end = source.index('\nstatic int physical_priority(', start)
with tempfile.TemporaryDirectory(prefix='xz-classic-selection-') as temporary:
    work = Path(temporary)
    (work / 'runtime-selection.inc').write_text(source[start:end], encoding='utf-8')
    binary = work / 'selection.exe'
    subprocess.run([args.zig, 'cc', '-O2', '-std=c11', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                    '-I', str(root), '-I', str(work), str(root / 'deck_select.c'),
                    str(root / 'tests/test_classic_selection.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
