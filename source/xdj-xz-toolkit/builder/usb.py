"""Build locally from supplied firmware inputs, then publish a verified new image."""
from __future__ import annotations
import hashlib
import json
import ctypes
import os
from pathlib import Path
import shutil
import tempfile
import sys
from .cache import _safe_path,_regular,_publish_new
from . import firmware

def sha(path):
    with Path(path).open('rb') as file:return hashlib.file_digest(file,'sha256').hexdigest()

# A drive root takes the loader when it is a USB drive: removable, or formatted FAT, FAT32 or exFAT (the disks this
# computer runs from never are), and writable. The file system is not a condition: the XZ boots a loader from an exFAT
# stick as well as a FAT32 one. Anything else is a folder whose contents the DJ copies to a USB afterward.
USB_FILESYSTEMS=('FAT','FAT32','EXFAT')
READ_ONLY='The selected USB is read-only. Reconnect it so it can be written, or unlock it, and try again.'
NOT_USB='This drive is part of this computer, not a USB. Choose a USB drive, or a folder. XZ Mods does not format drives.'

def target_info(path, *, boot_loader=True):
    path=_safe_path(path)
    if not path.is_dir():raise ValueError('Choose an existing USB root or staging folder')
    filesystem=None;root=None;serial=None;removable=False;writable=True
    if os.name=='nt':
        get_root=ctypes.windll.kernel32.GetVolumePathNameW
        get_root.argtypes=[ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_uint32];get_root.restype=ctypes.c_int
        get_info=ctypes.windll.kernel32.GetVolumeInformationW
        word=ctypes.POINTER(ctypes.c_uint32)
        get_info.argtypes=[ctypes.c_wchar_p,ctypes.c_wchar_p,ctypes.c_uint32,word,word,word,ctypes.c_wchar_p,ctypes.c_uint32]
        get_info.restype=ctypes.c_int
        buffer=ctypes.create_unicode_buffer(32768)
        if not get_root(str(path),buffer,len(buffer)):
            raise OSError('Could not identify the target volume')
        root=buffer.value;name=ctypes.create_unicode_buffer(64)
        volume_serial=ctypes.c_uint32();flags=ctypes.c_uint32()
        if not get_info(root,None,0,ctypes.byref(volume_serial),None,ctypes.byref(flags),name,len(name)):
            raise OSError('Could not read the target filesystem')
        filesystem=name.value.upper();serial=volume_serial.value
        writable=not flags.value&0x80000  # FILE_READ_ONLY_VOLUME: a locked card or a write-protected stick
        removable=ctypes.windll.kernel32.GetDriveTypeW(root)==2  # DRIVE_REMOVABLE
    elif sys.platform=='darwin':
        from .host import mac_volume
        info=mac_volume(path);root=info['volume_root'];filesystem=info['filesystem'];serial=info['volume_serial']
        writable=info.get('writable') is not False;removable=info.get('internal') is False
    at_root=bool(root and path==Path(root))
    direct=at_root and (removable or filesystem in USB_FILESYSTEMS)
    if boot_loader and at_root:
        if not direct:raise ValueError(NOT_USB)
        if not writable:raise ValueError(READ_ONLY)
    return {'filesystem':filesystem,'volume_serial':serial,'volume_root':root,'direct_usb_root':direct,'requires_copy_to_usb_root':not direct}

def require_usb_root(path):
    path=_safe_path(path)
    target=target_info(path)
    if not target['direct_usb_root']:
        raise ValueError('Choose the root of a USB drive. XZ Mods does not format drives or erase music.')
    if (path/'autoexec.bin').exists():
        raise FileExistsError('This USB already has autoexec.bin. Keep that loader or back it up before preparing a new one.')
    return path

def require_loader_target(path):
    """A USB drive root, or a staging folder whose loader the DJ copies to a USB afterward."""
    path=_safe_path(path)
    target=target_info(path)
    if target['direct_usb_root']:return require_usb_root(path),target
    if target.get('volume_root') and path==Path(target['volume_root']):
        raise ValueError(NOT_USB)
    if (path/'autoexec.bin').exists():
        raise FileExistsError('This folder already has autoexec.bin. Choose an empty folder.')
    return path,target

