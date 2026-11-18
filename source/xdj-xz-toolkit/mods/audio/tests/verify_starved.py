"""Deterministic real-mixer seek starvation and absolute loop-window regressions."""
from pathlib import Path
import argparse
import os
import shlex
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig')
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--sanitizer', choices=['address', 'thread'])
    args = parser.parse_args()
    compiler = [args.zig, 'cc'] if args.zig else shlex.split(args.cc)
    flags = ['-std=c11', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation']
    if args.sanitizer:
        flags += ['-fsanitize=' + ('address,undefined' if args.sanitizer == 'address' else 'thread'), '-fno-sanitize-recover=all']
    with tempfile.TemporaryDirectory(prefix='xz-seek-') as folder:
        binary = Path(folder) / 'seek.exe'
        subprocess.run(compiler + flags + [str(HERE/'test_overcue_starved.c'), '-pthread', '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=30)

if __name__ == '__main__':
    main()
