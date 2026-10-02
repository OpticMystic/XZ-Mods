"""Run WAD and OTA verification on the XZ without replacing the DJ runtime."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import time
import uuid
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT.parent));sys.path.insert(0,str(ROOT.parent/'doom'))
from device_smoke import command,upload
from device_verify import transfer
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--zig',type=Path,required=True);p.add_argument('--release',type=Path,required=True);p.add_argument('--evidence',type=Path,required=True);p.add_argument('--url',default='http://169.254.168.58:50009');a=p.parse_args()
    host='169.254.168.59';client='169.254.168.58';helper='/dev/shm/xz-doom-09b693fb2467/transfer'
    pid=command(host,'pidof rbp').strip();remote='/dev/shm/xz-wad-ota-'+uuid.uuid4().hex[:12]
    assert 'created=0' in command(host,f'mkdir {remote}; mkdir {remote}/usb {remote}/wads {remote}/wads/maps; echo created=$?')
    test=a.build/'wad-test'
    subprocess.run([str(a.zig.resolve()),'cc','-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9','-O2','-UNDEBUG','-std=c11','-Wall','-Wextra','-Werror',str(ROOT.parent/'doom/wad_catalog.c'),str(ROOT.parent/'audio/vendor/sha256/sha256.c'),str(ROOT.parent/'doom/test_wad_catalog.c'),'-o',str(test)],check=True)
    def stage(name,data):transfer(host,client,helper,'receive',remote+'/'+name,len(data),data)
    def wad(base,names):return (b'IWAD' if base else b'PWAD')+struct.pack('<II',len(names),12)+b''.join(struct.pack('<II8s',12,0,n.encode()) for n in names)
    fixtures={'full.wad':wad(True,['MAP01','PLAYPAL','TROOA1']), 'shareware.wad':wad(True,['E1M1','PLAYPAL','TROOA1']),
              'maps/map02.wad':wad(False,['MAP02','THINGS','LINEDEFS']), 'myhouse.wad':wad(False,['MAP01','TEXTMAP','ENDMAP']),
              'myhouse.pk3':b'PK\x03\x04placeholder', 'broken.wad':b'IWAD'+struct.pack('<II',1,0xffffffff)}
    for name,data in fixtures.items():stage('wads/'+name,data)
    stage('wad-test',test.read_bytes());stage('xz-updater',(a.build/'ota/xz-updater').read_bytes());stage('manifest.bin',(a.release/'manifest.bin').read_bytes())
    bad=bytearray((a.release/'manifest.bin').read_bytes());bad[512]^=1;stage('tampered.bin',bad)
    tests=command(host,f'chmod 700 {remote}/wad-test {remote}/xz-updater; {remote}/wad-test {remote}/wads; echo WAD_STATUS=$?; {remote}/xz-updater verify {remote}/usb {remote}/manifest.bin; echo SIGNATURE_STATUS=$?; {remote}/xz-updater verify {remote}/usb {remote}/tampered.bin; echo TAMPER_STATUS=$?')
    assert 'WAD_STATUS=0' in tests and 'SIGNATURE_STATUS=0' in tests and 'TAMPER_STATUS=1' in tests,tests
    check=command(host,f'{remote}/xz-updater check {remote}/usb {a.url}; echo CHECK_STATUS=$?',timeout=20)
    assert 'CHECK_STATUS=0' in check and 'AVAILABLE ' in check,check
    downloaded=command(host,f'{remote}/xz-updater stage {remote}/usb {a.url}; echo STAGE_STATUS=$?',timeout=40)
    assert 'STAGE_STATUS=0' in downloaded and 'STAGED ' in downloaded,downloaded
    repeated=command(host,f'{remote}/xz-updater stage {remote}/usb {a.url}; echo REPEAT_STATUS=$?; ls {remote}/usb/VJTOOLS/ota; pidof rbp',timeout=40)
    assert 'REPEAT_STATUS=0' in repeated and repeated.rstrip().endswith(pid),repeated
    assert '.incoming-' not in repeated,repeated
    a.evidence.mkdir(parents=True,exist_ok=False);(a.evidence/'receipt.json').write_text(json.dumps({'remote':remote,'pid_unchanged':pid,'tests':tests,'check':check,'download':downloaded,'retry':repeated,'usb_written':False,'runtime_replaced':False,'signature_tamper_rejected':True},indent=2)+'\n')
    print(json.dumps({'remote':remote,'tests':tests,'check':check,'download':downloaded,'retry':repeated},indent=2))
if __name__=='__main__':main()
