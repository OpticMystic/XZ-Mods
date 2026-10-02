"""Render what the XZ would draw for a track before and after builder/three_band.py, beside a layered
CDJ-3000 style reference drawn from the 3-band data, so colour tuning is judged by eye.

Detail lanes: one PWV5 entry per pixel, one solid colour per column at the XZ's 3-bit levels (7 -> 230),
mirrored, 2 px per height unit, black background. Preview: 1200 PWV4 columns, bottom anchored, height
from the peak colour byte and hue from bytes 3-5 (Beat Link's model of the RGB preview).
"""
import argparse
from pathlib import Path
import sys
import numpy as np
from PIL import Image, ImageDraw

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('folders', type=Path, nargs='+', help='folders holding ANLZ0000.EXT and ANLZ0000.2EX')
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--window', type=int, default=1200, help='detail entries per lane (8 s at 150 per second)')
parser.add_argument('--scales', help='low,mid,high height scales to try instead of three_band.LAYERS')
args = parser.parse_args()
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from builder import three_band

if args.scales:
    for band, scale in zip(('low', 'mid', 'high'), map(float, args.scales.split(','))):
        three_band.LAYERS[band] = (three_band.LAYERS[band][0], scale)
    three_band.mix.cache_clear()
    three_band.band_color.cache_clear()

LANE, PREVIEW, LABEL = 132, 64, 16


def entries(data, tag, size):
    start, count = three_band.waves(data)[tag]
    return np.frombuffer(data[start:start + count * size], np.uint8).reshape(count, size)


def xz_detail(ext, first, width):
    raw = entries(ext, b'PWV5', 2)[first:first + width]
    values = raw[:, 0].astype(np.int64) << 8 | raw[:, 1]
    image = np.zeros((LANE, width, 3), np.uint8)
    for x, value in enumerate(values):
        colour = [((value >> shift) & 7) * 230 // 7 for shift in (13, 10, 7)]
        half = ((value >> 2) & 31) * 2
        image[LANE // 2 - half:LANE // 2 + half, x] = colour
    return image


def xz_preview(ext):
    raw = entries(ext, b'PWV4', 6)[:, 3:].astype(np.int64)
    image = np.zeros((PREVIEW, len(raw), 3), np.uint8)
    for x, colour in enumerate(raw):
        peak = colour.max()
        if peak:
            image[PREVIEW - peak * PREVIEW // 127:, x] = colour * 255 // peak
    return image


def layered(bands, height, mirrored):
    """Beat Link's CDJ-3000 drawing: outer low or mid, brown overlap, white high core."""
    order = three_band.BAND_ORDER
    scaled = np.stack([bands[:, order.index(band)] * three_band.LAYERS[band][1] for band in ('low', 'mid', 'high')], 1)
    unit = (height // 2 if mirrored else height) / max(1e-9, np.percentile(scaled.max(1), 99.5))
    image = np.zeros((height, len(bands), 3), np.uint8)
    for x, (low, mid, high) in enumerate(scaled * unit):
        outer = min(max(low, mid), height)
        layers = ((outer, three_band.LAYERS['low' if low > mid else 'mid'][0]),
                  (min(low, mid), three_band.OVERLAP), (high, three_band.LAYERS['high'][0]))
        for size, colour in layers:
            size = int(min(size, height // 2 if mirrored else height))
            if mirrored:
                image[height // 2 - size:height // 2 + size, x] = colour
            elif size:
                image[height - size:, x] = colour
    return image


def label(text, width):
    image = Image.new('RGB', (width, LABEL), (40, 40, 40))
    ImageDraw.Draw(image).text((4, 2), text, fill=(220, 220, 220))
    return np.asarray(image)


args.out.mkdir(parents=True, exist_ok=True)
for folder in args.folders:
    original = (folder / 'ANLZ0000.EXT').read_bytes()
    two_ex = (folder / 'ANLZ0000.2EX').read_bytes()
    adapted = three_band.adapt_ext(original, two_ex)
    detail = entries(two_ex, b'PWV7', 3).astype(np.float64)
    width = args.window
    rows = []
    for fraction in (.3, .6):
        first = int(len(detail) * fraction)
        seconds = first / 150
        rows += [label(f'{folder.name} detail at {seconds:.0f}s: (a) XZ original RGB', width), xz_detail(original, first, width),
                 label('(b) XZ adapted 3-band', width), xz_detail(adapted, first, width),
                 label('(c) CDJ-3000 layered reference from PWV7', width), layered(detail[first:first + width], LANE, True)]
    preview = entries(two_ex, b'PWV6', 3).astype(np.float64)
    rows += [label('preview (a) XZ original RGB', 1200), xz_preview(original),
             label('preview (b) XZ adapted 3-band', 1200), xz_preview(adapted),
             label('preview (c) CDJ-3000 layered reference from PWV6', 1200), layered(preview, PREVIEW, False)]
    rows = [np.pad(row, ((0, 0), (0, max(width, 1200) - row.shape[1]), (0, 0))) for row in rows]
    target = args.out / f'{folder.name}.png'
    Image.fromarray(np.vstack(rows)).save(target)
    print(target)
