"""Render the XZ Mods mark at every size the unit uses, from the vector master.

    python mods/ui/build_logo.py            # rewrite logo_assets.h and the PNG masters
    python mods/ui/build_logo.py --check    # fail if they differ from what the SVG gives
    python mods/ui/build_logo.py --preview <folder>   # also write each use at 1x and 3x

Input: assets/xz-mods-mark.svg (polygons from trace_logo.py: yellow XZ, white MODS, on black).
Outputs:
  logo_assets.h           the built-in logo the runtime draws (mark without the address line):
                          the empty PLAY screen's 160 px square (the mark small, all in the shell's grey),
                          112 px for the ABOUT page (the mark's own colours)
  assets/xz-mods.png      the full lockup, 1536 x 1024 on black (master for the desktop app and packs)
  assets/xz-mods-mark.png the mark without the address line, 1024 x 1024 on black (square slots)
The boot splash and the stock empty-deck banner are composed here too (splash_image, banner_image)
and written by build_branding_assets.py.

Every size is rendered straight from the polygons with 16 x 16 samples per pixel (area coverage), so
edges are exact at that size; nothing is scaled from another bitmap. Colours are converted to the
screen's RGB565 by rounding, greys stay neutral, and backgrounds match what the drawing code fills.
Needs numpy and Pillow.
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
SVG = HERE / 'assets' / 'xz-mods-mark.svg'
HEADER = HERE / 'logo_assets.h'
FONT = HERE / 'fonts' / 'BarlowSemiCondensed-Medium.ttf'
ADDRESS_TOP = 895          # source y: everything below is the address line and its ornaments
SAMPLES = 16               # per pixel, per axis
ABOUT_PANEL = 0x0a0d15     # ui_about.c fills its panel with this before the logo


# ---------- RGB565, as the unit stores it ----------

def truncate565(rgb888: int) -> int:
    """What the C drawing code (rgb() in ui_draw.c) makes of a 0xRRGGBB colour."""
    return ((rgb888 >> 8) & 0xf800) | ((rgb888 >> 5) & 0x7e0) | ((rgb888 >> 3) & 31)


def expand565(v: int) -> tuple[int, int, int]:
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return round(r * 255 / 31), round(g * 255 / 63), round(b * 255 / 31)


def to565(rgb: np.ndarray) -> np.ndarray:
    """Round each channel to the nearest code. Neutral greys keep green and red/blue on the same level,
    so white-on-black edges get no green or magenta cast."""
    c = rgb.astype(np.float64)
    r5 = np.rint(c[..., 0] * 31 / 255)
    g6 = np.rint(c[..., 1] * 63 / 255)
    b5 = np.rint(c[..., 2] * 31 / 255)
    grey = (np.abs(c[..., 0] - c[..., 1]) <= 1) & (np.abs(c[..., 1] - c[..., 2]) <= 1)
    level = np.rint(c[..., 1] * 31 / 255)
    r5 = np.where(grey, level, r5)
    b5 = np.where(grey, level, b5)
    g6 = np.where(grey, np.rint(level * 63 / 31), g6)
    return ((r5.astype(np.uint16) << 11) | (g6.astype(np.uint16) << 5) | b5.astype(np.uint16))


def from565(v: np.ndarray) -> np.ndarray:
    v = v.astype(np.uint32)
    return np.dstack([np.rint(((v >> 11) & 31) * 255 / 31), np.rint(((v >> 5) & 63) * 255 / 63),
                      np.rint((v & 31) * 255 / 31)]).astype(np.uint8)


# ---------- the vector master ----------

@dataclass
class Layer:
    colour: tuple[int, int, int]
    polygons: list[np.ndarray]


def load(path: Path = SVG) -> list[Layer]:
    text = path.read_text(encoding='utf-8')
    layers = []
    for fill, d in re.findall(r'<path fill="#([0-9a-f]{6})" fill-rule="evenodd" d="([^"]*)"', text):
        polygons = []
        for part in d.split('Z'):
            points = re.findall(r'(-?[\d.]+),(-?[\d.]+)', part)
            if points:
                polygons.append(np.array(points, dtype=np.float64))
        layers.append(Layer(tuple(int(fill[i:i + 2], 16) for i in (0, 2, 4)), polygons))
    if not layers:
        raise ValueError(f'{path}: no polygon layers')
    return layers


def select(layers: list[Layer], variant: str) -> list[Layer]:
    """'full' is the whole lockup; 'mark' drops the address line (for small and square slots)."""
    if variant == 'full':
        return layers
    if variant != 'mark':
        raise ValueError(variant)
    return [Layer(l.colour, [p for p in l.polygons if p[:, 1].min() < ADDRESS_TOP]) for l in layers]


def bounds(layers: list[Layer]) -> tuple[float, float, float, float]:
    points = np.concatenate([p for l in layers for p in l.polygons])
    return points[:, 0].min(), points[:, 1].min(), points[:, 0].max(), points[:, 1].max()


def samples(w: int, h: int) -> int:
    """Samples per pixel and axis: 16 for the unit's small logos, fewer for large masters (same result to the eye)."""
    side = max(w, h)
    return SAMPLES if side <= 256 else 8 if side <= 1024 else 4


