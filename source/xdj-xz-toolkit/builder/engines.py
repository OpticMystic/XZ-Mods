# SPDX-License-Identifier: MIT
"""Managed, independent separation setup with verified model downloads, GPU planning and a persistent worker."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import tempfile
import time
import urllib.request
from . import cache
from .host import executable, detect_accelerator, detect_nvidia, torch_install, torch_download_bytes
from .jobs import exclusive_lock

TIMING_RUNS = 10

def registry():return json.loads(Path(__file__).with_name('models.json').read_text(encoding='utf-8'))

def data_root():
    configured=os.environ.get('XZ_BUILDER_DATA')
    base=Path(configured) if configured else Path(os.environ.get('LOCALAPPDATA',Path.home()/'.local/share'))/'XZ Mods'
    base=cache._safe_path(base);base.mkdir(parents=True,exist_ok=True);return base

def file_sha(path):
    with Path(path).open('rb') as file:return hashlib.file_digest(file,'sha256').hexdigest()

def artifact_present(record,folder):
    target=Path(folder)/record['filename']
    return target.is_file() and target.stat().st_size==record['bytes'] and file_sha(target)==record['sha256']

def download(record,folder,job):
    name=record['filename']
    if Path(name).name!=name or not record['url'].startswith('https://'):raise ValueError('Invalid packaged artifact manifest')
    folder=cache._safe_path(folder);folder.mkdir(parents=True,exist_ok=True)
    target=cache._safe_path(folder/name)
    if artifact_present(record,folder):return target
    with tempfile.NamedTemporaryFile(prefix='.download-',dir=folder,delete=False) as file:temporary=Path(file.name)
    try:
        request=urllib.request.Request(record['url'],headers={'User-Agent':'XZMods/0.2'})
        digest=hashlib.sha256();received=0;last_report=0
        with urllib.request.urlopen(request,timeout=15) as response,temporary.open('wb') as output:
            if not response.geturl().startswith('https://'):raise ValueError('Artifact download redirected away from HTTPS')
            while True:
                job.check();block=response.read(1024*1024)
                if not block:break
                received+=len(block)
                if received>record['bytes']:raise ValueError('Artifact exceeds its declared size')
                output.write(block);digest.update(block)
                if received-last_report>=8*1024*1024:
                    job.progress('download',f'{name}: {received*100//record["bytes"]}%',pct=received*100/record['bytes'],bytes=received,total=record['bytes']);last_report=received
        if received!=record['bytes'] or digest.hexdigest()!=record['sha256']:raise ValueError(f'Integrity check failed for {name}')
        cache._safe_path(target);os.replace(temporary,target)
        return target
    finally:
        if temporary.exists():temporary.unlink()

def engine_lock():return exclusive_lock(cache._safe_path(data_root()/'.engine.lock'))

def require_scratch(frames):
    """Worst-case raw roles plus FLAC pages, bounded PCM maps and filesystem headroom."""
    output_frames=-(-frames*320//147)
    needed=output_frames*4*7*5//2+frames*24+128*1024*1024
    if shutil.disk_usage(data_root()).free<needed:
        raise ValueError(f'Low-memory processing needs {needed/1024**3:.1f} GB of free temporary disk space for this track. Free space on the XZ Mods data drive and retry.')

def python_path(environment):return environment/('Scripts/python.exe' if os.name=='nt' else 'bin/python')

def engine_python():
    base=data_root();python=python_path(base/'engine-python')
    if not python.is_file() or not any(base.glob('setup-*.json')):
        raise ValueError('Set up the separation engine first. Imported stems are resampled to 96 kHz with it.')
    return python

def installed_torch_flavour(uv,python,job):
    """The flavour of the torch already in the venv, from its local version label; None when absent."""
    if not python.is_file():return None
    for line in job.run([uv,'pip','freeze','--python',python]).splitlines():
        name,separator,version=line.strip().partition('==')
        if name=='torch':
            _,plus,local=version.partition('+')
            return local if plus else 'default'
    return None

def preset_artifacts(catalog,preset):
    weights=[];code={}
    for item in catalog['presets'][preset]['passes']:
        record=catalog['separators'][item['model']]
        weights.extend(name for name in record['weights'] if name not in weights)
        if record.get('code'):code[record['code']]=catalog['code'][record['code']]
    return weights,code

def plan_engine_setup(request,job,resources):
    """What setup would install for this preset on this machine, before anything is downloaded.

    Also everything the setup wizard shows before the user commits: the machine, every preset with its
    installed state and expected time per track, and where the runtime lands.
    """
    import platform,sys
    catalog=registry();preset=request.get('preset') or catalog['default_preset']
    if preset not in catalog['presets']:raise ValueError('Unknown separation preset')
    if sys.platform=='darwin' and platform.machine()=='x86_64':
        return {'supported':False,'device':'cpu','device_name':'','preset':preset,'presets':[],'installed_preset':None,
                'reason':'Stem generation needs an Apple Silicon Mac. Intel Macs can import prepared stems and use every USB, settings, waveform and game tool.'}
    accelerator=detect_accelerator(catalog['torch'],detect=(lambda:None) if request.get('device')=='cpu' else detect_nvidia)
    base=data_root();python=python_path(base/'engine-python')
    uv=executable(resources,'uv')
    installed=installed_torch_flavour(uv,python,job) if uv.is_file() else None
    recreate=installed is not None and installed!=accelerator['torch_flavour']
    download_bytes=0
    if installed!=accelerator['torch_flavour']:
        download_bytes+=torch_download_bytes(catalog['torch'],accelerator['torch_flavour'])+catalog['runtime_packages_bytes']
    weights,code=preset_artifacts(catalog,preset)
    missing=[name for name in weights if not artifact_present(catalog['models'][name],base/'models')]
    download_bytes+=sum(catalog['models'][name]['bytes'] for name in missing)
    for group_id,group in code.items():
        download_bytes+=sum(record['bytes'] for record in group['files'].values() if not artifact_present(record,base/'vendored-code'/group_id))
    runtime_bytes=download_bytes-sum(catalog['models'][name]['bytes'] for name in missing)
    identity=hashlib.sha256(json.dumps(catalog,sort_keys=True).encode()).hexdigest()
    runtime_ready=python.is_file() and not recreate and installed==accelerator['torch_flavour']
    presets=[]
    for candidate,definition in catalog['presets'].items():
        weights,groups=preset_artifacts(catalog,candidate)
        marker=base/f'setup-{candidate}.json'
        try:previous=json.loads(marker.read_text(encoding='utf-8')) if marker.is_file() else {}
        except ValueError:previous={}
        model_bytes=sum(catalog['models'][name]['bytes'] for name in weights if not artifact_present(catalog['models'][name],base/'models'))
        code_missing=any(not artifact_present(record,base/'vendored-code'/group_id) for group_id,group in groups.items() for record in group['files'].values())
        # Installed means the next Prepare runs without a download or a runtime rebuild; anything less goes through the wizard.
        presets.append({'id':candidate,'name':definition.get('title',candidate),
            'installed':runtime_ready and previous.get('registry_sha256')==identity and previous.get('torch_flavour')==accelerator['torch_flavour']
                        and not model_bytes and not code_missing,
            'download_bytes':model_bytes,'marker_time':marker.stat().st_mtime if marker.is_file() else 0,
            'seconds_per_track':round(estimate_track_seconds(candidate,TRACK_FRAMES,accelerator['device']))})
    ready=[entry for entry in presets if entry['installed']]
    installed_preset=max(ready,key=lambda entry:entry['marker_time'])['id'] if ready else None
    for entry in presets:entry.pop('marker_time')
    import shutil
    return {**accelerator,'supported':True,'preset':preset,'torch_installed_flavour':installed,'recreate_runtime':recreate,
        'download_bytes_estimate':download_bytes,'models_to_download':missing,
        'estimate_note':catalog['torch']['download_bytes_note'],
        'recommended':'fast' if accelerator['device']=='cpu' else 'balanced','installed_preset':installed_preset,'presets':presets,
        'parts':[{'id':'runtime','bytes':runtime_bytes},{'id':'models','bytes':download_bytes-runtime_bytes}],
        'data_drive':base.drive or str(base),'data_free_bytes':shutil.disk_usage(base).free,**machine_facts(),
        'test_seconds':last_test_seconds()}

def setup(request,job,resources):
    with engine_lock():
        state=_setup(request,job,resources)
        if request.get('test_source'):state['test']=engine_test(state['preset'],request,job,resources)
        return state

TRACK_FRAMES=300*44100
TEST_SECONDS=30
PUBLISH_KEY='publish/any'
PUBLISH_PRIOR_RATE=0.2

def machine_facts():
    """CPU name, core count and RAM for the setup wizard's first step; blanks when the OS will not say."""
    import ctypes,platform,sys
    name=platform.processor() or ''
    if os.name=='nt':
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as key:
                name=winreg.QueryValueEx(key,'ProcessorNameString')[0].strip()
        except OSError:pass
    ram=0
    try:
        if os.name=='nt':
            class Status(ctypes.Structure):
                _fields_=[('length',ctypes.c_ulong),('load',ctypes.c_ulong),('total',ctypes.c_ulonglong),('avail',ctypes.c_ulonglong),
                          ('page_total',ctypes.c_ulonglong),('page_avail',ctypes.c_ulonglong),('virt_total',ctypes.c_ulonglong),
                          ('virt_avail',ctypes.c_ulonglong),('virt_ext',ctypes.c_ulonglong)]
            status=Status();status.length=ctypes.sizeof(Status)
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):ram=int(status.total)
        elif sys.platform=='darwin':
            import subprocess
            ram=int(subprocess.run(['/usr/sbin/sysctl','-n','hw.memsize'],capture_output=True,text=True,timeout=5).stdout.strip() or 0)
        else:
            for line in Path('/proc/meminfo').read_text().splitlines():
                if line.startswith('MemTotal:'):ram=int(line.split()[1])*1024
    except (OSError,ValueError,AttributeError):pass
    return {'cpu_name':name,'cpu_cores':os.cpu_count() or 0,'ram_bytes':ram}

