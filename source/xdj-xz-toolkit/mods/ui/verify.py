"""Run portable UI, touch and pad tests with live assertions."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from ui_sources import UI_SOURCES

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent
common = [str(Path(args.zig).resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror']
with tempfile.TemporaryDirectory(prefix='xz-ui-test-') as folder:
    for name, sources in {
        'pads': ['stem_pads.c', 'test_stem_pads.c'],
        'beat-jump': ['beat_jump.c', 'test_beat_jump.c'],
        'themes': ['test_themes.c'],
        'pixel-font': ['test_pixel_font.c'],
        'native-palette': ['test_native_palette.c'],
        'native-asset-view': ['test_native_asset_view.c'],
        'native-text-theme': ['test_native_text_theme.c'],
        'native-pixel-glyph': ['test_native_pixel_glyph.c'],
        'native-asset-theme': ['test_native_asset_theme.c'],
        'native-glass-theme': ['test_native_glass_theme.c'],
        'native-retro-theme': ['test_native_retro_theme.c'],
        'native-wave-style': ['test_native_wave_style.c'],
        'native-wave-material': ['test_native_wave_material.c'],
        'native-main-theme': ['test_native_main_theme.c'],
        'native-title-contrast': ['test_native_title_contrast.c'],
        'native-title-theme': ['test_native_title_theme.c'],
        'native-lcars-assets': ['test_native_lcars_assets.c'],
        'native-window-keys': ['test_native_window_keys.c'],
        'native-led': ['native_led.c','test_native_led.c'],
        'pad-order': [*UI_SOURCES, 'stem_pads.c', 'test_pad_order.c'],
        'ui': [*UI_SOURCES, 'test_ui.c'],
        'skin-states': [*UI_SOURCES, 'test_skin_states.c'],
        'touch': [*UI_SOURCES, 'native_touch.c', 'test_native_touch.c'],
        'wave': ['wave_viewport.c', 'test_wave_viewport.c'],
        'native-wave': ['native_wave.c', 'wave_viewport.c', 'test_native_wave.c'],
        'inline': [*UI_SOURCES, 'test_inline_stems.c'],
        'font': [*UI_SOURCES, 'test_font.c'],
        'draw': ['ui_draw.c', 'test_ui_draw.c'],
        'layered-wave': ['layered_wave.c', 'layered_wave_source.c', 'test_layered_wave.c', '../audio/overcue_file.c',
                         '../audio/vendor/miniz/miniz_tinfl.c', '../audio/vendor/sha256/sha256.c', '../audio/stem_decode.c'],
    }.items():
        output = Path(folder) / (name + '.exe')
        subprocess.run(common + ['-UNDEBUG'] + (['-DXZ_LED_PORTABLE_TEST'] if name=='native-led' else []) + (['-DMINIZ_NO_ARCHIVE_APIS', '-DMINIZ_NO_DEFLATE_APIS', '-D_CRT_SECURE_NO_WARNINGS'] if name=='layered-wave' else []) + [str(root / p) for p in sources] + ['-o', str(output)], check=True)
        subprocess.run([str(output)], check=True, cwd=folder)
    negative = subprocess.run(common + ['-DNDEBUG', '-c', str(root / 'test_stem_pads.c'), '-o', str(Path(folder) / 'negative.o')], capture_output=True, text=True)
    if negative.returncode == 0 or 'Pad acceptance requires active assertions' not in negative.stderr:
        raise RuntimeError('Missing assertion rejection')
print('PASS assertion rejection')
