"""Validate grouped imports before rendering and publishing OverCue files."""
import json
from pathlib import Path
from .host import executable
import tempfile
import shutil
from . import cache,engines,usb,overcue_writer

def validated_files(request):
    files=request.get('files')
    if not isinstance(files,list) or not 1<=len(files)<=128:raise ValueError('Choose between 1 and 128 stem files')
    result=[];seen=set()
    for item in files:
        if not isinstance(item,dict) or item.get('group') not in ('drums','vocals','harmonics'):raise ValueError('Assign every stem to Drums, Vocals or Harmonics')
        path=cache._regular(item['path']);key=str(path).casefold()
        if key in seen:raise ValueError('The same stem file was added twice')
        seen.add(key);result.append({'path':str(path),'group':item['group']})
    if type(request.get('fit_length',False)) is not bool:raise ValueError('Invalid length adjustment')
    return result

def import_python(job,resources):
    base=engines.data_root();environment=base/'import-python';python=environment/('Scripts/python.exe' if __import__('os').name=='nt' else 'bin/python')
    marker=environment/'xz-import-ready.json'
    if not python.is_file() or not marker.is_file():
        job.progress('setup','Setting up audio import tools. No separation models are needed.')
        uv=executable(resources,'uv')
        if not python.is_file():job.run([uv,'venv','--python','3.11',environment])
        job.run([uv,'pip','install','--python',python,'numpy==2.2.6','scipy==1.16.1','soundfile==0.13.1'])
        marker.write_text('{"version":1}')
    return python

def import_groups(request,job,resources):
    from .service import locate_track,VERSION
    from . import managed_usb
    files=validated_files(request)
    managed_usb.check_target(request['volume'],request['expected_identity'],media=True)
    from .stem_library import source_path
    source=source_path(Path(request['volume']),request['file_path'])
    track=locate_track(source)
    engines.require_scratch(track.frames)
    if any(Path(item['path'])==source for item in files):raise ValueError('The complete track cannot also be an instrument stem')
    needed=int(track.frames/44100*96000*4*7*1.04)+16*1024*1024
    if shutil.disk_usage(track.usb_root).free<needed:raise ValueError('Not enough USB space. Free space before importing stems.')
    with engines.engine_lock():
        python=import_python(job,resources)
        with tempfile.TemporaryDirectory(prefix='xz-import-groups-',dir=engines.data_root()) as temp:
            temp=Path(temp);manifest=temp/'request.json';manifest.write_text(json.dumps({'mix':str(source),'files':files,'fit_length':request.get('fit_length',False)}),encoding='utf-8')
            job.progress('import','Combining assigned stems and preparing OverCue audio')
            job.run([python,'-I',Path(resources)/'inference/grouped_stems.py','--request',manifest,'--output',temp/'audio'])
            if usb.sha(source)!=track.sha256:raise ValueError('The complete track changed during import')
            receipt=overcue_writer.publish(track.usb_root,track.file_path,track.track_id,track.sha256,source.suffix.lower()[1:],temp/'audio/roles','imported-groups-v1',VERSION,job,page_python=python)
            return {'format':'overcue-index/1','file_path':track.file_path,'usb_root':str(track.usb_root),**receipt}
