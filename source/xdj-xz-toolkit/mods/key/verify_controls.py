"""Run actual native UI key controls on Linux with sanitizers."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'ui'))
from ui_sources import UI_SOURCES
with tempfile.TemporaryDirectory(prefix='xz-key-controls-') as temporary:
    binary=Path(temporary)/'key-controls'
    sources=[ROOT/'key/test_controls.c',ROOT/'key/music.c',ROOT/'key/control.c',ROOT/'key/browser.c',ROOT/'key/catalog.c',ROOT/'key/traffic.c']
    sources += [ROOT/'ui'/name for name in UI_SOURCES]
    sources += [ROOT/'ui'/name for name in ('native_touch.c','stem_pads.c','beat_jump.c','wave_viewport.c')]
    sources += [ROOT/'arcade'/name for name in ('arcade.c','render.c','input.c','clock.c','native_clock.c')]
    sources += [ROOT/name for name in ('settings.c','usb_profile.c','doom/native_bridge.c','doom/wad_catalog.c','ota/native_update.c','audio/vendor/sha256/sha256.c','pc_control/stems.c')]
    if (ROOT/'kart/xz_kart_runtime.c').is_file():
        sources += [ROOT/'kart'/name for name in ('xz_kart_input.c','xz_kart_runtime.c')]
    subprocess.run(['gcc','-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all',*map(str,sources),'-pthread','-ldl','-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
