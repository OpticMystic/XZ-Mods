"""Read-only Rekordbox USB browsing and stem availability checks."""
from pathlib import Path, PurePosixPath
import ctypes
import os
import shutil
import sys
from . import cache,rekordbox_pdb,overcue_writer as writer,managed_usb

PAGE_SIZE=40

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
        supported=source.suffix.lower() in ('.wav','.flac') and track['sample_rate'] in (0,44100)
        result.update(source=str(source),can_generate=supported)
        if index_error:return {**result,'stem_status':'error','stem_label':'Stem index needs attention','can_generate':False,'detail':index_error}
        matches=[entry for table in ('tracks','tracks_onelibrary') for entry in index.get(table,{}).values() if entry.get('file_path')==track['file_path']]
        direct=index.get('tracks',{}).get(str(track['id']))
        if direct and direct.get('file_path')!=track['file_path']:return {**result,'stem_status':'error','stem_label':'Stem track ID conflict','can_generate':False}
        if not matches:
            if not supported:result.update(stem_label='Use OverCue',detail='XZ Mods currently generates stems from 44.1 kHz WAV or FLAC exports.')
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
            codecs.add('flac-96k' if magic==b'OVPGZ003' else 'zlib' if magic==b'OVPGZ001' else 'unknown')
        owner=manifest.get('writer',{}).get('name','OverCue')
        codec=next(iter(codecs)) if len(codecs)==1 else 'mixed'
        result.update(stem_status='present',stem_label='Prepared · '+owner+(' · FLAC' if codec=='flac-96k' else ''),
                      can_generate=supported and owner==writer.WRITER,can_verify=True,page_codec=codec,stored_bytes=stored,
                      detail='All seven stem files are present. Verify to check the decoded audio and source identity.')
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
    offset=max(0,int(request.get('offset',0)));offset=min(offset,max(0,(len(filtered)-1)//PAGE_SIZE*PAGE_SIZE))
    error=None
    try:index=writer.read_index(root/'CDJMODS')[0]
    except (OSError,ValueError) as exc:index={};error=str(exc)
    disk=shutil.disk_usage(root)
    rows=[]
    for track in filtered[offset:offset+PAGE_SIZE]:
        job.check();state=stem_status(root,track,index,error)
        # Upper bound for seven 96 kHz stereo PCM roles, with page and filesystem overhead.
        estimate=int(track['duration']*96000*4*7*1.04)+16*1024*1024
        if state['can_generate'] and track['duration'] and estimate>disk.free:
            state.update(can_generate=False,detail='Not enough USB space. Free space before generating stems.')
        if state['can_generate'] and track['duration']>1398:
            state.update(can_generate=False,detail='This track exceeds the stem format length limit.')
        rows.append({**track,**state,'estimated_bytes':estimate})
    return {'volume':str(root),'identity':managed_usb.identity(root,media=True),'database':str(pdb),'total':len(tracks),'matched':len(filtered),'offset':offset,'page_size':PAGE_SIZE,
            'free_bytes':disk.free,'total_bytes':disk.total,'tracks':rows,'index_error':error}

def generate(request,job,resources):
    from . import engines
    root=cache._safe_path(request['volume']);managed_usb.check_target(root,request['expected_identity'],media=True)
    source=source_path(root,request['file_path'])
    return engines.separate({'source':str(source),'preset':request['preset']},job,resources)
