"""Build the setup wizard's interface step media from the deck's own drawing code.

- New interface: a looping video and a poster from the FLAGSHIP shell renderer
  (source/.../mods/shell) driven by tools/wizard_media/shell_motion.c.
- Standard interface: a real capture of the stock screen. With --stock-clip it
  encodes a recording made by tools/wizard_media/record_stock_clip.py; without
  it, the wizard shows the still framebuffer capture given by --stock-still.
- "Switch it later": MODS > APPEARANCE drawn by the MODS panel renderer
  (source/.../mods/ui, tools/wizard_media/menu_render.c), one per interface,
  next to the MODS button that opens it, with the target marked.
- For the runtime work: MODS > APPEARANCE in the shell's own MODS screen
  (tools/wizard_media/shell_appearance.c), written to --reference only.

Writes ui/assets/wizard/* and ui/assets/wizard/manifest.json. Never connects to the XZ.
Run: python tools/build_wizard_media.py --zig <zig.exe> [--stock-clip clip.mkv] [--reference DIR]
"""
import argparse
import re
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

APP = Path(__file__).resolve().parents[1]
MODS = APP / 'source/xdj-xz-toolkit/mods'
HERE = APP / 'tools/wizard_media'
OUT = APP / 'ui/assets/wizard'
STILL = Path('C:/Users/short/Github/VJToolsLibrary/artifacts/xdj-xz-mods/native-layout-usb-ready/screen.png')
YELLOW = (243, 233, 0)   # the app's --yellow
FPS, SECONDS = 30, 9
sys.path.insert(0, str(MODS / 'shell'))
sys.path.insert(0, str(MODS / 'ui'))
from shell_sources import SHELL_SOURCES  # noqa: E402
from ui_sources import UI_SOURCES  # noqa: E402


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def compile_c(zig, sources, include, exe):
    subprocess.run([str(zig), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(include),
                    *map(str, sources), '-o', str(exe)], check=True)


def ffmpeg(*args):
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', *map(str, args)], check=True)


def webp(image, path, quality=88):
    image.save(path, 'WEBP', quality=quality, method=6)


def ring(draw, box, width=4):
    x0, y0, x1, y1 = box
    draw.rounded_rectangle((x0, y0, x1, y1), radius=10, outline=YELLOW, width=width)


def corner_step(screen, mods_box, path):
    """The play screen's top right with the MODS button ringed."""
    left = 800 - 300
    corner = screen.crop((left, 0, 800, 90)).resize((450, 135), Image.LANCZOS)
    x0, y0, x1, y1 = mods_box
    ring(ImageDraw.Draw(corner), ((x0 - left) * 1.5 - 6, 1, (x1 - left) * 1.5 - 2, y1 * 1.5 + 6), 4)
    webp(corner, path, 90)


def page_step(page, boxes, path):
    """A full 800x480 menu render with each box ringed, scaled to 560x336."""
    page = page.copy()
    for box in boxes:
        ring(ImageDraw.Draw(page), box, 5)
    webp(page.resize((560, 336), Image.LANCZOS), path, 90)


