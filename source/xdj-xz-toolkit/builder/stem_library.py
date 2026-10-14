"""Read-only Rekordbox USB browsing and stem availability checks."""
from pathlib import Path, PurePosixPath
import ctypes
import os
import shutil
import sys
from . import cache,rekordbox_pdb,overcue_writer as writer,managed_usb,stem_estimate

PAGE_SIZE=40
UNSUPPORTED_DETAIL='XZ Mods prepares stems from WAV, FLAC and MP3 exports. AIFF and AAC are not supported yet.'
REGENERATE_DETAIL=('These stems were made from an MP3 before XZ Mods 0.3.0-preview.2 and start 25 ms early against the player. '
                   'Prepare again to line them up; the XDJ-XZ corrects this live, the stem waveforms and other players do not.')

def discover():
    found=[]
    if os.name=='nt':
        kernel=ctypes.windll.kernel32
        for number in range(26):
            root=Path(chr(65+number)+':/')
            if not kernel.GetLogicalDrives()&(1<<number) or kernel.GetDriveTypeW(str(root)) not in (2,3):continue
            if root.joinpath(*rekordbox_pdb.EXPORT_PDB).is_file():
                label=ctypes.create_unicode_buffer(261)
                kernel.GetVolumeInformationW(str(root),label,261,None,None,None,None,0)
                found.append({'volume':str(root),'label':label.value or str(root),'removable':kernel.GetDriveTypeW(str(root))==2})
    elif sys.platform=='darwin':
        from .host import mac_volume
        for root in Path('/Volumes').iterdir():
            if root.is_symlink() or not root.is_dir() or not root.joinpath(*rekordbox_pdb.EXPORT_PDB).is_file():continue
            try:
                info=mac_volume(root)
                if Path(info['volume_root'])!=root:continue
                found.append({'volume':str(root),'label':root.name,'removable':True})
            except (OSError,ValueError):continue
    return {'drives':sorted(found,key=lambda item:(not item['removable'],item['volume']))}

def source_path(root,file_path):
    parts=PurePosixPath(file_path).parts
    if not parts or parts[0]!='/' or len(parts)<3 or parts[1].casefold()!='contents' or any(p in ('.','..') or '\\' in p or ':' in p for p in parts[1:]):
        raise ValueError('Invalid track path in the Rekordbox database')
    return cache._safe_path(root.joinpath(*parts[1:]))

