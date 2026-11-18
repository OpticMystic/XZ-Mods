"""Run the real Classic mixer adapter with guarded native fixtures on Linux, offline, and the real wave
transaction for 2 DECKS following the sides (test_two_pair_runtime.c)."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
sources = ['test_classic_mixer_runtime.c', '../shell_model.c', 'classic_mixer.c', 'classic_column.c',
           'extra_lane.c', 'ui_draw.c', 'layered_wave.c', 'wave_viewport.c']
pair_sources = ['test_two_pair_runtime.c', 'native_wave.c', 'wave_viewport.c', 'extra_lane.c', 'extra_detail.c',
                'layered_wave.c', 'ui_draw.c', 'route_card.c', 'cable_anim.c', 'deck_view.c']
with tempfile.TemporaryDirectory(prefix='xz-classic-mixer-') as folder:
    for name, files in (('runtime-test', sources), ('two-pair-test', pair_sources)):
        binary = Path(folder) / name
        subprocess.run(['gcc', '-O1', '-g', '-std=c11', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-Wno-misleading-indentation', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        *[str(root / f) for f in files], '-pthread', '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
