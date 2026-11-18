"""MODERN's quiet notices through the real shell runtime, on host, ARM and under sanitizers, without native I/O
(owner 2026-11-17: no notices over the waveforms). The drawing is shell/test_notice_place.c; the policy table and
CLASSIC's top-bar notice are ui/test_notice_place.c."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--out', type=Path, required=True)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=True)


def linux(path):
    value = str(path.resolve()).replace('\\', '/')
    return '/mnt/' + value[0].lower() + value[2:] if os.name == 'nt' else value


def run(command):
    prefix = ['wsl', '-d', 'Ubuntu', '--exec', 'env', 'TMPDIR=' + linux(args.out)] if os.name == 'nt' else []
    result = subprocess.run(prefix + command, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(' '.join(command) + '\n' + result.stdout + result.stderr)
    return result


# Every notice the runtime raises goes through notice_locked, which asks the policy first.
shell_runtime = (root / 'shell_runtime.c').read_text()
locked = shell_runtime[shell_runtime.index('static void notice_locked(uint32_t now, const char *text) {'):]
locked = locked.split('\n}\n', 1)[0]
assert locked.index('xz_notice_quiet(text)') < locked.index('snprintf(notice_text')
assert shell_runtime.count('model.notice=') == 1, 'the published notice has one writer'
print('PASS sources: notice_locked asks ui/notice_policy.h before it shows anything')

sys.path.insert(0, str(root / 'shell'))
from shell_sources import SHELL_SOURCES
sources = [root / 'tests/test_shell_quiet_notice.c', root / 'shell_runtime_core.c',
           *(root / 'shell' / path for path in SHELL_SOURCES)]
for name, compiler, objcopy, runner, sanitizer in [
        ('host', 'gcc', 'objcopy', [], []),
        ('arm', 'arm-linux-gnueabi-gcc', 'arm-linux-gnueabi-objcopy', ['qemu-arm', '-L', '/usr/arm-linux-gnueabi'], []),
        ('asan', 'gcc', 'objcopy', [], ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie'])]:
    common = [compiler, '-std=c11', '-O1', '-g', '-UNDEBUG', '-ffunction-sections', '-fdata-sections',
              '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', '-Wno-format-truncation', *sanitizer]
    obj = linux(args.out / (name + '.o'))
    binary = linux(args.out / name)
    run([*common, '-c', linux(sources[0]), '-o', obj])
    run([objcopy, '--remove-section=.init_array', '--remove-section=.fini_array', obj])
    run([*common, '-Wl,--gc-sections', obj, *map(linux, sources[1:]), '-pthread', '-ldl', '-lm', '-o', binary])
    print(name + ': ' + run([*runner, binary]).stdout.strip(), flush=True)
