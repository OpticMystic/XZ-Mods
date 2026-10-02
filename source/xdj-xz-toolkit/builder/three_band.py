# SPDX-License-Identifier: MIT
"""CDJ-3000 style 3-band colours for the XZ's RGB waveforms.

Rewrites only the colour of rekordbox's PWV4 preview and PWV5 detail waveforms (ANLZ0000.EXT)
from the 3-band analysis in PWV6/PWV7 (ANLZ0000.2EX). Heights and every other section stay as
rekordbox wrote them, so the stock RGB renderer draws the same shapes in 3-band colours.
"""
from __future__ import annotations
from functools import lru_cache
import os
from pathlib import Path
import struct

# Measured against the audio (FFT band energy per entry), agreeing with rekordbox's own PWV5 red/green/blue.
# PWV7's header is 24 bytes; reading it as 20 shifts every entry and fakes a (high, low, mid) order.
# Re-prove with builder/tests/verify_three_band_order.py.
BAND_ORDER=('low','mid','high')
LOW,MID,HIGH=(BAND_ORDER.index(band) for band in ('low','mid','high'))

# Beat Link's CDJ-3000 layer colours, and the drawn height per unit of band energy. Rekordbox's low and
# mid bytes run at similar levels, so mid at half keeps kicks and the outer body blue; high medians are
# small but its peaks are not, so a quarter keeps white to hats and snares. Judged with simulate_three_band.py.
LAYERS={'low':((32,83,217),1.0),'mid':((242,170,60),0.5),'high':((255,255,255),0.25)}
OVERLAP=(169,107,39)
XZ_FULL_LEVEL=230

ENTRY_SIZES={b'PWV4':6,b'PWV5':2,b'PWV6':3,b'PWV7':3}
PWV5_COLOUR=0xff80
ORIGINALS=Path('CDJMODS','waveform-rgb-originals')

def waves(data):
    """Entry offset and count of each waveform section, after strict PMAI length checks."""
    if len(data)<12 or data[:4]!=b'PMAI':raise ValueError('Not a rekordbox analysis file')
    header,length=struct.unpack_from('>II',data,4)
    if length!=len(data) or not 12<=header<=length:raise ValueError('Analysis file length is inconsistent')
    found={};offset=header
    while offset<length:
        if offset+12>length:raise ValueError('Analysis section header is truncated')
        tag=bytes(data[offset:offset+4]);head,total=struct.unpack_from('>II',data,offset+4)
        if not 12<=head<=total or offset+total>length:raise ValueError(f'{tag!r} section length is inconsistent')
        if tag in ENTRY_SIZES:
            if tag in found or head<20:raise ValueError(f'Unexpected {tag.decode()} section')
            size,count=struct.unpack_from('>II',data,offset+12)
            if size!=ENTRY_SIZES[tag] or head+size*count!=total:
                raise ValueError(f'Unexpected {tag.decode()} entry size {size}')
            if not count:raise ValueError(f'{tag.decode()} waveform is empty')
            found[tag]=(offset+head,count)
        offset+=total
    return found

@lru_cache(maxsize=65536)
def mix(low,mid,high):
    """One colour for a layered CDJ-3000 column (outer low or mid, brown overlap, white high core).

    The larger coloured layer wins, whitened by the core's share of the height. Blue and amber are
    near complements, so an area average of them turns grey or violet, which the CDJ-3000 never shows.
    """
    l,m,h=(value*LAYERS[band][1] for value,band in ((low,'low'),(mid,'mid'),(high,'high')))
    outer=max(l,m);inner=min(l,m);total=max(outer,h)
    if not total:return (0.0,0.0,0.0)
    overlap=max(0,inner-h);band=max(0,outer-max(h,inner))
    base=OVERLAP if overlap>band else LAYERS['low' if l>m else 'mid'][0];white=h/total
    return tuple(value*(1-white)+channel*white for value,channel in zip(base,LAYERS['high'][0]))

@lru_cache(maxsize=65536)
def band_color(low,mid,high):
    """3-bit channels as the XZ draws them, where level 7 is displayed as 230."""
    return tuple(min(7,round(value*7/XZ_FULL_LEVEL)) for value in mix(low,mid,high))

def _rgb(ext):
    found=waves(ext)
    for tag in (b'PWV4',b'PWV5'):
        if tag not in found:raise ValueError(f'No {tag.decode()} RGB waveform')
    return found[b'PWV4'],found[b'PWV5']

