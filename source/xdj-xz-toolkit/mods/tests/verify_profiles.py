"""Exercise the actual UI profile worker with disposable user volumes."""
from pathlib import Path
import subprocess
import tempfile
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'ui'))
from ui_sources import UI_SOURCES
with tempfile.TemporaryDirectory(prefix='xz-profiles-') as temporary:
    for fixture in ('test_settings.c','test_usb_profile.c'):
        binary=Path(temporary)/Path(fixture).stem
        subprocess.run(['gcc','-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
            '-fsanitize=address,undefined','-fno-sanitize-recover=all',str(ROOT/'tests'/fixture),str(ROOT/'settings.c'),
            str(ROOT/'usb_profile.c'),str(ROOT/'ui/beat_jump.c'),'-lm','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    binary=Path(temporary)/'internal'
    subprocess.run(['gcc','-std=c11','-O1','-g','-UNDEBUG','-DXZ_SETTINGS_INTERNAL_TEST','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all',str(ROOT/'tests/test_settings_internal.c'),str(ROOT/'settings_internal.c'),
        str(ROOT/'settings.c'),str(ROOT/'ui/beat_jump.c'),'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    sources=[ROOT/'tests/test_profile_runtime.c',ROOT/'key/music.c',ROOT/'key/browser.c',ROOT/'key/catalog.c',ROOT/'key/traffic.c']
    sources += [ROOT/'ui'/name for name in UI_SOURCES]
    sources += [ROOT/'ui'/name for name in ('native_touch.c','stem_pads.c','beat_jump.c','wave_viewport.c')]
    sources += [ROOT/'arcade'/name for name in ('arcade.c','render.c','input.c','clock.c','native_clock.c')]
    sources += [ROOT/name for name in ('settings.c','settings_internal.c','usb_profile.c','doom/native_bridge.c','doom/wad_catalog.c','ota/native_update.c','audio/vendor/sha256/sha256.c','pc_control/stems.c')]
    sources += [ROOT/'kart'/name for name in ('xz_kart_input.c','xz_kart_runtime.c')]
    binary=Path(temporary)/'profiles'
    subprocess.run(['gcc','-std=c11','-O1','-g','-UNDEBUG','-DXZ_SETTINGS_INTERNAL_TEST','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all',*map(str,sources),'-pthread','-ldl','-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
