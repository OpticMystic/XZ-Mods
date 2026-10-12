# SPDX-License-Identifier: MIT
"""Verified free game downloads and non-overwriting USB installation."""
from __future__ import annotations
import hashlib
import io
import json
from pathlib import Path
import tempfile
import zipfile
from . import cache,usb
from .engines import data_root,download

CATALOG={
 'doom':{'label':'Doom shareware','directory':'Doom','wad':'doom1.wad','sha256':'1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771',
         'archive':{'filename':'doom19s.zip','url':'https://youfailit.net/pub/idgames/idstuff/doom/doom19s.zip','bytes':2450688,'sha256':'cacf0142b31ca1af00796b4a0339e07992ac5f21bc3f81e7532fe1b5e1b486e6'}},
 'chex':{'label':'Chex Quest','directory':'ChexQuest','wad':'chex.wad','sha256':'d8eb5277918883f490fb1a4be3c9a8588df2dbaee6dc4beb8df4929148bbffb1',
         'archive':{'filename':'chex.zip','url':'https://www.chexquest3.com/downloads/chex.zip','bytes':3773924,'sha256':'df8e391b0f802f82ac9b71ec4faea4b1cd858097d04eb70aff2d1a39150e0419'},
         'patch':{'filename':'chexdeh.zip','url':'https://youfailit.net/pub/idgames/themes/chex/chexdeh.zip','bytes':9250,'sha256':'eeed61747165a4a90c792cf4ae4572593ff36a8f87d365af5107f68ed4000bad'}},
 'freedoom':{'label':'Freedoom Phase 2','directory':'Freedoom','wad':'freedoom2.wad','sha256':'a8772e088847032510d97ba2312406a6998f21cbab44d4ff10696faa9c0ecd4b',
         'archive':{'filename':'freedoom-0.13.0.zip','url':'https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip','bytes':24143781,'sha256':'3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59'}}}

def _record(game):
    if game not in CATALOG:raise ValueError('Choose Doom shareware, Chex Quest or Freedoom')
    return CATALOG[game]

def _publish(path,data):
    path=cache._safe_path(path);path.parent.mkdir(parents=True,exist_ok=True)
    if path.exists():
        if not path.is_file() or path.read_bytes()!=data:raise FileExistsError('Keep the existing game file or choose another destination: '+str(path))
        return
    with tempfile.NamedTemporaryFile(dir=path.parent,prefix='.xz-game-',delete=False) as f:
        temporary=Path(f.name);f.write(data)
    try:cache._publish_new(temporary,path)
    finally:
        if temporary.exists():temporary.unlink()

def _member(archive,name):
    with zipfile.ZipFile(archive) as z:
        matches=[n for n in z.namelist() if Path(n).name.lower()==name.lower()]
        if len(matches)!=1:raise ValueError('Unexpected game archive member: '+name)
        item=z.getinfo(matches[0])
        if item.file_size>64*1024*1024:raise ValueError('Game archive member exceeds its limit')
        return z.read(matches[0])

def obtain(game,job):
    record=_record(game);folder=cache._safe_path(data_root()/'games'/record['directory'])
    job.progress('download','Downloading '+record['label']+' from its original distribution')
    archive=download(record['archive'],data_root()/'games/downloads',job)
    files={}
    if game=='doom':
        joined=_member(archive,'DOOMS_19.1')+_member(archive,'DOOMS_19.2')
        files['doom1.wad']=_member(io.BytesIO(joined),'DOOM1.WAD')
        files['README.TXT']=_member(io.BytesIO(joined),'README.TXT')
        notice='Doom 1.9 shareware. Free first episode; the full game and Doom II data are licensed separately. Original id Software README is included.\n'
    elif game=='chex':
        files['chex.wad']=_member(archive,'chex.wad')
        patch=download(record['patch'],data_root()/'games/downloads',job)
        files['chex.deh']=_member(patch,'chex.deh');files['chexdeh.txt']=_member(patch,'chexdeh.txt')
        if hashlib.sha256(files['chex.deh']).hexdigest()!='8c0345089fb227fa7f71c25a6c6e31ff5bd4bea0580f286cd74e05918d72dd40':raise ValueError('Chex patch integrity failed')
        notice='Original Chex Quest freeware game data, not open-source assets. Downloaded on request from chexquest3.com. Do not sell the game data. The download host states that this package is not endorsed by General Mills. The separately licensed compatibility patch and its original notice are included.\n'
    elif game=='freedoom':
        for name in ('freedoom2.wad','COPYING.txt','CREDITS.txt','CREDITS-MUSIC.txt'):
            files[name]=_member(archive,name)
        notice='Freedoom Phase 2, version 0.13.0. BSD-3-Clause; original license and credits included. A free Doom II-compatible base game with different artwork, not commercial Doom II.\n'
    if hashlib.sha256(files[record['wad']]).hexdigest()!=record['sha256']:raise ValueError('Extracted WAD integrity failed')
    files['SOURCE.txt']=(notice+'Source: '+record['archive']['url']+'\n').encode('utf8')
    job.check()
    for name,data in files.items():_publish(folder/name,data)
    manifest={'format':'xz-game/1','game':game,'name':record['label'],'wad':record['wad'],'source':record['archive']['url'],'files':{name:hashlib.sha256(data).hexdigest() for name,data in files.items()}}
    _publish(folder/'game.json',(json.dumps(manifest,indent=2)+'\n').encode())
    return {'format':'xz-game-downloaded/1','game':game,'name':record['label'],'folder':str(folder),'wad':str(folder/record['wad']),'source':record['archive']['url']}

