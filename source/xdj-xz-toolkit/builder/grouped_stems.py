"""Combine user-assigned instrument stems into three groups and seven OverCue roles.

Runs in the managed import environment. Original files are read only.
"""
from pathlib import Path
import argparse
import json
import math
import sys
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly
import importlib.util

def _sibling(name):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(name+'.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
_roles=_sibling('overcue_roles');_align=_sibling('stem_align')
render,MAX_SOURCE_FRAMES=_roles.render,_roles.MAX_SOURCE_FRAMES

RATE=44100
BLOCK=65536
VERDICTS=('mismatch','weak','aligned')

def read_audio(path, target=None):
    info=sf.info(path)
    if info.channels not in (1,2):raise ValueError(Path(path).name+': use mono or stereo audio')
    if not 0<info.frames/info.samplerate<=MAX_SOURCE_FRAMES/RATE:raise ValueError(Path(path).name+': audio is empty or too long')
    factor=math.gcd(info.samplerate,RATE);up,down=RATE//factor,info.samplerate//factor
    frames=-(-info.frames*up//down)
    decoded=Path(str(target)+'.decoded') if target else None
    raw=np.memmap(decoded,dtype=np.float32,mode='w+',shape=(info.frames,info.channels)) if target else np.empty((info.frames,info.channels),dtype=np.float32)
    try:
        # One read call: libsndfile 1.2.2 returns wrong samples when a VBR MP3 is read in several calls.
        with sf.SoundFile(path) as source:count=len(source.read(out=raw))
        if count!=info.frames or not all(np.isfinite(raw[start:start+BLOCK]).all() for start in range(0,count,BLOCK)):
            raise ValueError(Path(path).name+': invalid audio samples')
        audio=np.memmap(target,dtype=np.float32,mode='w+',shape=(frames,2)) if target else np.empty((frames,2),dtype=np.float32)
        block=down*max(1,min(2048,65536//down))
        radius=-(-10*max(up,down)//up);context=-(-radius//down)*down
        for start in range(0,info.frames,block):
            end=min(info.frames,start+block);first,last=max(0,start-context),min(info.frames,end+context)
            samples=raw[first:last]
            converted=resample_poly(samples,up,down,axis=0) if up!=down else samples
            out_first,out_last=start*up//down,-(-end*up//down);trim=(start-first)*up//down
            audio[out_first:out_last]=converted[trim:trim+out_last-out_first]  # mono broadcasts to both channels
    finally:
        if decoded:_close(raw);_unlink(decoded)
    return audio

def _write(path,audio,frames,gain=1.0):
    with sf.SoundFile(path,'w',samplerate=RATE,channels=2,subtype='PCM_16') as writer:
        for start in range(0,frames,BLOCK):writer.write(audio[start:start+BLOCK]*gain)

def _close(audio):audio._mmap.close()

def _unlink(path):
    try:path.unlink(missing_ok=True)
    except PermissionError:pass

def group(request,output):
    """Sum the assigned files into group WAVs aligned to the mix. Writes groups.json and returns it."""
    output=Path(output);output.mkdir(exist_ok=False)
    names=sorted({item['group'] for item in request['files']}|{'drums','vocals','harmonics'})
    if 'instrumental' in names:names=['vocals','instrumental']
    if {item['group'] for item in request['files']}-set(names):
        raise ValueError('An instrumental stem already contains drums and harmonics. Remove it or the separate parts.')
    alignment=request.get('alignment')
    offset,scale=(int(alignment['offset_samples']),np.float32(alignment['gain'])) if alignment else (0,np.float32(1))
    mix=read_audio(request['mix'],output/'.mix.f32');frames=len(mix)
    groups={name:np.memmap(output/(name+'.f32'),dtype=np.float32,mode='w+',shape=(frames,2)) for name in names}
    for part in groups.values():part[:]=0
    for item in request['files']:
        audio=read_audio(item['path'],output/'.input.f32')
        try:
            if alignment:
                for start in range(0,frames,BLOCK):
                    count=min(BLOCK,frames-start);groups[item['group']][start:start+count]+=_align.segment(audio,start+offset,count)*scale
                continue
            if len(audio)!=frames and not request.get('fit_length',False):
                raise ValueError(Path(item['path']).name+': duration does not match the complete track. Choose the length adjustment option if these files start together.')
            count=min(frames,len(audio))
            for start in range(0,count,BLOCK):groups[item['group']][start:min(count,start+BLOCK)]+=audio[start:min(count,start+BLOCK)]
        finally:_close(audio)
    fit=_align.Fit()
    for start in range(0,frames,BLOCK):fit.add(mix[start:start+BLOCK],sum(part[start:start+BLOCK] for part in groups.values()))
    check={**fit.result(),'offset_samples':offset,'applied_gain':float(scale)}
    check['verdict']=_align.verdict(check['correlation'],check['residual_db'])
    required=request.get('require')
    if required and VERDICTS.index(check['verdict'])<VERDICTS.index(required):
        raise ValueError(_align.reason(check))
    gains={}
    for name,audio in groups.items():
        peak=max(float(np.max(np.abs(audio[start:start+BLOCK]))) for start in range(0,frames,BLOCK));gain=min(1.0,0.98/peak) if peak else 1.0
        # read_wav uses gain*32767, whereas libsndfile scales by 32768.
        _write(output/(name+'.wav'),audio,frames,gain)
        gains[name]=gain*32768/32767
    _write(output/'mix.wav',mix,frames)
    _close(mix)
    for audio in groups.values():_close(audio)
    groups.clear()
    for path in (*(output/(name+'.f32') for name in names),output/'.mix.f32',output/'.input.f32'):_unlink(path)
    state={'frames':frames,'gains':gains,'check':check,'needs_drum_split':'instrumental' in names}
    (output/'groups.json').write_text(json.dumps(state),encoding='utf-8')
    return state

def render_groups(output,split=None):
    """Render roles from group WAVs; split gives the drum separation of instrumental.wav."""
    output=Path(output);state=json.loads((output/'groups.json').read_text(encoding='utf-8'));gains=state['gains']
    if state['needs_drum_split']:
        if not split:raise ValueError('The instrumental stem needs drum separation before rendering')
        # Separated PCM / gain is at the scale the engine read instrumental.wav: PCM/32768.
        parts={name:(split[name]['path'],float(split[name]['gain'])*gains['instrumental']*32767/32768) for name in ('drums','harmonics')}
    else:parts={name:(output/(name+'.wav'),gains[name]) for name in ('drums','harmonics')}
    report=render(output/'mix.wav',output/'vocals.wav',gains['vocals'],parts['harmonics'][0],parts['harmonics'][1],output/'roles',parts['drums'][0],parts['drums'][1])
    return {**report,'check':state['check']}

def combine(request,output):
    state=group(request,output)
    return state if state['needs_drum_split'] else render_groups(output)

def plan(request,output):
    """Alignment of the summed stems against the mix, with each file's length at 44.1 kHz."""
    output=Path(output);output.mkdir(exist_ok=False)
    mix=read_audio(request['mix'],output/'.mix.f32');files=[];total=None
    try:
        for number,path in enumerate(request['files']):
            audio=read_audio(path,output/'.input.f32')
            files.append({'path':path,'frames':len(audio),'seconds':len(audio)/RATE})
            if total is None or len(total)<len(audio):
                grown=np.memmap(output/f'.sum-{number}.f32',dtype=np.float32,mode='w+',shape=(len(audio),2));grown[:]=0
                if total is not None:
                    grown[:len(total)]=total;_close(total);_unlink(Path(total.filename))
                total=grown
            for start in range(0,len(audio),BLOCK):total[start:start+BLOCK]+=audio[start:start+BLOCK]
            _close(audio)
        result={'mix_frames':len(mix),'files':files,'alignment':_align.align(mix,total)}
    finally:
        _close(mix)
        if total is not None:_close(total)
    for path in output.glob('.*.f32'):_unlink(path)
    (output/'result.json').write_text(json.dumps(result),encoding='utf-8')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--request',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--mode',choices=('combine','group','render','plan'),default='combine');a=p.parse_args()
    request=json.loads(a.request.read_text(encoding='utf-8'))
    try:
        if a.mode=='plan':plan(request,a.output)
        elif a.mode=='group':group(request,a.output)
        elif a.mode=='render':render_groups(a.output,request.get('split'))
        else:combine(request,a.output)
    except ValueError as exc:
        print(str(exc),file=sys.stderr);raise SystemExit(1)
