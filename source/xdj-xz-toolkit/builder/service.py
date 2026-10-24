# SPDX-License-Identifier: MIT
"""JSON-line backend for the independent XZ Mods desktop application."""
from __future__ import annotations
import json
import os
from pathlib import Path
from .host import executable
import sys
import tempfile
import re
from typing import NamedTuple
from . import cache,overcue_writer,rekordbox_pdb,usb,runtime_ota,games
from .jobs import Job,Cancelled

FIRMWARE_126={'filename':'XDJXZ_v126.zip',
    'url':'https://downloads.support.alphatheta.com/firmwares/all-in-one-dj-systems/XDJ-XZ/XDJXZ_v126.zip',
    'sha256':'7c4159ca90b0cfd651725a68e10a11cf1a621239f703a0071f24355cc89fed7f','bytes':70775680}

def official_firmware(job):
    from .engines import data_root,download
    job.progress('download','Getting the official XDJ-XZ 1.26 firmware from AlphaTheta')
    return download(FIRMWARE_126,data_root()/'local-firmware',job)

def resources():
    configured=os.environ.get('XZ_BUILDER_RESOURCES')
    if not configured:raise ValueError('Builder resources are not configured')
    return Path(configured).resolve()

VERSION='0.5.0-preview.19'
SOURCE_FORMATS=['WAV','FLAC','MP3']
# 96 kHz roles are capped at 4096 pages; resample_poly(320/147) output is ceil(frames*320/147).
MAX_SOURCE_FRAMES=overcue_writer.MAX_FRAMES*147//320

def status():
    root=resources();runtime=root/'runtime'/'manifest.json'
    manifest=json.loads(runtime.read_text()) if runtime.exists() else None
    catalog_path=Path(__file__).with_name('models.json')
    catalog=json.loads(catalog_path.read_text()) if catalog_path.exists() else {}
    return {'name':'XZ Mods','version':VERSION,'firmware':'XDJ-XZ 1.26',
        'prepared_formats':['overcue-stems/4','stemd-cache/1'],
        'prepared_containers':['OVPGZ001','OVPGZ003','OVPGZ004'], 'stem_page_codecs':['zlib','flac-96k','flac-44k1'],
        'separation_output_format':'overcue-index/1',
        'stem_import':{'plan_format':'stem-import-plan/1','archives':['ZIP'],'source_formats':['WAV','FLAC','AIFF','OGG','MP3'],
                       'groups':['vocals','drums','harmonics','instrumental','mix']},
        'stem_builder':{'beta':True,'source_formats':SOURCE_FORMATS,'sample_rate':44100,'source_sample_rates':[cache.MIN_RATE,cache.MAX_RATE],
                        'requires_rekordbox_export':True,
                        'memory_profile':'disk-backed-tracks-gpu-when-present','output_page_codec':'flac-96k','default_preset':catalog.get('default_preset')},
        'release_ready':False,'runtime_present':manifest is not None,'runtime':manifest,
        'vjtools_required':False,'vjtools_connection':True,'engines':catalog,
        'limits':{'sample_rate':44100,'channels':2,'source_formats':SOURCE_FORMATS,'source_sample_rates':[cache.MIN_RATE,cache.MAX_RATE],
                  'max_source_frames':MAX_SOURCE_FRAMES},
        'release_gates':['Physical pad/audio retest','Native inline UI acceptance',
            'Fresh boot and two-deck qualification','Real model execution and cache timing tests',
            'Packaged dependency/source notices audit']}

class Track(NamedTuple):
    source:Path
    usb_root:Path
    file_path:str
    track_id:int
    frames:int
    sha256:str
    source_rate:int=44100
    mp3:dict|None=None

def locate_track(source):
    """The USB root is everything before the first Contents folder, as the device reader derives it."""
    source=cache._regular(source);parts=source.parts
    at=parts.index('Contents',1) if 'Contents' in parts[1:-1] else 0
    if not at:raise ValueError('Choose the track inside the USB Contents folder that Rekordbox exported.')
    root=Path(*parts[:at])
    track_id,file_path=rekordbox_pdb.find_track(root,'/'+'/'.join(parts[at:]))
    info=cache.inspect_audio(source)
    if info['frames']>MAX_SOURCE_FRAMES:raise ValueError('This track is longer than OverCue stems allow (about 23 minutes).')
    overcue_writer.check_writable(root,file_path,track_id)
    return Track(source,root,file_path,track_id,info['frames'],usb.sha(source),info['sample_rate'],info.get('mp3'))

