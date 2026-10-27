"""Run real UI runtime actions and native-touch ownership on a Linux host."""
from pathlib import Path
import subprocess
import tempfile
from ui_sources import UI_SOURCES

HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xz-controls-') as temporary:
    binary=Path(temporary)/'controls'
    sources=[HERE/'test_stock_dialog_runtime.c',*(HERE/name for name in UI_SOURCES),HERE/'native_touch.c',HERE/'stem_pads.c',HERE/'beat_jump.c',HERE/'wave_viewport.c',HERE.parent/'settings.c',HERE.parent/'usb_profile.c',HERE.parent/'doom/native_bridge.c',HERE.parent/'doom/wad_catalog.c',HERE.parent/'ota/native_update.c']
    sources += [HERE/name for name in ('extra_detail.c','extra_lane.c','layered_wave.c')]
    sources += [HERE.parent/'key'/name for name in ('music.c','browser.c','catalog.c','traffic.c')]
    sources += [HERE.parent/'arcade'/name for name in ('arcade.c','render.c','input.c','clock.c','native_clock.c')]
    sources += [HERE.parent/'audio/vendor/sha256/sha256.c',HERE.parent/'pc_control/stems.c']
    sources += [HERE.parent/'kart'/name for name in ('xz_kart_input.c','xz_kart_runtime.c')]
    subprocess.run(['gcc','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all',*map(str,sources),'-pthread','-ldl','-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary),*__import__('sys').argv[1:]],check=True)
    # Compile the same frame probe against the assigned base runtime. Use temporary
    # source copies so this never creates a checkout or changes a tracked fixture.
    import hashlib
    base=Path(temporary)/'base-runtime.c'
    import os
    supplied=os.environ.get('XZ_STOCK_BASE_RUNTIME')
    base.write_bytes(Path(supplied).read_bytes() if supplied else subprocess.check_output(
        ['git','-C',str(HERE),'show','42ba2f6:source/xdj-xz-toolkit/mods/ui_runtime.c']))
    controls=Path(temporary)/'test_runtime_controls.c'
    controls.write_text((HERE/'test_runtime_controls.c').read_text().replace(
        '#include "../ui_runtime.c"','#include "'+str(base)+'"'))
    probe=Path(temporary)/'test_stock_dialog_runtime.c'
    probe.write_text((HERE/'test_stock_dialog_runtime.c').read_text().replace(
        '#include "../stock_dialog_fixture.h"','#include "'+str(HERE.parent/'stock_dialog_fixture.h')+'"').replace(
        '#include "../stock_dialog.c"','#include "'+str(HERE.parent/'stock_dialog.c')+'"'))
    # Both probes run from fresh state, bypassing all dialog cases, for a byte comparison.
    current=Path(temporary)/'current';previous=Path(temporary)/'previous'
    current.mkdir();previous.mkdir()
    subprocess.run([str(binary),str(current),'baseline'],check=True)
    reference=Path(temporary)/'reference'
    subprocess.run(['gcc','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-DXZ_STOCK_DIALOG_REFERENCE','-I'+str(HERE),'-I'+str(HERE.parent),
        str(probe),*map(str,sources[1:]),'-pthread','-ldl','-lm','-o',str(reference)],check=True)
    subprocess.run([str(reference),str(previous),'baseline'],check=True)
    for name in ['no-dialog-panel','no-dialog-classic','no-dialog-modern']:
        a=(current/(name+'.ppm')).read_bytes();b=(previous/(name+'.ppm')).read_bytes()
        assert a==b,name+' pixels differ from 42ba2f6'
        print('PASS '+name+' byte-identical to 42ba2f6:',hashlib.sha256(a).hexdigest())

with tempfile.TemporaryDirectory(prefix='xz-stock-wave-') as temporary:
    binary=Path(temporary)/'wave'
    files=['test_stock_dialog_wave.c','native_wave.c','wave_viewport.c','extra_lane.c','extra_detail.c',
           'layered_wave.c','ui_draw.c','route_card.c','cable_anim.c','deck_view.c']
    subprocess.run(['gcc','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all',*[str(HERE/f) for f in files],
        '-pthread','-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

with tempfile.TemporaryDirectory(prefix='xz-stock-model-') as temporary:
    import sys
    sys.path.insert(0,str(HERE.parent/'shell'))
    from shell_sources import SHELL_SOURCES
    binary=Path(temporary)/'model'
    # GCC's fortified snprintf warns about unchanged bounded UI strings in the
    # complete shell. Keep those warnings visible; sanitizers and all other warnings stay fatal.
    subprocess.run(['gcc','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all',str(HERE.parent/'test_stock_counter_model.c'),
        str(HERE.parent/'shell_model.c'),*[str(HERE.parent/'shell'/f) for f in SHELL_SOURCES],
        '-Wno-error=format-truncation','-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary),*sys.argv[1:]],check=True)