def coverage(polygons: list[np.ndarray], scale: float, ox: float, oy: float, w: int, h: int) -> np.ndarray:
    """Even-odd area coverage of the polygons on a w x h pixel grid. A source point p lands at p*scale+o."""
    n = samples(w, h)
    big = np.zeros((h * n, w * n), dtype=bool)
    for polygon in polygons:
        mask = Image.new('1', (w * n, h * n), 0)
        # Sample at sub-pixel centres: PIL fills a pixel when its integer corner is inside.
        points = [((x * scale + ox) * n - 0.5, (y * scale + oy) * n - 0.5) for x, y in polygon]
        ImageDraw.Draw(mask).polygon(points, fill=1)
        big ^= np.asarray(mask, dtype=bool)
    return big.reshape(h, n, w, n).mean(axis=(1, 3))


def render(layers: list[Layer], w: int, h: int, box: tuple[int, int, int, int],
           background: tuple[int, int, int]) -> tuple[np.ndarray, np.ndarray]:
    """Fit the layers into box (x, y, width, height) of a w x h image, centred, never stretched.
    Returns float RGB (0..255) composed on background, and the total coverage (alpha)."""
    x0, y0, x1, y1 = bounds(layers)
    bx, by, bw, bh = box
    scale = min(bw / (x1 - x0), bh / (y1 - y0))
    # Whole-pixel placement of the mark's box keeps the same edges sharp at every use.
    ox = bx + round((bw - (x1 - x0) * scale) / 2) - x0 * scale
    oy = by + round((bh - (y1 - y0) * scale) / 2) - y0 * scale
    rgb = np.empty((h, w, 3)); rgb[:] = background
    alpha = np.zeros((h, w))
    for layer in layers:
        a = coverage(layer.polygons, scale, ox, oy, w, h)
        rgb += a[..., None] * (np.array(layer.colour, dtype=np.float64) - rgb)
        alpha += a
    return rgb, np.clip(alpha, 0, 1)


def image(layers: list[Layer], w: int, h: int, box, background=(0, 0, 0)) -> Image.Image:
    rgb, _ = render(layers, w, h, box, background)
    return Image.fromarray(np.rint(rgb).astype(np.uint8))


def rgba(layers: list[Layer], w: int, h: int, box) -> Image.Image:
    """The mark alone, unpremultiplied, for pasting onto any background."""
    out = np.zeros((h, w, 4))
    base, alpha = render(layers, w, h, box, (0, 0, 0))
    safe = np.where(alpha > 0, alpha, 1)[..., None]
    out[..., :3] = np.clip(base / safe, 0, 255)
    out[..., 3] = alpha * 255
    return Image.fromarray(np.rint(out).astype(np.uint8), 'RGBA')


# ---------- the runtime's built-in logo ----------

@dataclass
class Art:
    name: str
    side: int
    pad: int         # around the mark inside the square: a larger pad draws the mark smaller in the same slot
    background: int  # 0xRRGGBB the drawing code fills around the band
    ink: int | None = None  # 0xRRGGBB for the whole mark, or None for the artwork's own colours

    def pixels(self, layers: list[Layer]) -> np.ndarray:
        bg = expand565(truncate565(self.background))
        if self.ink is not None:
            ink = tuple((self.ink >> shift) & 255 for shift in (16, 8, 0))
            layers = [Layer(ink, l.polygons) for l in layers]
        rgb, _ = render(select(layers, 'mark'), self.side, self.side,
                        (self.pad, self.pad, self.side - 2 * self.pad, self.side - 2 * self.pad), bg)
        out = to565(rgb)
        out[np.all(np.abs(rgb - bg) < 0.5, axis=2)] = truncate565(self.background)
        return out


# The empty PLAY screen sits in the middle of the deck screen whenever no track is loaded, so its mark is small and
# in the shell's grey text colour (0x8c8c8c): a yellow fill there would sit beside the unit's own yellow states.
ARTS = (Art('idle', 160, 30, 0x000000, ink=0x8c8c8c), Art('about', 112, 2, ABOUT_PANEL))


def band(pixels: np.ndarray, background: int) -> tuple[int, int]:
    rows = np.nonzero(np.any(pixels != background, axis=1))[0]
    return int(rows[0]), int(rows[-1]) - int(rows[0]) + 1


