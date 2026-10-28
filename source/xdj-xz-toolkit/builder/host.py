"""Names, accelerator detection and install requirements for the desktop host, separate from the ARM deck."""
import ctypes
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys


def executable(root, name):
    return Path(root) / (name + '.exe' if os.name == 'nt' else name)


def nvidia_smi_path():
    found = shutil.which('nvidia-smi')
    if found:
        return found
    if os.name == 'nt':
        for candidate in (Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/nvidia-smi.exe',
                          Path(os.environ.get('ProgramFiles', r'C:\Program Files')) / 'NVIDIA Corporation/NVSMI/nvidia-smi.exe'):
            if candidate.is_file():
                return str(candidate)
    return None


def query_nvidia_smi(path):
    result = subprocess.run([path, '--query-gpu=name,memory.total,driver_version', '--format=csv,noheader,nounits'],
                            capture_output=True, text=True, timeout=20,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if result.returncode:
        return None
    for line in result.stdout.splitlines():
        fields = [field.strip() for field in line.split(',')]
        if len(fields) == 3 and re.fullmatch(r'\d+', fields[1]) and re.fullmatch(r'\d+(\.\d+)*', fields[2]):
            return {'device_name': fields[0], 'vram_bytes': int(fields[1]) * 1024 * 1024,
                    'driver_version': fields[2], 'cuda_api_version': None}
    return None


def query_cuda_driver():
    """Driver API fallback for machines where nvidia-smi is missing from PATH."""
    try:
        library = ctypes.WinDLL('nvcuda.dll') if os.name == 'nt' else ctypes.CDLL('libcuda.so.1')
    except OSError:
        return None
    if library.cuInit(0) != 0:
        return None
    count, version, memory = ctypes.c_int(), ctypes.c_int(), ctypes.c_size_t()
    if library.cuDeviceGetCount(ctypes.byref(count)) != 0 or count.value < 1:
        return None
    name = ctypes.create_string_buffer(256)
    library.cuDeviceGetName(name, 256, 0)
    library.cuDeviceTotalMem_v2(ctypes.byref(memory), 0)
    library.cuDriverGetVersion(ctypes.byref(version))
    return {'device_name': name.value.decode('utf8', 'replace'), 'vram_bytes': int(memory.value),
            'driver_version': None, 'cuda_api_version': int(version.value)}


def detect_nvidia():
    path = nvidia_smi_path()
    found = query_nvidia_smi(path) if path else None
    return found or query_cuda_driver()


def supports_cuda_flavour(gpu, flavour):
    if gpu['driver_version']:
        return int(gpu['driver_version'].split('.')[0]) >= flavour['min_driver']
    return (gpu['cuda_api_version'] or 0) >= flavour['min_cuda_api']


def detect_accelerator(torch_catalog, detect=detect_nvidia, system=sys.platform, machine=platform.machine()):
    """Which torch flavour to install and which device inference should request."""
    if system == 'darwin':
        if machine == 'x86_64':
            raise ValueError('Stem generation needs an Apple Silicon Mac. Intel Macs can import prepared stems and use all USB, settings, waveform and game tools. Choose Import stems to continue.')
        return {'device': 'mps', 'device_name': 'Apple Silicon GPU', 'vram_bytes': None, 'torch_flavour': 'default', 'reason': None}
    gpu = detect()
    if gpu is None:
        return {'device': 'cpu', 'device_name': None, 'vram_bytes': None, 'torch_flavour': 'cpu',
                'reason': 'No NVIDIA GPU driver was found; the engine runs on the CPU.'}
    for flavour_id, flavour in torch_catalog['flavours'].items():
        if flavour.get('cuda') and supports_cuda_flavour(gpu, flavour):
            return {'device': 'cuda', 'device_name': gpu['device_name'], 'vram_bytes': gpu['vram_bytes'],
                    'torch_flavour': flavour_id, 'reason': None}
    return {'device': 'cpu', 'device_name': gpu['device_name'], 'vram_bytes': gpu['vram_bytes'], 'torch_flavour': 'cpu',
            'reason': 'The NVIDIA driver is older than CUDA 12.6 needs. Update the driver to use the GPU.'}


def torch_install(torch_catalog, flavour_id):
    flavour = torch_catalog['flavours'][flavour_id]
    packages = ['torch==' + torch_catalog['version'], 'torchaudio==' + torch_catalog['version']]
    if flavour.get('index'):
        packages[:0] = ['--index-url', flavour['index']]
    return packages


def torch_download_bytes(torch_catalog, flavour_id, system=sys.platform):
    sizes = torch_catalog['flavours'][flavour_id]['download_bytes']
    return sizes.get(system) or max(sizes.values())


def mac_volume(path):
    import plistlib
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
            'writable': info.get('WritableVolume') is True, 'internal': info.get('Internal')}
