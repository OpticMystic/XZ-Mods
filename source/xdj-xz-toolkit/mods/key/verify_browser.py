"""Create independent DeviceSQL fixtures and exercise the production browser and native text wrapper."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parent
def string(text):
    if text.isascii():
        raw=text.encode('ascii')
        return bytes([(len(raw)+1)*2+1])+raw if len(raw)<127 else b'\x40'+struct.pack('<H',len(raw)+4)+b'\0'+raw
    raw=text.encode('utf-16le');return b'\x90'+struct.pack('<H',len(raw)+4)+b'\0'+raw
def page(index,kind,rows,deleted=()):
    out=bytearray(4096);struct.pack_into('<4I',out,0,0,index,kind,0)
    struct.pack_into('<I',out,24,len(rows)|((len(rows)-len(deleted))<<13)|(0x24<<24))
    position=40;mask=0
    for i,row in enumerate(rows):
        struct.pack_into('<H',out,4096-6-i*2,position-40);out[position:position+len(row)]=row;position+=len(row)
        if i not in deleted:mask|=1<<i
    struct.pack_into('<H',out,4096-4,mask);return out
def track(number,key,title):
    out=bytearray(0x88);struct.pack_into('<H',out,0,0x24);struct.pack_into('<I',out,0x20,key)
    struct.pack_into('<I',out,0x4c,number);struct.pack_into('<H',out,0x5e+17*2,0x88)
    return out+string(title)
def fixture():
    header=bytearray(4096);struct.pack_into('<3I',header,0,0,4096,2)
    struct.pack_into('<4I',header,28,5,0,1,1);struct.pack_into('<4I',header,44,0,0,2,2)
    keys=[struct.pack('<II',id,id)+string(name) for id,name in [(10,'C'),(20,'D'),(30,'Am')]]
    tracks=[track(1,10,'Night Drive'),track(2,20,'Other'),track(3,10,'Duplicate'),track(4,20,'Duplicate'),
        track(5,999,'Unknown'),track(6,30,'日本🎵'),track(7,10,'Deleted')]
    return header+page(1,5,keys)+page(2,0,tracks,deleted=(6,))
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True);good=args.output/'valid.pdb';bad=args.output/'invalid.pdb'
    good.write_bytes(fixture());bad.write_bytes(fixture()[:8000])
    for name in ('browser','browser_skin'):
        binary=args.output/name
        sources=[ROOT/('test_'+name+'.c'),ROOT/'catalog.c',ROOT/'music.c',ROOT.parent/'audio/vendor/sha256/sha256.c']
        if name=='browser_skin':sources+=[ROOT.parent/'ui/ui_draw.c']
        subprocess.run(['gcc','-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-result',
            '-fsanitize=address,undefined','-fno-sanitize-recover=all',*[str(p) for p in sources],'-pthread','-ldl','-lm','-o',str(binary)],check=True)
        subprocess.run([str(binary),str(good.resolve()),*([str(bad.resolve())] if name=='browser' else [])],check=True)