def header(layers: list[Layer]) -> str:
    out = ['/* SPDX-License-Identifier: MIT */',
           '/* Generated by mods/ui/build_logo.py from assets/xz-mods-mark.svg; run it again after changing the SVG. */',
           '#ifndef XZ_LOGO_ASSETS_H', '#define XZ_LOGO_ASSETS_H', '#include <stdint.h>',
           '/* The built-in XZ Mods logo: a side x side square in RGB565. Only the rows from top to top+rows-1 are',
           ' * stored; every other row of the square is background (0xRRGGBB, filled with xz_rect). */',
           'struct xz_logo_art { const uint16_t *band; int side, top, rows; uint32_t background; };']
    for art in ARTS:
        pixels = art.pixels(layers)
        bg = truncate565(art.background)
        top, rows = band(pixels, bg)
        values = pixels[top:top + rows].ravel()
        out.append(f'static const uint16_t xz_logo_{art.name}_band[{art.side}*{rows}]={{')
        for i in range(0, len(values), 24):
            out.append(','.join(f'0x{v:04x}' for v in values[i:i + 24]) + ',')
        out.append('};')
        out.append(f'static const struct xz_logo_art xz_logo_{art.name}='
                   f'{{xz_logo_{art.name}_band,{art.side},{top},{rows},0x{art.background:06x}}};')
    out.append('#endif')
    return '\n'.join(out) + '\n'


# ---------- the stock player's images (boot splash, empty-deck banner) ----------

def _font(size: int):
    return ImageFont.truetype(str(FONT), size)


def splash_image(layers: list[Layer] | None = None) -> Image.Image:
    """Image 1446, 800 x 480, shown while the player starts: the full lockup."""
    layers = layers or load()
    # The lockup between the corner tab and the LOADING line, centred on the screen.
    canvas = image(select(layers, 'full'), 800, 480, (125, 50, 550, 380))
    draw = ImageDraw.Draw(canvas)
    draw.polygon([(0, 0), (190, 0), (202, 30), (0, 30)], fill=(27, 39, 52), outline=(168, 206, 255))
    draw.text((12, 2), 'XZ MODS', font=_font(23), fill=(245, 245, 245))
    draw.line((0, 478, 799, 478), fill=(74, 101, 130), width=2)
    draw.text((684, 448), 'LOADING', font=_font(21), fill=(168, 206, 255))
    return canvas


def banner_image(layers: list[Layer] | None = None) -> Image.Image:
    """Image 1487, 560 x 92, the stock player's empty-deck logo: the mark beside the address.
    The mark already reads XZ MODS, so the text column carries only the address, in the mark's white."""
    layers = layers or load()
    canvas = Image.new('RGB', (560, 92), (0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    draw.polygon([(0, 0), (544, 0), (559, 15), (559, 91), (0, 91)], fill=(10, 17, 24), outline=(168, 206, 255))
    mark = rgba(select(layers, 'mark'), 124, 74, (0, 0, 124, 74))
    canvas.paste(mark, (14, 9), mark)
    draw.line((154, 12, 154, 79), fill=(65, 89, 114), width=1)
    font = _font(38)
    top = draw.textbbox((0, 0), 'xzmods.com', font=font)
    draw.text((176, (92 - (top[3] - top[1])) // 2 - top[1]), 'xzmods.com', font=font, fill=(255, 255, 255))
    return canvas


def pack_rgb565(img: Image.Image) -> bytes:
    return to565(np.asarray(img.convert('RGB'))).astype('<u2').tobytes()


# ---------- command ----------

def masters(layers: list[Layer]) -> dict[Path, Image.Image]:
    return {HERE / 'assets' / 'xz-mods.png': image(select(layers, 'full'), 1536, 1024, (128, 96, 1280, 832)),
            HERE / 'assets' / 'xz-mods-mark.png': image(select(layers, 'mark'), 1024, 1024, (64, 64, 896, 896))}


def preview(layers: list[Layer], folder: Path) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    shots = {'splash-1446': Image.fromarray(from565(to565(np.asarray(splash_image(layers))))),
             'banner-1487': Image.fromarray(from565(to565(np.asarray(banner_image(layers)))))}
    for art in ARTS:
        shots[f'builtin-{art.name}-{art.side}'] = Image.fromarray(from565(art.pixels(layers)))
    for name, shot in shots.items():
        shot.save(folder / f'{name}-1x.png')
        shot.resize((shot.width * 3, shot.height * 3), Image.Resampling.NEAREST).save(folder / f'{name}-3x.png')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='compare instead of writing')
    parser.add_argument('--preview', type=Path)
    args = parser.parse_args()
    layers = load()
    text = header(layers)
    if args.check:
        stale = [] if HEADER.read_text(encoding='utf-8') == text else [HEADER.name]
        for path, img in masters(layers).items():
            if not path.is_file() or not np.array_equal(np.asarray(Image.open(path).convert('RGB')), np.asarray(img)):
                stale.append(path.name)
        if stale:
            print('Out of date with xz-mods-mark.svg: ' + ', '.join(stale) + '. Run mods/ui/build_logo.py.')
            return 1
        print('PASS logo assets match xz-mods-mark.svg')
        return 0
    HEADER.write_text(text, encoding='ascii', newline='\n')
    for path, img in masters(layers).items():
        img.save(path, optimize=True)
    if args.preview:
        preview(layers, args.preview)
    for art in ARTS:
        top, rows = band(art.pixels(layers), truncate565(art.background))
        print(f'{art.name}: {art.side} px square, rows {top}..{top + rows - 1}, {art.side * rows * 2} bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
