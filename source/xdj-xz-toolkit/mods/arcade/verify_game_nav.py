"""Check encoder navigation across every game screen, then build it for the XZ."""
import argparse
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent
SOURCES = [ROOT / n for n in (
    'test_game_nav.c', 'arcade.c', 'render.c', 'input.c', 'clock.c',
    'wave_rider.c', 'wave_rider_source.c', 'wave_rider_render.c', 'wave_rider_art.c',
    'rhythm_lab.c', 'rhythm_lab_render.c', 'glyph_run.c', 'glyph_run_render.c',
    'game_motion_draw.c', 'game_feel_art.c', 'game_font.c',
)] + [ROOT.parent / 'ui/ui_draw.c']

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
common = [str(args.zig.resolve()), 'cc', '-O2', '-s', '-std=c11', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
          '-DXZ_WAVE_RIDER_SOURCE_PORTABLE', '-DXZ_ARCADE_CLOCK_PORTABLE']
host = out / 'game-nav-test.exe'
subprocess.run(common + [str(s) for s in SOURCES] + ['-o', str(host)], check=True)
result = subprocess.run([str(host), str(out)], text=True, capture_output=True)
print(result.stdout, end='')
if result.returncode:
    print(result.stderr, end='', file=sys.stderr)
    sys.exit(result.returncode)
(out / 'tests.txt').write_text(result.stdout, encoding='utf8')
for picture in out.glob('*.ppm'):
    with Image.open(picture) as image:
        image.save(picture.with_suffix('.png'))
subprocess.run(common + ['-target', 'arm-linux-gnueabi.2.13', '-mcpu=cortex_a9'] + [str(s) for s in SOURCES]
               + ['-lm', '-o', str(out / 'game-nav-test-arm')], check=True)
print('PASS game navigation ARM32 soft-float build')
