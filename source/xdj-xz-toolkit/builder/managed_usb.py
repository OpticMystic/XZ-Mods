"""Inspect and explicitly update fixed XZ Mods files, retaining verified backups."""
from __future__ import annotations
from contextlib import contextmanager
from datetime import datetime,timezone
import hashlib
from itertools import islice
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import uuid
import ctypes
from . import dj_profile,games,usb,usb_settings
from .cache import _safe_path,_regular,_publish_new
from .jobs import exclusive_lock

SETTINGS='VJ.Tools/XZ-Mods.cfg'
TRANSACTIONS='VJ.Tools/XZ-Mods-updates'
ALLOWED=('autoexec.bin',SETTINGS)

def digest(path):return usb.sha(_regular(path))

def revision(path):
    path=_safe_path(path)
    if not path.exists():return {'exists':False}
    path=_regular(path);before=path.stat()
    if before.st_size>512*1024*1024:raise ValueError('Selected XZ Mods file is unexpectedly large')
    value=digest(path);after=path.stat()
    if (before.st_dev,before.st_ino,before.st_size,before.st_mtime_ns)!=(after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns):
        raise ValueError('File changed during inspection. Inspect the USB again.')
    return {'exists':True,'sha256':value,'bytes':after.st_size}

def identity(volume, *, media=False):
    volume=_safe_path(volume);info=usb.target_info(volume,boot_loader=not media);stat=volume.stat()
    return {'path':os.path.normcase(str(volume)),'device':str(stat.st_dev),'directory':str(stat.st_ino),
            'serial':str(info['volume_serial']) if info.get('volume_serial') is not None else None,'filesystem':info['filesystem'],'volume_root':info.get('volume_root')}

def check_target(volume,expected, *, media=False):
    if not isinstance(expected,dict) or identity(volume,media=media)!=expected:
        raise ValueError('The selected USB changed or was reconnected. Inspect it again before writing.')

def _check_revision(path,expected):
    if revision(path)!=expected:raise ValueError('The file changed since inspection. Reload the USB details before writing.')

def _mkdir(path):
    path=_safe_path(path);path.mkdir(parents=True,exist_ok=True);_safe_path(path)
    if not path.is_dir():raise ValueError('Expected an XZ Mods directory')

def _write(path,data):
    with _safe_path(path).open('xb') as stream:
        stream.write(data);stream.flush();os.fsync(stream.fileno())

def _receipt(folder,record):
    temp=folder/('.receipt-'+uuid.uuid4().hex)
    _write(temp,(json.dumps(record,indent=2)+'\n').encode())
    os.replace(temp,_safe_path(folder/'receipt.json'))

@contextmanager
def _lease(volume,expected):
    check_target(volume,expected)
    lock=_safe_path(Path(volume)/'.xzmods-write.lock')
    if lock.exists():
        _regular(lock)
        if lock.stat().st_nlink>1 or lock.stat().st_size>1 or lock.read_bytes() not in (b'',b'0'):raise ValueError('The XZ Mods write lock is not a recognized lock file')
    with exclusive_lock(lock,'Another XZ Mods window is writing to this USB'):
        check_target(volume,expected);yield

def _space(volume,bytes_needed):
    if shutil.disk_usage(volume).free<bytes_needed:raise ValueError(f'Not enough free space for a verified update and backup. Need about {bytes_needed//1048576+1} MiB.')

def _commit(volume,observed,target,expected,candidate,job):
    if target not in ALLOWED:raise ValueError('Unsupported XZ Mods destination')
    destination=_safe_path(Path(volume)/target)
    check_target(volume,observed);_check_revision(destination,expected);job.check()
    after=revision(candidate)
    if not after['exists']:raise ValueError('Verified replacement is missing')
    if expected==after:return {'unchanged':True,'revision':after,'backup':None}
    _space(volume,expected.get('bytes',0)+after['bytes']+1024*1024)
    parent=Path(volume)/TRANSACTIONS;_mkdir(parent)
    transaction=uuid.uuid4().hex;folder=parent/transaction;folder.mkdir()
    record={'format':'xz-mods-replacement/1','id':transaction,'target':target,'volume':observed,
            'before':expected,'after':after,'state':'preparing','created':datetime.now(timezone.utc).isoformat()}
    _receipt(folder,record)
    backup=None
    if expected['exists']:
        backup=folder/'previous.bin'
        with _regular(destination).open('rb') as src,backup.open('xb') as dst:
            shutil.copyfileobj(src,dst,1024*1024);dst.flush();os.fsync(dst.fileno())
        if revision(backup)!=expected:raise ValueError('The backup did not match the inspected file; original remains unchanged')
    record['state']='prepared';_receipt(folder,record)
    check_target(volume,observed);_check_revision(destination,expected);job.check()
    _mkdir(destination.parent)
    # Same-volume replace; cancellation ends at this boundary. Do not delete first.
    if expected['exists']:os.replace(_regular(candidate),destination)
    else:_publish_new(_regular(candidate),destination)
    try:
        check_target(volume,observed)
        if revision(destination)!=after:raise ValueError('Destination readback did not match')
        record['state']='committed';_receipt(folder,record)
    except Exception as exc:
        raise RuntimeError(f'Update may have been installed but readback failed. Reinspect the USB. Recovery files: {folder}. {exc}') from exc
    return {'unchanged':False,'revision':after,'backup':str(backup) if backup else None,'transaction_id':transaction}

