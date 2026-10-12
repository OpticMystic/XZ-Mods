"""Render the key strip and browse header on a real controller frame with the production C renderer.

The frame is a PNG of the actual LCD (800x480). It is converted to RGB565, the C test
draws into it, checks the reserved bounds, and writes previews back as PNG.
Compiler: Windows Zig (--zig) or host gcc with sanitizers (--gcc, e.g. under WSL).
"""
from pathlib import Path
import argparse, struct, subprocess, tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--frame', type=Path, required=True, help='800x480 PNG of the controller screen')
g = p.add_mutually_exclusive_group(required=True)
g.add_argument('--zig', type=Path)
g.add_argument('--gcc', action='store_true')
p.add_argument('--output', type=Path)
p.add_argument('--kind', choices=('play','browse'), required=True)
a = p.parse_args()
root = Path(__file__).resolve().parent
sources = [root / 'test_key_strip.c', root / 'key_strip.c', root / 'ui_draw.c']


def to_raw(png, raw):
    from PIL import Image
    image = Image.open(png).convert('RGB')
    assert image.size == (800, 480), image.size
    raw.write_bytes(b''.join(struct.pack('<H', (r >> 3) << 11 | (g >> 2) << 5 | b >> 3) for r, g, b in image.getdata()))


def to_png(raw, png):
    from PIL import Image
    data = raw.read_bytes()
    pixels = [struct.unpack_from('<H', data, i)[0] for i in range(0, len(data), 2)]
    Image.frombytes('RGB', (800, 480), bytes(c for p in pixels for c in (
        (p >> 11) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31))).save(png)


with tempfile.TemporaryDirectory(prefix='xz-key-strip-') as folder:
    folder = Path(folder)
    # A pre-converted .rgb565 frame avoids needing PIL on the gcc host.
    raw = a.frame if a.frame.suffix == '.rgb565' else folder / 'frame.rgb565'
    if raw is not a.frame:
        to_raw(a.frame, raw)
    if a.zig:
        exe = folder / 'key-strip.exe'
        subprocess.run([str(a.zig.resolve()), 'cc', '-O2', '-std=c11', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        *map(str, sources), '-o', str(exe)], check=True)
    else:
        exe = folder / 'key-strip'
        subprocess.run(['gcc', '-std=c11', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', *map(str, sources), '-lm', '-o', str(exe)], check=True)
    if a.output:
        a.output.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(exe), str(raw), '1' if a.kind=='play' else '2', *([str(a.output.resolve())] if a.output else [])], check=True)
if a.output:
    for raw_file in a.output.glob('*.raw'):
        to_png(raw_file, raw_file.with_suffix('.png'))
        raw_file.unlink()
    print(a.output.resolve())
