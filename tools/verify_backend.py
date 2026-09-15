"""Verify the frozen backend with generated audio and temporary staging folders."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import wave

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--resources',type=Path,required=True)
p.add_argument('--evidence',type=Path,required=True)
p.add_argument('--local-firmware',type=Path)
p.add_argument('--local-key',type=Path)
a=p.parse_args();resources=a.resources.resolve()
if a.evidence.exists():raise ValueError('Choose a new evidence file')
env=os.environ.copy();env.update({'XZ_BUILDER_RESOURCES':str(resources),'XZ_AUDIO_HELPER':str(resources/'xz-audio-helper.exe'),
    'PATH':str(Path(os.environ['SystemRoot'])/'System32')})
env.pop('PYTHONPATH',None);env.pop('PYTHONHOME',None)
def call(request):
    result=subprocess.run([str(resources/'backend/xz-mods-service.exe')],input=json.dumps(request),capture_output=True,text=True,env=env,timeout=90)
    if result.returncode:raise RuntimeError(result.stderr)
    return json.loads(result.stdout.splitlines()[-1])
def audio(path,frequency):
    with wave.open(str(path),'wb') as file:
        file.setnchannels(2);file.setsampwidth(2);file.setframerate(44100)
        file.writeframes(b''.join(struct.pack('<hh',*[int(4000*math.sin(i*frequency*2*math.pi/44100))]*2) for i in range(4410)))
checks={}
with tempfile.TemporaryDirectory(prefix='xz-builder-acceptance-') as temporary:
    root=Path(temporary);volume=root/'volume';volume.mkdir()
    protected=volume/'existing-music.txt';protected.write_text('retain existing files')
    files={name:root/(name+'.wav') for name in ('source','harmonics','vocals')}
    for name,frequency in [('source',220),('harmonics',330),('vocals',660)]:audio(files[name],frequency)
    before={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in files.items()}
    status=call({'method':'status'});assert status['ok'] and not status['result']['vjtools_required']
    checks['standalone_status_without_python_path']=True
    request={'method':'import_stems','volume':str(volume),'separation_id':'acceptance-v1',**{name:str(path) for name,path in files.items()}}
    imported=call(request);assert imported['ok'],imported
    receipt=imported['result'];directory=Path(receipt['directory'])
    assert (directory/'meta').is_file() and (directory/'harmonics.wav').is_file() and (directory/'vocals.wav').is_file()
    assert Path(receipt['track_on_usb']).read_bytes()==files['source'].read_bytes()
    assert before=={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in files.items()}
    assert protected.read_text()=='retain existing files'
    checks['real_cache_import_and_original_preservation']=True
    duplicate=call(request);assert duplicate['ok'] and duplicate['result']['reused']
    audio(files['harmonics'],880)
    different=call(request);assert not different['ok'] and 'different stems' in different['error']
    checks['no_overwrite']=True
    cancelled=root/'cancel';cancelled.write_text('cancel');env['XZ_BUILDER_CANCEL_FILE']=str(cancelled)
    stopped=call({**request,'separation_id':'cancelled-v1'});assert stopped.get('cancelled')
    checks['cancel_before_mutation']=not (volume/'mods/stemd-cache/cancelled-v1').exists()
    env.pop('XZ_BUILDER_CANCEL_FILE')
    if a.local_firmware and a.local_key:
        build=call({'method':'build_usb','volume':str(volume),'firmware':str(a.local_firmware.resolve()),'key':str(a.local_key.resolve()),'experimental':True})
        assert build['ok'],build
        assert (volume/'autoexec.bin').is_file() and not build['result']['share_image']
        checks['local_image_build_and_roundtrip_verification']=True
        repeat=call({'method':'build_usb','volume':str(volume),'firmware':str(a.local_firmware.resolve()),'key':str(a.local_key.resolve()),'experimental':True})
        assert not repeat['ok'] and 'already exists' in repeat['error']
        checks['existing_loader_preserved']=True
a.evidence.parent.mkdir(parents=True,exist_ok=True)
a.evidence.write_text(json.dumps({'checks':checks,'device_access':False,'vjtools_required':False,'real_model_execution':False},indent=2)+'\n')
print(json.dumps(checks,indent=2))
