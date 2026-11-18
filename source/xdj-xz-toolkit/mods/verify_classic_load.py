"""Run the portable classic LOAD divert regression without touching hardware."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xz-classic-load-') as folder:
    binary = Path(folder) / 'test.exe'
    subprocess.run([
        str(Path(args.zig).resolve()), 'cc', '-DXZ_CLASSIC_LOAD_PORTABLE_TEST', '-UNDEBUG',
        '-std=c11', '-Wall', '-Wextra', '-Werror',
        str(root / 'classic_load.c'), str(root / 'classic_popup.c'), str(root / 'test_classic_load.c'), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
    popup = Path(folder) / 'popup.exe'
    subprocess.run([str(Path(args.zig).resolve()), 'cc', '-UNDEBUG', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(root / 'classic_popup.c'), str(root / 'test_classic_popup.c'), '-o', str(popup)], check=True)
    subprocess.run([str(popup)], check=True)
