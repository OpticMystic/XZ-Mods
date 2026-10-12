"""Plan and import stems made elsewhere (Suno, Moises, UVR, a DAW) as OverCue bundles."""
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
from .host import executable
import shutil
import stat
import tempfile
import time
import zipfile
from . import cache,engines,usb,overcue_writer,stem_roles_map

AUDIO_SUFFIXES=('.wav','.flac','.aif','.aiff','.ogg','.mp3')
MAX_FILES=128
ZIP_LIMIT=4*1024**3
EXTRACT_TTL=2*24*3600
UNSAFE_ZIP='This ZIP has unsafe file paths. Extract it yourself and add the stem files.'

def extraction_root():
    root=cache._safe_path(engines.data_root()/'stem-imports');root.mkdir(exist_ok=True);return root

def sweep_extractions(now=None):
    now=time.time() if now is None else now
    for folder in extraction_root().iterdir():
        if folder.is_dir() and not folder.is_symlink() and (folder.name.startswith('.extract-') or now-folder.stat().st_mtime>EXTRACT_TTL):
            shutil.rmtree(cache._safe_path(folder),ignore_errors=True)

def _members(archive):
    members=[];total=0
    for info in archive.infolist():
        name=info.filename.replace('\\','/');parts=PurePosixPath(name).parts
        if name.startswith('/') or ':' in name or not parts or any(part in ('.','..') for part in parts):raise ValueError(UNSAFE_ZIP)
        if stat.S_ISLNK(info.external_attr>>16):raise ValueError(UNSAFE_ZIP)
        if info.is_dir() or parts[0]=='__MACOSX' or parts[-1].startswith('.') or PurePosixPath(name).suffix.lower() not in AUDIO_SUFFIXES:continue
        total+=info.file_size
        if total>ZIP_LIMIT:raise ValueError('This ZIP holds more than 4 GB of audio. Extract the stems you need and add them.')
        members.append((info,parts))
    if not members:raise ValueError('This ZIP has no WAV, FLAC, AIFF, OGG or MP3 files.')
    if len(members)>MAX_FILES:raise ValueError('This ZIP has more than 128 audio files.')
    return members

def extract_zip(path,job):
    """Extract audio members once per archive version; the original ZIP is only read."""
    path=cache._regular(path);info=path.stat()
    key=hashlib.sha256(f'{path}|{info.st_size}|{info.st_mtime_ns}'.encode()).hexdigest()[:16]
    root=extraction_root();target=root/key;listing=target/'.files.json'
    if listing.is_file():
        os.utime(target);return [target.joinpath(*PurePosixPath(name).parts) for name in json.loads(listing.read_text(encoding='utf-8'))]
    shutil.rmtree(cache._safe_path(target),ignore_errors=True)
    staging=Path(tempfile.mkdtemp(prefix='.extract-',dir=root));written=0;names=[]
    try:
        try:
            with zipfile.ZipFile(path) as archive:
                for info,parts in _members(archive):
                    job.check();output=staging.joinpath(*parts)
                    if not output.resolve().is_relative_to(staging.resolve()):raise ValueError(UNSAFE_ZIP)
                    output.parent.mkdir(parents=True,exist_ok=True)
                    with archive.open(info) as source,output.open('xb') as sink:
                        while block:=source.read(1<<20):
                            written+=len(block)
                            if written>ZIP_LIMIT:raise ValueError('This ZIP holds more than 4 GB of audio. Extract the stems you need and add them.')
                            sink.write(block)
                    names.append('/'.join(parts))
        except (zipfile.BadZipFile,zipfile.LargeZipFile,NotImplementedError,RuntimeError,EOFError) as exc:
            raise ValueError(f'{path.name} could not be read as a ZIP: {exc}') from None
        (staging/'.files.json').write_text(json.dumps(names),encoding='utf-8')
        os.replace(staging,target)
    finally:
        if staging.exists():shutil.rmtree(staging,ignore_errors=True)
    return [target.joinpath(*PurePosixPath(name).parts) for name in names]

def stem_files(request,job,titles=()):
    """Expand ZIPs, check every path, and map each file to a group. Groups in the request win."""
    raw=request.get('files')
    if not isinstance(raw,list) or not raw:raise ValueError('Choose between 1 and 128 stem files')
    entries=[];archives=[]
    for item in raw:
        path,group=(item.get('path'),item.get('group')) if isinstance(item,dict) else (item,None)
        if not isinstance(path,str):raise ValueError('Choose between 1 and 128 stem files')
        if path.lower().endswith('.zip'):
            archive=cache._regular(path);archives.append(archive.stem)
            entries.extend({'path':member,'group':None,'archive':archive.name} for member in extract_zip(archive,job))
        else:entries.append({'path':cache._regular(path),'group':group,'archive':None})
    if len(entries)>MAX_FILES:raise ValueError('Choose between 1 and 128 stem files')
    seen=set()
    for entry in entries:
        key=str(entry['path']).casefold()
        if key in seen:raise ValueError('The same stem file was added twice')
        seen.add(key)
    mapping=stem_roles_map.map_files([entry['path'].name for entry in entries],titles=[*titles,*archives],
        overrides=[entry['group'] for entry in entries],hints=archives)
    return entries,mapping

