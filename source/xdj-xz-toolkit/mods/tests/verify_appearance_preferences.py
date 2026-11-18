"""Host pixel tests and the real shell display preference input/queue glue. No device access."""
from pathlib import Path
import argparse
import os
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'shell'))
from shell_sources import SHELL_SOURCES

def linux(path):
    path=Path(path).resolve()
    return '/mnt/'+path.drive[0].lower()+'/'+path.as_posix()[3:] if os.name=='nt' else str(path)

def verify(zig,output):
    output.mkdir(parents=True,exist_ok=True)
    common=['-O2','-std=c11','-Wall','-Wextra','-Werror','-UNDEBUG']
    binary=output/'band-colors.exe'
    subprocess.run([str(zig.resolve()),'cc',*common,*[str(ROOT/'shell'/name) for name in SHELL_SOURCES],
                    str(ROOT/'ui/layered_wave.c'),str(ROOT/'tests/test_wave_band_colors.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    binary=output/'shell-display'
    prefix=['wsl','-d','Ubuntu','--exec'] if os.name=='nt' else []
    subprocess.run([*prefix,'gcc',*common,'-Wno-misleading-indentation','-ffunction-sections','-fdata-sections',
                    linux(ROOT/'tests/test_shell_display_preferences.c'),linux(ROOT/'shell/shell.c'),
                    linux(ROOT/'shell_runtime_core.c'),'-Wl,--gc-sections','-pthread','-lm','-o',linux(binary)],check=True)
    subprocess.run([*prefix,linux(binary)],check=True)
    binary=output/'native-band-preferences'
    subprocess.run([str(zig.resolve()),'cc','-target','x86_64-linux-gnu.2.28',*common,'-ffunction-sections','-fdata-sections',
                    str(ROOT/'tests/test_native_band_preferences.c'),'-Wl,--gc-sections','-pthread','-lm','-o',str(binary)],check=True)
    subprocess.run([*prefix,linux(binary)],check=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig',required=True,type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.output:verify(args.zig,args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='xz-appearance-') as folder:verify(args.zig,Path(folder))
