"""DECK VIEW follows the loopback cable on MODERN, through the real shell runtime, on host, ARM and under sanitizers,
without native I/O; and the source facts that put it on the owner tick and the deck-select path. The run itself is
tests/test_shell_deck_view_cable.c."""
import argparse
import os
from pathlib import Path
import re
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


def body(text, head):
    assert text.count(head) == 1, head
    return text[text.index(head):].split('\n}\n', 1)[0]


shell_runtime = (root / 'shell_runtime.c').read_text()
ui_runtime = (root / 'ui_runtime.c').read_text()
# The owner tick (native event thread) is the one caller: never a frame, a refresh or an audio path.
assert shell_runtime.count('cable_tick_locked(') == 2 and shell_runtime.count('cable_sides(') == 2
tick = body(shell_runtime, 'void xz_shell_runtime_owner_tick(void) {')
step = tick.index('int cable_back=cable_tick_locked(now);')
unlock = tick.index('pthread_mutex_unlock(&mutex);\n cable_sides(cable_back);')
assert tick.index('lock_holder="tick";') < step < unlock
for name in ['static void refresh_locked(', 'int xz_shell_runtime_frame(', 'static void extra_refresh_locked(']:
    assert 'cable_' not in body(shell_runtime, name).replace('cable_stock_window', ''), name
# The step reads published state only: the route's control bit, DECK VIEW, the last refresh's stock window.
locked = body(shell_runtime, 'static int cable_tick_locked(uint32_t now) {')
assert 'xz_extra_decks_route().route_up' in locked and 'xz_stock_dialog_active' not in locked
assert 'xz_runtime_deck_select' not in locked and 'xz_extra_decks_snapshot' not in locked
# The sides go back through the deck-select path the named DECK buttons use, after the shell's lock.
sides = body(shell_runtime, 'static void cable_sides(int back) {')
assert 'xz_runtime_deck_select_target(side,0)' in sides
runtime = (root / 'runtime.c').read_text()
assert re.search(r'int xz_runtime_deck_select_target\(int side, int extra\) \{\n.*\n    return deck_select_apply\(side,extra\);', runtime)
# The view is the saved DECK VIEW setting: ui_runtime.c saves any xz_deck_layout_set like a MODS change.
follow = body(ui_runtime, 'static void deck_view_follow_locked(void){')
assert 'settings_queue();' in follow and 'xz_deck_layout_revision()' in follow
assert 'deck_view_follow_locked();' in body(ui_runtime, 'static void deck_status_refresh_locked(void){')
assert 'xz_deck_layout_set(view);' in body(shell_runtime, 'static void cable_view_locked(int view) {')
print('PASS sources: owner tick only, published state only, deck select after the lock, the saved DECK VIEW setting')

sys.path.insert(0, str(root / 'shell'))
from shell_sources import SHELL_SOURCES
sources = [root / 'tests/test_shell_deck_view_cable.c', root / 'shell_runtime_core.c',
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
    print(name + ': ' + run([*runner, binary]).stdout.strip().replace('\n', '\n' + name + ': '), flush=True)
