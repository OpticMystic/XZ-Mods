"""Exercise the native General menu and retain real renderer screenshots."""
import argparse
from pathlib import Path
import subprocess
from ui_sources import UI_SOURCES

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
executable = output / 'general-menu.exe'
subprocess.run([str(args.zig.resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                *[str(root / name) for name in UI_SOURCES], str(root / 'test_general_menu.c'), '-o', str(executable)], check=True)
subprocess.run([str(executable)], cwd=output, check=True)
from PIL import Image
for path in output.glob('theme-*.ppm'):
    with Image.open(path) as frame:
        frame.save(path.with_suffix('.png'))
    path.unlink()
print('Renderer screenshots:', output)