def stem_status(root,track,index,index_error):
    result={'stem_status':'missing','stem_label':'Not prepared','can_generate':False,'can_verify':False}
    try:
        source=source_path(root,track['file_path'])
        if not source.is_file():return {**result,'stem_status':'unavailable','stem_label':'Audio file missing'}
        supported=source.suffix.lower() in cache.SUPPORTED_SUFFIXES
        result.update(source=str(source),can_generate=supported)
        if index_error:return {**result,'stem_status':'error','stem_label':'Stem index needs attention','can_generate':False,'detail':index_error}
        matches=[entry for table in ('tracks','tracks_onelibrary') for entry in index.get(table,{}).values() if entry.get('file_path')==track['file_path']]
        direct=index.get('tracks',{}).get(str(track['id']))
        if direct and direct.get('file_path')!=track['file_path']:return {**result,'stem_status':'error','stem_label':'Stem track ID conflict','can_generate':False}
        if not matches:
            if not supported:result.update(stem_status='unsupported',stem_label='Format not supported yet',detail=UNSUPPORTED_DETAIL)
            return result
        bundles={item.get('bundle') for item in matches}
        if len(bundles)!=1:raise ValueError('Conflicting stem entries')
        bundle=next(iter(bundles))
        if not isinstance(bundle,str) or not writer.BUNDLE.fullmatch(bundle):raise ValueError('Invalid stem folder')
        folder=cache._safe_path(root/'CDJMODS'/'stems'/bundle)
        manifest_path=folder/'overcue-manifest.json'
        manifest=writer._parse_json(writer._read_bytes(manifest_path,'Stem manifest'),'Stem manifest') if manifest_path.exists() else {}
        if 'frames' not in matches[0] and manifest.get('schema')!=writer.MANIFEST_SCHEMA:raise ValueError('Unsupported stem format')
        roles=manifest.get('roles',{})
        codecs=set();stored=0
        for role in writer.ROLES:
            entry=roles.get(role,{})
            name=entry.get('file',writer.role_file(role))
            if not isinstance(name,str) or PurePosixPath(name).name!=name or '\\' in name:raise ValueError('Invalid stem file path')
            file=cache._regular(folder/name);stored+=file.stat().st_size
            if not file.stat().st_size:raise ValueError('Empty stem file')
            with file.open('rb') as audio:magic=audio.read(8)
            codecs.add('flac-44k1' if magic==b'OVPGZ004' else 'flac-96k' if magic==b'OVPGZ003' else 'zlib' if magic==b'OVPGZ001' else 'unknown')
        if 'unknown' in codecs:
            raise ValueError('This stem format is not supported by this XZ Mods build. Update the app and USB loader.')
        owner=manifest.get('writer',{}).get('name','OverCue')
        codec=next(iter(codecs)) if len(codecs)==1 else 'mixed'
        separation=manifest.get('separation') if isinstance(manifest.get('separation'),dict) else {}
        frames=matches[0].get('frames',manifest.get('runtime',{}).get('frames'))
        waveforms=type(frames) is int and writer.waveforms_present(folder,frames)
        timeline=writer.mp3_timeline(manifest,source)
        regenerate=timeline=='gapless'
        result.update(stem_status='present',stem_label='Prepared · '+owner+(' · FLAC' if codec.startswith('flac-') else '')+(' · Prepare again' if regenerate else ''),
                      can_generate=supported and owner==writer.WRITER,can_verify=True,page_codec=codec,stored_bytes=stored,
                      producer='xz-mods' if owner==writer.WRITER else owner,prepared_at=int(folder.stat().st_mtime),
                      preset=separation.get('preset') or separation.get('method') or manifest.get('writer',{}).get('version'),
                      stem_waveforms=waveforms,can_add_waveforms=not waveforms and owner==writer.WRITER,
                      timeline=timeline,needs_regeneration=regenerate,
                      detail=REGENERATE_DETAIL if regenerate else 'All seven stem files are present. Verify to check the decoded audio and source identity.')
    except (ValueError,OSError,TypeError,KeyError) as exc:
        result.update(stem_status='error',stem_label='Stems need attention',can_generate=False,detail=str(exc))
    return result

