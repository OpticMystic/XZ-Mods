"""Compile and exercise the real C engine and RGB565 renderer, plus ARM ABI gates."""
import argparse
import json
from pathlib import Path
import subprocess
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent
args.output.mkdir(parents=True, exist_ok=True)
common = [str(args.zig.resolve()), 'cc', '-O2', '-s', '-std=c11', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-DXZ_ARCADE_CLOCK_PORTABLE']
sources = [str(root / n) for n in ('arcade.c', 'render.c', 'input.c', 'clock.c', 'test_arcade.c')] + [str(root.parent / 'ui/ui_draw.c')]
exe = args.output.resolve() / 'arcade-test.exe'
subprocess.run(common + sources + ['-o', str(exe)], check=True)
result = subprocess.run([str(exe), str(args.output.resolve())], check=True, text=True, capture_output=True)
print(result.stdout)
(args.output / 'tests.txt').write_text(result.stdout)
for image in args.output.glob('*.ppm'):
    Image.open(image).save(image.with_suffix('.png'))
arm = args.output.resolve() / 'arcade-test-arm'
subprocess.run(common + ['-target', 'arm-linux-gnueabi.2.13', '-mcpu=cortex_a9'] + sources + ['-lm', '-o', str(arm)], check=True)
import sys
sys.path.insert(0, str(root.parent))
from build import inspect
metadata = inspect(arm)
metadata['physical_controls_verified'] = False
metadata['clock_packet_verified_on_device'] = False
(args.output / 'arm.json').write_text(json.dumps(metadata, indent=2) + '\n')
print('PASS ARM32 soft-float / legacy glibc / native executable build')
for target, flags in [('native-clock-test.exe', []), ('native-clock-test-arm', ['-target', 'arm-linux-gnueabi.2.13', '-mcpu=cortex_a9'])]:
    output = args.output.resolve() / target
    subprocess.run(common + flags + [str(root/'native_clock.c'), str(root/'test_native_clock.c'), '-lm', '-o', str(output)], check=True)
    if not flags: subprocess.run([str(output)], check=True)
from ui.ui_sources import UI_SOURCES
runtime = args.output.resolve() / 'arcade-runtime-test-arm'
runtime_sources = [root / 'test_runtime_arcade.c', *[root / n for n in ('arcade.c', 'render.c', 'input.c', 'clock.c', 'native_clock.c')],
                   *[root.parent / 'ui' / n for n in UI_SOURCES],
                   *[root.parent / 'ui' / n for n in ('native_touch.c', 'stem_pads.c', 'beat_jump.c', 'wave_viewport.c')],
                   root.parent / 'settings.c', root.parent / 'doom/native_bridge.c', root.parent / 'doom/wad_catalog.c', root.parent / 'ota/native_update.c', root.parent / 'audio/vendor/sha256/sha256.c']
subprocess.run([str(args.zig.resolve()), 'cc', '-target', 'arm-linux-gnueabi.2.13', '-mcpu=cortex_a9',
                '-O2', '-s', '-std=c11', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', *map(str, runtime_sources), '-pthread', '-ldl', '-lm', '-o', str(runtime)], check=True)
(args.output / 'runtime-arm.json').write_text(json.dumps(inspect(runtime), indent=2) + '\n')
print('PASS real UI runtime and prior stem/touch regression suite cross-compiled for XZ')
