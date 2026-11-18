"""Run actual UI dispatch and input ownership under Linux sanitizers."""
import argparse,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parent
sys.path.insert(0,str(root.parent));from ui.ui_sources import UI_SOURCES
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
sources=[root/n for n in ('test_rhythm_lab_runtime.c','arcade.c','render.c','input.c','clock.c','native_clock.c','rhythm_lab.c','rhythm_lab_runtime.c','rhythm_lab_render.c','glyph_run.c','glyph_run_render.c','game_motion_draw.c','game_feel_art.c','game_font.c')]
sources += [root.parent/'ui'/n for n in UI_SOURCES]
# extra_detail/extra_lane/layered_wave: the included runtime-controls checks need them, as verify_runtime_controls.py links them.
sources += [root.parent/'ui'/n for n in ('native_touch.c','stem_pads.c','beat_jump.c','wave_viewport.c','extra_detail.c','extra_lane.c','layered_wave.c')]
sources += [root.parent/n for n in ('key/music.c','key/control.c','key/browser.c','key/catalog.c','key/traffic.c','kart/xz_kart_input.c','kart/xz_kart_runtime.c','settings.c','usb_profile.c','doom/native_bridge.c','doom/wad_catalog.c','ota/native_update.c','audio/vendor/sha256/sha256.c','pc_control/stems.c')]
binary=a.output.resolve()/'lab-runtime-linux'
subprocess.run(['gcc','-O1','-g','-std=c11','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',*map(str,sources),'-pthread','-ldl','-lm','-o',str(binary)],check=True)
r=subprocess.run([str(binary)],text=True,capture_output=True);(a.output/'runtime-linux.txt').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr);r.check_returncode()
