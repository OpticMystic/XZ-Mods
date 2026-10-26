"""Run the real Classic mixer adapter with guarded native fixtures on Linux, offline."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
sources = ['test_classic_mixer_runtime.c', '../shell_model.c', 'classic_mixer.c', 'classic_column.c',
           'extra_lane.c', 'ui_draw.c', 'layered_wave.c', 'wave_viewport.c']
with tempfile.TemporaryDirectory(prefix='xz-classic-mixer-') as folder:
    binary = Path(folder) / 'runtime-test'
    subprocess.run(['gcc', '-O1', '-g', '-std=c11', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                    '-Wno-misleading-indentation', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    *[str(root / name) for name in sources], '-pthread', '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
