"""DECK 3 / 4 loop adjust heard through the fake transport (tests/test_loop_adjust_jog.c).

Usage: python mods/tests/verify_loop_adjust_jog.py --zig PATH/zig.exe
Builds the test with zig (UndefinedBehaviorSanitizer) and runs it; then rebuilds it against a copy of
extra_player.c where the jog jumps on every tick again (the old rule), which must fail. Pure host code with
the player suite's fake transport; nothing touches a device.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

TESTS = Path(__file__).resolve().parent
MODS = TESTS.parent
sys.path.insert(0, str(MODS))
from extra_player_fixtures import fixtures  # noqa: E402

SOURCES = ['extra_player.c', 'key/keyshift.c', 'key/music.c', 'key/control.c', 'deck_select.c', 'anlz_extra.c',
           'shell_analysis.c', 'ui/layered_wave_source.c', 'ui/layered_wave.c', 'audio/stem_media.c',
           'audio/stem_mp3.c', 'audio/stem_decode.c']
GATE = ' if (loop_edge_cuts_queue(d, heard)) jump(d, loop_frame(d, heard), now);'
OLD = ' jump(d, loop_frame(d, heard), now);'

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
args = parser.parse_args()
cc = [str(Path(args.zig).resolve()), 'cc', '-O1', '-DXZ_XP_TEST_RENDER_HOOK', '-std=c11', '-Wall', '-Wextra',
      '-Werror', '-UNDEBUG', '-I' + str(MODS)]


def build_and_run(folder, player, name, extra=()):
    binary = folder / (name + '.exe')
    subprocess.run(cc + [*extra, '-fsanitize=undefined', str(player), *(str(MODS / s) for s in SOURCES[1:]),
                         str(TESTS / 'test_loop_adjust_jog.c'), '-lm', '-o', str(binary)], check=True)
    return subprocess.run([str(binary), str(folder)], cwd=folder, capture_output=True, text=True)


with tempfile.TemporaryDirectory(prefix='xz-loop-adjust-') as folder:
    folder = Path(folder)
    fixtures(folder)
    run = build_and_run(folder, MODS / 'extra_player.c', 'loop-adjust')
    print(run.stdout.strip())
    if run.returncode:
        raise SystemExit('loop adjust test failed\n' + run.stderr)
    source = (MODS / 'extra_player.c').read_text(encoding='utf-8')
    if source.count(GATE) != 1:
        raise SystemExit('Mutation target (the gated jump in jog) not found exactly once')
    mutant = folder / 'mutant'
    mutant.mkdir()
    (mutant / 'extra_player.c').write_text(source.replace(GATE, OLD), encoding='utf-8', newline='')
    broken = build_and_run(folder, mutant / 'extra_player.c', 'loop-adjust-old', ['-Wno-unused-function'])
    if broken.returncode == 0:
        raise SystemExit('A jump on every jog tick passed the test: the checks are not live')
    print('PASS the old rule (a jump on every jog tick) fails: ' +
          next((line for line in broken.stdout.splitlines() if line.startswith('FAIL')), '?'))
