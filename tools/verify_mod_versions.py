"""Verify packaged version choices using disposable encrypted loader images.

Requires local official firmware and its boot support key. Generated images stay
in the explicitly supplied workspace; this verifier never targets a USB drive.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

APP=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(APP/'source/xdj-xz-toolkit'))
from builder import firmware, usb_settings


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('resources','workspace','firmware','key'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    work=args.workspace.resolve()
    work.mkdir(parents=True,exist_ok=False)
    env={**os.environ,'XZ_BUILDER_RESOURCES':str(args.resources.resolve()),'XZ_BUILDER_DATA':str(work/'app-data')}
    backend=args.resources/'backend'/('xz-mods-service.exe' if os.name=='nt' else 'xz-mods-service')
    def call(method,**fields):
        p=subprocess.run([str(backend.resolve())],input=json.dumps({'method':method,**fields}),capture_output=True,text=True,env=env,timeout=300)
        (work/'backend-events.jsonl').open('a').write(p.stdout)
        assert p.returncode==0,p.stderr
        data=json.loads(p.stdout.splitlines()[-1]);assert data.get('ok'),data
        return data['result']
    state=call('list_mod_versions')
    preferred=state['selected']['id'];reports=[]
    def select(ident):
        s=call('list_mod_versions')
        result=call('select_mod_version',mod_version_id=ident,pinned=True,expected_revision=s['selected']['revision'])
        assert result['selected']['id']==ident and result['selected']['pinned'] is True
    for entry in state['versions']:
        select(entry['id'])
        volume=work/entry['id'];volume.mkdir()
        result=call('build_usb',volume=str(volume),firmware=str(args.firmware),key=str(args.key),experimental=True,
                    mod_version_id=entry['id'],pin_mod_version=True)
        record=result['mod_version'];assert record['id']==entry['id'] and record['pinned'] is True
        archive=work/'app-data/mod-versions'/entry['id']
        data=Path(result['image']).read_bytes()
        plain=firmware.crypt(data,firmware.effective_key(args.key),True);iso=firmware._open(plain)
        try:
            for name,path in [('tools/libxz-mods.so','runtime/libxz-mods.so'),('tools/libxz-directfb-hook.so','runtime/libxz-receiver.so')]:
                assert firmware._member(iso,'/'+name)==(archive/path).read_bytes(),entry['label']
            metadata=json.loads(firmware._member(iso,'/xz-mod-version.json'))
            assert metadata==record
            boot=firmware._member(iso,'/autoexec.sh')
            assert hashlib.sha256(boot).hexdigest()==record['bootstrap_sha256']
            assert b'if false; then # XZ Mods: selected version is pinned; skip OTA apply' in boot
        finally:iso.close()
        reports.append({'id':entry['id'],'label':entry['label'],'runtime_sha256':entry['runtime_sha256'],
                        'loader_sha256':result['sha256'],'selected_pair_verified':True,'pin_verified':True})
        print('PASS encrypted loader:',entry['label'],flush=True)
    # An actual downgrade also tests the profile adaptation and loader backup.
    current=state['bundled_id'];select(current)
    volume=work/current
    (volume/'VJ.Tools').mkdir()
    schema=usb_settings.contract(args.resources)
    original=usb_settings.serialize(usb_settings.defaults(schema),schema)
    (volume/'VJ.Tools/XZ-Mods.cfg').write_bytes(original)
    (volume/'Contents').mkdir();(volume/'Contents/keep.txt').write_text('media sentinel')
    oldest=next(v for v in state['versions'] if v['channel']=='released')
    select(oldest['id']);before=call('inspect_usb',volume=str(volume));plan=call('inspect_mod_settings',volume=str(volume))
    change=plan['settings_change'];assert change and change['required']
    result=call('update_usb',volume=str(volume),firmware=str(args.firmware),key=str(args.key),experimental=True,
                expected_identity=before['identity'],expected_loader=before['loader'],mod_version_id=oldest['id'],
                pin_mod_version=True,adapt_mod_settings=True,expected_mod_settings=change['expected'])
    assert Path(result['settings_adjustment']['backup']).read_bytes()==original
    assert (volume/'Contents/keep.txt').read_text()=='media sentinel'
    assert result['inspection']['recorded_mod_version']['id']==oldest['id']
    assert len(result['inspection']['settings']['schema']['fields'])==len(usb_settings.contract(work/'app-data/mod-versions'/oldest['id'])['fields'])
    after=result['inspection']
    restored=call('restore_usb_loader',volume=str(volume),expected_identity=after['identity'],
                  expected_loader=after['loader'],transaction_id=result['transaction_id'])
    assert restored['inspection']['loader']==before['loader']
    assert restored['inspection']['recorded_mod_version']['id']==current
    select(preferred)
    report={'versions':reports,'downgrade_with_profile_backup':True,'restore_loader_verified':True,
            'preferred_restored':preferred,'physical_usb_written':False,'controller_tested':False}
    (work/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS downgrade, profile backup, recorded-version settings schema, loader restore and persistent choice')


if __name__=='__main__':main()
