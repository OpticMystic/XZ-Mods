#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fast offline regression for the production XZ media decoder. Linux/WSL."""
from pathlib import Path
import argparse, hashlib, json, math, shlex, shutil, struct, subprocess, tempfile, time, wave
AUDIO_FILES=('stem_media.c','stem_media.h','stem_mp3.c','aiff_reader.h','vendor/dr_libs/dr_mp3.h','vendor/dr_libs/dr_wav.h','vendor/dr_libs/dr_flac.h','vendor/stb_vorbis.c')
TEST_FILES=('test_media_seek_regression.c','test_media_seek_failure.c','test_media_allocation.c')
IMPL='#define DR_FLAC_IMPLEMENTATION\n#define DR_FLAC_NO_OGG\n#include "audio/vendor/dr_libs/dr_flac.h"\n#define DR_WAV_IMPLEMENTATION\n#include "audio/vendor/dr_libs/dr_wav.h"\n'
FAULT=r'''#include "../stem_media.h"
#include <math.h>
#include <stdio.h>
int main(int argc,char **argv){if(argc!=2)return 2;unsigned errors=0,opened=0;for(int k=0;k<8;k++){struct xz_stem_media*m=xz_stem_media_open(argv[1],0);if(!m)continue;opened++;float pcm[8192];unsigned long long total=xz_stem_media_frames(m);for(unsigned long long p=0;p<total;){unsigned n=total-p>4096?4096:(unsigned)(total-p);int result=xz_stem_media_read(m,(long long)p,n,pcm);for(unsigned i=0;i<n*2;i++)if(!isfinite(pcm[i])){xz_stem_media_close(m);return 3;}if(result){errors++;break;}p+=n;}xz_stem_media_close(m);}printf("{\"opened\":%u,\"reported_read_errors\":%u,\"finite\":true}\n",opened,errors);return 0;}
'''
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def command(argv,out,name,expected=(0,)):
    start=time.monotonic();p=subprocess.run([str(x) for x in argv],capture_output=True,text=True,timeout=180)
    record={'command':[str(x) for x in argv],'exit_code':p.returncode,'wall_seconds':time.monotonic()-start}
    if p.stdout.strip().startswith('{'):record['result']=json.loads(p.stdout)
    (out/(name+'.stdout')).write_text(p.stdout);(out/(name+'.stderr')).write_text(p.stderr);(out/(name+'.json')).write_text(json.dumps(record,indent=2)+'\n')
    if p.returncode not in expected:raise RuntimeError(f'{name} exited {p.returncode}; see {out/(name+".stderr")}')
    return record

