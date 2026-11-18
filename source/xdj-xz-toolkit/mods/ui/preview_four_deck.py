"""Composite the four-deck classic wave surface into a real play-screen capture.

The capture must show stem rows in BELOW / HALF layout (stems_overlay 2), as
E:/Codex/xz-four-deck-20261016/live-screen.png does. Its two shrunk native lanes are
resampled back to the 132-row native surface, run through the real C layout and
DECK 3/4 composition the runtime draws (xz_four_frame_draw: lanes, deck tags,
detail panel) for each fixture scenario, and pasted back at the surface's
native offset (131,18). The mod header strip that overlaps the surface top is
restored on top, as on the device (STRIP below). Nothing contacts the device.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

from PIL import Image

X, Y, W, H = 131, 18, 536, 268
HALF_LANE = 108
# The mod header strip (metrics readout and METRICS button) the receiver draws over the surface's
# top rows. The capture has it baked into native DECK 1's cue band; orders that move that lane down
# would carry it along, so those pixels are rebuilt as the empty cue band they cover.
STRIP = (282, 0, 672, 24)


def lane_row(row, height):
    if row < 10:
        return row
    if row >= height - 10:
        return 132 - (height - row)
    return 10 + (row - 10) * 112 // (height - 20)


def native_surface(screen):
    """Invert the BELOW / HALF row map: each native row takes the nearest shown row."""
    rows = []
    for deck, lane_y in ((0, 0), (1, 132)):
        shown = [lane_row(r, HALF_LANE) for r in range(HALF_LANE)]
        for source in range(132):
            best = min(range(HALF_LANE), key=lambda r: abs(shown[r] - source))
            rows.append(Y + lane_y + best)
        if deck == 0:
            rows.extend([None] * 4)
    pixels = bytearray()
    for screen_y in rows:
        for x in range(W):
            covered = screen_y is not None and screen_y < STRIP[3] and STRIP[0] <= X + x < STRIP[2]
            r, g, b = (0, 0, 0) if screen_y is None or covered else screen.getpixel((X + x, screen_y))
            pixels += struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return bytes(pixels)


def to_image(raw):
    image = Image.new('RGB', (W, H))
    values = struct.unpack('<%dH' % (W * H), raw)
    image.putdata([(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31) for v in values])
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', required=True)
    parser.add_argument('--screen', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--bench', type=int, default=0, help='also time this many frames on the host')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    args.output.mkdir(parents=True, exist_ok=False)
    screen = Image.open(args.screen).convert('RGB')
    with tempfile.TemporaryDirectory(prefix='xz-four-preview-') as folder:
        folder = Path(folder)
        exe = folder / 'preview.exe'
        sources = ['preview_four_deck.c', 'extra_lane.c', 'extra_detail.c', 'wave_viewport.c', 'layered_wave.c', 'ui_draw.c', 'route_card.c', 'cable_anim.c']
        subprocess.run([args.zig, 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        *[str(here / s) for s in sources], '-lm', '-o', str(exe)], check=True)
        (folder / 'native.raw').write_bytes(native_surface(screen))
        subprocess.run([str(exe), str(folder / 'native.raw'), str(folder)], check=True)
        if args.bench:
            subprocess.run([str(exe), str(folder / 'native.raw'), str(folder), str(args.bench)], check=True)
        for raw in sorted(folder.glob('[0-9]*.raw')):
            frame = screen.copy()
            frame.paste(to_image(raw.read_bytes()), (X, Y))
            frame.paste(screen.crop(STRIP), STRIP[:2])
            frame.save(args.output / (raw.stem + '.png'))
            frame.crop((0, 0, 800, 310)).resize((1600, 620), Image.NEAREST).save(args.output / (raw.stem + '-zoom.png'))
    print('wrote', args.output)


if __name__ == '__main__':
    main()