def _bands(data,start,count,index,target):
    at=start+(index*count//target)*3
    return data[at+LOW],data[at+MID],data[at+HIGH]

def adapt_ext(ext,two_ex):
    """Size-preserving and idempotent: only PWV5 bits 15-7 and PWV4 bytes 3-5 change.

    PWV4 keeps each entry's peak colour byte, which rekordbox players also read as bar height.
    A column the 3-band analysis calls silent keeps its colour.
    """
    (preview_at,preview_count),(detail_at,detail_count)=_rgb(ext);bands=waves(two_ex)
    if b'PWV6' not in bands:raise ValueError('No PWV6 3-band waveform')
    out=bytearray(ext)
    preview=bands[b'PWV6'];detail=bands.get(b'PWV7',preview)
    for index in range(detail_count):
        at=detail_at+index*2
        r,g,b=band_color(*_bands(two_ex,*detail,index,detail_count))
        value=int.from_bytes(out[at:at+2],'big')&~PWV5_COLOUR&0xffff
        out[at:at+2]=(value|r<<13|g<<10|b<<7).to_bytes(2,'big')
    for index in range(preview_count):
        at=preview_at+index*6+3
        peak=max(out[at:at+3]);colour=mix(*_bands(two_ex,*preview,index,preview_count));top=max(colour)
        if top:out[at:at+3]=bytes(min(peak,round(value*peak/top)) for value in colour)
    return bytes(out)

def _geometry(ext):
    """Everything adapt_ext keeps from a waveform: the two files show the same shapes if equal."""
    (preview_at,preview_count),(detail_at,detail_count)=_rgb(ext);shape=bytearray()
    for at in range(preview_at,preview_at+preview_count*6,6):shape+=ext[at:at+3]+bytes([max(ext[at+3:at+6])])
    for at in range(detail_at,detail_at+detail_count*2,2):shape.append(ext[at+1]&0x7f)
    return bytes(shape)

def _graft(current,original):
    """The current file with the waveform colours of an original that has the same geometry."""
    out=bytearray(current)
    ((preview_at,preview_count),(detail_at,detail_count)),((old_preview,_),(old_detail,_))=_rgb(current),_rgb(original)
    for index in range(detail_count):
        at=detail_at+index*2;source=old_detail+index*2
        out[at]=original[source];out[at+1]=out[at+1]&0x7f|original[source+1]&0x80
    for index in range(preview_count):
        out[preview_at+index*6+3:preview_at+index*6+6]=original[old_preview+index*6+3:old_preview+index*6+6]
    return bytes(out)

def _write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    temporary=path.with_name(path.name+'.xzmods-new')
    with temporary.open('wb') as file:
        file.write(data);file.flush();os.fsync(file.fileno())
    os.replace(temporary,path)

def _usb_root(root):
    root=Path(root).absolute()
    if not (root/'PIONEER'/'USBANLZ').is_dir():raise ValueError('Choose the root of a USB exported by rekordbox. It has a PIONEER folder.')
    return root

def _stale(backup,current):
    """A missing or unreadable original, or one rekordbox has since replaced by re-exporting."""
    try:return _geometry(backup.read_bytes())!=_geometry(current)
    except (FileNotFoundError,ValueError):return True

def apply(root,job=None):
    """Adapt every analysed track; the first rewrite of a rekordbox file keeps its original."""
    root=_usb_root(root)
    report={'adapted':0,'already_current':0,'skipped_no_2ex':0,'failed':[]}
    tracks=sorted((root/'PIONEER'/'USBANLZ').glob('*/*/ANLZ0000.EXT'))
    for number,ext in enumerate(tracks):
        if job:job.check()
        if job and not number%50:job.progress('waveforms',f'Updating waveform colours ({number} of {len(tracks)} tracks)')
        two_ex=ext.with_suffix('.2EX')
        if not two_ex.is_file():report['skipped_no_2ex']+=1;continue
        try:
            current=ext.read_bytes();adapted=adapt_ext(current,two_ex.read_bytes())
            if adapted==current:report['already_current']+=1;continue
            backup=root/ORIGINALS/ext.relative_to(root)
            if _stale(backup,current):_write(backup,current)
            _write(ext,adapted);report['adapted']+=1
        except (OSError,ValueError) as exc:report['failed'].append((ext.relative_to(root).as_posix(),str(exc)))
    return report

def restore(root,job=None):
    """Put rekordbox's colours back, keeping any cue or grid changes made since."""
    root=_usb_root(root);originals=root/ORIGINALS
    report={'restored':0,'stale_originals_removed':0,'failed':[]}
    backups=sorted(originals.glob('PIONEER/USBANLZ/*/*/ANLZ0000.EXT'))
    for number,backup in enumerate(backups):
        if job:job.check()
        if job and not number%50:job.progress('waveforms',f'Restoring rekordbox colours ({number} of {len(backups)} tracks)')
        ext=root/backup.relative_to(originals)
        try:
            saved=backup.read_bytes()
            current=ext.read_bytes() if ext.is_file() else None
            if current is not None and _geometry(saved)==_geometry(current):
                _write(ext,_graft(current,saved));report['restored']+=1
            else:report['stale_originals_removed']+=1
            backup.unlink()
        except (OSError,ValueError) as exc:report['failed'].append((ext.relative_to(root).as_posix(),str(exc)))
    for folder in sorted((path for path in originals.rglob('*') if path.is_dir()),key=lambda path:len(path.parts),reverse=True)+[originals,originals.parent]:
        if folder.is_dir() and not any(folder.iterdir()):folder.rmdir()
    return report