def fixtures(out,ffmpeg):
    root=out/'fixtures';root.mkdir(exist_ok=True);wav=root/'transient.wav';rate=44100
    with wave.open(str(wav),'wb') as f:
        f.setnchannels(2);f.setsampwidth(2);f.setframerate(rate);block=bytearray()
        for n in range(rate*12):
            t=n/rate;left=.22*math.sin(2*math.pi*(173*t+17*math.sin(.7*t)));right=.19*math.sin(2*math.pi*(281*t+23*math.sin(.43*t)))
            if n%22050<3:left+=.58
            if n%32193<5:right-=.59
            block.extend(struct.pack('<hh',int(left*32767),int(right*32767)))
        f.writeframes(block)
    for name,options in [('cbr.mp3',['-b:a','192k']),('vbr.mp3',['-q:a','2'])]:command([*ffmpeg,'-y','-v','error','-i',wav,'-c:a','libmp3lame',*options,root/name],out,'encode-'+name)
    base=(root/'cbr.mp3').read_bytes();item=struct.pack('<II',64,0)+b'test\0'+b'v'*64
    def tag(version,header,payload,count):
        size=len(payload)+32
        def part(flags):return b'APETAGEX'+struct.pack('<IIII',version,size,count,flags)+b'\0'*8
        return (part(0xa0000000) if header else b'')+payload+part(0x80000000 if header else 0)
    for name,data in [('ape-v1.mp3',tag(1000,False,item,1)),('ape-v2.mp3',tag(2000,True,item*800,800)),('ape-v2-noheader.mp3',tag(2000,False,item*800,800))]:(root/name).write_bytes(base+data)
    malformed=bytearray(tag(2000,True,item,1));struct.pack_into('<I',malformed,len(malformed)-20,0xffffffff);(root/'malformed-tail.mp3').write_bytes(base+malformed)
    return root

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,default=Path(__file__).resolve().parent.parent,help='Readonly mods or mods/audio directory; defaults to this tests directory parent')
    p.add_argument('--out',type=Path,help='Persistent result directory; defaults to a temporary directory');p.add_argument('--cc',default='cc',help='Compiler command, e.g. gcc or "zig cc"');p.add_argument('--ffmpeg',default='ffmpeg');p.add_argument('--sanitize',action='store_true');p.add_argument('--expect-failure',choices=('ape-tail','seek-cursor'),help='Optional original-source negative control; run without --sanitize')
    a=p.parse_args();start=time.monotonic()
    if a.sanitize and a.expect_failure:p.error('Use negative controls without --sanitize; the old failing test exits before normal cleanup.')
    source=a.source.resolve();source=source/'audio' if (source/'audio/stem_media.c').exists() else source
    temporary=tempfile.TemporaryDirectory(prefix='xz-media-cache-') if not a.out else None;out=Path(temporary.name) if temporary else a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    report={'hardware_tested':False,'sanitize':a.sanitize,'source':str(source),'source_sha256':{},'results':{}};stage=out/'build';audio=stage/'audio';tests=audio/'tests';tests.mkdir(parents=True,exist_ok=True)
    for rel in AUDIO_FILES:
        data=(source/rel).read_bytes();dest=audio/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data);report['source_sha256'][rel]=hashlib.sha256(data).hexdigest()
    for name in TEST_FILES:shutil.copyfile(Path(__file__).resolve().parent/name,tests/name)
    (stage/'decoder_impl.c').write_text(IMPL);(tests/'test_media_malformed.c').write_text(FAULT)
    cc=shlex.split(a.cc);flags=['-std=c11','-UNDEBUG','-ffp-contract=off','-O1' if a.sanitize else '-O2']
    if a.sanitize:flags+=['-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-sanitize-recover=all']
    objects=[]
    for name,path in [('media',audio/'stem_media.c'),('mp3',audio/'stem_mp3.c'),('impl',stage/'decoder_impl.c')]:
        obj=stage/(name+'.o');command([*cc,*flags,'-c',path,'-o',obj],out,'compile-'+name);objects.append(obj)
    def build(name,source,extra=()):
        exe=stage/name;command([*cc,*flags,source,*objects,*extra,'-lm','-o',exe],out,'link-'+name);return exe
    seek=build('seek',tests/'test_media_seek_regression.c');failure=build('seek-failure',tests/'test_media_seek_failure.c',['-Wl,--wrap=drmp3_seek_to_pcm_frame']);root=fixtures(out,shlex.split(a.ffmpeg))
    report['fixture_sha256']={x.name:sha(x) for x in root.iterdir() if x.suffix=='.mp3'}
    if a.expect_failure:
        if a.expect_failure=='ape-tail':report['results']['negative-ape-tail']=command([seek,root/'ape-v2.mp3','0'],out,'negative-ape-tail',(5,6,12))
        else:report['results']['negative-seek-cursor']=command([failure,root/'cbr.mp3'],out,'negative-seek-cursor',(7,))
    else:
        for name in ('cbr.mp3','vbr.mp3','ape-v1.mp3','ape-v2.mp3','ape-v2-noheader.mp3'):
            for raw in (0,1):
                key=name+'-raw'+str(raw);report['results'][key]=command([seek,root/name,str(raw)],out,key)
        report['results']['seek-failure']=command([failure,root/'cbr.mp3'],out,'seek-failure')
        oom=build('oom',tests/'test_media_seek_regression.c',['-DXZ_TEST_ALLOC_FAILURES',tests/'test_media_allocation.c','-Wl,--wrap=calloc','-Wl,--wrap=malloc']);report['results']['oom']=command([oom,root/'cbr.mp3','0'],out,'oom')
        fault=build('malformed',tests/'test_media_malformed.c');report['results']['malformed-tail']=command([fault,root/'malformed-tail.mp3'],out,'malformed-tail')
    report['wall_seconds']=time.monotonic()-start;(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'passed':True,'wall_seconds':report['wall_seconds'],'report':str(out/'report.json'),'temporary_results':temporary is not None}))
    if temporary:temporary.cleanup()
if __name__=='__main__':main()
