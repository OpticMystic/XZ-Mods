"""Check the frozen layered-data operation on copied analysis files only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--resources',type=Path,required=True)
p.add_argument('--corpus',type=Path,required=True)
p.add_argument('--evidence',type=Path,required=True)
a=p.parse_args()
if a.evidence.exists():raise ValueError('Choose a new evidence file')
backend=a.resources.resolve()/'backend/xz-mods-service.exe'
env={**os.environ,'XZ_BUILDER_RESOURCES':str(a.resources.resolve()),'PATH':str(Path(os.environ['SystemRoot'])/'System32')}
env.pop('PYTHONPATH',None);env.pop('PYTHONHOME',None)
with tempfile.TemporaryDirectory(prefix='xz-layered-backend-') as temporary:
    root=Path(temporary);usb=root/'usb';originals={}
    for i,source in enumerate(sorted(a.corpus.rglob('ANLZ0000.EXT'))):
        if not source.with_suffix('.2EX').is_file():continue
        target=usb/'PIONEER/USBANLZ/P000'/f'{i:08X}'/source.name;target.parent.mkdir(parents=True)
        for suffix in ('.EXT','.2EX'):
            dest=target.with_suffix(suffix);shutil.copyfile(source.with_suffix(suffix),dest);originals[dest]=dest.read_bytes()
    assert originals,'No paired analysis files'
    def call():
        proc=subprocess.run([str(backend)],input=json.dumps({'method':'prepare_layered_waveforms','volume':str(usb)}),
            capture_output=True,text=True,encoding='utf8',env=env,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW)
        assert proc.returncode==0,proc.stderr
        return json.loads(proc.stdout.splitlines()[-1])
    first=call();assert first['ok'] and first['result']['prepared']==len(originals)//2 and first['result']['failed_count']==0,first
    files=list((usb/'CDJMODS/waveform-3band').glob('*.xzw'));assert len(files)==len(originals)//2
    sizes=[]
    for file in files:
        raw=file.read_bytes();assert raw[:8]==b'XZ3BAND1'
        count,norm,length=struct.unpack_from('>III',raw,8)
        assert 0<count<=1000000 and 0<norm<=1020 and len(raw)==20+length+count*3
        assert raw[20:20+length].decode('utf8').startswith('/Contents/')
        sizes.append(count)
    repeat=call();assert repeat['ok'] and repeat['result']['already_current']==len(files) and repeat['result']['prepared']==0,repeat
    cancel=root/'cancel';cancel.write_text('cancel');env['XZ_BUILDER_CANCEL_FILE']=str(cancel)
    stopped=call();assert stopped.get('cancelled'),stopped
    assert all(path.read_bytes()==data for path,data in originals.items())
    result={'prepared_tracks':len(files),'detail_samples':sorted(sizes),'repeat_is_idempotent':True,
        'original_analysis_unchanged':True,'cancel_before_write':True,'device_access':False,
        'backend_sha256':hashlib.sha256(backend.read_bytes()).hexdigest()}
a.evidence.parent.mkdir(parents=True,exist_ok=True);a.evidence.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
