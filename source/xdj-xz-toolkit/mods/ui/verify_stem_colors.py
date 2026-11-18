"""Run the native stem color editor and generate previews from its actual C renderer."""
from pathlib import Path
import argparse, subprocess, sys, tempfile
from ui_sources import UI_SOURCES

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--zig',type=Path,required=True)
p.add_argument('--output',type=Path)
a=p.parse_args();root=Path(__file__).resolve().parent
if a.output:a.output.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='xz-stem-colors-') as folder:
    exe=Path(folder)/'stem-colors.exe'
    subprocess.run([str(a.zig.resolve()),'cc','-O2','-std=c11','-UNDEBUG','-Wall','-Wextra','-Werror',
        *(str(root/name) for name in UI_SOURCES),'%s'%(root/'layered_wave.c'),str(root/'test_stem_colors.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),*([str(a.output.resolve())] if a.output else [])],check=True)
if a.output:
    from PIL import Image
    for path in a.output.glob('stem-colors-*.ppm'):
        Image.open(path).save(path.with_suffix('.png'));path.unlink()
    print(a.output.resolve())