def _as_wav(path,target,job):
    """A 44.1 kHz 16-bit stereo WAV for the inference and role stages, decoded and resampled only when needed."""
    path=Path(path);target=Path(target);info=cache.inspect_audio(path)
    native=info['sample_rate']==44100
    if path.suffix.lower()=='.wav' and native and info.get('source_bits',16)==16 and info.get('source_channels',2)==2:return path
    decoded=target if native else target.with_name(target.stem+'.source-rate.wav')
    job.run([executable(resources(),'xz-audio-helper'),'convert',path,decoded])
    if native:return target
    from .stem_import import import_python
    job.progress('decode',f'Resampling {info["sample_rate"]} Hz audio to 44.1 kHz. Your original track is unchanged.')
    job.run([import_python(job,resources()),'-I',resources()/'inference'/'resample_source.py','--input',decoded,'--output',target,'--frames',str(info['frames'])])
    decoded.unlink()
    return target

def prepare_stems(track,parts,separation_id,python,job,work,mix=None):
    """One pipeline for import and separation: 44.1 kHz parts, 96 kHz roles, verified OverCue bundle."""
    from .engines import require_scratch
    require_scratch(track.frames)
    wavs={}
    for name,(path,gain) in parts.items():
        if cache.inspect_audio(path)['frames']!=track.frames:raise ValueError('Stems must have exactly the same length as the track')
        wavs[name]=_as_wav(path,work/(name+'.wav'),job)
    mix=mix or _as_wav(track.source,work/'mix.wav',job)
    roles=work/'roles'
    import time
    publish_started=time.time()
    job.progress('render','Resampling stems to 96 kHz for OverCue. Your original track is unchanged.')
    job.run([python,'-I',resources()/'inference'/'overcue_roles.py','--mix',mix,
        '--vocals',wavs['vocals'],'--vocals-gain',repr(parts['vocals'][1]),
        '--harmonics',wavs['harmonics'],'--harmonics-gain',repr(parts['harmonics'][1]),'--output-dir',roles],
        # Resampling needs no BLAS; default OpenBLAS thread buffers alone commit about 1 GB on many-core PCs.
        env={**os.environ,'OPENBLAS_NUM_THREADS':'1','OMP_NUM_THREADS':'1','MKL_NUM_THREADS':'1'})
    if usb.sha(track.source)!=track.sha256:raise ValueError('The track changed while stems were being prepared')
    from .stem_import import import_python
    receipt=overcue_writer.publish(track.usb_root,track.file_path,track.track_id,track.sha256,
        track.source.suffix.lower()[1:],roles,separation_id,VERSION,job,page_python=import_python(job,resources()),
        source={'sample_rate':track.source_rate,'mp3':track.mp3})
    from . import stem_estimate
    from .engines import data_root,record_publish_timing
    stem_estimate.observe_bundle(Path(track.usb_root)/'CDJMODS'/'stems'/receipt['bundle'],receipt['frames'],data_root())
    if not receipt.get('reused'):record_publish_timing(track.frames,time.time()-publish_started)
    return {'format':overcue_writer.INDEX_SCHEMA,'beta':True,'usb_root':str(track.usb_root),'file_path':track.file_path,
        'track_id':track.track_id,**receipt,'alignment_verified':False,'separation_id':separation_id}

def import_stems(request,job):
    from .engines import data_root,engine_lock,engine_python
    separation_id=cache.validate_model_id(request['separation_id'])
    job.progress('inspect','Checking the USB track, its Rekordbox export and the stem files')
    track=locate_track(request['source'])
    parts={name:(cache._regular(request[name]),cache._gain(request.get(name+'_gain',1))) for name in ('vocals','harmonics')}
    with engine_lock():
        python=engine_python()
        with tempfile.TemporaryDirectory(prefix='xz-import-',dir=data_root()) as work:
            return prepare_stems(track,parts,separation_id,python,job,Path(work))