def browse(request,job):
    root=cache._safe_path(request['volume'])
    pdb=root.joinpath(*rekordbox_pdb.EXPORT_PDB)
    if not pdb.is_file():raise ValueError('No Rekordbox Device Library found. Export this USB for the XDJ-XZ in Rekordbox, then refresh.')
    tracks=rekordbox_pdb.library_tracks(cache._regular(pdb))
    query=str(request.get('query','')).casefold()[:200]
    filtered=sorted((t for t in tracks if query in (t['title']+' '+t['artist']+' '+t['file_path']).casefold()),key=lambda t:(t['artist'].casefold(),t['title'].casefold(),t['id']))
    wanted=request.get('status')
    if wanted in STATUS_FILTERS:
        try:index_for_filter=writer.read_index(root/'CDJMODS')[0];filter_error=None
        except (OSError,ValueError) as exc:index_for_filter={};filter_error=str(exc)
        prepared={entry.get('file_path') for table in ('tracks','tracks_onelibrary') for entry in index_for_filter.get(table,{}).values()}
        filtered=[t for t in filtered if quick_status(root,t,prepared,filter_error) in STATUS_FILTERS[wanted]]
    offset=max(0,int(request.get('offset',0)));offset=min(offset,max(0,(len(filtered)-1)//PAGE_SIZE*PAGE_SIZE))
    error=None
    try:index=writer.read_index(root/'CDJMODS')[0]
    except (OSError,ValueError) as exc:index={};error=str(exc)
    disk=shutil.disk_usage(root)
    from .engines import data_root
    model=stem_estimate.SizeModel.load(stem_estimate.model_path(data_root()))
    index_path=root/'CDJMODS'/'index.json'
    index_bytes=index_path.stat().st_size if index_path.is_file() else 0
    summary=library_summary(root,tracks,index,error)
    rows=[]
    for track in filtered[offset:offset+PAGE_SIZE]:
        job.check();state=stem_status(root,track,index,error)
        estimate=stem_estimate.estimate_track(stem_estimate.frames_from_seconds(track['duration']),model)
        if state['can_generate'] and track['duration'] and estimate['high_bytes']>disk.free:
            state.update(can_generate=False,detail='Not enough USB space. Free space before generating stems.')
        if state['can_generate'] and not estimate['fits_format']:
            state.update(can_generate=False,detail='This track exceeds the stem format length limit (about 23 minutes).')
        rows.append({**track,**state,'estimated_bytes':estimate['bytes'],'estimate':estimate})
    return {'volume':str(root),'identity':managed_usb.identity(root,media=True),'database':str(pdb),'total':len(tracks),'matched':len(filtered),'offset':offset,'page_size':PAGE_SIZE,
            'free_bytes':disk.free,'total_bytes':disk.total,'index_bytes':index_bytes,'summary':summary,
            'index_room_tracks':max(0,(stem_estimate.INDEX_LIMIT-index_bytes)//stem_estimate.INDEX_ENTRY_BYTES),
            'size_model':{'learned':model.observations()>0,'observations':model.observations()},
            'tracks':rows,'index_error':error}

STATUS_FILTERS={'missing':('missing',),'present':('present',),'cant':('unsupported','unavailable','error')}

def quick_status(root,track,prepared,index_error):
    """The row's status from the index and the file system alone; no bundle is opened."""
    try:source=source_path(root,track['file_path'])
    except ValueError:return 'error'
    if index_error:return 'error'
    if not source.is_file():return 'unavailable'
    if track['file_path'] in prepared:return 'present'
    return 'missing' if source.suffix.lower() in cache.SUPPORTED_SUFFIXES else 'unsupported'

def library_summary(root,tracks,index,index_error):
    """Library-wide counts by status and the bytes stems already take, without opening any bundle."""
    prepared={entry.get('file_path') for table in ('tracks','tracks_onelibrary') for entry in index.get(table,{}).values()}
    counts={'present':0,'missing':0,'unsupported':0,'unavailable':0,'error':0}
    for track in tracks:
        counts[quick_status(root,track,prepared,index_error)]+=1
    return {'counts':counts,'stems_bytes':stems_on_usb(root)['bytes'],'tracks':len(tracks)}

def stems_on_usb(root):
    """Prepared stem folders under CDJMODS/stems and the bytes their files take."""
    tracks=size=0
    stems=Path(root)/'CDJMODS'/'stems'
    if stems.is_dir():
        for folder in stems.iterdir():
            if folder.is_dir() and not folder.is_symlink():
                tracks+=1
                size+=sum(f.stat().st_size for f in folder.iterdir() if f.is_file())
    return {'tracks':tracks,'bytes':size}

def add_waveforms(request,job,resources):
    from .stem_import import import_python
    root=cache._safe_path(request['volume']);managed_usb.check_target(root,request['expected_identity'],media=True)
    return writer.add_waveforms(root,request['file_path'],import_python(job,resources),job)

def generate(request,job,resources):
    from . import engines
    root=cache._safe_path(request['volume']);managed_usb.check_target(root,request['expected_identity'],media=True)
    source=source_path(root,request['file_path'])
    return engines.separate({'source':str(source),'preset':request['preset']},job,resources)
