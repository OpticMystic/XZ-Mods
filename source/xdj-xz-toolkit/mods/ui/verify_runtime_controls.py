"""Run real UI runtime actions and native-touch ownership on a Linux host."""
from pathlib import Path
import subprocess
import tempfile
from ui_sources import UI_SOURCES

HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='xz-controls-') as temporary:
    binary=Path(temporary)/'controls'
    sources=[HERE/'test_runtime_controls.c',*(HERE/name for name in UI_SOURCES),HERE/'native_touch.c',HERE/'stem_pads.c',HERE/'beat_jump.c',HERE/'wave_viewport.c',HERE.parent/'settings.c',HERE.parent/'doom/native_bridge.c',HERE.parent/'doom/wad_catalog.c',HERE.parent/'ota/native_update.c']
    subprocess.run(['gcc','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all',*map(str,sources),'-pthread','-ldl','-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