def assigned(entries,mapping):
    files=[{'path':str(entry['path']),'group':found['group']} for entry,found in zip(entries,mapping['files']) if found['group']!=stem_roles_map.MIX]
    if any(item['group'] is None for item in files):raise ValueError('Assign every stem to Drums, Vocals or Harmonics')
    if not files:raise ValueError('Choose at least one stem file. The full song is not a stem.')
    return files

def import_python(job,resources):
    base=engines.data_root();environment=base/'import-python';python=environment/('Scripts/python.exe' if os.name=='nt' else 'bin/python')
    marker=environment/'xz-import-ready.json'
    if not python.is_file() or not marker.is_file():
        job.progress('setup','Setting up audio import tools. No separation models are needed.')
        uv=executable(resources,'uv')
        if not python.is_file():job.run([uv,'venv','--python','3.11',environment])
        job.run([uv,'pip','install','--python',python,'numpy==2.2.6','scipy==1.16.1','soundfile==0.13.1'])
        marker.write_text('{"version":1}')
    return python

def _target(request):
    from .service import locate_track
    from . import managed_usb
    from .stem_library import source_path
    managed_usb.check_target(request['volume'],request['expected_identity'],media=True)
    source=source_path(Path(request['volume']),request['file_path'])
    track=locate_track(source)
    titles=[source.stem]+([request['title']] if isinstance(request.get('title'),str) else [])
    return source,track,titles

def _grouped(resources,mode,request_file,output):
    return [Path(resources)/'inference/grouped_stems.py','--mode',mode,'--request',request_file,'--output',output]

def parse_alignment(value):
    if value is None:return None
    value=value if isinstance(value,dict) else {}
    offset,gain=value.get('offset_samples'),value.get('gain')
    if type(offset) is not int or abs(offset)>44100 or not isinstance(gain,(int,float)) or not (math.isfinite(gain) and 1/64<=gain<=64):
        raise ValueError('The stem alignment is invalid. Check the stems again.')
    return {'offset_samples':offset,'gain':float(gain)}

def _length_warnings(files,mix_frames):
    warnings=[]
    for item in files:
        difference=(item['frames']-mix_frames)/44100
        if abs(difference)>=1:
            warnings.append(f"{Path(item['path']).name} is {abs(difference):.1f} s {'longer' if difference>0 else 'shorter'} than the track.")
    return warnings

def plan(request,job,resources):
    """Proposed mapping and alignment for an import. Reads the stems; writes nothing to the USB."""
    source,track,titles=_target(request)
    with engines.engine_lock():
        sweep_extractions()
        entries,mapping=stem_files(request,job,titles)
        if any(entry['path']==source for entry in entries):raise ValueError('The complete track cannot also be an instrument stem')
        stems=[entry for entry,found in zip(entries,mapping['files']) if found['group']!=stem_roles_map.MIX]
        alignment=None;lengths={}
        if stems:
            engines.require_scratch(track.frames)
            python=import_python(job,resources)
            with tempfile.TemporaryDirectory(prefix='xz-import-plan-',dir=engines.data_root()) as temp:
                temp=Path(temp);request_file=temp/'request.json'
                request_file.write_text(json.dumps({'mix':str(source),'files':[str(entry['path']) for entry in stems]}),encoding='utf-8')
                job.progress('align','Lining the stems up with the track on the USB')
                job.run([python,'-I',*_grouped(resources,'plan',request_file,temp/'plan')])
                result=json.loads((temp/'plan/result.json').read_text(encoding='utf-8'))
            alignment=result['alignment'];lengths={item['path']:item for item in result['files']}
    files=[]
    for entry,found in zip(entries,mapping['files']):
        measured=lengths.get(str(entry['path']),{})
        files.append({**found,'path':str(entry['path']),'archive':entry['archive'],**{key:measured[key] for key in ('frames','seconds') if key in measured}})
    warnings=[*mapping['warnings']]
    if alignment:
        warnings+=_length_warnings(lengths.values(),track.frames)
        if alignment['reason']:warnings.append(alignment['reason'])
    verdict=('mismatch' if alignment and alignment['verdict']=='mismatch' else
             'ready' if mapping['verdict']=='auto' and alignment and alignment['verdict']=='aligned' else 'needs_review')
    proposal=[{'path':item['path'],'group':item['group']} for item in files if item['group'] not in (None,stem_roles_map.MIX)]
    return {'format':'stem-import-plan/1','usb_root':str(track.usb_root),'file_path':track.file_path,'track_frames':track.frames,
            'files':files,'mapping':{key:mapping[key] for key in ('verdict','source','layout','warnings')},'alignment':alignment,
            'fit_length':any(item['frames']!=track.frames for item in lengths.values()),
            'needs_drum_split':mapping['layout']=='vocals_instrumental','verdict':verdict,'warnings':warnings,
            'import_request':{'files':proposal,'alignment':alignment and {'offset_samples':alignment['offset_samples'],'gain':alignment['gain']}}}

