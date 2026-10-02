# SPDX-License-Identifier: MIT
"""Managed, independent CPU separation setup with verified model downloads."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
from .host import executable, torch_install
import shutil
import tempfile
import urllib.request
from . import cache
from .jobs import exclusive_lock

def registry():return json.loads(Path(__file__).with_name('models.json').read_text())

def data_root():
    configured=os.environ.get('XZ_BUILDER_DATA')
    base=Path(configured) if configured else Path(os.environ.get('LOCALAPPDATA',Path.home()/'.local/share'))/'XZ Mods'
    base=cache._safe_path(base);base.mkdir(parents=True,exist_ok=True);return base

def file_sha(path):
    with Path(path).open('rb') as file:return hashlib.file_digest(file,'sha256').hexdigest()

def download(record,folder,job):
    name=record['filename']
    if Path(name).name!=name or not record['url'].startswith('https://'):raise ValueError('Invalid packaged artifact manifest')
    folder=cache._safe_path(folder);folder.mkdir(parents=True,exist_ok=True)
    target=cache._safe_path(folder/name)
    if target.is_file() and target.stat().st_size==record['bytes'] and file_sha(target)==record['sha256']:return target
    with tempfile.NamedTemporaryFile(prefix='.download-',dir=folder,delete=False) as file:temporary=Path(file.name)
    try:
        request=urllib.request.Request(record['url'],headers={'User-Agent':'XZMods/0.1'})
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
                    job.progress('download',f'{name}: {received*100//record["bytes"]}%');last_report=received
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

def engine_python():
    base=data_root();python=base/'engine-python'/('Scripts/python.exe' if os.name=='nt' else 'bin/python')
    if not python.is_file() or not any(base.glob('setup-*.json')):
        raise ValueError('Set up the separation engine first. Imported stems are resampled to 96 kHz with it.')
    return python

def setup(request,job,resources):
    with engine_lock():return _setup(request,job,resources)

def _setup(request,job,resources):
    catalog=registry();preset=request['preset']
    torch_install(preset)
    if preset not in catalog['presets']:raise ValueError('Unknown separation preset')
    base=data_root();environment=base/'engine-python';python=environment/('Scripts/python.exe' if os.name=='nt' else 'bin/python')
    uv=executable(resources,'uv')
    if not uv.is_file():raise ValueError('The packaged engine installer is missing')
    marker=base/('setup-'+preset+'.json')
    identity=hashlib.sha256(json.dumps(catalog,sort_keys=True).encode()).hexdigest()
    ready=marker.is_file() and json.loads(marker.read_text()).get('registry_sha256')==identity and python.is_file()
    if not ready:
        job.progress('runtime','Setting up a separate Python runtime for XZ Mods')
        if not python.is_file():job.run([uv,'venv','--python','3.11',environment])
        job.progress('runtime','Installing the CPU separation runtime; this does not use VJ.Tools')
        job.run([uv,'pip','install','--python',python,*torch_install(preset)])
        packages=['openunmix==1.3.0','numpy==2.2.6','scipy==1.16.1']
        if preset=='vocal-focus':packages.extend(name+'=='+version for name,version in catalog['vocal_focus_dependencies'].items())
        job.run([uv,'pip','install','--python',python,*packages])
    models=base/'models'
    for name in catalog['presets'][preset]['models']:
        job.progress('models','Checking '+name);download(catalog['models'][name],models,job)
    code=base/'smule-code'
    if preset=='vocal-focus':
        for record in catalog['smule_code']['files'].values():download(record,code,job)
    notices=base/'licenses';notices.mkdir(exist_ok=True)
    for name,license in catalog['licenses'].items():(notices/(name+'.txt')).write_text(license['text'])
    lock=job.run([uv,'pip','freeze','--python',python])
    (base/'installed-runtime.txt').write_text(lock)
    result={'preset':preset,'registry_sha256':identity,'python':str(python),'models':str(models),'smule_code':str(code),
        'code_and_weights_license':'MIT','runtime_validation_verified':False,'installed':True}
    marker.write_text(json.dumps(result,indent=2)+'\n')
    return result

def separate(request,job,resources):
    from .service import locate_track
    track=locate_track(request['source'])
    import shutil
    needed=int(track.frames/44100*96000*4*7*1.04)+16*1024*1024
    if shutil.disk_usage(track.usb_root).free<needed:raise ValueError('Not enough USB space for this track. Free space and try again.')
    with engine_lock():return _separate(track,request,job,resources)

def _separate(track,request,job,resources):
    require_scratch(track.frames)
    state=_setup(request,job,resources)
    with tempfile.TemporaryDirectory(prefix='xz-separate-',dir=data_root()) as temporary:
        temporary=Path(temporary);canonical=track.source
        if canonical.suffix.lower()=='.flac':
            canonical=temporary/'source.wav';job.run([executable(resources,'xz-audio-helper'),'convert',track.source,canonical])
        output=temporary/'separated'
        adapter=Path(resources)/'inference'/'inference.py'
        argv=[state['python'],'-I',adapter,'--preset',request['preset'],'--input',canonical,
              '--model-dir',state['models'],'--output-dir',output,'--device','cpu']
        if request['preset']=='vocal-focus':argv+=['--smule-code',state['smule_code']]
        env=os.environ.copy();env.update({'OPENBLAS_NUM_THREADS':'1','OMP_NUM_THREADS':'1','MKL_NUM_THREADS':'1','CUDA_VISIBLE_DEVICES':'-1'})
        job.progress('separate','Separating audio on CPU. Your original track is unchanged.')
        job.run(argv,env=env)
        result=json.loads((output/'result.json').read_text())
        stems={item['name']:item for item in result['stems']}
        for item in stems.values():
            if file_sha(output/item['file'])!=item['sha256']:raise ValueError('Generated stem integrity check failed')
        from .service import prepare_stems
        parts={name:(output/stems[name]['file'],cache._gain(stems[name]['gain'])) for name in ('vocals','harmonics')}
        receipt=prepare_stems(track,parts,cache.validate_model_id(result['model_id']),state['python'],job,temporary,mix=canonical)
        return {**receipt,'preset':request['preset'],'vjtools_required':False}
