"""Build the Windows installer, optional portable ZIP, and signed update feed locally."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from urllib.parse import quote

APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--portable',action='store_true')
p.add_argument('--skip-build',action='store_true',help='Package an installer already built from this source')
a=p.parse_args()
config=json.loads((APP/'src-tauri/tauri.conf.json').read_text())
version=config['version']
subprocess.run(['python',str(APP/'tools/verify_release_source.py')],check=True)
key=Path(os.environ.get('TAURI_SIGNING_PRIVATE_KEY',str(Path(os.environ['LOCALAPPDATA'])/'XZ Mods Release Keys/updater.key')))
if not key.is_file():raise SystemExit('Updater signing key missing. Set TAURI_SIGNING_PRIVATE_KEY to its path.')
if key.with_suffix(key.suffix+'.pub').read_text().strip()!=config['plugins']['updater']['pubkey'].strip():raise SystemExit('Signing key does not match the app updater public key.')
notes=json.loads((APP/'release-notes.json').read_text())['releases']
release=next(r for r in notes if r['version']==version)
markdown='\n\n'.join('## '+title+'\n'+'\n'.join('- '+line for line in release.get(field,[])) for field,title in [('features','New features'),('fixes','Bug fixes'),('changes','Other changes')] if release.get(field))+'\n'
if not a.skip_build:
    env=os.environ.copy();env.update(TAURI_SIGNING_PRIVATE_KEY=str(key),TAURI_SIGNING_PRIVATE_KEY_PASSWORD='',RUSTFLAGS='-C target-feature=+crt-static',CARGO_BUILD_JOBS='2')
    subprocess.run(['npx.cmd','--yes','@tauri-apps/cli@2.12.0','build','--target','x86_64-pc-windows-msvc','--bundles','nsis'],cwd=APP,env=env,check=True)
installer=APP/f'src-tauri/target/x86_64-pc-windows-msvc/release/bundle/nsis/XZ Mods_{version}_x64-setup.exe'
if not installer.is_file() or not Path(str(installer)+'.sig').is_file():raise SystemExit('Signed installer is missing.')
if a.portable:
    subprocess.run(['python','-X','utf8',str(APP/'tools/package_preview.py'),'--output',str(a.output),'--exe',str(APP/'src-tauri/target/x86_64-pc-windows-msvc/release/xz-mods-builder.exe')],check=True)
else:a.output.mkdir(parents=True,exist_ok=False)
# GitHub normalizes spaces in asset names to dots; publish that name explicitly.
asset_name=installer.name.replace(' ','.')
shutil.copyfile(installer,a.output/asset_name)
shutil.copyfile(Path(str(installer)+'.sig'),a.output/(asset_name+'.sig'))
feed={'version':version,'notes':markdown,'pub_date':datetime.now(timezone.utc).isoformat().replace('+00:00','Z'),'platforms':{'windows-x86_64':{'signature':Path(str(installer)+'.sig').read_text().strip(),'url':f'https://github.com/OpticMystic/XZ-Mods/releases/download/v{version}/'+quote(asset_name)}}}
(a.output/'latest.json').write_text(json.dumps(feed,indent=2)+'\n')
(a.output/'release-notes.md').write_text(markdown)
checks=[]
for path in sorted(a.output.iterdir()):
    if path.is_file():
        with path.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
        checks.append(digest+'  '+path.name)
(a.output/'SHA256SUMS.txt').write_text('\n'.join(checks)+'\n')
print('Local release files: '+str(a.output.resolve()))
print('Not published. Upload the installer and signature before publishing latest.json.')
