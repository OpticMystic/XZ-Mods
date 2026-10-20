"""Compile and run the DECK 3 / DECK 4 player host test with live assertions.

Usage: python mods/verify_extra_player.py --zig PATH/zig.exe [--no-wsl]
Zig host build with UndefinedBehaviorSanitizer; then, when WSL is present, gcc with AddressSanitizer and
UndefinedBehaviorSanitizer, and ThreadSanitizer, over the same test, and the copied USB transport test. A mutated back-cue
rule must make the test fail. Pure host code with a fake transport; nothing touches a device.
"""
import argparse
import math
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import wave

ROOT = Path(__file__).resolve().parent
SOURCES = ['extra_player.c', 'anlz_extra.c', 'shell_analysis.c', 'ui/layered_wave_source.c', 'ui/layered_wave.c',
           'audio/stem_media.c', 'audio/stem_mp3.c', 'audio/stem_decode.c']
MUTATION = ('if (d->state == XZ_XP_PLAYING || d->state == XZ_XP_ENDED) { jump(d, d->cue, now); d->state = XZ_XP_CUED; }',
            'if (d->state == XZ_XP_PLAYING || d->state == XZ_XP_ENDED) { jump(d, d->cue, now); }')

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
parser.add_argument('--no-wsl', action='store_true')
args = parser.parse_args()
common = [str(Path(args.zig).resolve()), 'cc', '-O1', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT)]


def linux(path):
    p = Path(path).resolve().as_posix()
    return '/mnt/' + p[0].lower() + p[2:]


def fixtures(folder):
    for rate in (44100, 48000):
        with wave.open(str(folder / f'{rate}.wav'), 'wb') as f:
            f.setnchannels(2); f.setsampwidth(2); f.setframerate(rate)
            f.writeframes(b''.join(struct.pack('<hh', *(2 * [round(16384 * math.sin(2 * math.pi * 440 * n / rate))]))
                                   for n in range(2 * rate)))
    (folder / 'anlz').mkdir()


with tempfile.TemporaryDirectory(prefix='xz-extra-player-') as folder:
    folder = Path(folder)
    fixtures(folder)
    binary = folder / 'test-extra-player.exe'
    subprocess.run(common + ['-UNDEBUG', '-fsanitize=undefined', *(str(ROOT / s) for s in SOURCES),
                             str(ROOT / 'test_extra_player.c'), '-lm', '-o', str(binary)], check=True)
    run = subprocess.run([str(binary), str(folder)], cwd=folder, capture_output=True, text=True)
    print(run.stdout.strip())
    if run.returncode:
        raise RuntimeError('host test failed\n' + run.stderr)
    print('PASS zig host build, UndefinedBehaviorSanitizer')

    source = (ROOT / 'extra_player.c').read_text(encoding='utf-8')
    if source.count(MUTATION[0]) != 1:
        raise RuntimeError('Mutation target (back cue pauses) not found exactly once')
    mutant = folder / 'mutant'
    mutant.mkdir()
    shutil.copy(ROOT / 'extra_player.h', mutant / 'extra_player.h')
    (mutant / 'extra_player.c').write_text(source.replace(*MUTATION), encoding='utf-8', newline='')
    broken = folder / 'broken.exe'
    subprocess.run(common + ['-UNDEBUG', '-I' + str(mutant), str(mutant / 'extra_player.c'),
                             *(str(ROOT / s) for s in SOURCES[1:]), str(ROOT / 'test_extra_player.c'), '-lm',
                             '-o', str(broken)], check=True)
    failed = subprocess.run([str(broken), str(folder)], cwd=folder, capture_output=True, text=True)
    if failed.returncode == 0:
        raise RuntimeError('A back cue that does not pause passed the test: the assertions are not live')
    print('PASS mutated back-cue rule fails: ' + (failed.stderr.strip().splitlines() or ['?'])[0][:160])

    negative = subprocess.run(common + ['-DNDEBUG', '-c', str(ROOT / 'test_extra_player.c'), '-o', str(folder / 'n.o')],
                              capture_output=True, text=True)
    if negative.returncode == 0 or 'Extra player acceptance requires active assertions' not in negative.stderr:
        raise RuntimeError('Missing assertion rejection')
    print('PASS assertion rejection')

    if args.no_wsl or not shutil.which('wsl'):
        print('NOT RUN WSL AddressSanitizer (no WSL)')
    else:
        gcc = ['wsl', '-d', 'Ubuntu', '--', 'gcc', '-O1', '-g', '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-Wno-misleading-indentation', '-pthread', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-I' + linux(ROOT)]
        asan = folder / 'test-extra-player-asan'
        subprocess.run(gcc + [*(linux(ROOT / s) for s in SOURCES), linux(ROOT / 'test_extra_player.c'), '-lm',
                              '-o', linux(asan)], check=True)
        subprocess.run(['wsl', '-d', 'Ubuntu', '--', linux(asan), linux(folder)], check=True)
        print('PASS WSL gcc AddressSanitizer + UndefinedBehaviorSanitizer')
        tsan = folder / 'test-extra-player-tsan'
        subprocess.run(gcc[:gcc.index('-fsanitize=address,undefined')] + ['-fsanitize=thread', '-fno-pie', '-no-pie',
                       '-I' + linux(ROOT), *(linux(ROOT / s) for s in SOURCES), linux(ROOT / 'test_extra_player.c'),
                       '-lm', '-o', linux(tsan)], check=True)
        # Without ASLR: WSL kernels map memory where ThreadSanitizer refuses to start.
        subprocess.run(['wsl', '-d', 'Ubuntu', '--', 'setarch', 'x86_64', '-R', linux(tsan), linux(folder)], check=True)
        print('PASS WSL gcc ThreadSanitizer')
        transport = folder / 'test-usb-transport'
        subprocess.run(gcc + [linux(ROOT / 'audio/usb_transport/tests/test_usb_transport.c'), '-o', linux(transport)],
                       check=True)
        subprocess.run(['wsl', '-d', 'Ubuntu', '--', linux(transport)], check=True)
        print('PASS copied USB transport test under AddressSanitizer')
print('PASS extra player')
