"""Combine user-assigned instrument stems into three groups and seven OverCue roles.

Runs in the managed import environment. Original files are read only.
"""
from pathlib import Path
import argparse
import json
import math
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly
import importlib.util
_spec=importlib.util.spec_from_file_location("overcue_roles",Path(__file__).with_name("overcue_roles.py"))
_roles=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(_roles)
render,MAX_SOURCE_FRAMES=_roles.render,_roles.MAX_SOURCE_FRAMES

RATE=44100

def read_audio(path, target=None):
    info=sf.info(path)
    if info.channels not in (1,2):raise ValueError(Path(path).name+': use mono or stereo audio')
    if not 0<info.frames/info.samplerate<=MAX_SOURCE_FRAMES/RATE:raise ValueError(Path(path).name+': audio is empty or too long')
    factor=math.gcd(info.samplerate,RATE);up,down=RATE//factor,info.samplerate//factor
    frames=-(-info.frames*up//down)
    audio=np.memmap(target,dtype=np.float32,mode='w+',shape=(frames,2)) if target else np.empty((frames,2),dtype=np.float32)
    block=down*max(1,min(2048,65536//down))
    radius=-(-10*max(up,down)//up);context=-(-radius//down)*down
    with sf.SoundFile(path) as source:
        for start in range(0,info.frames,block):
            end=min(info.frames,start+block);first,last=max(0,start-context),min(info.frames,end+context)
            source.seek(first);samples=source.read(last-first,dtype='float32',always_2d=True)
            if len(samples)!=last-first or not np.isfinite(samples).all():raise ValueError(Path(path).name+': invalid audio samples')
            converted=resample_poly(samples,up,down,axis=0) if up!=down else samples
            out_first,out_last=start*up//down,-(-end*up//down);trim=(start-first)*up//down
            audio[out_first:out_last]=converted[trim:trim+out_last-out_first]  # mono broadcasts to both channels
    return audio

def combine(request,output):
    output=Path(output);output.mkdir(exist_ok=False)
    mix=read_audio(request['mix'],output/'.mix.f32');frames=len(mix)
    groups={name:np.memmap(output/(name+'.f32'),dtype=np.float32,mode='w+',shape=(frames,2)) for name in ('drums','vocals','harmonics')}
    for part in groups.values():part[:]=0
    for item in request['files']:
        audio=read_audio(item['path'],output/'.input.f32')
        try:
            if len(audio)!=frames and not request.get('fit_length',False):
                raise ValueError(Path(item['path']).name+': duration does not match the complete track. Choose the length adjustment option if these files start together.')
            count=min(frames,len(audio))
            for start in range(0,count,65536):groups[item['group']][start:min(count,start+65536)]+=audio[start:min(count,start+65536)]
        finally:audio._mmap.close()
    gains={}
    for name,audio in groups.items():
        peak=max(float(np.max(np.abs(audio[start:start+65536]))) for start in range(0,frames,65536));gain=min(1.0,0.98/peak) if peak else 1.0
        # read_wav uses gain*32767, whereas libsndfile scales by 32768.
        with sf.SoundFile(output/(name+'.wav'),'w',samplerate=RATE,channels=2,subtype='PCM_16') as writer:
            for start in range(0,frames,65536):writer.write(audio[start:start+65536]*gain)
        gains[name]=gain*32768/32767
    with sf.SoundFile(output/'mix.wav','w',samplerate=RATE,channels=2,subtype='PCM_16') as writer:
        for start in range(0,frames,65536):writer.write(mix[start:start+65536])
    mix._mmap.close()
    report=render(output/'mix.wav',output/'vocals.wav',gains['vocals'],output/'harmonics.wav',gains['harmonics'],output/'roles',output/'drums.wav',gains['drums'])
    for audio in groups.values():audio._mmap.close()
    groups.clear()
    for name in ('drums','vocals','harmonics'):
        try:(output/(name+'.f32')).unlink()
        except PermissionError:pass
    for path in (output/'.mix.f32',output/'.input.f32'):path.unlink()
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--request',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    combine(json.loads(a.request.read_text(encoding='utf-8')),a.output)
