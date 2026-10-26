"""Build/run real C offline fixtures and composite only its lower rows onto retained stock screen.
Usage: python preview_classic_mixer.py [--zig PATH] [--screen PATH] [--out PATH]
No controller, USB, network, installation or runtime integration.
"""
import argparse
from pathlib import Path
import subprocess
import json
import importlib.util
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', default='C:/Users/short/Github/VJToolsLibrary/.tmp/cdj-zig/ziglang/zig.exe')
    parser.add_argument('--screen', default='E:/Codex/xz-four-deck-20261016/live-screen.png')
    parser.add_argument('--out', required=True)
    parser.add_argument('--layout', choices=['four','mixer'], required=True)
    args = parser.parse_args()
    ui = Path(__file__).resolve().parent
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    common = [args.zig, 'cc', '-std=c11', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections']
    deps = [str(ui / n) for n in ['ui_draw.c', 'extra_lane.c', 'layered_wave.c', 'wave_viewport.c', 'extra_detail.c']]
    commands = []
    for name, sources in [('test', [ui / 'test_classic_mixer.c']), ('preview', [ui / 'preview_classic_mixer.c', ui / 'classic_mixer.c'])]:
        command = common + [str(p) for p in sources] + deps + ['-o', str(out / (name + '.exe'))]
        commands.append(command)
        subprocess.run(command, check=True)
    result = subprocess.run([str(out / 'test.exe')], check=True, capture_output=True, text=True)
    print(result.stdout, end='')
    image = Image.open(args.screen).convert('RGB')
    assert image.size == (800, 480)
    raw = bytearray()
    for r, g, b in image.get_flattened_data():
        pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        raw.extend(pixel.to_bytes(2, 'little'))
    retained = out / 'retained-stock.rgb565'
    retained.write_bytes(raw)
    spec = importlib.util.spec_from_file_location('existing_preview', ui / 'preview_four_deck.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    native_surface = out / 'native-surface.rgb565'
    native_surface.write_bytes(module.native_surface(image))
    files = []
    for scenario, name in enumerate(['01-loaded-offline-fixture', '02-unknown-and-empty-offline-fixture', '03-long-title-and-modes-offline-fixture']):
        rendered = out / (name + '.rgb565')
        subprocess.run([str(out / 'preview.exe'), str(retained), str(rendered), str(scenario), str(native_surface), args.layout], check=True)
        data = rendered.read_bytes()
        assert len(data) == 800*480*2
        rgb = bytearray()
        for i in range(0, len(data), 2):
            q = int.from_bytes(data[i:i+2], 'little')
            rgb.extend((((q >> 11) & 31)*255//31, ((q >> 5) & 63)*255//63, (q & 31)*255//31))
        target = out / (name + '.png')
        Image.frombytes('RGB', (800,480), bytes(rgb)).save(target)
        files.append(str(target))
    (out / 'verification.json').write_text(json.dumps({'fixture_notice':'OFFLINE FIXTURES. Upper waveform area uses real xz_four_frame_draw on reconstructed retained native lanes; bottom drawn by compiled classic_mixer.c. No live/runtime proof.', 'tests':result.stdout.strip(), 'commands':commands, 'previews':files}, indent=2))
    print('\n'.join(files))


if __name__ == '__main__':
    main()

