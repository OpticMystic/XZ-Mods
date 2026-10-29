"""Seeded SYNC/MASTER simulation of the real own-C four-deck chain; host sanitizers and ARM32.

Builds tests/test_sync_master_sim.c with the real players, snapshot, master
observer/bridge, deck selection, LED hook and jog snapshot, then runs:
  1. the named regressions (each one failed before its fix; stories that model
     one stock behaviour report NOTE and do not gate);
  2. a seed batch on a fixed stock profile, which gates;
  3. a seeded lock batch (master/follower pairs, VINYL and CDJ, disturbances),
     checking tempo and beat lock and the published beat stream against what
     the decks actually play, which gates;
  4. optionally (--explore) a seed batch on randomly drawn stock policies,
     which reports the stock-dependent classes without gating.
Every failure prints its class, the seed, the schedule and a minimized action list.
Usage: python mods/tests/verify_sync_master_sim.py --out DIR [--arch host|arm|all]
       [--seeds 400] [--lock-seeds 200] [--first-seed 1] [--run-ms 15000] [--explore]
       [--stage /dev/shm/DIR]
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', required=True, type=Path)
p.add_argument('--arch', choices=['host', 'arm', 'all'], default='all')
p.add_argument('--seeds', type=int, default=400)
p.add_argument('--first-seed', type=int, default=1)
p.add_argument('--run-ms', type=int, default=15000)
p.add_argument('--lock-seeds', type=int, default=200)
p.add_argument('--explore', action='store_true')
p.add_argument('--stage', help='Linux scratch folder (default /tmp/xz-sync-master-sim-<hash>)')
a = p.parse_args()
mods = Path(__file__).resolve().parents[1]
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=True)
SOURCES = ['tests/test_sync_master_sim.c', 'tests/sync_master_sim_decks.c', 'tests/sync_master_sim_bridge.c',
           'tests/sync_master_sim_led.c', 'extra_player.c', 'key/keyshift.c', 'key/music.c', 'key/control.c',
           'deck_select.c', 'anlz_extra.c', 'shell_analysis.c', 'ui/layered_wave_source.c', 'ui/layered_wave.c',
           'audio/stem_media.c', 'audio/stem_mp3.c', 'audio/stem_decode.c', 'sync_link_plan.c',
           'jog_display_runtime.c', 'jog_display_wave.c', 'jog_display.c']
# Stock behaviour the code cannot know is drawn per seed unless fixed here. This profile keeps
# master on a stopped native deck, moves it off an unloaded one when it can, never re-asserts it,
# never turns it off or ignores a press, and calls the sync manager at least every 50 ms. Classes
# that only exist under some stock behaviour (no master at all; master left on an unloaded deck)
# are reported as NOTE lines, not failures.
GATED = {'SIM_REASSERT': '0', 'SIM_TOGGLE': '0', 'SIM_EMPTY': '1', 'SIM_UNLOAD': '1', 'SIM_STOP': '0',
         'SIM_CONSUMER_MAX': '50', 'SIM_STOCK_CLASSES_REPORT_ONLY': '1'}


def linux(path):
    s = Path(path).resolve().as_posix()
    return '/mnt/' + s[0].lower() + s[2:] if os.name == 'nt' else s


def sh(cmd, env=None, label=None):
    prefix = ['wsl', '-d', 'Ubuntu', '--exec'] if os.name == 'nt' else []
    if env:
        cmd = ['env', *[k + '=' + v for k, v in env.items()], *cmd]
    r = subprocess.run(prefix + cmd, capture_output=True, timeout=3600,
                       env=dict(os.environ, MSYS_NO_PATHCONV='1'))
    if label:
        (out / (label + '.stdout')).write_bytes(r.stdout)
        (out / (label + '.stderr')).write_bytes(r.stderr)
    return r


receipt = {'inputs': {s: hashlib.sha256((mods / s).read_bytes()).hexdigest() for s in SOURCES}, 'runs': [],
           'hardware': False, 'gated_profile': GATED}
failed = False
# The fixture WAV lives on the Linux filesystem: reading it through a Windows mount is slow.
stage = a.stage or '/tmp/xz-sync-master-sim-' + hashlib.sha256(str(out).encode()).hexdigest()[:12]
sh(['mkdir', '-p', stage + '/data/anlz'])
for arch, cc in [('host', 'gcc'), ('arm', 'arm-linux-gnueabi-gcc')]:
    if a.arch not in ['all', arch]:
        continue
    flags = ['-std=c11', '-O1', '-g', '-UNDEBUG', '-DXZ_XP_TEST_RENDER_HOOK', '-Wall', '-Wextra', '-Werror',
             '-Wno-misleading-indentation', '-Wno-unused-function', '-pthread', '-ffunction-sections',
             '-fdata-sections', '-I' + linux(mods)]
    # Host: non-PIE keeps the LED callback's 32-bit record addresses below 4 GiB.
    flags += (['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-no-pie']
              if arch == 'host' else ['-mcpu=cortex-a9', '-mfpu=vfpv3', '-mfloat-abi=softfp'])
    objects = []
    for i, source in enumerate(SOURCES):
        obj = stage + '/' + arch + '-' + str(i) + '.o'
        r = sh([cc, *flags, '-c', linux(mods / source), '-o', obj], label=arch + '-compile-' + str(i))
        receipt['runs'].append({'label': arch + '-compile ' + source, 'exit': r.returncode})
        if r.returncode:
            raise SystemExit(r.stderr.decode(errors='replace'))
        objects.append(obj)
    binary = stage + '/sim-' + arch
    r = sh([cc, *flags, *objects, '-Wl,--gc-sections', '-lm', '-o', binary], label=arch + '-link')
    if r.returncode:
        raise SystemExit(r.stderr.decode(errors='replace'))
    run = (['qemu-arm', '-cpu', 'cortex-a9', '-L', '/usr/arm-linux-gnueabi'] if arch == 'arm' else []) + [binary, stage + '/data']
    env = {'ASAN_OPTIONS': 'detect_leaks=1:abort_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'} if arch == 'host' else {}
    jobs = [('regress', run + ['regress'], env)]
    jobs.append(('seeds', run + [str(a.first_seed), str(a.seeds), str(a.run_ms), '--minimize'], {**env, **GATED}))
    if a.lock_seeds:
        jobs.append(('lock', run + ['lock', str(a.first_seed), str(a.lock_seeds), '30000'], {**env, **GATED}))
    if a.explore:
        jobs.append(('explore', run + [str(a.first_seed), str(a.seeds), str(a.run_ms), '--minimize'], env))
    for name, cmd, job_env in jobs:
        label = arch + '-' + name
        r = sh(cmd, env=job_env, label=label)
        text = r.stdout.decode(errors='replace')
        summary = [line for line in text.splitlines() if line.startswith(('PASS', 'FAIL', 'NOTE', 'CLASS', 'RESULT', 'LOCK'))]
        receipt['runs'].append({'label': label, 'command': cmd, 'env': job_env, 'exit': r.returncode, 'summary': summary})
        print(label, 'exit', r.returncode)
        for line in summary:
            print(' ', line)
        if r.returncode and name != 'explore':
            failed = True
            print(r.stderr.decode(errors='replace')[-2000:])
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
assert receipt['inputs'] == {s: hashlib.sha256((mods / s).read_bytes()).hexdigest() for s in SOURCES}
print('FAIL' if failed else 'PASS', 'SYNC/MASTER simulation: regressions, gated seed batch and lock batch')
raise SystemExit(int(failed))
