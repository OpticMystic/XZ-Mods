"""Run lossless screen codec bounds/parity tests, optionally with captured BMPs."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xz-screen-codec-') as folder:
    binary = Path(folder) / 'screen-codec'
    subprocess.run(['gcc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(root / 'test_screen_codec.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary), *sys.argv[1:]], check=True, timeout=60)
