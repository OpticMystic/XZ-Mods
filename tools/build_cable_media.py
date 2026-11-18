"""Build the four-deck guide's cable animation from the deck's own renderer.

The XZ draws the loopback-cable explainer natively (source/.../mods/ui/cable_anim.c, from a bake of the
Blender scene). This renders the same 13 s loop, every 60 fps frame, in the app's colours with
tools/wizard_media/cable_motion.c and encodes it for the web view:

  ui/assets/wizard/loopback-cable.mp4         H.264, 60 fps, faststart, no audio
  ui/assets/wizard/loopback-cable-poster.webp the loop's first frame (its main shot), for reduced motion
  ui/assets/wizard/loopback-cable.json        what made them: the renderer's sources and their hashes

Never connects to the XZ. Run: python tools/build_cable_media.py --zig <zig.exe> [--ffmpeg ffmpeg]
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

APP = Path(__file__).resolve().parents[1]
UI = APP / 'source/xdj-xz-toolkit/mods/ui'
OUT = APP / 'ui/assets/wizard'
WIDTH, HEIGHT = 1280, 400   # 3.2:1, fills the guide's figure (about 580 to 1080 CSS px wide) edge to edge
CRF = 30


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--zig', required=True)
    ap.add_argument('--ffmpeg', default='ffmpeg')
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='xz-cable-media-') as tmp:
        tmp = Path(tmp)
        exe = tmp / 'cable_motion.exe'
        subprocess.run([str(Path(a.zig).resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', f'-I{UI}',
                        str(APP / 'tools/wizard_media/cable_motion.c'), str(UI / 'cable_anim.c'), str(UI / 'ui_draw.c'),
                        '-o', str(exe)], check=True)
        frames = tmp / 'frames'; frames.mkdir()
        subprocess.run([str(exe), str(frames), str(WIDTH), str(HEIGHT)], check=True)
        video = OUT / 'loopback-cable.mp4'
        subprocess.run([a.ffmpeg, '-y', '-loglevel', 'error', '-framerate', '60', '-i', str(frames / 'f%04d.ppm'),
                        '-c:v', 'libx264', '-preset', 'veryslow', '-tune', 'animation', '-crf', str(CRF),
                        '-pix_fmt', 'yuv420p', '-profile:v', 'high', '-movflags', '+faststart', '-an', str(video)], check=True)
        # ffmpeg can stop reading an image sequence early and still exit 0: count what it wrote
        probe = subprocess.run([a.ffmpeg.replace('ffmpeg', 'ffprobe'), '-v', 'error', '-select_streams', 'v:0', '-count_frames',
                                '-show_entries', 'stream=nb_read_frames', '-of', 'csv=p=0', str(video)], capture_output=True, text=True, check=True)
        if probe.stdout.strip() != '780':
            raise SystemExit(f'{video.name} has {probe.stdout.strip()} frames, not 780')
        Image.open(frames / 'f0000.ppm').convert('RGB').save(OUT / 'loopback-cable-poster.webp', quality=92, method=6)
    sources = ['source/xdj-xz-toolkit/mods/ui/cable_anim.c', 'source/xdj-xz-toolkit/mods/ui/cable_anim_data.h',
               'source/xdj-xz-toolkit/mods/ui/ui_draw.c', 'tools/wizard_media/cable_motion.c']
    (OUT / 'loopback-cable.json').write_text(json.dumps({
        'generator': 'tools/build_cable_media.py', 'device_access': False,
        'renderer': 'mods/ui cable_anim.c, the loopback-cable explainer the XZ draws, baked from the Blender scene in cable-anim/',
        'width': WIDTH, 'height': HEIGHT, 'fps': 60, 'frames': 780, 'seconds': 13, 'crf': CRF,
        'source_sha256': {name: hashlib.sha256((APP / name).read_bytes()).hexdigest() for name in sources}}, indent=2) + '\n',
        encoding='utf-8')
    for name in ('loopback-cable.mp4', 'loopback-cable-poster.webp', 'loopback-cable.json'):
        print(name, (OUT / name).stat().st_size, 'bytes')


if __name__ == '__main__':
    main()
