"""Copy the FLAGSHIP shell and the on-deck library reader from the VJToolsLibrary toolkit into source/.

Those two directories are developed in the monorepo; everything else under source/xdj-xz-toolkit is
newer here. The copy replaces only mods/shell and mods/pdb and skips build output.
"""
import argparse
import shutil
from pathlib import Path

APP = Path(__file__).resolve().parents[1]
SKIP = shutil.ignore_patterns('.build', '__pycache__', '*.exe', '*.o', '*.obj', '*.pdb', '*.so', '*.ppm', '*.png')

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--monorepo', type=Path, default=APP.parent / 'VJToolsLibrary')
args = parser.parse_args()
source = args.monorepo / 'packages/xdj-xz-toolkit/mods'
target = APP / 'source/xdj-xz-toolkit/mods'
for name in ('shell', 'pdb'):
    if not (source / name).is_dir():
        raise SystemExit(f'{source / name} is missing')
    if (target / name).exists():
        shutil.rmtree(target / name)
    shutil.copytree(source / name, target / name, ignore=SKIP)
    print('synced', name, sum(1 for p in (target / name).rglob('*') if p.is_file()), 'files')
