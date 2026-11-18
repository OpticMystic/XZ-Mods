"""Build exhaustive, pixel-deduplicated review sheets from retained nav frames."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageDraw

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture', type=Path)
    p.add_argument('out', type=Path)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    unique = {}
    all_frames = {}
    for path in sorted(a.capture.glob('*/frame-*.png')):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        all_frames[path.relative_to(a.capture).as_posix()] = digest
        unique.setdefault(digest, path)
    frames = list(unique.values())
    pages = []
    for start in range(0, len(frames), 20):
        sheet = Image.new('RGB', (1600, 1300), '#141414')
        draw = ImageDraw.Draw(sheet)
        for i, path in enumerate(frames[start:start+20]):
            x, y = i % 4 * 400, i // 4 * 260
            with Image.open(path) as im:
                sheet.paste(im.resize((400, 240)), (x, y+20))
            draw.text((x+2, y+3), path.parent.name + '/' + path.stem, fill='white')
        name = f'review-{start//20+1:02d}.png'
        sheet.save(a.out/name)
        pages.append(name)
    (a.out/'inventory.json').write_text(json.dumps(dict(frames=all_frames, unique=len(frames), sheets=pages), indent=2)+'\n')
    print(len(all_frames), 'frames;', len(frames), 'distinct PNGs;', len(pages), 'sheets')

if __name__ == '__main__':
    main()