def inspect_cache_entry(entry,source):
    entry=cache._safe_path(entry)
    if not re.fullmatch('[0-9a-f]{16}',entry.name):raise ValueError('Choose the upstream cache entry folder containing meta and the two stems')
    fields={}
    for line in cache._regular(entry/'meta').read_text(encoding='ascii').splitlines():
        if not line.strip():continue
        name,separator,value=line.partition('=')
        if not separator or name in fields:raise ValueError('Cache metadata is malformed')
        fields[name]=value
    if fields.get('v')!='1':raise ValueError('Only upstream cache version 1 is supported')
    frames=int(fields['frames'])
    info=cache.inspect_audio(source)
    if info['frames']!=frames or cache.track_key(source,frames)!=entry.name:
        raise ValueError('This cache does not match the selected original track')
    result={'separation_id':entry.parent.parent.name,'frames':frames,'key':entry.name}
    for name in ('harmonics','vocals'):
        paths=[entry/(name+extension) for extension in ('.wav','.flac') if (entry/(name+extension)).exists()]
        if len(paths)!=1 or cache.inspect_audio(paths[0])['frames']!=frames:raise ValueError('Cache stem is missing, ambiguous or misaligned')
        result[name]=str(paths[0]);result[name+'_gain']=cache._gain(float(fields[name]))
    return result

def three_band_waveforms(request,job,restore):
    job.progress('waveforms','Putting back the Rekordbox waveform colours' if restore else 'Giving the USB waveforms 3-band colours')
    from . import three_band
    volume=cache._safe_path(request['volume'])
    if not volume.is_dir():raise ValueError('Choose the USB root folder that has the PIONEER folder')
    report=(three_band.restore if restore else three_band.apply)(volume,job);failed=report['failed']
    # The Tauri host drops result lines over 64 KiB.
    return {'format':'rgb-waveforms-restored/1' if restore else 'three-band-waveforms/1','usb_root':str(volume),
        **report,'failed':failed[:50],'failed_count':len(failed)}

def dispatch(request,job):
    from . import mod_versions
    method=request.get('method')
    if method=='list_mod_versions':return mod_versions.inventory(resources())
    if method=='select_mod_version':return mod_versions.choose(resources(),request)
    if method=='import_mod_version':return mod_versions.import_version(resources(),request['folder'])
    if method=='inspect_mod_settings':return mod_versions.inspect_settings(resources(),request['volume'])
    if method in ('prepare_usb','build_usb','update_usb'):
        with mod_versions.selected_resources(resources(),request,job) as (selected,version):
            change=mod_versions.settings_change(request['volume'],selected,resources())
            if change and request.get('adapt_mod_settings') is not True:
                raise ValueError('This version needs compatible USB settings. Review the settings adjustment before switching versions.')
            if change and request.get('expected_mod_settings')!=change['expected']:
                raise ValueError('USB settings changed since the adjustment was reviewed. Inspect them again.')
            result=_dispatch(request,job,selected,change)
            result['mod_version']=version
            try:
                mod_versions.record_loader(result,version)
                if 'inspection' in result:result['inspection']['recorded_mod_version']=version
            except (OSError,ValueError) as exc:
                result['version_record_warning']='Loader verified, but its local version record could not be saved: '+str(exc)
            return result
    return _dispatch(request,job)

