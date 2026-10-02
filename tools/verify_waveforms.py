"""Exercise the frozen waveform backend on disposable analysis files, never a USB."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--resources',type=Path,required=True)
p.add_argument('--toolkit',type=Path,default=Path(__file__).resolve().parents[2]/'xdj-xz-toolkit')
p.add_argument('--corpus',type=Path,help='Optional read-only folder of real exported analysis files')
p.add_argument('--evidence',type=Path,required=True)
a=p.parse_args()
if a.evidence.exists():raise ValueError('Choose a new evidence file')
sys.path.insert(0,str(a.toolkit.resolve()))
from builder.tests.test_three_band import usb_track,rekordbox_ext,CUES
from builder.three_band import waves,ORIGINALS

resources=a.resources.resolve()
env={**os.environ,'XZ_BUILDER_RESOURCES':str(resources),'PATH':str(Path(os.environ['SystemRoot'])/'System32')}
env.pop('PYTHONPATH',None);env.pop('PYTHONHOME',None)
def call(method,root):
    proc=subprocess.run([str(resources/'backend/xz-mods-service.exe')],
        input=json.dumps({'method':method,'volume':str(root)}),capture_output=True,text=True,
        encoding='utf8',env=env,timeout=90,creationflags=subprocess.CREATE_NO_WINDOW)
    if proc.returncode:raise RuntimeError(proc.stderr)
    return json.loads(proc.stdout.splitlines()[-1])

checks={}
with tempfile.TemporaryDirectory(prefix='xz-waveform-acceptance-') as temp:
    root=Path(temp)/'usb';track=usb_track(root);original=track.read_bytes()
    sentinel=root/'Contents/music.txt';sentinel.parent.mkdir();sentinel.write_bytes(b'do not change music')
    first=call('apply_three_band',root)
    assert first['ok'] and first['result']['adapted']==1,first
    adapted=track.read_bytes();assert adapted!=original and len(adapted)==len(original)
    backup=root/ORIGINALS/track.relative_to(root);assert backup.read_bytes()==original
    again=call('apply_three_band',root)
    assert again['ok'] and again['result']['already_current']==1,again
    # Simulate cue editing after adaptation. Restore must graft only colours.
    new_cues=b'edited memory cues'.ljust(len(CUES),b' ')
    edited=adapted.replace(CUES,new_cues)
    assert len(edited)==len(adapted)
    track.write_bytes(edited)
    restored=call('restore_rgb_waveforms',root)
    assert restored['ok'] and restored['result']['restored']==1,restored
    assert track.read_bytes()==original.replace(CUES,new_cues)
    assert sentinel.read_bytes()==b'do not change music'
    checks['apply_repeat_restore_preserves_cue_edits_and_music']=True
    bad=usb_track(root,'0000BAD0');bad.with_suffix('.2EX').write_bytes(b'broken')
    partial=call('apply_three_band',root)
    assert partial['ok'] and partial['result']['failed_count']==1,partial
    assert bad.read_bytes()==rekordbox_ext()
    checks['malformed_track_isolated']=True
    call('restore_rgb_waveforms',root)
    cancel=Path(temp)/'cancel';cancel.write_text('cancel');env['XZ_BUILDER_CANCEL_FILE']=str(cancel)
    before=track.read_bytes();stopped=call('apply_three_band',root)
    assert stopped.get('cancelled') and track.read_bytes()==before,stopped
    env.pop('XZ_BUILDER_CANCEL_FILE')
    checks['cancel_before_write']=True
    if a.corpus:
        real=Path(temp)/'real';pairs=[]
        for i,source in enumerate(sorted(a.corpus.rglob('ANLZ0000.EXT'))):
            if not source.with_suffix('.2EX').is_file():continue
            target=real/'PIONEER/USBANLZ/P000'/f'{i:08X}'/source.name
            target.parent.mkdir(parents=True)
            for suffix in ('.EXT','.2EX'):shutil.copyfile(source.with_suffix(suffix),target.with_suffix(suffix))
            pairs.append((source,target,source.read_bytes()))
        assert pairs,'No real analysis pairs'
        applied=call('apply_three_band',real)
        assert applied['ok'] and applied['result']['failed_count']==0,applied
        for source,target,original in pairs:
            at,count=waves(original)[b'PWV5'];changed=target.read_bytes()
            assert changed[at+1:at+2*count:2].translate(bytes(x&127 for x in range(256)))==original[at+1:at+2*count:2].translate(bytes(x&127 for x in range(256)))
            assert source.read_bytes()==original
        undone=call('restore_rgb_waveforms',real)
        assert undone['ok'] and undone['result']['failed_count']==0,undone
        assert all(target.read_bytes()==original for _,target,original in pairs)
        checks['real_tracks_roundtrip']=len(pairs)

a.evidence.parent.mkdir(parents=True,exist_ok=True)
report={'checks':checks,'device_access':False,'backend_sha256':hashlib.sha256((resources/'backend/xz-mods-service.exe').read_bytes()).hexdigest()}
a.evidence.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