def runtime_tabs():
    """The MODS tab labels and summaries shell_runtime.c publishes, which the harness must draw the same."""
    text = (MODS / 'shell_runtime.c').read_text(encoding='utf-8')
    block = text[text.index('mod_tabs[5]={'):]
    return re.findall(r'\{"([A-Z ]+)",XZ_SHELL_TAB_PAGE,NULL,NULL,0,\w+,"([^"]+)"\}', block[:block.index('};') + 2])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zig', required=True, type=Path)
    p.add_argument('--stock-clip', type=Path, help='A recording from tools/wizard_media/record_stock_clip.py')
    p.add_argument('--stock-still', type=Path, default=STILL, help='A real /dev/fb0 capture of the stock screen')
    p.add_argument('--reference', type=Path, help='Also write full-size PNG renders here')
    a = p.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='xz-wizard-media-') as folder:
        tmp = Path(folder)
        shell = [MODS / 'shell' / name for name in SHELL_SOURCES]
        ui = [MODS / 'ui' / name for name in UI_SOURCES]
        compile_c(a.zig, [*shell, HERE / 'shell_motion.c'], MODS / 'shell', tmp / 'shell_motion.exe')
        compile_c(a.zig, [*shell, HERE / 'shell_appearance.c'], MODS / 'shell', tmp / 'shell_appearance.exe')
        compile_c(a.zig, [*ui, HERE / 'menu_render.c'], MODS / 'ui', tmp / 'menu_render.exe')
        subprocess.run([tmp / 'shell_motion.exe', tmp / 'motion.rgb', str(FPS), str(SECONDS), tmp / 'poster.ppm'], check=True)
        rects = json.loads(subprocess.run([tmp / 'shell_appearance.exe', tmp], check=True, capture_output=True, text=True).stdout)
        harness = re.findall(r'\{"([A-Z ]+)",XZ_SHELL_TAB_PAGE,NULL,NULL,0,\w+,"([^"]+)"\}', (HERE / 'shell_appearance.c').read_text(encoding='utf-8'))
        assert harness and harness == runtime_tabs(), 'tools/wizard_media/shell_appearance.c tabs differ from shell_runtime.c mod_tabs'

        subprocess.run([tmp / 'menu_render.exe', tmp], check=True)

        # New interface: H.264 in MP4 plays in the app's WebView2 under media-src 'self'.
        ffmpeg('-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', '800x480', '-r', FPS, '-i', tmp / 'motion.rgb',
               '-c:v', 'libx264', '-preset', 'veryslow', '-crf', '20', '-tune', 'animation', '-pix_fmt', 'yuv420p',
               '-movflags', '+faststart', '-an', OUT / 'new-interface.mp4')
        poster = Image.open(tmp / 'poster.ppm').convert('RGB')
        webp(poster, OUT / 'new-interface-poster.webp')

        # Standard interface: a recording when there is one, else the honest still.
        for stale in ('standard-interface.mp4',):
            (OUT / stale).unlink(missing_ok=True)
        if a.stock_clip:
            ffmpeg('-i', a.stock_clip, '-t', '10', '-vf', 'scale=800:480:flags=neighbor,fps=30', '-c:v', 'libx264',
                   '-preset', 'veryslow', '-crf', '26', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', '-an',
                   OUT / 'standard-interface.mp4')
            ffmpeg('-i', a.stock_clip, '-vf', 'select=eq(n\\,0)', '-frames:v', '1', tmp / 'stock.png')
            stock = Image.open(tmp / 'stock.png').convert('RGB').resize((800, 480))
        else:
            stock = Image.open(a.stock_still).convert('RGB')
            assert stock.size == (800, 480), 'the stock still must be a full 800x480 framebuffer capture'
        webp(stock, OUT / 'standard-interface.webp')

        # Switch later. MODS button boxes come from the renderers: the shell header (XZ_SHELL_MODS_X..800, 0..24)
        # and the MODS badge (ui/skin_classic.c xz_ui_render_badge: 744,0 56x24). The INTERFACE row is
        # ui_layout.c APPEARANCE_HEAD(0)+16 = y 66, 44 tall, across the content width x 136..792.
        row = (131, 60, 797, 115)
        for stale in OUT.glob('switch-from-*.webp'):
            stale.unlink()
        # New interface: its MODS button opens the shell's MODS screen; the APPEARANCE tab's OPEN APPEARANCE
        # button opens the panel page. Standard: the MODS badge opens the panel; APPEARANCE is on its rail.
        x0, y0, x1, y1 = rects['open']
        tx0, ty0, tx1, ty1 = rects['tab']
        page_step(Image.open(tmp / 'shell-mods-appearance.ppm').convert('RGB'),
                  [(tx0 - 3, ty0 - 2, tx1 + 3, ty1 + 2), (x0 - 6, y0 - 6, x1 + 6, y1 + 6)], OUT / 'switch-from-new-mods.webp')
        page_step(Image.open(tmp / 'appearance-modern.ppm').convert('RGB'), [row], OUT / 'switch-from-new-appearance.webp')
        corner_step(stock, (744, 0, 800, 24), OUT / 'switch-from-standard-mods.webp')
        page_step(Image.open(tmp / 'appearance-classic.ppm').convert('RGB'), [row], OUT / 'switch-from-standard-appearance.webp')

        if a.reference:
            a.reference.mkdir(parents=True, exist_ok=True)
            for ppm in tmp.glob('*.ppm'):
                Image.open(ppm).save(a.reference / (ppm.stem + '.png'))

        sources = [*shell, *ui, *(HERE / n for n in ('shell_motion.c', 'shell_appearance.c', 'menu_render.c')),
                   MODS / 'shell/fixtures.h', MODS / 'shell/browse_fixtures.h', MODS / 'shell/settings_fixtures.h',
                   MODS / 'ui/ui_layout.c', Path(__file__)]
        outputs = sorted(p for p in OUT.iterdir() if p.suffix in ('.mp4', '.webp'))
        manifest = {
            'device_access': False,
            'generator': 'tools/build_wizard_media.py',
            'new_interface': {'renderer': 'mods/shell (xz_shell_render), fixture decks', 'fps': FPS, 'seconds': SECONDS},
            'standard_interface': ({'kind': 'recording', 'source': a.stock_clip.name, 'source_sha256': sha(a.stock_clip)} if a.stock_clip else
                                   {'kind': 'still', 'source': 'real /dev/fb0 capture, artifacts/xdj-xz-mods/native-layout-usb-ready/screen.png (VJToolsLibrary)',
                                    'source_sha256': sha(a.stock_still)}),
            'switch_later': {'renderer': 'mods/ui MODS panel (xz_ui_render) page APPEARANCE', 'path': 'MODS > APPEARANCE > INTERFACE'},
            'source_sha256': {str(Path(s).resolve().relative_to(APP)).replace('\\', '/'): sha(s) for s in dict.fromkeys(map(str, sources))},
            'files': {p.name: {'bytes': p.stat().st_size, 'sha256': sha(p)} for p in outputs},
        }
        names = {p.name for p in outputs}
        media = {'newVideo': 'assets/wizard/new-interface.mp4', 'newPoster': 'assets/wizard/new-interface-poster.webp',
                 'standardVideo': 'assets/wizard/standard-interface.mp4' if 'standard-interface.mp4' in names else None,
                 'standardPoster': 'assets/wizard/standard-interface.webp',
                 'switchFromNew': ['assets/wizard/switch-from-new-mods.webp', 'assets/wizard/switch-from-new-appearance.webp'],
                 'switchFromStandard': ['assets/wizard/switch-from-standard-mods.webp', 'assets/wizard/switch-from-standard-appearance.webp']}
        (APP / 'ui/wizard-media.js').write_text('// Generated by tools/build_wizard_media.py. Do not edit by hand.\nconst WIZARD_MEDIA='
                                               + json.dumps(media, separators=(',', ':')) + ';\n', encoding='utf-8', newline='\n')
        (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
        for p in outputs:
            print(f'{p.name:32} {p.stat().st_size:>9} bytes')


if __name__ == '__main__':
    main()