def _drum_split(instrumental,output,job,resources):
    job.progress('split','Separating drums from the instrumental stem')
    result=engines.separate_drums(instrumental,output,job,resources)
    split={}
    for name in ('drums','harmonics'):
        part=result[name];gain=float(part['gain'])
        if not (math.isfinite(gain) and gain>0):raise ValueError('Drum separation returned an invalid gain')
        split[name]={'path':str(cache._regular(part['path'])),'gain':gain}
    return split,result.get('model_id')

def import_groups(request,job,resources):
    from .service import VERSION
    source,track,titles=_target(request)
    alignment=parse_alignment(request.get('alignment'))
    if type(request.get('accept_weak_alignment',False)) is not bool:raise ValueError('Invalid alignment choice')
    require=None if alignment is None else 'weak' if request.get('accept_weak_alignment') else 'aligned'
    engines.require_scratch(track.frames)
    needed=int(track.frames/44100*96000*4*7*1.04)+16*1024*1024
    if shutil.disk_usage(track.usb_root).free<needed:raise ValueError('Not enough USB space. Free space before importing stems.')
    with engines.engine_lock():
        entries,mapping=stem_files(request,job,titles)
        files=assigned(entries,mapping)
        if type(request.get('fit_length',False)) is not bool:raise ValueError('Invalid length adjustment')
        if any(Path(item['path'])==source for item in files):raise ValueError('The complete track cannot also be an instrument stem')
        groups={item['group'] for item in files}
        if stem_roles_map.INSTRUMENTAL in groups and groups&{'drums','harmonics'}:
            raise ValueError('An instrumental stem already contains drums and harmonics. Remove it or the separate parts.')
        python=import_python(job,resources)
        with tempfile.TemporaryDirectory(prefix='xz-import-groups-',dir=engines.data_root()) as temp:
            temp=Path(temp);audio=temp/'audio';manifest=temp/'request.json'
            manifest.write_text(json.dumps({'mix':str(source),'files':files,'fit_length':request.get('fit_length',False),
                'alignment':alignment,'require':require}),encoding='utf-8')
            job.progress('import','Combining stems and preparing OverCue audio')
            split_model=None
            if stem_roles_map.INSTRUMENTAL in groups:
                job.run([python,'-I',*_grouped(resources,'group',manifest,audio)])
                split,split_model=_drum_split(audio/'instrumental.wav',temp/'split',job,resources)
                render_request=temp/'render.json';render_request.write_text(json.dumps({'split':split}),encoding='utf-8')
                job.run([python,'-I',*_grouped(resources,'render',render_request,audio)])
            else:job.run([python,'-I',*_grouped(resources,'combine',manifest,audio)])
            check=json.loads((audio/'groups.json').read_text(encoding='utf-8'))['check']
            if usb.sha(source)!=track.sha256:raise ValueError('The complete track changed during import')
            receipt_alignment={'offset_samples':check['offset_samples'],'offset_ms':round(1000*check['offset_samples']/44100,3),
                'gain':check['applied_gain'],'correlation':round(check['correlation'],6),'residual_db':round(check['residual_db'],2),
                'verdict':check['verdict']}
            separation={'method':'import','source':mapping['source'],'layout':mapping['layout'],'alignment':receipt_alignment}
            if split_model:separation['drum_split']=split_model
            receipt=overcue_writer.publish(track.usb_root,track.file_path,track.track_id,track.sha256,source.suffix.lower()[1:],audio/'roles',separation,VERSION,job,page_python=python,
                                           source={'sample_rate':track.source_rate,'mp3':track.mp3})
        root=extraction_root()
        for folder in {root/entry['path'].relative_to(root).parts[0] for entry in entries if entry['path'].is_relative_to(root)}:
            shutil.rmtree(cache._safe_path(folder),ignore_errors=True)
    files_report=[{'name':found['name'],'group':found['group'],'label':found['label'],'confidence':found['confidence'],'origin':found['origin']} for found in mapping['files']]
    bundle_folder=Path(track.usb_root)/'CDJMODS'/'stems'/receipt['bundle']
    try:stored_bytes=sum(f.stat().st_size for f in bundle_folder.iterdir() if f.is_file())
    except OSError:stored_bytes=None
    return {'format':'overcue-index/1','file_path':track.file_path,'usb_root':str(track.usb_root),**receipt,
            'stored_bytes':stored_bytes,'files':files_report,'separation':separation}
