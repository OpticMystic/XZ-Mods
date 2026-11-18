"""Build the actual native game engines as a locally playable browser preview."""
import argparse
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
sources = ('game_preview', 'wave_rider', 'wave_rider_source', 'wave_rider_render', 'wave_rider_art',
           'rhythm_lab', 'rhythm_lab_render', 'glyph_run', 'glyph_run_render',
           'game_motion_draw', 'game_feel_art', 'game_font')
exports = ('preview_pixels', 'preview_stats', 'preview_start', 'preview_tick',
           'preview_jog', 'preview_action', 'preview_pad', 'preview_touch', 'preview_retry')
command = [str(args.zig.resolve()), 'cc', '-target', 'wasm32-wasi', '-O2', '-DNDEBUG',
           '-std=c11', '-Wall', '-Wextra', '-Werror', '-DXZ_WAVE_RIDER_SOURCE_PORTABLE',
           '-mexec-model=reactor']
command += [str(root / (name + '.c')) for name in sources]
command += [str(root.parent / 'ui/ui_draw.c'), '-lm', '-Wl,-z,stack-size=1048576',
            '-Wl,--initial-memory=16777216', '-Wl,--max-memory=67108864']
command += ['-Wl,--export=' + name for name in exports]
command += ['-o', str(args.output.resolve() / 'games.wasm')]
subprocess.run(command, check=True)
shutil.copyfile(root / 'game_preview.html', args.output / 'index.html')
print('Built native game preview:', args.output.resolve())