def record_publish_timing(frames,seconds):
    """Seconds per source second for the 96 kHz render, FLAC pages, verification and waveforms, last TIMING_RUNS runs."""
    if not frames or seconds<=0:return
    store=read_timings();entry=store.setdefault(PUBLISH_KEY,{'rates':[]})
    entry['rates']=(entry.get('rates',[])+[seconds/(frames/44100)])[-TIMING_RUNS:];entry['updated']=time.time()
    path=timing_store();temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps(store,indent=2),encoding='utf-8');os.replace(temporary,path)

def estimate_publish_seconds(frames):
    entry=read_timings().get(PUBLISH_KEY,{})
    rate=statistics.median(entry['rates']) if entry.get('rates') else PUBLISH_PRIOR_RATE
    return rate*frames/44100

def estimate_track_seconds(preset,frames,device=None,include_load=False):
    """Expected wall-clock for one track through separation and publication."""
    return estimate_separation_seconds(preset,frames,device,include_load)+estimate_publish_seconds(frames)

def last_test_seconds():
    try:return json.loads((data_root()/'engine-test.json').read_text(encoding='utf-8')).get('seconds')
    except (OSError,ValueError):return None

def _excerpt(source,target,start,seconds):
    """Cut `seconds` of a 44.1 kHz 16-bit stereo WAV starting at `start` seconds, without numpy."""
    import wave
    with wave.open(str(source),'rb') as reader:
        total=reader.getnframes();count=min(total,seconds*44100)
        first=max(0,min(int(start*44100),total-count))
        reader.setpos(first);data=reader.readframes(count)
        with wave.open(str(target),'wb') as writer:
            writer.setparams(reader.getparams());writer.writeframes(data)
    return count