def _dispatch(request,job,loader_resources=None,settings_change=None):
    method=request.get('method')
    if method in ('inspect_device_library','repair_device_library','restore_device_library','save_device_library_report'):
        from . import library_repair
        if method=='inspect_device_library':return library_repair.inspect(request['volume'])
        if method=='save_device_library_report':return library_repair.save_report(request['volume'])
        return library_repair.mutate(request,job,restore=method=='restore_device_library')
    if method in ('read_dj_profiles','read_logo','save_dj_profile','set_active_dj_profile','write_profile_track','dj_profile_track_status'):
        from . import dj_profile
        if method=='read_dj_profiles':return dj_profile.read(request['volume'])
        if method=='read_logo':return dj_profile.read_logo(request['path'])
        if method=='save_dj_profile':return dj_profile.save(request['volume'],request)
        if method=='set_active_dj_profile':return dj_profile.set_active(request['volume'],request['dj_id'],request.get('expected_revision'))
        if method=='dj_profile_track_status':return dj_profile.profile_track_status(request['volume'])
        return dj_profile.write_profile_track(request['volume'],request['dj_id'],request.get('destination'),request.get('folder'))
    if method=='games_status':return games.status()
    if method=='download_game':return games.obtain(request['game'],job)
    if method=='install_game_usb':return games.install(request['game'],request['volume'],job)
    if method=='prepare_games_usb':return games.prepare_usb(request['volume'],job,request.get('games'))
    if method=='games_on_usb':return {'format':'xz-games-on-usb/1','games':games.installed(request['volume'])}
    if method=='discover_usbs':
        from . import managed_usb
        return managed_usb.discover(resources())
    if method=='ota_networks':return {'addresses':runtime_ota.network_addresses(),**runtime_ota.readiness(resources())}
    if method=='serve_runtime_updates':return runtime_ota.serve(resources(),request['address'],job)
    if method=='generate_library_stem_batch':
        from . import stem_batch
        return stem_batch.generate(request,job,resources())
    if method in ('discover_stem_usbs','browse_stem_library','generate_library_stems','add_stem_waveforms'):
        from . import stem_library
        if method=='discover_stem_usbs':return stem_library.discover()
        if method=='browse_stem_library':return stem_library.browse(request,job)
        if method=='add_stem_waveforms':return stem_library.add_waveforms(request,job,resources())
        return stem_library.generate(request,job,resources())

    if method=='prepare_layered_waveforms':
        from . import layered_waveforms
        volume=cache._safe_path(request['volume'])
        report=layered_waveforms.prepare(volume,job);failed=report['failed']
        return {'format':'layered-waveforms/1','usb_root':str(volume),**report,
            'failed':failed[:50],'failed_count':len(failed),'requires_new_runtime':True,'hardware_verified':False}
    if method in ('inspect_usb','read_usb_settings','save_usb_settings','update_usb','restore_usb_loader'):
        from . import managed_usb
        volume=request['volume']
        if method=='inspect_usb':return managed_usb.inspect(volume,resources())
        if method=='read_usb_settings':
            from . import mod_versions
            return {'identity':managed_usb.identity(volume),'settings':managed_usb.read_settings(volume,mod_versions.settings_resources(volume,resources()))}
        observed=request['expected_identity']
        managed_usb.check_target(volume,observed)
        if method=='save_usb_settings':
            from . import mod_versions
            return managed_usb.save_settings(volume,observed,request['expected_revision'],request['values'],mod_versions.settings_resources(volume,resources()),job)
        if method=='restore_usb_loader':return managed_usb.restore_loader(volume,observed,request['expected_loader'],request['transaction_id'],resources(),job)
        if request.get('experimental') is not True:raise ValueError('Review the loader update details before updating')
        managed_usb._check_revision(Path(volume)/'autoexec.bin',request['expected_loader'])
        from .boot_support import ensure_boot_key
        firmware=request.get('firmware') or official_firmware(job)
        key=request.get('key') or ensure_boot_key(job)
        result=managed_usb.update_loader(volume,observed,request['expected_loader'],firmware,key,loader_resources or resources(),job,settings_change=settings_change)
        if usb.target_info(volume,boot_loader=False)['direct_usb_root']:result['game_setup']=games.prepare_usb(volume,job,request.get('games'))
        return result
    if method in ('plan_stem_import','import_grouped_stems'):
        from . import stem_import
        return (stem_import.plan if method=='plan_stem_import' else stem_import.import_groups)(request,job,resources())
    if method=='status':return status()
    if method=='inspect_overcue':
        source=cache._regular(request['source'])
        job.progress('verify','Checking the OverCue source identity and every prepared audio page')
        output=job.run([executable(resources(),'xz-overcue-check'),source])
        return json.loads(output)
    if method=='download_firmware':
        path=official_firmware(job)
        return {'firmware_path':str(path),'sha256':FIRMWARE_126['sha256'],'source':'AlphaTheta','local_input_only':True}
    if method=='inspect_audio':return cache.inspect_audio(request['source'])
    if method=='inspect_cache':return inspect_cache_entry(request['entry'],request['source'])
    if method=='inspect_firmware':
        from .boot_support import ensure_boot_key
        key=request.get('key') or ensure_boot_key(job)
        return usb.inspect_inputs(request['firmware'],key)
    if method=='import_stems':return import_stems(request,job)
    if method=='import_branding':
        from .branding import import_branding
        return import_branding(request,job)
    if method=='build_usb':
        from .boot_support import ensure_boot_key
        if request.get('experimental') is not True:raise ValueError('Review the experimental build notice before preparing a USB')
        key=request.get('key') or ensure_boot_key(job)
        if settings_change:
            from . import managed_usb
            result=managed_usb.build_with_settings(request['volume'],request['firmware'],key,loader_resources or resources(),job,settings_change)
        else:result=usb.build_usb(request['volume'],request['firmware'],key,loader_resources or resources(),job,True)
        if result.get('direct_usb_root'):result['game_setup']=games.prepare_usb(request['volume'],job,request.get('games'))
        return result
    if method=='prepare_usb':
        from .boot_support import ensure_boot_key
        if request.get('experimental') is not True:raise ValueError('Review the experimental build notice before preparing a USB')
        volume=usb.require_usb_root(request['volume'])
        firmware=official_firmware(job)
        key=ensure_boot_key(job)
        if settings_change:
            from . import managed_usb
            result=managed_usb.build_with_settings(volume,firmware,key,loader_resources or resources(),job,settings_change)
        else:result=usb.build_usb(volume,firmware,key,loader_resources or resources(),job,True)
        return {**result,'firmware_source':'AlphaTheta','one_step':True,'game_setup':games.prepare_usb(volume,job,request.get('games'))}
    if method in ('apply_three_band','restore_rgb_waveforms'):return three_band_waveforms(request,job,method=='restore_rgb_waveforms')
    if method=='save_engine_report':
        from . import engines
        return engines.save_engine_report(request,job,resources())
    if method in ('setup_engine','separate','plan_engine_setup','estimate_separation'):
        from . import engines
        if method=='plan_engine_setup':return engines.plan_engine_setup(request,job,resources())
        if method=='estimate_separation':
            frames=request['frames']
            if type(frames) is not int or not 0<frames<=MAX_SOURCE_FRAMES:raise ValueError('Frames must be a positive track length')
            return {'preset':request['preset'],'frames':frames,'device':engines.planned_device(request['preset']),
                'seconds':engines.estimate_separation_seconds(request['preset'],frames)}
        return engines.setup(request,job,resources()) if method=='setup_engine' else engines.separate(request,job,resources())
    raise ValueError('Unknown builder operation')

def main():
    try:
        raw=sys.stdin.buffer.read(65537)
        if len(raw)>65536:raise ValueError('Request is too large')
        request=json.loads(raw)
        if not isinstance(request,dict):raise ValueError('Expected an operation object')
        result=dispatch(request,Job(os.environ.get('XZ_BUILDER_CANCEL_FILE')))
        cancelled=result.get('cancelled',False) if isinstance(result,dict) else False
        aborted=result.get('aborted',False) if isinstance(result,dict) else False
        event={'event':'result','ok':not (cancelled or aborted),'result':result}
        if cancelled:event.update(cancelled=True,error='Cancelled')
        if aborted:event['error']=result['error']
        print(json.dumps(event),flush=True)
    except Cancelled:
        print(json.dumps({'event':'result','ok':False,'cancelled':True,'error':'Cancelled'}),flush=True)
    except Exception as exc:
        # Input secrets are never included in diagnostics or requests echoed to stdout.
        print(json.dumps({'event':'result','ok':False,'error':str(exc)}),flush=True)

if __name__=='__main__':main()
