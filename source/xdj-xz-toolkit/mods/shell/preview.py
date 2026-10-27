"""Render the FLAGSHIP shell fixtures to PNG locally. Never connects to the XZ."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from PIL import Image
from shell_sources import SHELL_SOURCES, SHELL_HEADERS

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--zig', required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parent
a.output.mkdir(parents=True, exist_ok=False)
with tempfile.TemporaryDirectory(prefix='xz-shell-preview-') as temporary:
    # preview.c and every preview_*.c are separate programs writing PPMs into the same folder. MODS itself is the
    # panel (../ui), previewed by ../ui's own tools.
    for program in [root / 'preview.c', *sorted(root.glob('preview_*.c'))]:
        exe = Path(temporary) / (program.stem + '.exe')
        subprocess.run([str(Path(a.zig).resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        *(str(root / name) for name in SHELL_SOURCES),
                        str(program), '-o', str(exe)], check=True)
        subprocess.run([str(exe), str(a.output.resolve())], check=True)
for path in sorted(a.output.glob('*.ppm')):
    Image.open(path).save(path.with_suffix('.png'))
    path.unlink()
images = sorted(path.name for path in a.output.glob('*.png'))
figures = ''.join(f'<figure><img src="{name}" width="800" height="480" alt="{name[:-4]}"><figcaption>{name[:-4]}</figcaption></figure>' for name in images)
(a.output / 'index.html').write_text('<!doctype html><html lang="en"><meta charset="utf-8"><title>XZ FLAGSHIP shell preview</title>'
    '<style>body{background:#11151a;color:#edf0f4;font:16px system-ui;margin:24px auto;max-width:840px}img{display:block;border:1px solid #465362}figcaption{color:#b6c0cb;margin:6px 0 18px}</style>'
    '<h1>XZ FLAGSHIP shell</h1><p>Local previews from the C renderer with fixture data. Not loaded on the XZ.</p>' + figures + '</html>', encoding='utf8')
manifest = {'device_access': False, 'installed': False,
            'source_sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in [*SHELL_SOURCES, *SHELL_HEADERS, 'preview.c', *(p.name for p in sorted(root.glob('preview_*.c')))]},
            'images': {name: hashlib.sha256((a.output / name).read_bytes()).hexdigest() for name in images}}
(a.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(a.output / 'index.html')