def engine_test(preset,request,job,resources):
    """Separate 30 s of the user's own track with the freshly installed engine; seeds the timing store."""
    from .service import _as_wav
    import tempfile
    source=cache._regular(request['test_source']);start=max(0.0,float(request.get('test_start') or 0))
    with tempfile.TemporaryDirectory(prefix='xz-engine-test-',dir=data_root()) as temporary:
        temporary=Path(temporary)
        job.progress('test','Taking 30 seconds of your track for the test')
        full=_as_wav(source,temporary/'full.wav',job)
        excerpt=temporary/'excerpt.wav';frames=_excerpt(full,excerpt,start,TEST_SECONDS)
        started=time.time()
        with Engine(preset,job,resources,locked=True) as engine:
            engine.separate(excerpt,temporary/'out',('vocals','harmonics'))
        seconds=round(time.time()-started,1)
    result={'seconds':seconds,'frames':frames,'preset':preset,'source':str(source),'start':start,'time':time.time()}
    (data_root()/'engine-test.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    return result

def save_engine_report(request,job,resources):
    """Everything a support thread needs about the engine on this machine, in one JSON file."""
    base=data_root();report={'time':time.time(),'markers':{},'timings':read_timings(),'test':None,'plan':None}
    for marker in sorted(base.glob('setup-*.json')):
        try:report['markers'][marker.name]=json.loads(marker.read_text(encoding='utf-8'))
        except ValueError as exc:report['markers'][marker.name]={'error':str(exc)}
    runtime=base/'installed-runtime.txt'
    report['installed_runtime']=runtime.read_text(encoding='utf-8',errors='replace')[-20000:] if runtime.is_file() else None
    try:report['test']=json.loads((base/'engine-test.json').read_text(encoding='utf-8'))
    except (OSError,ValueError):pass
    try:report['plan']=plan_engine_setup({},job,resources)
    except Exception as exc:report['plan_error']=str(exc)
    path=base/'engine-report.json';path.write_text(json.dumps(report,indent=2,default=str),encoding='utf-8')
    return {'report_path':str(path)}

def _setup(request,job,resources):
    catalog=registry();preset=request['preset']
    plan=plan_engine_setup(request,job,resources)
    base=data_root();environment=base/'engine-python';python=python_path(environment)
    uv=executable(resources,'uv')
    if not uv.is_file():raise ValueError('The packaged engine installer is missing')
    marker=base/('setup-'+preset+'.json')
    identity=hashlib.sha256(json.dumps(catalog,sort_keys=True).encode()).hexdigest()
    previous=json.loads(marker.read_text()) if marker.is_file() else {}
    ready=(previous.get('registry_sha256')==identity and previous.get('torch_flavour')==plan['torch_flavour']
           and python.is_file() and not plan['recreate_runtime'] and plan['torch_installed_flavour']==plan['torch_flavour'])
    if not ready:
        if plan['recreate_runtime']:
            job.progress('runtime','Replacing the separation runtime with the '+('GPU' if plan['device']!='cpu' else 'CPU')+' build')
            shutil.rmtree(environment)
        job.progress('runtime','Setting up a separate Python runtime for XZ Mods')
        if not python.is_file():job.run([uv,'venv','--python','3.11',environment])
        job.progress('runtime',f'Installing the separation runtime for {plan["device_name"] or "the CPU"}; this does not use VJ.Tools',
            download_bytes_estimate=plan['download_bytes_estimate'])
        job.run([uv,'pip','install','--python',python,*torch_install(catalog['torch'],plan['torch_flavour'])])
        packages=[name+'=='+version for name,version in catalog['runtime_versions'].items() if name not in ('torch','torchaudio')]
        job.run([uv,'pip','install','--python',python,*packages])
    models=base/'models';code=base/'vendored-code'
    weights,groups=preset_artifacts(catalog,preset)
    for name in weights:
        job.progress('models','Checking '+name);download(catalog['models'][name],models,job)
    for group_id,group in groups.items():
        for record in group['files'].values():download(record,code/group_id,job)
    notices=base/'licenses';notices.mkdir(exist_ok=True)
    for name,license in catalog['licenses'].items():(notices/(name+'.txt')).write_text(license['text'],encoding='utf-8')
    lock=job.run([uv,'pip','freeze','--python',python])
    (base/'installed-runtime.txt').write_text(lock)
    result={'preset':preset,'registry_sha256':identity,'python':str(python),'models':str(models),'code':str(code),
        'device':plan['device'],'device_name':plan['device_name'],'vram_bytes':plan['vram_bytes'],'torch_flavour':plan['torch_flavour'],
        'download_bytes_estimate':plan['download_bytes_estimate'],
        'code_and_weights_license':'MIT','runtime_validation_verified':False,'installed':True}
    marker.write_text(json.dumps(result,indent=2)+'\n')
    return result

def timing_store():return data_root()/'separation-timings.json'

def read_timings():
    path=timing_store()
    try:return json.loads(path.read_text(encoding='utf-8')) if path.is_file() else {}
    except ValueError:return {}

def timing_key(preset,device,parts=('vocals','harmonics')):
    return f'{preset}/{device}' if tuple(parts)==('vocals','harmonics') else f'{preset}/{device}/'+'+'.join(parts)

def record_timing(preset,device,timing,parts=('vocals','harmonics')):
    """Seconds per second of source audio and model-load seconds, last TIMING_RUNS runs per preset, device and parts."""
    if not timing.get('frames'):return
    store=read_timings();entry=store.setdefault(timing_key(preset,device,parts),{'rates':[],'loads':[]})
    seconds=timing['separate_seconds']+timing['postprocess_seconds']
    entry['rates']=(entry['rates']+[seconds/(timing['frames']/44100)])[-TIMING_RUNS:]
    if timing.get('load_seconds'):entry['loads']=(entry['loads']+[timing['load_seconds']])[-TIMING_RUNS:]
    entry['updated']=time.time()
    path=timing_store();temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps(store,indent=2),encoding='utf-8');os.replace(temporary,path)

