"""Source-only RAM acceptance. Never opens firmware or enumerates devices.

Windows: python ram/verify_extra.py --out <scratch> [--sanitizer address,undefined]
Linux: same command. All fixtures, executables and receipts stay under --out.
"""
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import wave

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--sanitizer', default='undefined')
parser.add_argument('--suite', choices=['all', 'codecs', 'player', 'native'], default='all')
args = parser.parse_args()
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
(out / 'tmp').mkdir(exist_ok=True)


def linux(path):
    s = Path(path).resolve().as_posix()
    return '/mnt/' + s[0].lower() + s[2:] if os.name == 'nt' else s


def run(label, command, fail=False):
    if args.sanitizer == 'thread' and command[0].startswith(linux(out) + '/'):
        command = ['setarch', 'x86_64', '-R', *command]
    if os.name == 'nt':
        command = ['wsl', '-d', 'Ubuntu', '--cd', linux(out), '--exec', 'env',
                   'TMPDIR=' + linux(out / 'tmp'), *command]
    result = subprocess.run(command, cwd=out, capture_output=True, timeout=600)
    (out / (label + '.log')).write_bytes(result.stdout + result.stderr)
    with (out / 'commands.jsonl').open('a') as log:
        log.write(json.dumps({'label': label, 'command': command, 'exit': result.returncode}) + '\n')
    print(label, result.returncode, flush=True)
    if bool(result.returncode) != fail:
        print((result.stdout + result.stderr).decode(errors='replace')[-10000:])
        raise SystemExit(1)
    return result


flags = ['gcc', '-O1', '-g', '-std=c11', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
         '-Wno-misleading-indentation', '-pthread', '-fno-sanitize-recover=all',
         '-fsanitize=' + args.sanitizer, '-I' + linux(ROOT / 'ram'), '-I' + linux(ROOT)]
if args.sanitizer == 'thread':
    flags += ['-fno-pie', '-no-pie']
objects = []
for name in ['ram/budget.c', 'ram/cache.c', 'ram/file.c', 'ram/extra.c',
             'audio/stem_media.c', 'audio/stem_mp3.c', 'audio/stem_decode.c']:
    obj = out / (Path(name).stem + '.o')
    run('compile-' + obj.stem, flags + ['-c', linux(ROOT / name), '-o', linux(obj)])
    objects.append(linux(obj))
for name, inputs in [('budget', objects[:1]), ('extra', objects)]:
    binary = out / ('test-' + name)
    run('link-' + name, flags + [linux(ROOT / 'ram' / ('test_' + name + '.c')), *inputs,
                               *(['-Wl,--wrap=close', '-Wl,--wrap=pread64'] if name == 'extra' else []), '-lm', '-o', linux(binary)])
negative = run('assertions-disabled', flags + ['-DNDEBUG', '-c', linux(ROOT / 'ram/test_budget.c'), '-o', linux(out / 'negative.o')], True)
assert b'RAM acceptance requires active assertions' in negative.stderr
run('budget', [linux(out / 'test-budget')])
if args.suite in ('all', 'codecs'):
    run('link-file-faults', flags + [linux(ROOT / 'ram/test_file.c'), objects[0], objects[1], *objects[4:],
                                   '-lm', '-o', linux(out / 'test-file')])
    run('file-faults', [linux(out / 'test-file'), linux(out / 'file-fault-source')])
wav = out / 'reference.wav'
with wave.open(str(wav), 'wb') as f:
    f.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
    data = bytearray()
    for n in range(44100 * 14):
        t = n / 44100
        data.extend(struct.pack('<hh', round(12000 * math.sin(2 * math.pi * (220 * t + 35 * t * t))), round(10000 * math.sin(2 * math.pi * 337 * t))))
    f.writeframes(data)
formats = [('flac', ['-c:a', 'flac']), ('cbr.mp3', ['-c:a', 'libmp3lame', '-b:a', '320k']),
           ('vbr.mp3', ['-c:a', 'libmp3lame', '-q:a', '2']), ('ogg', ['-c:a', 'libvorbis']),
           ('aiff', ['-c:a', 'pcm_s16be']), ('aifc', ['-c:a', 'pcm_s16le', '-f', 'aiff'])]
for ext, codec in formats:
    run('encode-' + ext, ['ffmpeg', '-v', 'error', '-y', '-i', linux(wav), *codec, linux(out / ('reference.' + ext))])
for ext in (['wav', *(x[0] for x in formats)] if args.suite in ('all', 'codecs') else []):
    reference = out / ('reference.' + ext)
    usbs = [out / ('usb' + str(d) + '.' + ext) for d in (1, 2)]
    for usb in usbs:
        shutil.copyfile(reference, usb)
    run('retention-' + ext, [linux(out / 'test-extra'), linux(reference), *(linux(x) for x in usbs)])
    if ext.endswith('mp3'):
        for usb in usbs:
            shutil.copyfile(reference, usb)
        run('untrimmed-' + ext, [linux(out / 'test-extra'), linux(reference), *(linux(x) for x in usbs), '1'])
if args.suite in ('all', 'player'):
    (out / 'anlz').mkdir(exist_ok=True)
    player_objects = []
    for name in ['extra_player.c', 'key/keyshift.c', 'key/music.c', 'key/control.c', 'deck_select.c',
                 'anlz_extra.c', 'shell_analysis.c', 'ui/layered_wave_source.c', 'ui/layered_wave.c']:
        obj = out / (name.replace('/', '_') + '.o')
        run('compile-player-' + obj.stem, flags + ['-DXZ_XP_TEST_RENDER_HOOK', '-c', linux(ROOT / name), '-o', linux(obj)])
        player_objects.append(linux(obj))
    run('link-player', flags + ['-DXZ_XP_TEST_RENDER_HOOK', linux(ROOT / 'tests/test_ram_player.c'),
                               *objects, *player_objects, '-lm', '-o', linux(out / 'test-player')])
    run('player', [linux(out / 'test-player'), linux(out), linux(wav)])
if args.suite in ('all', 'native'):
    for name, sources in [('runtime', ['ram/cache.c', 'ram/budget.c']), ('pin', ['ram/pin.c'])]:
        # Existing tests use disposable /tmp files. Relocate only their fixture prefix.
        source = (ROOT / 'ram' / ('test_' + name + '.c')).read_text()
        copied = out / ('test_' + name + '.c')
        copied.write_text(source.replace('"/tmp/', '"' + linux(out) + '/'))
        binary = out / ('test-native-' + name)
        run('link-native-' + name, flags + ['-fno-pie', '-no-pie', linux(copied), *(linux(ROOT / x) for x in sources), '-o', linux(binary)])
        modes = ['', '--auto', '--presentation', '--scalars', '--display-assets', '--stop-key'] if name == 'runtime' else ['']
        for mode in modes:
            run('native-' + name + mode, [linux(binary), *([mode] if mode else [])])
print('PASS source-only RAM suite ' + args.suite, flush=True)
