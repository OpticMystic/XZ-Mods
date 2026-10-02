"""Render real analysis through the production layered C renderer, entirely offline."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
from PIL import Image

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--zig',type=Path,required=True)
p.add_argument('--corpus',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root))
from builder.layered_waveforms import encode
a.output.mkdir(parents=True,exist_ok=False)
exe=a.output/'preview.exe'
subprocess.run([str(a.zig.resolve()),'cc','-O2','-std=c11','-Wall','-Wextra','-Werror',
 str(root/'mods/ui/layered_wave.c'),str(root/'mods/ui/preview_layered_wave.c'),'-o',str(exe.resolve())],check=True)
for source in sorted(a.corpus.glob('*/ANLZ0000.EXT')):
    original=source.read_bytes();analysis=source.with_suffix('.2EX').read_bytes()
    path,data=encode(original,analysis)
    sidecar=a.output/(source.parent.name+'.xzw');sidecar.write_bytes(data)
    ppm=sidecar.with_suffix('.ppm')
    subprocess.run([str(exe.resolve()),str(sidecar.resolve()),str(ppm.resolve())],check=True)
    image=Image.open(ppm);image.save(sidecar.with_suffix('.png'));image.close();ppm.unlink()
    assert source.read_bytes()==original and source.with_suffix('.2EX').read_bytes()==analysis
    print(source.parent.name,path,hashlib.sha256(data).hexdigest())