def _record(volume,transaction):
    if not isinstance(transaction,str) or not re.fullmatch('[0-9a-f]{32}',transaction):raise ValueError('Invalid backup identifier')
    folder=_safe_path(Path(volume)/TRANSACTIONS/transaction)
    path=_regular(folder/'receipt.json')
    if path.stat().st_size>8192:raise ValueError('Invalid recovery receipt')
    record=json.loads(path.read_text())
    if record.get('format')!='xz-mods-replacement/1' or record.get('id')!=transaction or record.get('target') not in ALLOWED:
        raise ValueError('Unrecognized recovery receipt')
    if not isinstance(record.get('created'),str) or len(record['created'])>64:raise ValueError('Invalid backup date')
    for key in ('before','after'):
        rev=record.get(key)
        if not isinstance(rev,dict) or type(rev.get('exists')) is not bool:raise ValueError('Invalid backup revision')
        if rev['exists'] and (not re.fullmatch('[0-9a-f]{64}',str(rev.get('sha256'))) or type(rev.get('bytes')) is not int or not 0<=rev['bytes']<=512*1024*1024):
            raise ValueError('Invalid backup revision')
    return folder,record

def recoveries(volume):
    root=_safe_path(Path(volume)/TRANSACTIONS)
    if not root.exists():return []
    if not root.is_dir():raise ValueError('Recovery location is not a directory')
    results=[]
    for entry in islice(root.iterdir(),100):
        if not re.fullmatch('[0-9a-f]{32}',entry.name):continue
        try:
            folder,r=_record(volume,entry.name)
            if r['target']=='autoexec.bin' and r['before']['exists'] and revision(folder/'previous.bin')==r['before']:
                results.append({'id':entry.name,'created':r.get('created'),'state':r.get('state'),'previous':r['before'],'backup':str(folder/'previous.bin')})
        except (ValueError,OSError,KeyError):continue
    return sorted(results,key=lambda x:x.get('created') or '',reverse=True)[:20]

def read_settings(volume,resources):
    schema=usb_settings.contract(resources);path=_safe_path(Path(volume)/SETTINGS);rev=revision(path)
    try:
        if rev.get('bytes',0)>1023:raise ValueError('Settings file is larger than this runtime supports; original was preserved')
        values=usb_settings.parse(path.read_bytes(),schema) if rev['exists'] else usb_settings.defaults(schema)
    except ValueError as exc:return {'editable':False,'reason':str(exc),'revision':rev,'schema':schema}
    return {'editable':True,'values':values,'revision':rev,'schema':schema,'path':str(path)}

def inspect(volume,resources):
    volume=_safe_path(volume);observed=identity(volume)
    manifest=json.loads((Path(resources)/'runtime/manifest.json').read_text())
    loader=revision(volume/'autoexec.bin')
    from . import mod_versions,rekordbox_pdb,stem_library
    try:space=shutil.disk_usage(volume);space={'free_bytes':space.free,'total_bytes':space.total}
    except OSError:space={'free_bytes':None,'total_bytes':None}
    try:recorded=mod_versions.lookup_loader(loader)
    except (OSError,ValueError):recorded=None
    return {'format':'xz-mods-usb/1','volume':str(volume),'identity':observed,'loader':loader,'recorded_mod_version':recorded,
            'installed_version':recorded.get('label') if recorded else None,'runtime_sha256':manifest['runtime_sha256'],'settings':read_settings(volume,mod_versions.settings_resources(volume,resources)),
            'backups':recoveries(volume),'games':games.installed(volume),'space':space,'stems':stem_library.stems_on_usb(volume),
            'rekordbox':volume.joinpath(*rekordbox_pdb.EXPORT_PDB).is_file(),'dj_profiles':dj_profile.summary(volume),**usb.target_info(volume)}