def planned_device(preset=None):
    base=data_root()
    for marker in sorted(base.glob('setup-*.json'),key=lambda p:p.stat().st_mtime,reverse=True):
        if preset and marker.name!=f'setup-{preset}.json' and (base/f'setup-{preset}.json').is_file():continue
        try:return json.loads(marker.read_text()).get('device','cpu')
        except ValueError:continue
    return 'cpu'

def estimate_separation_seconds(preset,frames,device=None,include_load=True):
    """Median of this machine's recent runs for the preset and device, or the registry prior before any run."""
    catalog=registry()
    if preset not in catalog['presets']:raise ValueError('Unknown separation preset')
    device=device or planned_device(preset)
    definition=catalog['presets'][preset];entry=read_timings().get(f'{preset}/{device}',{})
    rate=statistics.median(entry['rates']) if entry.get('rates') else definition['prior_seconds_per_source_second'].get(device,definition['prior_seconds_per_source_second']['cpu'])
    load=statistics.median(entry['loads']) if entry.get('loads') else definition['prior_load_seconds'].get(device,definition['prior_load_seconds']['cpu'])
    return rate*frames/44100+(load if include_load else 0)

def cpu_threads():return str(max(1,min(8,(os.cpu_count() or 2)//2)))

class Engine:
    """One set-up runtime and one worker process; models stay loaded for every track separated through it."""
    def __init__(self,preset,job,resources,locked=False):
        self.preset,self.job,self.resources=preset,job,resources
        self.lock=None if locked else engine_lock();self.worker=None;self.state=None
    def __enter__(self):
        if self.lock:self.lock.__enter__()
        try:
            self.state=_setup({'preset':self.preset},self.job,self.resources)
            adapter=Path(self.resources)/'inference'/'inference.py'
            argv=[self.state['python'],'-I',adapter,'--serve','--preset',self.preset,'--model-dir',self.state['models'],
                  '--code-dir',self.state['code'],'--device',self.state['device'],'--vram-bytes',str(self.state['vram_bytes'] or 0)]
            env=os.environ.copy();threads=cpu_threads()
            env.update({'OPENBLAS_NUM_THREADS':threads,'OMP_NUM_THREADS':threads,'MKL_NUM_THREADS':threads,'PYTHONIOENCODING':'utf-8'})
            if self.state['device']=='cpu':env['CUDA_VISIBLE_DEVICES']='-1'
            self.worker=self.job.worker(argv,env=env)
        except BaseException:
            if self.lock:self.lock.__exit__(None,None,None)
            raise
        return self
    def __exit__(self,*error):
        try:
            if self.worker:self.worker.close(kill=error[0] is not None)
        finally:
            if self.lock:self.lock.__exit__(*error)
    def separate(self,source_wav,output_dir,parts):
        """Run one track through the worker; returns result.json contents with the worker's timing and device."""
        def forward(event):
            if event.get('event')=='progress':
                stage=event.get('stage','separate');details={key:event[key] for key in ('pct','overall','device','fallback') if key in event}
                self.job.progress(stage,event.get('message',''),**details)
        final=self.worker.request({'op':'separate','input':str(source_wav),'output_dir':str(output_dir),'parts':list(parts)},forward)
        if final['event']!='done':raise RuntimeError(final.get('error','Separation failed'))
        result=json.loads(Path(final['result']).read_text(encoding='utf-8'))
        for item in result['stems']:
            if file_sha(Path(output_dir)/item['file'])!=item['sha256']:raise ValueError('Generated stem integrity check failed')
        record_timing(self.preset,final['device'],final['timing'],tuple(parts))
        return result

def separate(request,job,resources,engine=None):
    from .service import locate_track
    track=locate_track(request['source'])
    import shutil
    from . import stem_estimate
    needed=stem_estimate.required_free_bytes(track.frames,stem_estimate.SizeModel.load(stem_estimate.model_path(data_root())))
    if shutil.disk_usage(track.usb_root).free<needed:raise ValueError('Not enough USB space for this track. Free space and try again.')
    if engine is not None:return _separate(track,request,job,engine)
    with Engine(request['preset'],job,resources) as owned:return _separate(track,request,job,owned)

def _separate(track,request,job,engine):
    require_scratch(track.frames)
    with tempfile.TemporaryDirectory(prefix='xz-separate-',dir=data_root()) as temporary:
        temporary=Path(temporary)
        from .service import _as_wav
        canonical=_as_wav(track.source,temporary/'source.wav',job)
        output=temporary/'separated'
        job.progress('separate',f'Separating audio on {"the GPU" if engine.state["device"]!="cpu" else "the CPU"}. Your original track is unchanged.',pct=0,overall=0)
        result=engine.separate(canonical,output,('vocals','harmonics'))
        stems={item['name']:item for item in result['stems']}
        from .service import prepare_stems
        parts={name:(output/stems[name]['file'],cache._gain(stems[name]['gain'])) for name in ('vocals','harmonics')}
        receipt=prepare_stems(track,parts,cache.validate_model_id(result['model_id']),engine.state['python'],job,temporary,mix=canonical)
        return {**receipt,'preset':request['preset'],'device':result['identity']['device'],'diagnostics':result.get('diagnostics',{}),'vjtools_required':False}

def separate_drums(source,output_dir,job,resources):
    """Drums and the drum-free remainder of a 44.1 kHz stereo 16-bit WAV, same frame count, encode_part convention.

    The caller already holds engine_lock (the import path); the engine is used without re-acquiring it.
    """
    source=cache._regular(source);output=cache._safe_path(output_dir)
    preset=active_preset()
    with Engine(preset,job,resources,locked=True) as engine:
        result=engine.separate(source,output,('drums','harmonics'))
    stems={item['name']:item for item in result['stems']}
    return {'model_id':result['model_id'],**{name:{'path':str(output/stems[name]['file']),'gain':stems[name]['gain']} for name in ('drums','harmonics')}}

def active_preset():
    base=data_root();catalog=registry()
    markers=sorted((marker for marker in base.glob('setup-*.json') if marker.name[6:-5] in catalog['presets']),key=lambda p:p.stat().st_mtime,reverse=True)
    if not markers or not python_path(base/'engine-python').is_file():raise ValueError('Drum separation needs the separation engine. Set it up first.')
    return markers[0].name[6:-5]
