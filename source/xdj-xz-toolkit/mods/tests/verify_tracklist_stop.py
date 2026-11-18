"""USB STOP and the recording tracklist: the tracklist as a holder of the stick it writes to. Builds and runs the core
test (tests/tracklist/test_tracklist.c, which has the stop's list text) and the holder test
(tests/tracklist/test_tracklist_stop.c: real sampler and writer threads, the holders' board and its wait from
usb_release.c, a folder standing in for the stick). Source only: no firmware, no device. verify_recorder.py runs the
same tests with ASan+UBSan and as ARM32 under qemu, and the mutation check; this one adds TSan.

Windows: python tests/verify_tracklist_stop.py --out <scratch> [--sanitizer address,undefined|thread]
Linux: the same. Everything it builds and writes stays under --out.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

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
        command = ['wsl', '-d', 'Ubuntu', '--cd', linux(ROOT), '--exec', 'env', 'TMPDIR=' + linux(out / 'tmp'), *command]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=900)
    (out / (label + '.log')).write_bytes(result.stdout + result.stderr)
    with (out / 'commands.jsonl').open('a') as log:
        log.write(json.dumps({'label': label, 'command': command, 'exit': result.returncode}) + '\n')
    print(label, result.returncode, flush=True)
    if result.returncode:
        print((result.stdout + result.stderr).decode(errors='replace')[-8000:])
        raise SystemExit(1)
    return result


flags = ['gcc', '-std=c11', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-fno-sanitize-recover=all',
         '-fsanitize=' + args.sanitizer]
if args.sanitizer == 'thread':
    flags += ['-fno-pie', '-no-pie']

run('link-core', flags + ['-D_GNU_SOURCE', 'tracklist.c', 'tests/tracklist/test_tracklist.c', '-o', linux(out / 'test-tracklist')])
print(run('core', [linux(out / 'test-tracklist')]).stdout.decode(), end='')
run('link-stop', flags + ['-pthread', '-Wno-format-truncation', '-Wno-unused-function', '-I.', '-DXZ_USB_RELEASE_NO_RUNTIME',
                          'tracklist.c', 'tracklist_file.c', 'usb_release.c', 'tests/tracklist/test_tracklist_stop.c',
                          '-o', linux(out / 'test-tracklist-stop')])
print(run('stop', [linux(out / 'test-tracklist-stop'), linux(out / 'stick')]).stdout.decode(), end='')
print('PASS verify_tracklist_stop', flush=True)