def discover(resources):
    """Drives the setup wizard can offer: removable volumes, plus any volume already carrying an XZ Mods loader.
    filesystem is shown to the DJ; writable False means no loader can go on it. Each is None when unknown."""
    found=[]
    def describe(root,label,removable,filesystem=None,writable=None):
        loader=(root/'autoexec.bin').is_file();settings=(root/SETTINGS).is_file()
        rekordbox=root.joinpath('PIONEER','rekordbox','export.pdb').is_file()
        if not (removable or loader):return
        found.append({'volume':str(root),'label':label or str(root),'removable':removable,'loader':loader,'settings':settings,'rekordbox':rekordbox,
                      'filesystem':filesystem or None,'writable':writable,'games':sum(game['installed'] for game in games.installed(root))})
    if os.name=='nt':
        kernel=ctypes.windll.kernel32
        for number in range(26):
            root=Path(chr(65+number)+':/')
            if not kernel.GetLogicalDrives()&(1<<number):continue
            kind=kernel.GetDriveTypeW(str(root))
            if kind not in (2,3):continue
            label=ctypes.create_unicode_buffer(261);name=ctypes.create_unicode_buffer(64);flags=ctypes.c_uint32()
            try:known=kernel.GetVolumeInformationW(str(root),label,261,None,None,ctypes.byref(flags),name,64)
            except OSError:continue
            # FILE_READ_ONLY_VOLUME: a locked SD card or a write-protected stick.
            describe(root,label.value,kind==2,name.value.upper() if known else None,not flags.value&0x80000 if known else None)
    elif sys.platform=='darwin':
        import subprocess
        from .host import mac_volume
        for root in Path('/Volumes').iterdir():
            if root.is_symlink() or not root.is_dir():continue
            try:
                # Network shares and plain folders also appear in /Volumes; keep only local /dev/disk mounts rooted at this entry.
                info=mac_volume(root)
                if Path(info['volume_root'])!=root:continue
                removable=os.stat(root).st_dev!=os.stat('/').st_dev
            except (OSError,ValueError,subprocess.SubprocessError):continue
            describe(root,root.name,removable,info.get('filesystem'),info.get('writable'))
    return {'format':'xz-usbs/1','usbs':found}

def _commit_loader(volume,observed,expected_loader,candidate,job,settings_change=None):
    changed=None
    if settings_change:
        _check_revision(volume/SETTINGS,settings_change['expected'])
        compatible=candidate.parent/'compatible-settings.cfg'
        _write(compatible,settings_change['data'])
        changed=_commit(volume,observed,SETTINGS,settings_change['expected'],compatible,job)
    try:
        result=_commit(volume,observed,'autoexec.bin',expected_loader,candidate,job)
    except BaseException as error:
        if changed:
            # Restore the profile only while the old loader and our exact adjusted
            # profile remain in place. Never overwrite an intervening user edit.
            try:
                check_target(volume,observed)
                if revision(volume/'autoexec.bin')==expected_loader and revision(volume/SETTINGS)==changed['revision']:
                    from .jobs import Job
                    rollback=candidate.parent/'rollback-settings.cfg'
                    _write(rollback,_regular(changed['backup']).read_bytes())
                    _commit(volume,observed,SETTINGS,changed['revision'],rollback,Job())
                else:
                    raise RuntimeError('Loader or settings changed during the failed switch')
            except (OSError,ValueError,RuntimeError) as recovery_error:
                raise RuntimeError(f'{error}. Settings recovery needs inspection: {recovery_error}. Original settings backup: {changed["backup"]}') from error
        raise
    if changed:result['settings_adjustment']={'backup':changed['backup'],'transaction_id':changed['transaction_id'],
        'removed_fields':settings_change['removed_fields'],'reset_fields':settings_change['reset_fields']}
    return result

def build_with_settings(volume,rbp,key,resources,job,settings_change):
    volume=_safe_path(volume);observed=identity(volume);expected={'exists':False}
    with _lease(volume,observed):
        _check_revision(volume/'autoexec.bin',expected)
        with tempfile.TemporaryDirectory(prefix='.xzmods-version-',dir=volume) as temp:
            built=usb.build_usb(temp,rbp,key,resources,job,True)
            result=_commit_loader(volume,observed,expected,Path(built['image']),job,settings_change)
        return {**built,**result,**usb.target_info(volume),'image':str(volume/'autoexec.bin')}

