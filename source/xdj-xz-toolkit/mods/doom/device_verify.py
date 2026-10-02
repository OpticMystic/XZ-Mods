"""Stage and test native Doom in isolated XZ RAM, preserving the DJ process."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import socket
import subprocess
import sys
import time
from PIL import Image

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parent))
from device_smoke import command, upload
from tools.xz_nand_dump import open_telnet

def transfer(host, client, helper, mode, path, size, data=None):
    with open_telnet(host, source_address=(client, 0)) as shell:
        redirect = '>' if mode == 'receive' else '<'
        shell.sendall(f'set -C; {helper} {mode} {size} {host} {client} {redirect} {shlex.quote(path)}; exit\r\n'.encode())
        deadline = time.monotonic()+5
        while True:
            try:
                connection=socket.create_connection((host,50008),timeout=.5,source_address=(client,0))
                break
            except OSError:
                if time.monotonic()>deadline: raise
                time.sleep(.1)
        with connection:
            connection.settimeout(30)
            if mode == 'receive':
                connection.sendall(data); connection.shutdown(socket.SHUT_WR)
                while connection.recv(1024): pass
                expected=hashlib.md5(data).hexdigest()
                if command(host,'md5sum '+shlex.quote(path)).split()[0]!=expected:
                    raise ValueError('Device transfer readback failed')
            else:
                result=bytearray()
                while len(result)<size:
                    chunk=connection.recv(min(262144,size-len(result)))
                    if not chunk: raise ValueError('Short device transfer')
                    result.extend(chunk)
                return bytes(result)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--wad',type=Path,required=True)
    p.add_argument('--zig',type=Path,required=True)
    p.add_argument('--evidence',type=Path,required=True)
    p.add_argument('--host',default='169.254.168.59')
    p.add_argument('--client',default='169.254.168.58')
    args=p.parse_args()
    before=command(args.host,'pidof rbp; md5sum /proc/$(pidof rbp)/exe; ls /lib/libc-2.13.so /lib/librt-2.13.so')
    pid=before.splitlines()[0]
    if not re.fullmatch(r'\d+',pid) or '6a7ccb454e52afa26a73f3380706c9ca' not in before:
        raise ValueError('Unknown firmware application')
    manifest=json.loads((args.build/'build.json').read_text())
    binary=(args.build/'xz-doom').read_bytes()
    if hashlib.sha256(binary).hexdigest()!=manifest['game']['sha256']: raise ValueError('Build changed')
    remote='/dev/shm/xz-doom-'+manifest['game']['sha256'][:12]
    if 'created=0' not in command(args.host,f'mkdir {remote}; echo created=$?'):
        raise ValueError('Choose a new build; its device directory already exists')
    helper=args.build/'transfer'
    subprocess.run([str(args.zig.resolve()),'cc','-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9',
                    '-O2','-s',str(ROOT.parent/'transfer.c'),'-o',str(helper)],check=True)
    upload(args.host,remote+'/transfer',helper.read_bytes())
    command(args.host,f'chmod 700 {remote}/transfer')
    for name,data in [('xz-doom',binary),('doom-controls-test',(args.build/'doom-controls-test').read_bytes()),
                      ('doom-native-input-test',(args.build/'doom-native-input-test').read_bytes()),
                      ('doom1.wad',args.wad.read_bytes())]:
        transfer(args.host,args.client,remote+'/transfer','receive',remote+'/'+name,len(data),data)
    tests=command(args.host,f'chmod 700 {remote}/xz-doom {remote}/doom-controls-test {remote}/doom-native-input-test; {remote}/doom-controls-test; echo CONTROLS_STATUS=$?; {remote}/doom-native-input-test; echo NATIVE_STATUS=$?')
    if 'CONTROLS_STATUS=0' not in tests or 'NATIVE_STATUS=0' not in tests: raise ValueError(tests)
    run=command(args.host,f'cd {remote} && ./xz-doom -nogui -nosound -iwad doom1.wad -warp 1 1 -xz-headless -xz-seconds 8 -xz-snapshot game.rgb565 >headless.log 2>&1; echo GAME_STATUS=$?',timeout=15)
    log=command(args.host,f'cat {remote}/headless.log; pidof rbp')
    if 'GAME_STATUS=0' not in run or 'XZ_DOOM_FRAME' not in log or not log.rstrip().endswith(pid):
        raise ValueError(run+'\n'+log)
    pixels=transfer(args.host,args.client,remote+'/transfer','send',remote+'/game.rgb565',800*480*2)
    args.evidence.mkdir(parents=True,exist_ok=False)
    Image.frombytes('RGB',(800,480),pixels,'raw','BGR;16').save(args.evidence/'native-game.png')
    receipt={'remote':remote,'before':before,'tests':tests,'run':run,'log':log,'game_sha256':manifest['game']['sha256'],
             'wad_sha256':hashlib.sha256(args.wad.read_bytes()).hexdigest(),'game_logic_on_xz':True,
             'display_verified':False,'physical_controls_verified':False,'usb_boot_verified':False}
    (args.evidence/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2))

if __name__=='__main__': main()
