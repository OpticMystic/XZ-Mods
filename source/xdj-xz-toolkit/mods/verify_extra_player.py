"""Compile and run the DECK 3 / DECK 4 player host test with live assertions.

Usage: python mods/verify_extra_player.py --zig PATH/zig.exe [--no-wsl]
Zig host build with UndefinedBehaviorSanitizer; then, when WSL is present, gcc with AddressSanitizer and
UndefinedBehaviorSanitizer, and ThreadSanitizer, over the same test, the route connector test
(test_extra_decks.c) and the copied USB transport test. A mutated back-cue
rule must make the test fail. Pure host code with a fake transport; nothing touches a device.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
from extra_player_fixtures import fixtures

ROOT = Path(__file__).resolve().parent
SOURCES = ['extra_player.c','key/keyshift.c','key/music.c','key/control.c', 'deck_select.c', 'anlz_extra.c', 'shell_analysis.c', 'ui/layered_wave_source.c', 'ui/layered_wave.c',
           'audio/stem_media.c', 'audio/stem_mp3.c', 'audio/stem_decode.c']
MUTATION = ('if (d->state == XZ_XP_PLAYING || d->state == XZ_XP_ENDED) { jump(d, d->cue, now); d->state = XZ_XP_CUED; }',
            'if (d->state == XZ_XP_PLAYING || d->state == XZ_XP_ENDED) { jump(d, d->cue, now); }')

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
parser.add_argument('--no-wsl', action='store_true')
parser.add_argument('--tsan-no-aslr', action='store_true', help='Use setarch -R for the TSan child only, for host mapping collisions')
args = parser.parse_args()
common = [str(Path(args.zig).resolve()), 'cc', '-O1', '-DXZ_XP_TEST_RENDER_HOOK', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + str(ROOT)]
tsan_unavailable = None


def linux(path):
    p = Path(path).resolve().as_posix()
    return '/mnt/' + p[0].lower() + p[2:]


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

    epoch_mutant = folder / 'epoch-mutant.c'
    epoch_mutant.write_text(source.replace('s->ctx, d->epoch, ', 's->ctx, s->link(s->ctx), '), encoding='utf-8')
    epoch_broken = folder / 'epoch-broken.exe'
    subprocess.run(common + ['-UNDEBUG', str(epoch_mutant), *(str(ROOT / s) for s in SOURCES[1:]),
                             str(ROOT / 'test_extra_player.c'), '-lm', '-o', str(epoch_broken)], check=True)
    failed_epoch = subprocess.run([str(epoch_broken), str(folder)], cwd=folder, capture_output=True, text=True)
    if failed_epoch.returncode == 0 or 'f.submits[0] == before' not in failed_epoch.stderr:
        raise RuntimeError('Reconnect render regression did not reject current-epoch mutant: ' + failed_epoch.stderr)
    print('PASS reconnect during render fails when captured epoch is replaced by current link')

    negative = subprocess.run(common + ['-DNDEBUG', '-c', str(ROOT / 'test_extra_player.c'), '-o', str(folder / 'n.o')],
                              capture_output=True, text=True)
    if negative.returncode == 0 or 'Extra player acceptance requires active assertions' not in negative.stderr:
        raise RuntimeError('Missing assertion rejection')
    print('PASS assertion rejection')

    if args.no_wsl or not shutil.which('wsl'):
        print('NOT RUN WSL sanitizers and transport test (--no-wsl or no WSL)')
    else:
        gcc = ['wsl', '-d', 'Ubuntu', '--', 'gcc', '-O1', '-DXZ_XP_TEST_RENDER_HOOK', '-g', '-std=c11', '-Wall', '-Wextra', '-Werror',
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
        for attempt in range(3):
            checked = subprocess.run(['wsl', '-d', 'Ubuntu', '--', *(['setarch','x86_64','-R'] if args.tsan_no_aslr else []), linux(tsan), linux(folder)],
                                     capture_output=True, text=True)
            if checked.returncode == 0:
                print(checked.stdout.strip())
                print('PASS WSL gcc ThreadSanitizer')
                break
            if 'FATAL: ThreadSanitizer: unexpected memory mapping' not in checked.stderr:
                print(checked.stderr)
                checked.check_returncode()
            print(f'TSan startup mapping collision, unmodified execution {attempt+1}/3: {checked.stderr.strip()}')
        else:
            tsan_unavailable = checked.stderr.strip()
        connector = folder / 'test-extra-decks'
        subprocess.run(gcc + ['-DXZ_PLACEMENT_CONNECTOR_TEST', '-DUSB_MAJOR=1', '-DPOLL_DOWN_MS=50', '-DROUTE_EVENT_SAFETY_MS=50', '-DPOLL_UP_MS=5', linux(ROOT / 'extra_decks.c'), linux(ROOT / 'extra_cue_store.c'),
                              *(linux(ROOT / s) for s in SOURCES), linux(ROOT / 'test_extra_decks.c'), '-lm',
                              '-o', linux(connector)], check=True)
        subprocess.run(['wsl', '-d', 'Ubuntu', '--', linux(connector)], check=True)
        print('PASS route connector under AddressSanitizer')
        transport = folder / 'test-usb-transport'
        subprocess.run(gcc + [linux(ROOT / 'audio/usb_transport/tests/test_usb_transport.c'), '-o', linux(transport)],
                       check=True)
        subprocess.run(['wsl', '-d', 'Ubuntu', '--', linux(transport)], check=True)
        print('PASS copied USB transport test under AddressSanitizer')
if tsan_unavailable:
    raise RuntimeError('Other player/connector/transport checks passed, but ThreadSanitizer could not start: ' + tsan_unavailable)
print('PASS extra player')
