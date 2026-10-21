"""Run the actual shell refresh over fake snapshots, on host and ARM, without native I/O."""
import argparse
import hashlib
import json
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
    transcript.append({'command': prefix + command, 'exit_code': result.returncode,
                       'output': result.stdout + result.stderr})
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result

sys.path.insert(0,str(root / 'shell'))
from shell_sources import SHELL_SOURCES
sources = [root / 'tests/test_shell_control_focus.c', root / 'shell_runtime_core.c',
           *(root / 'shell' / path for path in SHELL_SOURCES)]
inputs = [*sources, root / 'shell_runtime.c', root / 'shell/shell.h', root / 'shell/shell_info.h']
before = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
transcript = []
try:
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
finally:
    after = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    (args.out / 'receipt.json').write_text(json.dumps({'sources': before, 'source_unchanged': before == after,
        'commands': transcript, 'hardware_contacted': False}, indent=2) + '\n', encoding='utf-8')
assert before == after, 'Source changed during verification'
