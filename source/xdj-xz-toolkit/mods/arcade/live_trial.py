"""Launch a reversible, hash-checked arcade RAM trial. Never flash or write USB."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import shlex
import sys
import time
from PIL import Image

root = Path(__file__).resolve().parent
sys.path[:0] = [str(root.parent), str(root.parent.parent / 'vendor')]
from device_smoke import command
from doom.device_verify import transfer
from tools.xz_firmware.mods_bundle import load_mods_bundle

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bundle', type=Path, required=True)
parser.add_argument('--helper', required=True)
parser.add_argument('--evidence', type=Path, required=True)
parser.add_argument('--game', choices=('arcade', 'wave-rider'), default='arcade')
args = parser.parse_args()
host, client = '169.254.168.59', '169.254.168.58'
if not re.fullmatch(r'/dev/shm/xz-doom-[0-9a-f]{12}/transfer', args.helper): raise ValueError('Expected verified RAM helper')
manifest = load_mods_bundle(args.bundle.resolve())
pid = command(host, 'pidof rbp').strip()
if not re.fullmatch(r'[0-9]+', pid): raise ValueError('Expected one DJ app')
if command(host, 'pidof xz-doom').strip(): raise ValueError('Doom is running; preserve its current session')
if not command(host, f'md5sum /proc/{pid}/exe').startswith('6a7ccb454e52afa26a73f3380706c9ca '): raise ValueError('Unqualified DJ executable')
raw_env = base64.b64decode(''.join(command(host, f'base64 /proc/{pid}/environ').splitlines()), validate=True)
env = dict(item.decode().split('=', 1) for item in raw_env.rstrip(b'\0').split(b'\0'))
tokens = env.get('LD_PRELOAD', '').split(':')
if len(tokens) != 2 or any(not re.fullmatch(r'/dev/shm/xz-[a-zA-Z0-9-]+/(?:receiver|mods)\.so', t) for t in tokens):
    raise ValueError('Current trial preload differs from the known isolated pair')
old_hashes = command(host, 'md5sum ' + ' '.join(shlex.quote(t) for t in tokens))
argv = base64.b64decode(''.join(command(host, f'base64 /proc/{pid}/cmdline').splitlines()), validate=True).rstrip(b'\0').split(b'\0')
argv = [a.decode() for a in argv]
if argv[0] not in ('./rbp', '/root/pdj/rbp') or any(not re.fullmatch(r'-[A-Za-z0-9]+', a) for a in argv[1:]): raise ValueError('Unknown DJ arguments')
remote = '/dev/shm/xz-arcade-trial-' + manifest['files']['libxz-mods.so'][:12] + '-' + pid
if 'CREATED=0' not in command(host, f'mkdir {remote}; echo CREATED=$?'): raise ValueError('Trial directory already exists')
for source, target in [('libxz-mods.so', 'mods.so'), ('libxz-receiver.so', 'receiver.so'), ('xz-runtime-smoke', 'runtime-smoke')]:
    data = (args.bundle / source).read_bytes()
    transfer(host, client, args.helper, 'receive', remote + '/' + target, len(data), data)
smoke = command(host, f'chmod 700 {remote}/runtime-smoke; {remote}/runtime-smoke {remote}/mods.so {remote}/receiver.so; echo SMOKE_STATUS=$?')
if 'SMOKE_STATUS=0' not in smoke or command(host, 'pidof rbp').strip() != pid: raise ValueError(smoke)
env_new = dict(env, LD_PRELOAD=remote + '/receiver.so:' + remote + '/mods.so', XZ_MODS_ARCADE_TRIAL='wave-rider' if args.game=='wave-rider' else '1', XZ_MODS_SETTINGS_READONLY='1')
invocation = shlex.join(['/root/pdj/rbp'] + argv[1:])
def launch(values, log): return 'env -i ' + shlex.join([k + '=' + v for k, v in values.items()]) + ' ' + invocation + ' >' + log + ' 2>&1 &'
network = f'ifconfig eth0 {host} netmask 255.255.0.0 up; route add -net 169.254.0.0 netmask 255.255.0.0 dev eth0 2>/dev/null || true'
rollback = f'''#!/bin/sh
trap '' HUP
cd /root/pdj || exit 1
trial_pid=$(cat {remote}/pid 2>/dev/null)
if [ "$(readlink /proc/$trial_pid/exe 2>/dev/null)" = /root/pdj/rbp ]; then kill -TERM "$trial_pid"; sleep 2; fi
{launch(env, remote + '/rollback.log')}
sleep 5
{network}
'''
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
for name, data in [('rollback.sh', rollback.encode()), ('start.sh', start.encode())]: transfer(host, client, args.helper, 'receive', remote + '/' + name, len(data), data)
private = args.bundle.parent / 'private-previous-environment.bin'
private.write_bytes(raw_env)
command(host, f'sh {remote}/start.sh >{remote}/launcher.log 2>&1 </dev/null & sleep 0')
time.sleep(10)
after = command(host, f'cat {remote}/status; cat {remote}/pid; pidof rbp; tail -12 {remote}/application.log')
if not after.startswith('STARTED\n'): raise RuntimeError(after)
current = command(host, 'pidof rbp').strip()
if current != after.splitlines()[1]: raise RuntimeError('Trial app PID changed')
pixels = transfer(host, client, args.helper, 'send', '/dev/fb0', 800 * 480 * 2)
args.evidence.mkdir(parents=True, exist_ok=False)
Image.frombytes('RGB', (800, 480), pixels, 'raw', 'BGR;16').save(args.evidence / 'screen.png')
receipt = {'remote': remote, 'pid_before': int(pid), 'pid_after': int(current), 'after': after,
           'runtime_sha256': manifest['files']['libxz-mods.so'], 'receiver_sha256': manifest['files']['libxz-receiver.so'],
           'previous_runtime_md5': old_hashes, 'smoke': smoke, 'flash_written': False, 'usb_written': False,
           'rollback_command': 'sh ' + remote + '/rollback.sh', 'screen_sha256': hashlib.sha256(pixels).hexdigest(),
           'physical_controls_verified': False, 'track_phase_verified': False}
(args.evidence / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt, indent=2))
