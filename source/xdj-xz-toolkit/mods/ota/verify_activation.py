"""Prove OTA trial/rollback and malformed-manifest gates in isolated XZ RAM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT.parent));sys.path.insert(0,str(ROOT.parent/'doom'))
from device_smoke import command
from device_verify import transfer
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--remote',required=True);p.add_argument('--evidence',type=Path,required=True);p.add_argument('--release',type=Path,required=True);a=p.parse_args()
    host='169.254.168.59';client='169.254.168.58';helper='/dev/shm/xz-doom-09b693fb2467/transfer';r=a.remote
    def stage(path,data):transfer(host,client,helper,'receive',path,len(data),data)
    stage(r+'/xz-updater-final',(a.build/'ota/xz-updater').read_bytes())
    command(host,f'mkdir {r}/ram; chmod 700 {r}/xz-updater-final; cp /dev/shm/libxz-mods.so {r}/ram/libxz-mods.so; cp /dev/shm/libxz-directfb-hook.so {r}/ram/libxz-directfb-hook.so; cp /dev/shm/xz-doom {r}/ram/xz-doom')
    stage(r+'/ram/xz-runtime-smoke',(a.build/'runtime-smoke').read_bytes());command(host,f'chmod 700 {r}/ram/xz-runtime-smoke')
    before=command(host,f'md5sum {r}/ram/libxz-mods.so {r}/ram/libxz-directfb-hook.so {r}/ram/xz-doom')
    applied=command(host,f'{r}/xz-updater-final apply {r}/usb unused {r}/ram; echo APPLY_STATUS=$?',timeout=25)
    assert 'APPLY_STATUS=0' in applied and 'TRIAL ' in applied,applied
    accept=command(host,f'{r}/xz-updater-final accept {r}/usb 4495 {r}/ram; echo ACCEPT_STATUS=$?; ls {r}/usb/VJTOOLS/ota')
    assert 'ACCEPT_STATUS=1' in accept and '\naccepted\n' not in accept,accept
    rejected=command(host,f'{r}/xz-updater-final reject {r}/usb unused {r}/ram; echo REJECT_STATUS=$?; md5sum {r}/ram/libxz-mods.so {r}/ram/libxz-directfb-hook.so {r}/ram/xz-doom')
    assert 'REJECT_STATUS=0' in rejected and before in rejected,rejected
    suppressed=command(host,f'{r}/xz-updater-final apply {r}/usb unused {r}/ram; echo FAILED_RETRY_STATUS=$?')
    assert 'FAILED_RETRY_STATUS=1' in suppressed,suppressed
    rollback=command(host,f'{r}/xz-updater-final rollback {r}/usb; {r}/xz-updater-final apply {r}/usb unused {r}/ram; echo BASE_STATUS=$?; pidof rbp')
    assert 'BASE_STATUS=2' in rollback and rollback.rstrip().endswith('4495'),rollback
    keypath=Path(os.environ['LOCALAPPDATA'])/'XZ Mods Release Keys/runtime-ota-ed25519.key';key=Ed25519PrivateKey.from_private_bytes(keypath.read_bytes())
    original=(a.release/'manifest.bin').read_bytes();vectors={}
    for label,mutate in [('wrong-target',lambda data:data.__setitem__(16,ord('Y'))),
                         ('path-traversal',lambda data:data.__setitem__(slice(116,148),b'../bad'+bytes(26))),
                         ('oversized-file',lambda data:struct.pack_into('<I',data,148,24*1024*1024)),
                         ('nonzero-reserved',lambda data:data.__setitem__(500,1)),
                         ('zero-sequence',lambda data:struct.pack_into('<I',data,12,0))]:
        data=bytearray(original[:512]);mutate(data);signed=bytes(data)+key.sign(bytes(data));stage(r+'/'+label+'.bin',signed)
        result=command(host,f'{r}/xz-updater-final verify {r}/usb {r}/{label}.bin; echo VECTOR_STATUS=$?')
        assert 'VECTOR_STATUS=1' in result,(label,result);vectors[label]=True
    # A higher accepted floor cannot be replaced by an older signed manifest.
    seq=struct.unpack_from('<I',original,12)[0];digest=hashlib.sha256(original[:512]).hexdigest()
    floor=f'{seq+1:08x} {digest} N\n'.encode();stage(r+'/usb/VJTOOLS/ota/accepted',floor)
    downgrade=command(host,f'{r}/xz-updater-final check {r}/usb http://169.254.168.58:50009; echo DOWNGRADE_STATUS=$?')
    assert 'DOWNGRADE_STATUS=1' in downgrade,downgrade
    record={'remote':r,'apply':applied,'accept_other_runtime_rejected':accept,'rollback':rejected,'failed_slot_suppressed':suppressed,'base_recovery':rollback,'malformed_signed_manifests':vectors,'downgrade':downgrade,'pid_unchanged':'4495','real_usb_written':False,'live_runtime_replaced':False}
    a.evidence.parent.mkdir(parents=True,exist_ok=True);a.evidence.write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
if __name__=='__main__':main()
