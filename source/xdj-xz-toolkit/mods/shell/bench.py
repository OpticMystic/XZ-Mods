"""Build and run the shell's host benchmarks (bench_*.c) at -O2. Never connects to the XZ."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from shell_sources import SHELL_SOURCES

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xz-shell-bench-') as folder:
    for bench in sorted(root.glob('bench_*.c')):
        output = Path(folder) / (bench.stem + '.exe')
        subprocess.run([str(Path(args.zig).resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        *(str(root / p) for p in SHELL_SOURCES), str(bench), '-o', str(output)], check=True)
        print(bench.name, flush=True)
        subprocess.run([str(output)], check=True)