def inspect_inputs(rbp,key):
    application_path=_regular(rbp);secret=_regular(key)
    application,key_verified=firmware.import_application(application_path,firmware.effective_key(secret))
    return {'firmware':'XDJ-XZ 1.26','application_md5':hashlib.md5(application).hexdigest(),
        'key_present':True,'key_verified_against_input':key_verified}

def build_usb(volume,rbp,key,resources,job,experimental=False):
    inspect_inputs(rbp,key)
    if not experimental:raise ValueError('This runtime is experimental; a public release has not been qualified')
    volume=_safe_path(volume)
    target=target_info(volume)
    destination=volume/'autoexec.bin';_safe_path(destination)
    if destination.exists():raise FileExistsError('autoexec.bin already exists. Back it up or choose an empty staging folder; this builder will not overwrite it.')
    resources=Path(resources)
    manifest=json.loads((resources/'runtime'/'manifest.json').read_text())
    runtime=resources/'runtime'/'libxz-mods.so';receiver=resources/'runtime'/'libxz-receiver.so'
    for name,path in [('runtime_sha256',runtime),('receiver_sha256',receiver)]:
        if sha(path)!=manifest[name]:raise ValueError('Bundled mod files failed integrity verification')
    for field,name in [('doom_sha256','xz-doom'),('private_doom_wad_sha256','doom1.wad'),('ota_updater_sha256','xz-updater'),('ota_runtime_smoke_sha256','xz-runtime-smoke'),('support_sha256','xz-support')]:
        if manifest.get(field) and sha(resources/'runtime'/name)!=manifest[field]:
            raise ValueError('Bundled Doom files failed integrity verification')
    secret=firmware.effective_key(key)
    patched,key_verified=firmware.import_application(rbp,secret)
    temporary=Path(tempfile.mkdtemp(prefix='.xzmods-build-',dir=volume))
    try:
        staging=temporary/'payload'
        job.progress('build','Assembling the standalone mod and VJ.Tools connection')
        expected=firmware.stage_payload(staging,patched,runtime,receiver,resources/'bootstrap.sh')
        version_path=resources/'mod-version.json'
        if version_path.is_file():
            version_data=version_path.read_bytes()
            version=json.loads(version_data)
            if version.get('runtime_sha256')!=manifest['runtime_sha256'] or version.get('receiver_sha256')!=manifest['receiver_sha256']:
                raise ValueError('Selected mod version does not match the payload libraries')
            if version.get('bootstrap_sha256')!=sha(resources/'bootstrap.sh'):
                raise ValueError('Selected mod startup policy changed before building')
            (staging/'xz-mod-version.json').write_bytes(version_data)
            expected['xz-mod-version.json']=version_data
        branding=resources/'branding'
        if branding.is_dir():
            (staging/'branding').mkdir()
            for name in ('apply.sh','MD5SUMS','splash.rgb565','logo.rgb565'):
                data=(branding/name).read_bytes()
                (staging/'branding'/name).write_bytes(data)
                expected['branding/'+name]=data
        notices=staging/'licenses';notices.mkdir(exist_ok=True)
        for path in (resources/'licenses').iterdir():
            # Corresponding-source archives ship with the desktop package and release.
            # Only license notices belong in the small controller boot image.
            if path.is_file() and not path.name.lower().endswith(('.zip','.tar.gz','.tar.xz','.tar.bz2')):
                shutil.copyfile(path,notices/path.name)
        image=temporary/'autoexec.bin'
        data=firmware.author_image(staging,secret);image.write_bytes(data)
        job.progress('verify','Verifying the encrypted USB image before installation')
        structure=firmware.verify_image(data,secret,expected)
        digest=sha(image);job.check()
        _safe_path(volume);_safe_path(destination)
        _publish_new(image,destination)
        return {'image':str(destination),'sha256':digest,'bytes':destination.stat().st_size,
            'profile':'experimental','hardware_qualified':False,'contains_local_firmware':True,
            'share_image':False,'key_verified_against_input':key_verified,'vjtools_required':False,'vjtools_connection':True,'structure':structure,**target}
    finally:
        if temporary.exists():
            _safe_path(temporary);shutil.rmtree(temporary)
