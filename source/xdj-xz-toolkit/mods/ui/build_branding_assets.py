"""Build original loading artwork only; no manufacturer image pack is shipped.

    python mods/ui/build_branding_assets.py <xdj-xz-toolkit> <output folder>

Writes splash.rgb565 (image 1446, the start-up screen) and logo.rgb565 (image 1487, the stock player's
empty-deck logo), both drawn from the vector mark by build_logo.py, plus apply.sh and MD5SUMS. The
folder is the runtime bundle's branding/ folder; the loader patches the two images into the player's
image pack in RAM at start-up."""
from pathlib import Path
import hashlib
import sys


def build(source: Path, output: Path):
    sys.path.insert(0, str(source / 'mods/ui'))
    from build_logo import banner_image, load, pack_rgb565, splash_image
    output.mkdir(parents=True, exist_ok=True)
    layers = load()
    for name, image in [('splash.rgb565', splash_image(layers)), ('logo.rgb565', banner_image(layers))]:
        (output/name).write_bytes(pack_rgb565(image))
    (output/'apply.sh').write_text('''#!/bin/sh
set -eu
pack="$1"
assets=$(dirname "$0")
[ "$(wc -c < "$pack")" -eq 17666908 ]
[ "$(dd if="$pack" bs=44 count=1582 2>/dev/null | md5sum | cut -d ' ' -f 1)" = 1160eaec6cbb0e6e13ea22232a4b2edf ]
[ "$(wc -c < "$assets/splash.rgb565")" -eq 768000 ]
[ "$(wc -c < "$assets/logo.rgb565")" -eq 103040 ]
dd if="$assets/splash.rgb565" of="$pack" bs=8 seek=1541365 conv=notrunc 2>/dev/null
dd if="$assets/logo.rgb565" of="$pack" bs=8 seek=1779173 conv=notrunc 2>/dev/null
''',encoding='ascii',newline='\n')
    names=['splash.rgb565','logo.rgb565','apply.sh']
    (output/'MD5SUMS').write_text(''.join(hashlib.md5((output/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in names),encoding='ascii',newline='\n')

if __name__=='__main__':
    build(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve())
