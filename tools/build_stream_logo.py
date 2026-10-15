"""Encode the existing XZ Mods logo for the 800x480 RGB565 stream cover.
Run after changing ui/assets/xz-mods.png. Requires Pillow only when regenerating.
"""
from pathlib import Path
import struct
from PIL import Image

APP = Path(__file__).resolve().parents[1]
logo = Image.open(APP / 'ui/assets/xz-mods.png').convert('RGBA')
logo.thumbnail((300, 300), Image.Resampling.LANCZOS)
canvas = Image.new('RGB', (800, 480), 'black')
canvas.paste(logo, ((800-logo.width)//2, (480-logo.height)//2), logo)
pixels = bytearray()
for r, g, b in canvas.get_flattened_data():
    pixels.extend(struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
(APP / 'src-tauri/src/stream-logo.rgb565').write_bytes(pixels)
