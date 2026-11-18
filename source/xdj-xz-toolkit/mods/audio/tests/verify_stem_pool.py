"""Host tests of the stem pool: arbiter, pool, segmented store and its reader protocol.

Every test is built with AddressSanitizer and UndefinedBehaviorSanitizer; the reader-against-worker test is built
a second time with ThreadSanitizer, and once more with the reader wait removed from the store, where it must fail.
The store is compared with the whole-track store of the reference commit (taken from git), and the mixed audio of
the stream with that commit's stream, byte for byte. Run on Linux:
    python3 verify_stem_pool.py --cc gcc
"""
import argparse
import io
import platform
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
MODS = ROOT.parent
FLAGS = ['-std=c11', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
         '-fno-omit-frame-pointer', '-fno-sanitize-recover=all']
SANITIZERS = {'address': ['-fsanitize=address,undefined'], 'thread': ['-fsanitize=thread']}
# gcc's ThreadSanitizer stops with "unexpected memory mapping" on kernels that randomise mappings over more bits
# than it expects (WSL2 6.6: vm.mmap_rnd_bits 32). Its runs go without address randomisation where setarch exists.
RUN_PREFIX = {'thread': ['setarch', platform.machine(), '-R'] if shutil.which('setarch') else []}
# The whole-track store this work replaces: the integration merge the stems-pool branch started from.
REFERENCE = '6178e43'
# The one line of the reader protocol whose removal test_stem_protocol.c must notice.
WAIT = '        if (!__atomic_load_n(&f->busy, __ATOMIC_SEQ_CST)) return 1;\n'


def git_show(revision, path):
    top = subprocess.run(['git', '-c', 'safe.directory=*', '-C', str(MODS), 'rev-parse', '--show-toplevel'], check=True,
                         capture_output=True, text=True).stdout.strip()
    relative = (MODS / path).resolve().relative_to(Path(top).resolve()).as_posix()
    return subprocess.run(['git', '-c', 'safe.directory=*', '-C', top, 'show', f'{revision}:{relative}'], check=True,
                          capture_output=True).stdout


def git_tree(revision, destination):
    """The mods folder of `revision`, extracted under `destination`; returns its path."""
    top = subprocess.run(['git', '-c', 'safe.directory=*', '-C', str(MODS), 'rev-parse', '--show-toplevel'], check=True,
                         capture_output=True, text=True).stdout.strip()
    relative = MODS.resolve().relative_to(Path(top).resolve()).as_posix()
    archive = subprocess.run(['git', '-c', 'safe.directory=*', '-C', top, 'archive', revision, relative], check=True,
                             capture_output=True).stdout
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(destination, filter='data')
    return destination / relative


STREAM_SOURCES = ['overcue_file.c', 'stem_decode.c', 'overcue_stream.c', 'foreign_stems.c', 'stem_media.c', 'stem_mp3.c',
                  'vendor/miniz/miniz_tinfl.c', 'vendor/sha256/sha256.c']


def identity(compiler, work, reference):
    """The same blocks mixed by the reference commit's stream and by this one: equal bytes."""
    sys.path.insert(0, str(HERE))
    from verify_overcue import fixture
    from flac_fixture import repack
    old = git_tree(reference, work / 'old') / 'audio'
    shutil.copyfile(HERE / 'test_flat_identity.c', old / 'tests' / 'test_flat_identity.c')
    binaries = {}
    for name, audio, extra in (('old', old, []), ('new', ROOT, ['stem_pool.c'])):
        binaries[name] = work / f'identity-{name}'
        build(compiler, 'address', [audio / s for s in STREAM_SOURCES + extra] + [audio / 'tests' / 'test_flat_identity.c'],
              binaries[name], ['-DMINIZ_NO_ARCHIVE_APIS', '-DMINIZ_NO_DEFLATE_APIS'])
    source96, _, _, _ = fixture(work / 'usb96', signal=True)
    source44, bundle44, _, _ = fixture(work / 'usb44', signal=True)
    repack(source44, bundle44, rate=44100)
    for source, lag, label in ((source96, '29.4', '96 kHz stems, resampled'), (source96, '2257.4', '96 kHz stems, MP3 delay'),
                               (source44, '30', '44.1 kHz stems, whole-frame shift: copied'),
                               (source44, '29.4', '44.1 kHz stems, fractional shift: resampled')):
        out = {}
        for name in ('old', 'new'):
            out[name] = work / f'mix-{name}.bin'
            result = subprocess.run([str(binaries[name]), str(source), lag, str(out[name])], check=True, capture_output=True,
                                    text=True, timeout=300)
            print(f'   {name}: ' + '; '.join(result.stdout.strip().splitlines()))
        a, b = out['old'].read_bytes(), out['new'].read_bytes()
        if a[:16] != b[:16]:
            raise SystemExit(f'FAIL {label}: the two streams aligned differently; nothing to compare')
        if a != b:
            first = next(i for i in range(min(len(a), len(b))) if a[i] != b[i]) if len(a) == len(b) else -1
            raise SystemExit(f'FAIL {label}: mixed audio differs (first byte {first}; sizes {len(a)} and {len(b)})')
        print(f'== identity {label}: {len(a)} bytes equal')