def _target(volume):
    volume=cache._safe_path(volume);target=usb.target_info(volume,boot_loader=False)
    if not target['direct_usb_root']:raise ValueError('Choose the root of a FAT/FAT32 USB. XZ Mods does not format drives.')
    stat=volume.stat()
    return (stat.st_dev,stat.st_ino,target.get('volume_serial'),target.get('volume_root'))

def install(game,volume,job):
    record=_record(game);volume=cache._safe_path(volume);identity=_target(volume)
    result=obtain(game,job)
    source=Path(result['folder']);manifest=json.loads((source/'game.json').read_text());destination=cache._safe_path(volume/'VJTOOLS/Games'/record['directory'])
    job.progress('install','Copying verified '+record['label']+' to USB')
    for name,digest in manifest['files'].items():
        path=cache._safe_path(source/name);data=path.read_bytes()
        if hashlib.sha256(data).hexdigest()!=digest:raise ValueError('Downloaded game files changed')
        job.check()
        if _target(volume)!=identity:raise ValueError('The selected USB changed. Choose it again before copying games.')
        _publish(destination/name,data)
        if usb.sha(destination/name)!=digest:raise ValueError('USB game file failed readback verification')
    _publish(destination/'game.json',(source/'game.json').read_bytes())
    return {'format':'xz-game-installed/1','game':game,'name':record['label'],'folder':str(destination),'wad':str(destination/record['wad']),'native_menu':'MODS > Extras > Games','existing_music_preserved':True,'hardware_verified':False}

def selection(requested):
    """Catalog ids to copy. None or a missing key means every game; an explicit list is honoured as given."""
    if requested is None:return list(CATALOG)
    if not isinstance(requested,list):raise ValueError('games must be a list of game ids')
    unknown=[game for game in requested if game not in CATALOG]
    if unknown:raise ValueError('Unknown game: '+', '.join(map(str,unknown)))
    return [game for game in CATALOG if game in requested]

def installed(volume):
    """What each catalog game looks like on this USB, read without network or writes."""
    volume=cache._safe_path(volume);result=[]
    for game,record in CATALOG.items():
        folder=volume/'VJTOOLS/Games'/record['directory']
        result.append({'id':game,'name':record['label'],'installed':(folder/record['wad']).is_file(),'folder':str(folder),
                       'bytes':record['archive']['bytes']})
    return result

def prepare_usb(volume,job,requested=None):
    """Copy the chosen games; keep the loader usable when an optional download fails; report every result."""
    volume=cache._safe_path(volume);identity=_target(volume);results=[]
    for game in selection(requested):
        record=CATALOG[game];job.check()
        if _target(volume)!=identity:raise ValueError('The selected USB changed. Choose it again before copying games.')
        try:results.append({**install(game,volume,job),'ok':True})
        except (OSError,ValueError,RuntimeError,zipfile.BadZipFile) as exc:
            results.append({'game':game,'name':record['label'],'ok':False,'error':str(exc)})
    failed=sum(not r['ok'] for r in results)
    return {'format':'xz-games-usb/1','games':results,'failed_count':failed,'skipped':[game for game in CATALOG if game not in selection(requested)],
            'commercial_doom2_downloaded':False,'retry_method':'prepare_games_usb','existing_music_preserved':True}

def status():
    result=[]
    for game,record in CATALOG.items():
        folder=data_root()/'games'/record['directory'];path=folder/record['wad']
        result.append({'id':game,'name':record['label'],'downloaded':path.is_file(),'folder':str(folder),'source':record['archive']['url']})
    return {'games':result,'commercial_doom2_downloaded':False}
