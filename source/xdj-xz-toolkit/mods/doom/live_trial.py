"""Load the reviewed Doom trial pair in XZ RAM, retaining an exact rollback."""
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
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT.parent))
from device_smoke import command,upload
from device_verify import transfer

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--wad',type=Path,required=True)
    p.add_argument('--helper',required=True)
    p.add_argument('--evidence',type=Path,required=True)
    p.add_argument('--host',default='169.254.168.59')
    p.add_argument('--client',default='169.254.168.58')
    args=p.parse_args()
    if not re.fullmatch(r'/dev/shm/xz-doom-[0-9a-f]{12}/transfer',args.helper): raise ValueError('Expected verified RAM transfer helper')
    pid=command(args.host,'pidof rbp').strip()
    if not re.fullmatch(r'\d+',pid): raise ValueError('Expected exactly one DJ application')
    if '6a7ccb454e52afa26a73f3380706c9ca' not in command(args.host,f'md5sum /proc/{pid}/exe'):
        raise ValueError('Unknown firmware application')
    environment=base64.b64decode(''.join(command(args.host,f'base64 /proc/{pid}/environ').splitlines()),validate=True)
    entries=environment.rstrip(b'\0').split(b'\0')
    env=dict(item.decode().split('=',1) for item in entries)
    preload=env.get('LD_PRELOAD','')
    old_mod='/dev/shm/libxz-mods.so'; old_receiver='/dev/shm/libxz-directfb-hook.so'
    if set(preload.split(':'))!={old_mod,old_receiver}: raise ValueError('Unknown active preload composition')
    argv=base64.b64decode(''.join(command(args.host,f'base64 /proc/{pid}/cmdline').splitlines()),validate=True).rstrip(b'\0').split(b'\0')
    argv=[a.decode() for a in argv]
    if argv[0] not in ('./rbp','/root/pdj/rbp') or any(not re.fullmatch(r'-[A-Za-z0-9]+',a) for a in argv[1:]):
        raise ValueError('Unknown DJ application arguments')
    runtime=(args.build/'libxz-mods-development.so').read_bytes()
    receiver=(args.build/'libxz-directfb-mods-test.so').read_bytes()
    game=(args.build/'doom/xz-doom').read_bytes()
    build=json.loads((args.build/'libxz-mods-development.json').read_text())
    doom=json.loads((args.build/'doom/build.json').read_text())
    for data,digest in [(runtime,build['sha256']),(receiver,build['display_bridge']['sha256']),(game,doom['game']['sha256'])]:
        if hashlib.sha256(data).hexdigest()!=digest: raise ValueError('Build artifacts changed')
    remote=f'/dev/shm/xz-doom-trial-{build["sha256"][:12]}-{pid}'
    if 'created=0' not in command(args.host,f'mkdir {remote}; echo created=$?'): raise ValueError('Trial already exists')
    for name,data in [('mods.so',runtime),('receiver.so',receiver),('xz-doom',game),('doom1.wad',args.wad.read_bytes()),
                      ('runtime-smoke',(args.build/'runtime-smoke').read_bytes())]:
        transfer(args.host,args.client,args.helper,'receive',remote+'/'+name,len(data),data)
    smoke=command(args.host,f'chmod 700 {remote}/xz-doom {remote}/runtime-smoke; {remote}/runtime-smoke {remote}/mods.so {remote}/receiver.so; echo SMOKE_STATUS=$?')
    if 'SMOKE_STATUS=0' not in smoke or command(args.host,'pidof rbp').strip()!=pid: raise ValueError(smoke)
    command(args.host,f'cp {old_mod} {remote}/previous-mods.so; cp {old_receiver} {remote}/previous-receiver.so; md5sum {remote}/previous-mods.so {remote}/previous-receiver.so')
    env_new=dict(env,LD_PRELOAD=remote+'/receiver.so:'+remote+'/mods.so',XZ_MODS_SETTINGS_READONLY='1')
    invocation=shlex.join(['/root/pdj/rbp']+argv[1:])
    env_old=dict(env,LD_PRELOAD=remote+'/previous-receiver.so:'+remote+'/previous-mods.so')
    def launch(e,log): return 'env -i '+shlex.join([k+'='+v for k,v in e.items()])+' '+invocation+' >'+log+' 2>&1 &'
    network=f'ifconfig eth0 {args.host} netmask 255.255.0.0 up; route add -net 169.254.0.0 netmask 255.255.0.0 dev eth0 2>/dev/null || true'
    rollback=f'#!/bin/sh\ntrap "" HUP\ncd /root/pdj || exit 1\nkill -TERM $(pidof xz-doom) 2>/dev/null\nkill -TERM $(pidof rbp) 2>/dev/null\nsleep 2\n{launch(env_old,remote+"/rollback.log")}\nsleep 5\n{network}\n'
    script=f'''#!/bin/sh
trap '' HUP
ulimit -c 0
cd /root/pdj || exit 1
kill -TERM {pid}
i=0
while kill -0 {pid} 2>/dev/null && [ "$i" -lt 5 ]; do sleep 1; i=$((i+1)); done
if [ "$(readlink /proc/{pid}/exe 2>/dev/null)" = /root/pdj/rbp ]; then kill -KILL {pid}; fi
cp {remote}/xz-doom /dev/shm/xz-doom
cp {remote}/doom1.wad /dev/shm/doom1.wad
chmod 700 /dev/shm/xz-doom
{launch(env_new,remote+'/application.log')}
new_pid=$!
echo "$new_pid" >{remote}/pid
sleep 6
{network}
if kill -0 "$new_pid" 2>/dev/null; then echo STARTED >{remote}/status; else sh {remote}/rollback.sh; echo ROLLED_BACK >{remote}/status; fi
'''
    for name,data in [('rollback.sh',rollback.encode()),('start.sh',script.encode())]:
        transfer(args.host,args.client,args.helper,'receive',remote+'/'+name,len(data),data)
    # The original environment is retained privately on the developer machine;
    # it never enters the public evidence receipt or the repository.
    private=args.build/'previous-environment.bin'; private.write_bytes(environment)
    command(args.host,f'cd /; sh {remote}/start.sh >{remote}/launcher.log 2>&1 </dev/null & sleep 0')
    time.sleep(10)
    after=command(args.host,f'cat {remote}/status; cat {remote}/pid; pidof rbp; tail -8 {remote}/application.log',timeout=10)
    if not after.startswith('STARTED\n'): raise ValueError(after)
    current=command(args.host,'pidof rbp').strip()
    maps=command(args.host,f'grep {remote} /proc/{current}/maps | head -4; md5sum /dev/shm/xz-doom /dev/shm/doom1.wad')
    args.evidence.mkdir(parents=True,exist_ok=False)
    receipt={'remote':remote,'pid_before':int(pid),'pid_after':int(current),'runtime_sha256':build['sha256'],
             'receiver_sha256':build['display_bridge']['sha256'],'game_sha256':doom['game']['sha256'],
             'smoke':smoke,'after':after,'maps':maps,'rollback_command':'sh '+remote+'/rollback.sh',
             'flash_written':False,'usb_written':False,'physical_controls_verified':False}
    (args.evidence/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2))

if __name__=='__main__': main()
