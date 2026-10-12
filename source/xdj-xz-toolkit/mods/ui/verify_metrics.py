"""Run the actual native metrics parser, drawing and top-bar touch path."""
import argparse
import json
from pathlib import Path
import subprocess
from PIL import Image
from ui_sources import UI_SOURCES

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parent
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
binary=out/'metrics-test.exe'
sources=[root/name for name in UI_SOURCES]+[root/'native_touch.c',root/'test_metrics.c']
subprocess.run([str(args.zig.resolve()),'cc','-O2','-std=c11','-UNDEBUG','-Wall','-Wextra','-Werror',*map(str,sources),'-o',str(binary)],check=True)
result=subprocess.run([str(binary)],cwd=out,check=True,capture_output=True,text=True)
print(result.stdout)
for raw in out.glob('*.rgb565'):
    image=Image.frombytes('RGB',(800,480),raw.read_bytes(),'raw','BGR;16')
    image.crop((270,0,800,26)).resize((1060,52)).save(raw.with_suffix('.png'))
# This is a native drawing fixture, not a photograph or controller acceptance.
(out/'receipt.json').write_text(json.dumps({'passed':True,'native_test':result.stdout.strip(),
    'temperature_thresholds_c':[80,90],'flash_hz':1,'hardware_tested':False},indent=2)+'\n')