def build(compiler, sanitizer, sources, output, extra=()):
    subprocess.run(compiler + FLAGS + SANITIZERS[sanitizer] + list(extra) + [str(s) for s in sources] +
                   ['-pthread', '-lm', '-o', str(output)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--reference', default=REFERENCE, help='commit holding the whole-track store')
    parser.add_argument('--only', action='append', help='arbiter, pool, store, protocol, mutation, identity')
    parser.add_argument('--protocol-seconds', default='1100',
                        help='upper bound per run: a run ends once every extent was taken 300 times (TSan needs minutes)')
    args = parser.parse_args()
    compiler = shlex.split(args.cc)
    wanted = lambda name: not args.only or name in args.only
    with tempfile.TemporaryDirectory(prefix='xz-stem-pool-') as folder:
        work = Path(folder)
        if wanted('arbiter'):
            build(compiler, 'address', [HERE / 'test_stem_arbiter.c'], work / 'arbiter')
            print('== arbiter (address)', flush=True)
            subprocess.run([str(work / 'arbiter')], check=True, timeout=600)
        if wanted('pool'):
            build(compiler, 'address', [ROOT / 'stem_pool.c', HERE / 'test_stem_pool.c'], work / 'pool')
            print('== pool (address)', flush=True)
            subprocess.run([str(work / 'pool')], check=True, timeout=600)
        if wanted('store'):
            old = work / 'reference'
            (old / 'audio').mkdir(parents=True)
            (old / 'ram').mkdir()
            (old / 'audio' / 'overcue_flat.h').write_bytes(git_show(args.reference, 'audio/overcue_flat.h'))
            (old / 'ram' / 'lowzone.h').write_bytes(git_show(args.reference, 'ram/lowzone.h'))
            build(compiler, 'address', [ROOT / 'stem_pool.c', HERE / 'stem_store_reference.c', HERE / 'test_stem_store.c'],
                  work / 'store', ['-I' + str(old / 'audio'), '-Wno-unused-function'])
            for mode in ('rank', 'reference'):
                print(f'== store {mode} (address; reference {args.reference})', flush=True)
                subprocess.run([str(work / 'store'), mode], check=True, timeout=1200)
        protocol = [ROOT / 'stem_pool.c', HERE / 'test_stem_protocol.c']
        for sanitizer in ('address', 'thread'):
            if wanted('protocol'):
                build(compiler, sanitizer, protocol, work / f'protocol-{sanitizer}')
                print(f'== protocol ({sanitizer})', flush=True)
                subprocess.run(RUN_PREFIX.get(sanitizer, []) + [str(work / f'protocol-{sanitizer}'), args.protocol_seconds], check=True,
                               timeout=1200)
        if wanted('identity'):
            print(f'== identity: mixed audio of this stream against the stream of {args.reference}', flush=True)
            identity(compiler, work, args.reference)
        if wanted('mutation'):
            # The same test against a store whose giving-up step does not wait for readers: it must fail.
            copy = work / 'mutant'
            for name in ('audio/overcue_flat.h', 'audio/stem_pool.h', 'audio/stem_arbiter.h', 'audio/stem_pool.c',
                         'audio/tests/test_stem_protocol.c', 'ram/lowzone.h', 'xz_mutex.h', 'clock_compat.h'):
                (copy / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(MODS / name, copy / name)
            header = (copy / 'audio/overcue_flat.h').read_text()
            assert header.count(WAIT) == 1, 'the reader wait line moved: update WAIT'
            (copy / 'audio/overcue_flat.h').write_text(header.replace(WAIT, '        (void)f; return 1;\n'))
            for sanitizer in ('address', 'thread'):
                binary = work / f'mutant-{sanitizer}'
                build(compiler, sanitizer, [copy / 'audio/stem_pool.c', copy / 'audio/tests/test_stem_protocol.c'], binary)
                print(f'== protocol without the reader wait ({sanitizer}): must fail', flush=True)
                result = subprocess.run(RUN_PREFIX.get(sanitizer, []) + [str(binary), args.protocol_seconds], capture_output=True,
                                        text=True, timeout=1200)
                tail = (result.stdout + result.stderr).strip().splitlines()[-12:]
                print('\n'.join('   | ' + line for line in tail))
                if result.returncode == 0:
                    raise SystemExit('FAIL: the protocol test passed without the reader wait')
                print(f'   failed as it must (exit {result.returncode})')
    print('PASS verify_stem_pool')


if __name__ == '__main__':
    main()
