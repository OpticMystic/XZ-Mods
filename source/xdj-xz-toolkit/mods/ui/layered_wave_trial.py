"""Restart only rbp onto a RAM build with the layered waveform, keeping an exact rollback.

Nothing is written to flash or USB. The current preload pair is copied beside the
trial so `sh <remote>/rollback.sh` (or a power cycle) returns to it.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import shlex
import sys
import time
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from device_smoke import command, upload

APP_MD5 = '6a7ccb454e52afa26a73f3380706c9ca'


def blob(host, path):
    return base64.b64decode(''.join(command(host, f'base64 {path}').splitlines()), validate=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True, help='build.py --output folder')
    p.add_argument('--evidence', type=Path, required=True)
    p.add_argument('--wave-mode', type=int, choices=(0, 1, 2), default=1)
    p.add_argument('--host', default='169.254.168.59')
    p.add_argument('--helper', help='Verified RAM transfer helper already on the deck (fast, md5 readback)')
    a = p.parse_args()
    pid = command(a.host, 'pidof rbp').strip()
    if not re.fullmatch(r'\d+', pid):
        raise ValueError('Expected exactly one DJ application')
    if APP_MD5 not in command(a.host, f'md5sum /proc/{pid}/exe'):
        raise ValueError('Unknown firmware application')
    environment = blob(a.host, f'/proc/{pid}/environ')
    env = dict(e.decode().split('=', 1) for e in environment.rstrip(b'\0').split(b'\0'))
    preload = env.get('LD_PRELOAD', '').split(':')
    if len(preload) != 2 or not all(re.fullmatch(r'/dev/shm/[A-Za-z0-9_./-]+\.so', x) for x in preload):
        raise ValueError(f'Unknown active preload composition: {preload}')
    argv = [x.decode() for x in blob(a.host, f'/proc/{pid}/cmdline').rstrip(b'\0').split(b'\0')]
    if argv[0] not in ('./rbp', '/root/pdj/rbp') or any(not re.fullmatch(r'-[A-Za-z0-9]+', x) for x in argv[1:]):
        raise ValueError('Unknown DJ application arguments')
    runtime = (a.build / 'libxz-mods-development.so').read_bytes()
    receiver = (a.build / 'libxz-directfb-mods-test.so').read_bytes()
    build = json.loads((a.build / 'libxz-mods-development.json').read_text())
    if hashlib.sha256(runtime).hexdigest() != build['sha256']:
        raise ValueError('Runtime differs from its build record')
    def send(path, data):
        if a.helper:
            sys.path.insert(0, str(ROOT / 'doom'))
            from device_verify import transfer
            transfer(a.host, '169.254.168.58', a.helper, 'receive', path, len(data), data)
        else:
            upload(a.host, path, data)
    remote = f'/dev/shm/xz-wave-trial-{build["sha256"][:12]}-{pid}'
    if 'created=0' not in command(a.host, f'mkdir {remote}; echo created=$?'):
        raise ValueError('Trial directory already exists')
    for name, data in (('mods.so', runtime), ('receiver.so', receiver),
                       ('runtime-smoke', (a.build / 'runtime-smoke').read_bytes())):
        send(f'{remote}/{name}', data)
    smoke = command(a.host, f'chmod 700 {remote}/runtime-smoke; {remote}/runtime-smoke {remote}/mods.so {remote}/receiver.so; echo SMOKE_STATUS=$?', 30)
    if 'SMOKE_STATUS=0' not in smoke or command(a.host, 'pidof rbp').strip() != pid:
        raise ValueError(smoke)
    old_receiver, old_mod = preload
    command(a.host, f'cp {old_receiver} {remote}/previous-receiver.so; cp {old_mod} {remote}/previous-mods.so')
    env_new = dict(env, LD_PRELOAD=f'{remote}/receiver.so:{remote}/mods.so', XZ_MODS_SETTINGS_READONLY='1', XZ_MODS_NATIVE_VIEW='1',
                   XZ_MODS_WAVE_MODE=str(a.wave_mode))
    env_old = dict(env, LD_PRELOAD=f'{remote}/previous-receiver.so:{remote}/previous-mods.so')
    invocation = shlex.join(['/root/pdj/rbp'] + argv[1:])
    def launch(e, log):
        return 'env -i ' + shlex.join([k + '=' + v for k, v in e.items()]) + ' ' + invocation + ' >' + log + ' 2>&1 &'
    network = f'ifconfig eth0 {a.host} netmask 255.255.0.0 up; route add -net 169.254.0.0 netmask 255.255.0.0 dev eth0 2>/dev/null || true'
    rollback = f'#!/bin/sh\ntrap "" HUP\ncd /root/pdj || exit 1\nkill -TERM $(pidof rbp) 2>/dev/null\nsleep 3\n{launch(env_old, remote + "/rollback.log")}\nsleep 5\n{network}\n'
    start = f'''#!/bin/sh
trap '' HUP
ulimit -c 0
cd /root/pdj || exit 1
kill -TERM {pid}
i=0
while kill -0 {pid} 2>/dev/null && [ "$i" -lt 5 ]; do sleep 1; i=$((i+1)); done
if [ "$(readlink /proc/{pid}/exe 2>/dev/null)" = /root/pdj/rbp ]; then kill -KILL {pid}; fi
{launch(env_new, remote + '/application.log')}
new_pid=$!
echo "$new_pid" >{remote}/pid
sleep 6
{network}
if kill -0 "$new_pid" 2>/dev/null; then echo STARTED >{remote}/status; else sh {remote}/rollback.sh; echo ROLLED_BACK >{remote}/status; fi
'''
    send(f'{remote}/rollback.sh', rollback.encode())
    send(f'{remote}/start.sh', start.encode())
    command(a.host, f'cd /; sh {remote}/start.sh >{remote}/launcher.log 2>&1 </dev/null & sleep 0')
    time.sleep(12)
    after = command(a.host, f'cat {remote}/status; cat {remote}/pid; pidof rbp; tail -5 {remote}/application.log; tail -12 /dev/shm/xz-mods.log', 15)
    a.evidence.mkdir(parents=True, exist_ok=False)
    receipt = {'remote': remote, 'pid_before': int(pid), 'runtime_sha256': build['sha256'], 'wave_mode': a.wave_mode,
               'smoke': smoke, 'after': after, 'rollback_command': f'sh {remote}/rollback.sh',
               'flash_written': False, 'usb_written': False}
    (a.evidence / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))
    if not after.startswith('STARTED\n'):
        raise RuntimeError('Trial did not stay running; the rollback script ran')


if __name__ == '__main__':
    main()
