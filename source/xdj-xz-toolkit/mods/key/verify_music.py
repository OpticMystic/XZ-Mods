"""Verify musical matching and the exact native metadata reader on host and ARM."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--zig',default=shutil.which('zig'));a=p.parse_args()
if not a.zig:p.error('Supply --zig')
zig=shutil.which(a.zig) or str(Path(a.zig).resolve())
with tempfile.TemporaryDirectory(prefix='xz-key-music-') as temporary:
    out=Path(temporary)
    for test,sources in [('music',['music.c','test_music.c']),('native',['music.c','native.c','test_native.c'])]:
        exe=out/(test+'.exe')
        subprocess.run([zig,'cc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',*[str(ROOT/name) for name in sources],'-lm','-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    for source in ('music.c','native.c'):
        subprocess.run([zig,'cc','-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9','-std=c11','-O2','-Wall','-Wextra','-Werror','-fPIC','-c',str(ROOT/source),'-o',str(out/(source+'.o'))],check=True)
