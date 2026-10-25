"""Reproduce shallow-borrow UAF and verify the current-view copy on host/ARM.

No native firmware, controller, USB or network is accessed.
"""
import argparse
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--out', required=True, type=Path)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=False)
root = Path(__file__).resolve().parents[1]

def linux(path):
    s = path.resolve().as_posix()
    return '/mnt/' + s[0].lower() + s[2:]

results = []
for arch, compiler in [('host', 'gcc'), ('arm', 'arm-linux-gnueabi-gcc')]:
    binary = a.out / ('wave-copy-' + arch)
    flags = ['-std=c11', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
             '-Wno-misleading-indentation', '-pthread', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections']
    flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie'] if arch == 'host' else ['-march=armv7-a']
    commands = [[compiler, *flags, linux(root/'tests/test_jog_wave_copy.c'), '-lm', '-o', linux(binary)]]
    run = (['qemu-arm', '-L', '/usr/arm-linux-gnueabi'] if arch == 'arm' else []) + [linux(binary)]
    commands.append(run)
    for i, command in enumerate(commands):
        r = subprocess.run(['wsl', '-d', 'Ubuntu', '--exec', *command], capture_output=True, text=True, timeout=60)
        (a.out/f'{arch}-{i}.log').write_text(r.stdout+r.stderr)
        results.append({'command': command, 'returncode': r.returncode})
        if r.returncode:
            print(r.stdout+r.stderr)
            raise SystemExit(r.returncode)
        print(r.stdout, end='')
    if arch == 'host':
        r = subprocess.run(['wsl', '-d', 'Ubuntu', '--exec', *run, 'unsafe-borrow'], capture_output=True, text=True, timeout=60)
        (a.out/'unsafe-borrow-red.log').write_text(r.stdout+r.stderr)
        assert r.returncode != 0 and 'heap-use-after-free' in r.stderr
        results.append({'command': run+['unsafe-borrow'], 'returncode': r.returncode, 'expected': 'heap-use-after-free'})
        print('RED: old shallow borrow reports heap-use-after-free after retirement')
    connector=a.out/('connector-'+arch)
    sources=['extra_decks.c', 'extra_cue_store.c', 'extra_player.c', 'deck_select.c', 'anlz_extra.c', 'shell_analysis.c',
             'ui/layered_wave_source.c', 'ui/layered_wave.c', 'audio/stem_media.c', 'audio/stem_mp3.c',
             'audio/stem_decode.c', 'test_extra_decks.c']
    command=[compiler,*flags,'-DXZ_XP_TEST_RENDER_HOOK','-DXZ_PLACEMENT_CONNECTOR_TEST',
             '-DUSB_MAJOR=1','-DPOLL_DOWN_MS=50','-DPOLL_UP_MS=5',
             *[linux(root/s) for s in sources],'-lm','-o',linux(connector)]
    for i,cmd in enumerate([command,run[:-1]+[linux(connector)]]):
        r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
        (a.out/f'{arch}-connector-{i}.log').write_text(r.stdout+r.stderr)
        results.append({'command':cmd,'returncode':r.returncode})
        if r.returncode:
            print(r.stdout+r.stderr)
            raise SystemExit(r.returncode)
        print(r.stdout,end='')
(a.out/'results.json').write_text(json.dumps(results, indent=2)+'\n')
