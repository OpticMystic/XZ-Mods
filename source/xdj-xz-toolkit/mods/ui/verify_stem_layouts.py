"""Verify all five layouts using the actual native compositor and button renderer."""
from pathlib import Path
import argparse,subprocess,tempfile,json
from ui_sources import UI_SOURCES
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--zig',type=Path,required=True);p.add_argument('--output',type=Path)
a=p.parse_args();root=Path(__file__).resolve().parent
if a.output:a.output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='xz-stem-layouts-') as folder:
 exe=Path(folder)/'layouts.exe'
 subprocess.run([str(a.zig.resolve()),'cc','-O2','-std=c11','-UNDEBUG','-Wall','-Wextra','-Werror',
  *(str(root/n) for n in UI_SOURCES),*(str(root/n) for n in ('native_wave.c','wave_viewport.c','layered_wave.c','test_stem_layouts.c')),'-o',str(exe)],check=True)
 subprocess.run([str(exe),*([str(a.output.resolve())] if a.output else [])],check=True)
if a.output:
 from PIL import Image
 for path in a.output.glob('theme-*-layout-*.ppm'):Image.open(path).save(path.with_suffix('.png'));path.unlink()
 labels=['Hidden / normal waves','Below / full height','Below / half height','Outer / full height','Outer / half height']
 cards=''.join(f'<figure><figcaption>{labels[i]}</figcaption><img src="theme-0-layout-{i}.png" width="536" height="268"></figure>' for i in (1,2,3,4,0))
 (a.output/'index.html').write_text('<!doctype html><meta charset="utf-8"><title>XZ stem layouts</title><style>body{font:16px system-ui;background:#111;color:#eee;margin:24px}main{display:flex;flex-wrap:wrap;gap:20px}figure{margin:0}figcaption{margin:0 0 8px}img{border:1px solid #555;max-width:100%}p{color:#aaa}</style><h1>Five stem layouts</h1><p>Actual C compositor and renderer, fixture waveform and stem levels. Controller and physical USB unchanged.</p><main>'+cards+'</main>')
 print(a.output.resolve())
