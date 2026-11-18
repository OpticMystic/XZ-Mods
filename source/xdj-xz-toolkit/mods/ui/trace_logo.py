"""Trace the owner's XZ Mods mark (a soft, noisy bitmap) into flat vector outlines.

Run once when the owner supplies new artwork:

    python mods/ui/trace_logo.py mods/ui/assets/xz-mods-source.png mods/ui/assets/xz-mods-mark.svg

The output SVG holds only straight-edged polygons in two flat colours (the XZ in yellow #f3e900, MODS
and the address in white) on black. It is the input of build_logo.py, which renders every size the
unit uses. Edit the SVG by hand or re-trace; never scale the source bitmap down for the unit.

Method: each colour's coverage is denoised, enlarged 4x with a smooth filter and cut at half
coverage, which puts the edge where the generator's soft edge crosses 50 %. The outlines are then
simplified to polygons (Douglas-Peucker, 0.6 source px), and edges within 2 degrees of horizontal or
vertical are snapped straight, so the squared-off letters stay square at small sizes.
Needs numpy, Pillow and OpenCV (opencv-python); only this tracing step needs OpenCV.
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

# The XZ letters are traced from the source's red (median interior (250, 2, 1)) and filled with the old
# XZ Mods yellow: the owner chose the new mark with a yellow XZ on 2026-11-17.
XZ = '#f3e900'
WHITE = '#ffffff'  # source white interior is (254, 253, 253): paper white
BLACK = '#000000'  # source black interior is (0, 0, 0)
UP = 4             # trace on a 4x grid
EPSILON = 0.6      # simplification tolerance, source pixels
SNAP_DEGREES = 2.0
MIN_AREA = 12.0    # drop specks under this many source px^2


def coverage(rgb: np.ndarray) -> dict[str, np.ndarray]:
    r, g, b = (rgb[..., i].astype(np.float32) for i in range(3))
    low = np.minimum(np.minimum(r, g), b)
    return {
        XZ: np.clip((r - np.maximum(g, b)) / 248.0, 0, 1),
        WHITE: np.clip(low / 253.0, 0, 1),
    }


def snap(points: np.ndarray) -> np.ndarray:
    """Make near-axis edges exactly axis-aligned by averaging their shared coordinate."""
    pts = points.astype(np.float64).copy()
    n = len(pts)
    for _ in range(3):
        for i in range(n):
            a, b = pts[i], pts[(i + 1) % n]
            dx, dy = b[0] - a[0], b[1] - a[1]
            length = math.hypot(dx, dy)
            if length < 2.0:
                continue
            angle = math.degrees(math.atan2(abs(dy), abs(dx)))
            if angle < SNAP_DEGREES:
                y = (a[1] + b[1]) / 2
                pts[i][1] = pts[(i + 1) % n][1] = y
            elif angle > 90 - SNAP_DEGREES:
                x = (a[0] + b[0]) / 2
                pts[i][0] = pts[(i + 1) % n][0] = x
    return pts


def trace(mask: np.ndarray) -> list[np.ndarray]:
    smooth = cv2.medianBlur((mask * 255).astype(np.uint8), 5)
    big = cv2.resize(smooth, None, fx=UP, fy=UP, interpolation=cv2.INTER_CUBIC)
    big = cv2.GaussianBlur(big, (0, 0), UP * 0.6)
    _, binary = cv2.threshold(big, 127, 255, cv2.THRESH_BINARY)
    contours, _ = cv2.findContours(binary, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
    polygons = []
    for contour in contours:
        if cv2.contourArea(contour) < MIN_AREA * UP * UP:
            continue
        simple = cv2.approxPolyDP(contour, EPSILON * UP, True)[:, 0, :]
        # Pixel-centre contour coordinates: shift by half a pixel to land on the coverage edge.
        points = (simple.astype(np.float64) + 0.5) / UP
        polygons.append(snap(points))
    return polygons


def svg(width: int, height: int, layers: dict[str, list[np.ndarray]]) -> str:
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
           f'viewBox="0 0 {width} {height}">',
           f'<!-- Traced by mods/ui/trace_logo.py from the owner\'s XZ Mods mark. Polygons only: '
           f'each path is M/L/Z, filled even-odd. -->',
           f'<rect width="{width}" height="{height}" fill="{BLACK}"/>']
    for colour, polygons in layers.items():
        d = ' '.join('M' + ' L'.join(f'{x:.2f},{y:.2f}' for x, y in p) + ' Z' for p in polygons)
        out.append(f'<path fill="{colour}" fill-rule="evenodd" d="{d}"/>')
    out.append('</svg>')
    return '\n'.join(out) + '\n'


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    rgb = np.asarray(Image.open(args.source).convert('RGB'))
    layers = {colour: trace(mask) for colour, mask in coverage(rgb).items()}
    args.output.write_text(svg(rgb.shape[1], rgb.shape[0], layers), encoding='utf-8', newline='\n')
    for colour, polygons in layers.items():
        print(colour, len(polygons), 'polygons', sum(len(p) for p in polygons), 'points')


if __name__ == '__main__':
    main()
