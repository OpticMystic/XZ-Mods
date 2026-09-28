"""Verify the frozen backend with generated audio and temporary staging folders."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--resources',type=Path,required=True)
p.add_argument('--evidence',type=Path,required=True)
p.add_argument('--toolkit',type=Path,default=Path(__file__).resolve().parents[2]/'xdj-xz-toolkit')
p.add_argument('--no-engine',action='store_true',help='Skip checks that install NumPy/SciPy into a temporary engine environment')
p.add_argument('--local-firmware',type=Path)
p.add_argument('--local-key',type=Path)
a=p.parse_args();resources=a.resources.resolve()
if a.evidence.exists():raise ValueError('Choose a new evidence file')
sys.path.insert(0,str(a.toolkit.resolve()/'builder/tests'))
from pdb_fixture import write_export
env=os.environ.copy();env.update({'XZ_BUILDER_RESOURCES':str(resources),'XZ_AUDIO_HELPER':str(resources/'xz-audio-helper.exe'),
    'PATH':str(Path(os.environ['SystemRoot'])/'System32')})
env.pop('PYTHONPATH',None);env.pop('PYTHONHOME',None)
def call(request):
    result=subprocess.run([str(resources/'backend/xz-mods-service.exe')],input=json.dumps(request),capture_output=True,text=True,
        encoding='utf-8',env=env,timeout=300)
    if result.returncode:raise RuntimeError(result.stderr)
    return json.loads(result.stdout.splitlines()[-1])
def audio(path,frequency):
    path.parent.mkdir(parents=True,exist_ok=True)
    with wave.open(str(path),'wb') as file:
        file.setnchannels(2);file.setsampwidth(2);file.setframerate(44100)
        file.writeframes(b''.join(struct.pack('<hh',*[int(4000*math.sin(i*frequency*2*math.pi/44100))]*2) for i in range(4410)))
def tree(folder):return {p.relative_to(folder).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.rglob('*') if p.is_file()}
checks={}
with tempfile.TemporaryDirectory(prefix='xz-builder-acceptance-') as temporary:
    root=Path(temporary);volume=root/'volume';volume.mkdir()
    env['XZ_BUILDER_DATA']=str(root/'data')
    (volume/'existing-music.txt').write_text('retain existing files')
    source=volume/'Contents/Artist/Café track.wav';other=volume/'Contents/Artist/Other.wav';loose=volume/'Contents/Loose.wav'
    stems={name:root/(name+'.wav') for name in ('harmonics','vocals')}
    for path,frequency in [(source,220),(other,110),(loose,440),(stems['harmonics'],330),(stems['vocals'],660)]:audio(path,frequency)
    status=call({'method':'status'});assert status['ok'] and not status['result']['vjtools_required']
    assert status['result']['separation_output_format']=='overcue-index/1' and status['result']['stem_builder']['beta'],status
    checks['standalone_status_without_python_path']=True
    request={'method':'import_stems','source':str(source),'separation_id':'acceptance-v1',**{name:str(path) for name,path in stems.items()}}
    missing=call(request);assert not missing['ok'] and 'no Rekordbox export' in missing['error'],missing
    write_export(volume/'PIONEER/rekordbox/export.pdb',{7:'/Contents/Artist/Café track.wav',8:'/Contents/Artist/Other.wav'})
    unexported=call({**request,'source':str(loose)});assert not unexported['ok'] and 'Rekordbox' in unexported['error'],unexported
    outside=call({**request,'source':str(stems['vocals'])});assert not outside['ok'] and 'Contents' in outside['error'],outside
    checks['requires_rekordbox_exported_usb_track']=True
    no_engine=call(request);assert not no_engine['ok'] and 'Set up the separation engine first' in no_engine['error'],no_engine
    checks['import_requires_engine']=True
    before=tree(volume);inputs={name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in stems.items()}
    assert not (volume/'CDJMODS').exists()
    if not a.no_engine:
        python=root/'data/engine-python/Scripts/python.exe'
        subprocess.run([str(resources/'uv.exe'),'venv','--python','3.11',str(python.parents[1])],check=True,capture_output=True)
        subprocess.run([str(resources/'uv.exe'),'pip','install','--python',str(python),'numpy==2.2.6','scipy==1.16.1'],check=True,capture_output=True)
        (root/'data/setup-umxhq.json').write_text('{}')
        imported=call(request);assert imported['ok'],imported
        receipt=imported['result']
        assert (receipt['format'],receipt['beta'],receipt['track_id'],receipt['file_path'],receipt['verified'],receipt['reused'],receipt['alignment_verified'])==\
            ('overcue-index/1',True,7,'/Contents/Artist/Café track.wav',True,False,False),receipt
        index=json.loads((volume/'CDJMODS/index.json').read_text(encoding='utf-8'))
        assert index['tracks']['7']['bundle']==receipt['bundle'] and index['tracks']['7']['source_sha256']==before['Contents/Artist/Café track.wav']
        after=tree(volume)
        assert {k:v for k,v in after.items() if not k.startswith('CDJMODS/')}==before
        assert {name:hashlib.sha256(path.read_bytes()).hexdigest() for name,path in stems.items()}==inputs
        checks['overcue_import_and_original_preservation']=True
        inspected=call({'method':'inspect_overcue','source':str(source)})
        assert inspected['ok'] and inspected['result']['verified_mixes']==7 and inspected['result']['all_pages_verified'],inspected
        checks['device_reader_accepts_import']=True
        duplicate=call(request);assert duplicate['ok'] and duplicate['result']['reused'],duplicate
        audio(stems['harmonics'],880)
        replaced=call(request)
        assert replaced['ok'] and replaced['result']['replaced_previous'] and replaced['result']['bundle']!=receipt['bundle'],replaced
        assert (volume/'CDJMODS/stems'/receipt['bundle']).is_dir()
        checks['replaces_only_own_entry_and_keeps_old_bundle']=True
        index=json.loads((volume/'CDJMODS/index.json').read_text(encoding='utf-8'))
        index['tracks']['8']={'bundle':'aaaaaaaaaaaaaaaa','file_path':'/Contents/Artist/Other.wav','writer':'another tool'}
        (volume/'CDJMODS/stems/aaaaaaaaaaaaaaaa').mkdir()
        (volume/'CDJMODS/stems/aaaaaaaaaaaaaaaa/overcue-manifest.json').write_text('{"schema":"overcue-stems/4"}')
        (volume/'CDJMODS/index.json').write_text(json.dumps(index),encoding='utf-8')
        foreign=(volume/'CDJMODS/index.json').read_bytes()
        refused=call({**request,'source':str(other)})
        assert not refused['ok'] and 'OverCue already prepared this track' in refused['error'],refused
        assert (volume/'CDJMODS/index.json').read_bytes()==foreign
        checks['foreign_overcue_entry_preserved']=True
        cancelled=root/'cancel';cancelled.write_text('cancel');env['XZ_BUILDER_CANCEL_FILE']=str(cancelled)
        stopped=call({**request,'separation_id':'cancelled-v1'});assert stopped.get('cancelled'),stopped
        assert (volume/'CDJMODS/index.json').read_bytes()==foreign
        checks['cancel_before_mutation']=True
        env.pop('XZ_BUILDER_CANCEL_FILE')
    if a.local_firmware and a.local_key:
        automatic=call({'method':'inspect_firmware','firmware':str(a.local_firmware.resolve())})
        assert automatic['ok'] and automatic['result']['key_verified_against_input'],automatic
        checks['packaged_automatic_boot_support']=True
        unsuitable=call({'method':'prepare_usb','volume':str(volume),'experimental':True})
        assert not unsuitable['ok'] and 'FAT/FAT32 USB drive' in unsuitable['error'],unsuitable
        checks['one_step_rejects_non_usb_before_download']=True
        build=call({'method':'build_usb','volume':str(volume),'firmware':str(a.local_firmware.resolve()),'key':str(a.local_key.resolve()),'experimental':True})
        assert build['ok'],build
        assert (volume/'autoexec.bin').is_file() and not build['result']['share_image']
        checks['local_image_build_and_roundtrip_verification']=True
        repeat=call({'method':'build_usb','volume':str(volume),'firmware':str(a.local_firmware.resolve()),'key':str(a.local_key.resolve()),'experimental':True})
        assert not repeat['ok'] and 'already exists' in repeat['error']
        checks['existing_loader_preserved']=True
a.evidence.parent.mkdir(parents=True,exist_ok=True)
a.evidence.write_text(json.dumps({'checks':checks,'device_access':False,'vjtools_required':False,'real_model_execution':False,
    'engine_checks':not a.no_engine},indent=2)+'\n')
print(json.dumps(checks,indent=2))
