from pathlib import Path
import subprocess,tempfile,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'ui'))
from ui_sources import UI_SOURCES
with tempfile.TemporaryDirectory(prefix='xz-direct-key-') as tmp:
    binary=Path(tmp)/'direct-keys'
    files=[ROOT/'key/test_direct_keys.c',ROOT/'key/music.c',ROOT/'key/browser.c',ROOT/'key/catalog.c',ROOT/'key/traffic.c']
    files += [ROOT/'ui'/name for name in UI_SOURCES]
    files += [ROOT/'ui'/name for name in ('native_touch.c','stem_pads.c','beat_jump.c','wave_viewport.c')]
    files += [ROOT/'arcade'/name for name in ('arcade.c','render.c','input.c','clock.c','native_clock.c')]
    files += [ROOT/name for name in ('settings.c','usb_profile.c','doom/native_bridge.c','doom/wad_catalog.c','ota/native_update.c','audio/vendor/sha256/sha256.c','pc_control/stems.c')]
    subprocess.run(['gcc','-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all',*map(str,files),'-pthread','-ldl','-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
