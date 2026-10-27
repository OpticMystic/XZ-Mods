"""Four-deck key acceptance with synthetic media and sinks; never contacts a device.

Run with Windows Python. GCC/sanitizers run in Ubuntu WSL. --suite selects a
focused rerun. The existing full player and SYNC verifiers remain separate gates.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--suite', default='all', choices=['all', 'load', 'alignment', 'music', 'native', 'planner', 'shifter', 'runtime', 'player', 'proof', 'admission', 'router', 'controls', 'ui', 'thread'])
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
(out / 'tmp').mkdir(exist_ok=True)
os.environ.update(TMP=str(out / 'tmp'), TEMP=str(out / 'tmp'), PYTHONDONTWRITEBYTECODE='1')


def linux(path):
    path = Path(path).resolve().as_posix()
    return '/mnt/' + path[0].lower() + path[2:] if os.name == 'nt' else path


def run(label, command):
    if os.name == 'nt':
        command = ['wsl', '-d', 'Ubuntu', '--cd', linux(out), '--exec', 'env', 'TMPDIR=' + linux(out / 'tmp'), *command]
    result = subprocess.run(command, cwd=out, capture_output=True, timeout=300)
    (out / (label + '.log')).write_bytes(result.stdout + result.stderr)
    with (out / 'commands.jsonl').open('a') as log:
        log.write(json.dumps({'label': label, 'command': command, 'exit': result.returncode}) + '\n')
    print(label, result.returncode, flush=True)
    if result.returncode:
        print((result.stdout + result.stderr).decode(errors='replace')[-5000:])
        raise SystemExit(result.returncode)


sys.path.insert(0, str(ROOT))
from extra_player_fixtures import fixtures
data = out / 'data'
data.mkdir(exist_ok=True)
fixtures(data)
import wave
with wave.open(str(data / "silence.wav"), "wb") as silence:
    silence.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
    silence.writeframes(bytes(44100 * 2 * 2 * 2))
player = ['extra_player.c', 'key/keyshift.c', 'key/music.c', 'key/control.c', 'deck_select.c', 'anlz_extra.c', 'shell_analysis.c', 'ui/layered_wave_source.c', 'ui/layered_wave.c', 'audio/stem_media.c', 'audio/stem_mp3.c', 'audio/stem_decode.c']
sys.path.insert(0, str(ROOT / 'ui'))
sys.path.insert(0, str(ROOT / 'shell'))
from ui_sources import UI_SOURCES
from shell_sources import SHELL_SOURCES
controls = [ROOT / n for n in ['key/music.c', 'key/control.c', 'key/browser.c', 'key/catalog.c', 'key/traffic.c', 'settings.c', 'usb_profile.c', 'doom/native_bridge.c', 'doom/wad_catalog.c', 'ota/native_update.c', 'audio/vendor/sha256/sha256.c', 'pc_control/stems.c', 'kart/xz_kart_input.c', 'kart/xz_kart_runtime.c']]
controls += [ROOT / 'ui' / n for n in UI_SOURCES + ['native_touch.c', 'stem_pads.c', 'beat_jump.c', 'wave_viewport.c']]
controls += [ROOT / 'arcade' / n for n in ['arcade.c', 'render.c', 'input.c', 'clock.c', 'native_clock.c']]
groups = {
    'load': [*player[1:], 'tests/test_key_load.c'],
    'alignment': ['key/test_alignment.c'],
    'music': ['key/music.c', 'key/test_music.c'],
    'native': ['key/music.c', 'key/native.c', 'key/test_native.c'],
    'planner': ['key/music.c', 'key/control.c', 'key/test_control.c'],
    'shifter': ['key/keyshift.c', 'key/test_keyshift.c'],
    'runtime': ['key/keyshift.c', 'key/runtime.c', 'key/test_runtime.c'],
    'player': [*player, 'tests/test_key_player.c'],
    'proof': [*player[1:], 'tests/test_key_proof.c'],
    'admission': ['tests/test_key_admission.c'],
    'router': ['deck_select.c', 'test_deck_select.c'],
    'controls': [*controls, 'key/test_controls.c'],
    'ui': [*controls, *(ROOT / 'shell' / n for n in SHELL_SOURCES), 'ui/test_key_four_deck.c'],
    'thread': ['key/runtime.c', 'key/test_requests_threaded.c'],
}
for name, sources in groups.items():
    if args.suite not in ('all', name):
        continue
    paths = list(dict.fromkeys((ROOT / path).resolve() for path in sources))
    binary = out / name
    flags = ['-O1', '-g', '-std=c11', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-ffunction-sections', '-fdata-sections', '-I' + linux(ROOT)]
    if name == 'ui':
        # Existing shell format bounds trigger GCC warnings that Zig does not emit.
        flags += ['-Wno-format-truncation']
    if name == 'player':
        flags += ['-DXZ_XP_TEST_RENDER_HOOK']
    flags += ['-fsanitize=thread', '-fno-pie', '-no-pie'] if name == 'thread' else ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie']
    hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    run(name + '-build', ['gcc', *flags, *map(linux, paths), '-Wl,--gc-sections', '-pthread', '-ldl', '-lm', '-o', linux(binary)])
    command = ['setarch', 'x86_64', '-R', linux(binary)] if name == 'thread' else [linux(binary)]
    if name == 'player':
        command += [linux(data)]
    if name == 'controls':
        command += ['key-only']
    run(name + '-run', command)
    assert hashes == {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    (out / (name + '-sources.json')).write_text(json.dumps(hashes, indent=2))
print('PASS selected key acceptance suites; hardware unverified')
