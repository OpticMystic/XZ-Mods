"""Names and install requirements for the desktop host, separate from the ARM deck."""
import os
import platform
from pathlib import Path
import sys


def executable(root, name):
    return Path(root) / (name + '.exe' if os.name == 'nt' else name)


def torch_install(preset):
    if sys.platform == 'darwin' and platform.machine() == 'x86_64':
        raise ValueError('Stem generation needs an Apple Silicon Mac. Intel Macs can import prepared stems and use all USB, settings, waveform and game tools. Choose Import stems to continue.')
    packages = ['torch==2.8.0', 'torchaudio==2.8.0']
    if sys.platform != 'darwin':
        packages[:0] = ['--index-url', 'https://download.pytorch.org/whl/cpu']
    return packages


def mac_volume(path):
    import plistlib
    import subprocess
    volume = subprocess.run(['/bin/df', '-P', str(path)], check=True, capture_output=True,
                            text=True, timeout=15)
    device = volume.stdout.splitlines()[-1].split()[0]
    if not device.startswith('/dev/disk'):
        raise ValueError('Choose a mounted USB volume or a local staging folder')
    result = subprocess.run(['/usr/sbin/diskutil', 'info', '-plist', device],
                            check=True, capture_output=True, timeout=15)
    info = plistlib.loads(result.stdout)
    mount = info.get('MountPoint')
    if not isinstance(mount, str) or not mount.startswith('/'):
        raise ValueError('Could not identify the selected volume')
    filesystem = str(info.get('FilesystemType', info.get('FileSystemType', ''))).lower()
    personality = str(info.get('FileSystemPersonality', info.get('FilesystemName', ''))).upper()
    if filesystem == 'msdos':
        filesystem = 'FAT32' if 'FAT32' in personality else 'FAT'
    else:
        filesystem = filesystem.upper()
    return {'volume_root': mount, 'filesystem': filesystem,
            'volume_serial': info.get('VolumeUUID') or info.get('DiskUUID'),
            'writable': info.get('WritableVolume') is True}
