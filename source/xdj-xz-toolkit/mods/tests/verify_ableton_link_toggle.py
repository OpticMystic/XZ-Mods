"""PLAY's ABLETON LINK control through the real shell runtime, on host, ARM and under sanitizers, without native I/O;
and the source facts that make its touch the NETWORK row's own action. The drawing is shell/test_ableton_link.c."""
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


# The touch reaches apply() as every queued panel action does, and apply() has one ABLETON LINK branch: the one the
# NETWORK row's press runs. So the setting, xz_link_runtime_set_enabled and the save are the same code.
shell_runtime = (root / 'shell_runtime.c').read_text()
ui_runtime = (root / 'ui_runtime.c').read_text()
assert 'case XZ_SHELL_ACT_ABLETON_LINK:ableton_action_locked(a->value,now);break;' in shell_runtime
assert 'ui_action_locked(XZ_UI_ABLETON_LINK,-1,0,(float)value,now)' in shell_runtime
assert re.search(r'struct xz_ui_action act=\{\.kind=\(enum xz_ui_action_kind\)kind,\.phase=XZ_UI_PRESS,\.deck=deck,'
                 r'\.index=index,\.value=value\};apply\(NULL,&act,1\);', ui_runtime)
branch = 'if(a->kind==XZ_UI_ABLETON_LINK){'
assert ui_runtime.count(branch) == 1
body = ui_runtime[ui_runtime.index(branch):].split('continue;', 1)[0]
assert 'model.ableton_link=a->value!=0?XZ_ABLETON_LINK_SEND:XZ_ABLETON_LINK_OFF' in body
assert 'xz_link_runtime_set_enabled(model.ableton_link)' in body and 'settings_queue()' in body
network = (root / 'ui/ui_network.c').read_text()
assert '.kind=XZ_UI_ABLETON_LINK,.phase=XZ_UI_PRESS,.deck=-1,.index=index,.value=m->ableton_link?0.f:1.f' in network
# The display thread reads one word: the frame's fill calls the glance, never xz_link_runtime_status (a mutex).
assert 'model.ableton=ableton_glance();' in shell_runtime and 'xz_link_runtime_status' not in shell_runtime
link = (root / 'link_runtime.c').read_text()
glance = link[link.index('void xz_link_runtime_glance('):].split('\n}\n', 1)[0]
assert '__atomic_load_n(&glance_word' in glance and 'mutex' not in glance and 'pthread' not in glance
# The control is drawn by the top bar, after the BEAT FX strip, and only there (not at the lane foot any more); its
# touch is tried with the top bar's own band, before the lanes.
play = (root / 'shell/shell_play.c').read_text()
top = play[play.index('static void render_top('):].split('\n}\n', 1)[0]
assert play.count('render_ableton(u,m,c);') == 1 and 'render_fx(m,c,fx_rect(u,m));\n render_ableton(u,m,c);' in top.replace('\r\n', '\n')
touch = play[play.index('static size_t touch_down('):].split('\n}\n', 1)[0]
assert touch.index('header_band(x,y,link_rect(u,m))') < touch.index('if(y<TOP_H)return 0;')
# One line that says LINK (the owner's choice, 2026-11-17), and a BEAT FX strip whose place does not depend on Link.
drawing = play[play.index('static void render_ableton('):].split('\n}\n', 1)[0]
assert drawing.count('"LINK"') == 1 and 'ABLETON"' not in drawing
assert 'case XZ_SHELL_R_FX:return (struct xz_shell_rect){FX_X,FX_Y,fx_right(m)-FX_X,FX_H};' in play
print('PASS sources: the touch queues XZ_UI_ABLETON_LINK for apply(), whose one branch the NETWORK row also runs; '
      'the frame reads the glance word only; the top bar draws the control (one line, LINK) and its band takes the '
      'touch; the BEAT FX strip keeps its place')

sys.path.insert(0, str(root / 'shell'))
from shell_sources import SHELL_SOURCES
sources = [root / 'tests/test_shell_ableton_link.c', root / 'shell_runtime_core.c',
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
