"""Actual streaming mixer fallback with no covered prepared window."""
from pathlib import Path
import subprocess,tempfile
HERE=Path(__file__).resolve().parent;ROOT=HERE.parent
with tempfile.TemporaryDirectory(prefix='xz-starved-') as folder:
 binary=Path(folder)/'test'
 sources=[HERE/'test_overcue_starved.c',ROOT/'overcue_file.c',ROOT/'stem_decode.c',ROOT/'foreign_stems.c',ROOT/'stem_media.c',ROOT/'stem_mp3.c',ROOT/'vendor/miniz/miniz_tinfl.c',ROOT/'vendor/sha256/sha256.c']
 subprocess.run(['cc','-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all',*map(str,sources),'-pthread','-lm','-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
