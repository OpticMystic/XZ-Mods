"""Replay actual UI runtime input at the stock-call boundary, without hardware."""
from pathlib import Path
import argparse,subprocess,tempfile
from ui_sources import UI_SOURCES
HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('case',nargs='?',default='all',choices=['all','scene','shortcut','buttons','redraw','ram','back'])
args=parser.parse_args()
with tempfile.TemporaryDirectory(prefix='xz-input-') as folder:
 binary=Path(folder)/'input'
 sources=[HERE/'test_input_regression.c',*[HERE/n for n in UI_SOURCES]]
 sources += [HERE/n for n in ('native_touch.c','stem_pads.c','beat_jump.c','wave_viewport.c')]
 sources += [HERE.parent/'key'/n for n in ('music.c','control.c','browser.c','catalog.c','traffic.c')]
 sources += [HERE.parent/'arcade'/n for n in ('arcade.c','render.c','input.c','clock.c','native_clock.c')]
 sources += [HERE.parent/n for n in ('settings.c','usb_profile.c','doom/native_bridge.c','doom/wad_catalog.c','ota/native_update.c','audio/vendor/sha256/sha256.c','pc_control/stems.c')]
 sources += [HERE.parent/'kart'/n for n in ('xz_kart_input.c','xz_kart_runtime.c')]
 subprocess.run(['cc','-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
                 '-fsanitize=address,undefined','-fno-sanitize-recover=all',*map(str,sources),'-pthread','-ldl','-lm','-o',str(binary)],check=True)
 for case in ['scene','shortcut','buttons','redraw','ram','back'] if args.case=='all' else [args.case]:
  subprocess.run([str(binary),case],check=True)
