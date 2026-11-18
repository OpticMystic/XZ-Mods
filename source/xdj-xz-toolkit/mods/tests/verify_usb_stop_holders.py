"""USB STOP and the long-lived holders of a stick: the holders' board and the unmount's wait (usb_release.c core),
and DECK 3/4 letting go (real players, real RAM copy service, fake transport). Source only: no firmware, no device.
The stem streams' side runs in audio/tests/verify_overcue.py (test_overcue_flat.c gone-*).

Windows: python tests/verify_usb_stop_holders.py --out <scratch> [--sanitizer address,undefined|thread]
Linux: the same. Everything it builds and writes stays under --out.
"""
import argparse
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import wave

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--sanitizer', default='address,undefined')
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
(out / 'tmp').mkdir(exist_ok=True)


def linux(path):
    s = Path(path).resolve().as_posix()
    return '/mnt/' + s[0].lower() + s[2:] if os.name == 'nt' else s


def run(label, command):
    if args.sanitizer == 'thread' and command[0].startswith(linux(out) + '/'):
        command = ['setarch', 'x86_64', '-R', *command]
    if os.name == 'nt':
        command = ['wsl', '-d', 'Ubuntu', '--cd', linux(out), '--exec', 'env', 'TMPDIR=' + linux(out / 'tmp'), *command]
    result = subprocess.run(command, cwd=out, capture_output=True, timeout=900)
    (out / (label + '.log')).write_bytes(result.stdout + result.stderr)
    with (out / 'commands.jsonl').open('a') as log:
        log.write(json.dumps({'label': label, 'command': command, 'exit': result.returncode}) + '\n')
    print(label, result.returncode, flush=True)
    if result.returncode:
        print((result.stdout + result.stderr).decode(errors='replace')[-8000:])
        raise SystemExit(1)
    return result


flags = ['gcc', '-O1', '-g', '-std=c11', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-Wno-misleading-indentation',
         '-pthread', '-fno-sanitize-recover=all', '-fsanitize=' + args.sanitizer, '-I' + linux(ROOT / 'ram'), '-I' + linux(ROOT)]
if args.sanitizer == 'thread':
    flags += ['-fno-pie', '-no-pie']

# 1. The holders' board and the unmount's wait for it.
run('link-usb-release', flags + ['-DXZ_USB_RELEASE_NO_RUNTIME', linux(ROOT / 'usb_release.c'),
                                 linux(ROOT / 'tests/test_usb_release.c'), '-o', linux(out / 'test-usb-release')])
print(run('usb-release', [linux(out / 'test-usb-release')]).stdout.decode(), end='')

# 2. DECK 3/4: a fourteen-second reference track, copied onto two "sticks" by the test.
wav = out / 'reference.wav'
with wave.open(str(wav), 'wb') as f:
    f.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
    data = bytearray()
    for n in range(44100 * 14):
        t = n / 44100
        data.extend(struct.pack('<hh', round(12000 * math.sin(2 * math.pi * (220 * t + 35 * t * t))),
                                round(10000 * math.sin(2 * math.pi * 337 * t))))
    f.writeframes(data)
objects = []
for name in ['ram/budget.c', 'ram/cache.c', 'ram/file.c', 'ram/extra.c', 'audio/stem_media.c', 'audio/stem_mp3.c',
             'audio/stem_decode.c', 'tests/ram_player_engine.c', 'key/keyshift.c', 'key/music.c', 'key/control.c',
             'deck_select.c', 'anlz_extra.c', 'shell_analysis.c', 'ui/layered_wave_source.c', 'ui/layered_wave.c']:
    obj = out / (name.replace('/', '_') + '.o')
    run('compile-' + obj.stem, flags + ['-DXZ_XP_TEST_RENDER_HOOK', '-c', linux(ROOT / name), '-o', linux(obj)])
    objects.append(linux(obj))
fixture = out / 'decks'
(fixture / 'anlz').mkdir(parents=True, exist_ok=True)
run('link-decks', flags + ['-DXZ_XP_TEST_RENDER_HOOK', linux(ROOT / 'tests/test_usb_stop_decks.c'), *objects, '-lm',
                           '-o', linux(out / 'test-usb-stop-decks')])
print(run('decks', [linux(out / 'test-usb-stop-decks'), linux(fixture), linux(wav)]).stdout.decode(), end='')
print('PASS verify_usb_stop_holders', flush=True)
