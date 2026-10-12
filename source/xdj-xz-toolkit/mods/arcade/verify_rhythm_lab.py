"""Check native Lab input rules, render bounds, and ARM compatibility."""
import argparse,json,subprocess,sys
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--zig',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
common=[str(a.zig.resolve()),'cc','-O2','-UNDEBUG','-s','-std=c11','-Wall','-Wextra','-Werror','-DXZ_WAVE_RIDER_SOURCE_PORTABLE']
sources=[root/n for n in ('rhythm_lab.c','rhythm_lab_render.c','glyph_run.c','glyph_run_render.c','game_motion_draw.c','game_feel_art.c','game_font.c','wave_rider_source.c','test_rhythm_lab.c')]+[root.parent/'ui/ui_draw.c']
def run(cmd):
 r=subprocess.run(list(map(str,cmd)),text=True,capture_output=True)
 if r.returncode:print(r.stdout+r.stderr);r.check_returncode()
 return r.stdout
host=out/'lab-test.exe';run(common+sources+['-o',host]);log=run([host,out]);(out/'tests.txt').write_text(log);print(log)
for src in out.glob('lab-*.ppm'):
 with Image.open(src) as im:im.save(src.with_suffix('.png'))
sys.path.insert(0,str(root.parent));from build import inspect
arm=out/'lab-test-arm';run(common+['-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9']+sources+['-lm','-o',arm])
proof=inspect(arm);proof['executed_on_device']=False;(out/'arm.json').write_text(json.dumps(proof,indent=2)+'\n')
from ui.ui_sources import UI_SOURCES
rt=[root/n for n in ('test_rhythm_lab_runtime.c','arcade.c','render.c','input.c','clock.c','native_clock.c','rhythm_lab.c','rhythm_lab_runtime.c','rhythm_lab_render.c','glyph_run.c','glyph_run_render.c','game_motion_draw.c','game_feel_art.c','game_font.c')]
rt += [root.parent/'ui'/n for n in UI_SOURCES]
rt += [root.parent/'ui'/n for n in ('native_touch.c','stem_pads.c','beat_jump.c','wave_viewport.c')]
rt += [root.parent/n for n in ('key/music.c','key/browser.c','key/catalog.c','key/traffic.c','kart/xz_kart_input.c','kart/xz_kart_runtime.c','settings.c','usb_profile.c','doom/native_bridge.c','doom/wad_catalog.c','ota/native_update.c','audio/vendor/sha256/sha256.c','pc_control/stems.c')]
armrt=out/'lab-runtime-arm';run(common+['-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9']+rt+['-pthread','-ldl','-lm','-o',armrt])
proof=inspect(armrt);proof['executed_on_device']=False;(out/'runtime-arm.json').write_text(json.dumps(proof,indent=2)+'\n')
print('PASS Lab host engine/render and ARM ABI')