def update_loader(volume,expected_identity,expected_loader,rbp,key,resources,job,settings_change=None):
    volume=_safe_path(volume)
    if not isinstance(expected_loader,dict) or expected_loader.get('exists') is not True:raise ValueError('Inspect an existing loader first')
    with _lease(volume,expected_identity):
        _check_revision(volume/'autoexec.bin',expected_loader)
        _space(volume,expected_loader['bytes']+_regular(rbp).stat().st_size*4+32*1024*1024)
        with tempfile.TemporaryDirectory(prefix='.xzmods-update-',dir=volume) as temp:
            candidate=usb.build_usb(temp,rbp,key,resources,job,True)
            job.progress('backup','Backing up the existing loader before replacement')
            result=_commit_loader(volume,expected_identity,expected_loader,Path(candidate['image']),job,settings_change)
        return {'format':'xz-mods-loader-updated/1',**result,'image':str(volume/'autoexec.bin'),'inspection':inspect(volume,resources)}

def save_settings(volume,expected_identity,expected_revision,values,resources,job):
    schema=usb_settings.contract(resources);data=usb_settings.serialize(values,schema);volume=_safe_path(volume)
    with _lease(volume,expected_identity):
        _check_revision(volume/SETTINGS,expected_revision)
        existing=read_settings(volume,resources)
        if not existing['editable']:raise ValueError(existing['reason'])
        if any(f.get('readonly') and values[f['key']]!=existing['values'][f['key']] for f in schema['fields']):raise ValueError('Reserved settings must be preserved')
        with tempfile.TemporaryDirectory(prefix='.xzmods-settings-',dir=volume) as temp:
            candidate=Path(temp)/'settings.cfg';_write(candidate,data)
            result=_commit(volume,expected_identity,SETTINGS,expected_revision,candidate,job)
        actual=read_settings(volume,resources)
        if not actual['editable'] or actual['values']!=values:raise RuntimeError('Saved preferences failed value readback; keep the backup')
        return {'format':'xz-mods-settings-saved/1',**result,'settings':actual,'identity':identity(volume)}

def restore_loader(volume,expected_identity,expected_loader,transaction,resources,job):
    volume=_safe_path(volume)
    with _lease(volume,expected_identity):
        folder,record=_record(volume,transaction)
        if record.get('volume')!=expected_identity:raise ValueError('This backup belongs to a different USB or folder')
        if record['target']!='autoexec.bin' or not record['before']['exists']:raise ValueError('This is not a loader backup')
        previous=_regular(folder/'previous.bin')
        if revision(previous)!=record['before']:raise ValueError('Backup checksum differs; nothing was restored')
        with tempfile.TemporaryDirectory(prefix='.xzmods-restore-',dir=volume) as temp:
            candidate=Path(temp)/'autoexec.bin';shutil.copyfile(previous,candidate)
            result=_commit(volume,expected_identity,'autoexec.bin',expected_loader,candidate,job)
        return {'format':'xz-mods-loader-restored/1',**result,'inspection':inspect(volume,resources)}


def support_candidates():
    """Metadata-only candidates. No loader hashing, music inspection, or writes."""
    found=[]
    if os.name=='nt':
        kernel=ctypes.windll.kernel32
        mask=kernel.GetLogicalDrives()
        for number in range(26):
            if not mask&(1<<number):continue
            path=Path(chr(65+number)+':/')
            kind=kernel.GetDriveTypeW(str(path))
            if kind not in (2,3):continue
            # support_sources validates fixed markers, parents and identities.
            found.append({'volume':str(path),'removable':kind==2})
    elif sys.platform=='darwin':
        from .host import mac_volume
        for path in Path('/Volumes').iterdir():
            if path.is_symlink() or not path.is_dir():continue
            try:
                if Path(mac_volume(path)['volume_root'])==path:found.append({'volume':str(path),'removable':True})
            except (OSError,ValueError):continue
    return found


def attach_loader_identity(result,prepared):
    """Commit the host's staged identity immediately after loader readback."""
    if prepared is not None:
        from . import controller_queue
        result['loader_identity']=controller_queue.finish_loader_request(prepared)
    return result
