"""Prepare native PWV7 sidecars without recolouring or replacing rekordbox files."""
from pathlib import Path
import struct
from .three_band import waves,_write,_usb_root

MAGIC=b'XZ3BAND1'
FOLDER=Path('CDJMODS','waveform-3band')
def path_hash(path):
    h=14695981039346656037
    for c in path.encode('utf8'):
        if 65<=c<=90:c+=32
        h=((h^c)*1099511628211)&0xffffffffffffffff
    return f'{h:016x}'

def track_path(ext):
    waves(ext) # Validate all section boundaries before reading PPTH.
    pos=struct.unpack_from('>I',ext,4)[0]
    while pos<len(ext):
        head,total=struct.unpack_from('>II',ext,pos+4)
        if ext[pos:pos+4]==b'PPTH':
            if head<16:raise ValueError('Invalid PPTH header')
            length=struct.unpack_from('>I',ext,pos+12)[0]
            if length%2 or length>total-head:raise ValueError('Invalid PPTH path length')
            path=ext[pos+head:pos+head+length].decode('utf-16-be').rstrip('\0')
            if not path.startswith('/Contents/') or '\0' in path or '\\' in path or any(p in ('.','..') for p in path.split('/')):
                raise ValueError('Expected a rekordbox /Contents/ track path')
            if len(path.encode('utf8'))>1023:raise ValueError('Track path is too long')
            return path
        pos+=total
    raise ValueError('No PPTH track path')

def encode(ext,two_ex):
    path=track_path(ext);parsed=waves(two_ex)
    if b'PWV7' not in parsed:raise ValueError('No PWV7 detail analysis')
    at,count=parsed[b'PWV7']
    if count>1000000:raise ValueError('Waveform exceeds native sample budget')
    bands=two_ex[at:at+count*3]
    histogram=[0]*1021
    for i in range(count):histogram[max(bands[3*i]*4,bands[3*i+1]*2,bands[3*i+2])]+=1
    target=(count*995+999)//1000;seen=0;normalization=1
    for energy,n in enumerate(histogram):
        seen+=n
        if seen>=target:normalization=max(1,energy);break
    encoded=path.encode('utf8')
    return path,MAGIC+struct.pack('>III',count,normalization,len(encoded))+encoded+bands

def prepare(root,job=None):
    root=_usb_root(root);report={'prepared':0,'already_current':0,'skipped_no_2ex':0,'failed':[]}
    tracks=sorted((root/'PIONEER/USBANLZ').glob('*/*/ANLZ0000.EXT'))
    for i,ext in enumerate(tracks):
        if job:job.progress('layered-waveforms',f'Preparing layered waveform {i+1} of {len(tracks)}')
        analysis=ext.with_suffix('.2EX')
        if not analysis.is_file():report['skipped_no_2ex']+=1;continue
        try:
            path,data=encode(ext.read_bytes(),analysis.read_bytes())
            target=root/FOLDER/(path_hash(path)+'.xzw')
            if not target.resolve().is_relative_to(root.resolve()):raise ValueError('Waveform destination escapes USB')
            if target.exists() and target.read_bytes()==data:report['already_current']+=1;continue
            _write(target,data);report['prepared']+=1
        except (OSError,ValueError,UnicodeError) as exc:report['failed'].append((ext.relative_to(root).as_posix(),str(exc)))
    return report
