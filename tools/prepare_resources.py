"""Prepare an explicit public-resource allowlist, excluding firmware and keys."""
import argparse
import hashlib
import importlib.metadata
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile
import uuid

APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
runtime_input=p.add_mutually_exclusive_group(required=True)
runtime_input.add_argument('--runtime-build',type=Path)
runtime_input.add_argument('--runtime-bundle',type=Path,help='Verified paired bundle shared with the VJ.Tools XZ loader')
p.add_argument('--zig',type=Path,required=True)
p.add_argument('--runtime-source',type=Path,help='Native source checkout matching the paired runtime; defaults to toolkit')
p.add_argument('--doom-wad',type=Path,help='Private local Doom WAD to include in USB images; never uploaded or published')
p.add_argument('--skip-backend',action='store_true',help='Retain an already built backend when only native runtime resources changed')
p.add_argument('--toolkit',type=Path,default=None,
    help='Path to the xdj-xz-toolkit checkout (default: $XZ_TOOLKIT_DIR, sibling ../xdj-xz-toolkit, or monorepo packages/xdj-xz-toolkit)')
a=p.parse_args()
def _resolve_toolkit():
    if a.toolkit:return Path(a.toolkit)
    env=os.environ.get('XZ_TOOLKIT_DIR')
    if env:return Path(env)
    sibling=APP.parent/'xdj-xz-toolkit'
    if (sibling/'builder'/'firmware.py').is_file():return sibling
    legacy=APP.parents[1]/'packages/xdj-xz-toolkit'
    return legacy
TOOLKIT=_resolve_toolkit().resolve()
RUNTIME_SOURCE=(a.runtime_source or TOOLKIT).resolve()
resources=APP/'resources';resources.mkdir(exist_ok=True)
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def copy(source,target):target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
runtime=resources/'runtime';runtime.mkdir(exist_ok=True)
bundle_manifest=None
if a.runtime_bundle:
    sys.path.insert(0,str(TOOLKIT/'vendor'))
    from tools.xz_firmware.mods_bundle import verify_mods_source
    a.runtime_bundle=a.runtime_bundle.resolve()
    bundle_manifest=verify_mods_source(a.runtime_bundle,RUNTIME_SOURCE)
    copy(a.runtime_bundle/'libxz-mods.so',runtime/'libxz-mods.so')
    copy(a.runtime_bundle/'libxz-receiver.so',runtime/'libxz-receiver.so')
    if (a.runtime_bundle/'xz-doom').is_file():copy(a.runtime_bundle/'xz-doom',runtime/'xz-doom')
    for name in ('xz-updater','xz-runtime-smoke','ota-trust.json'):
        if (a.runtime_bundle/name).is_file():copy(a.runtime_bundle/name,runtime/name)
    if (a.runtime_bundle/'settings-schema.json').is_file():copy(a.runtime_bundle/'settings-schema.json',runtime/'settings-schema.json')
    if (a.runtime_bundle/'branding').is_dir():
        shutil.copytree(a.runtime_bundle/'branding',resources/'branding',dirs_exist_ok=True)
else:
    copy(a.runtime_build/'libxz-mods-development.so',runtime/'libxz-mods.so')
    copy(a.runtime_build/'libxz-directfb-mods-test.so',runtime/'libxz-receiver.so')
    if (a.runtime_build/'doom/xz-doom').is_file():copy(a.runtime_build/'doom/xz-doom',runtime/'xz-doom')
    if (a.runtime_build/'ota/xz-updater').is_file():copy(a.runtime_build/'ota/xz-updater',runtime/'xz-updater')
    copy(a.runtime_build/'runtime-smoke',runtime/'xz-runtime-smoke')
if a.doom_wad:
    wad=a.doom_wad.read_bytes()
    if len(wad)>64*1024*1024 or wad[:4]!=b'IWAD':raise ValueError('Expected an IWAD no larger than 64 MiB')
    copy(a.doom_wad,runtime/'doom1.wad')
elif (runtime/'doom1.wad').is_file():
    # A later normal build must not inherit privately selected game data.
    (runtime/'doom1.wad').unlink()
(runtime/'manifest.json').write_text(json.dumps({'firmware':'XDJ-XZ 1.26','profile':'experimental','hardware_qualified':False,
    'prepared_formats':['overcue-stems/4','stemd-cache/1'],
    'prepared_containers':['OVPGZ001','OVPGZ003'], 'stem_page_codecs':['zlib','flac-96k'],
    'bundle_manifest_sha256':digest(a.runtime_bundle/'manifest.json') if a.runtime_bundle else None,
    'source_repository':bundle_manifest.get('source_repository') if bundle_manifest else 'https://github.com/OpticMystic/xdj-xz-toolkit',
    'source_directory':bundle_manifest.get('source_directory','.') if bundle_manifest else '.',
    'source_commit':bundle_manifest['source_commit'] if bundle_manifest else (subprocess.check_output(['git','-C',str(TOOLKIT),'rev-parse','HEAD'],text=True).strip() if (TOOLKIT/'.git').exists() else None),
    'settings_schema_sha256':digest(runtime/'settings-schema.json') if (runtime/'settings-schema.json').is_file() else None,
    'runtime_sha256':digest(runtime/'libxz-mods.so'),'receiver_sha256':digest(runtime/'libxz-receiver.so'),
    'doom_sha256':digest(runtime/'xz-doom') if (runtime/'xz-doom').is_file() else None,
    'private_doom_wad_sha256':digest(runtime/'doom1.wad') if (runtime/'doom1.wad').is_file() else None,
    'ota_updater_sha256':digest(runtime/'xz-updater') if (runtime/'xz-updater').is_file() else None,
    'ota_runtime_smoke_sha256':digest(runtime/'xz-runtime-smoke') if (runtime/'xz-runtime-smoke').is_file() else None,
    'vjtools_connection':True,'vjtools_required':False},indent=2)+'\n')
copy(a.runtime_bundle/'bootstrap.sh' if a.runtime_bundle else TOOLKIT/'vendor/tools/xz_runtime/orchestrator.sh',resources/'bootstrap.sh')
for name in ('inference.py','overcue_roles.py','overcue_flac_pack.py','grouped_stems.py','models.json'):copy(TOOLKIT/'builder'/name,resources/'inference'/name)
licenses=resources/'licenses';licenses.mkdir(exist_ok=True)
copy(APP/'LICENSE',licenses/'XZ-Mods-MIT.txt')
copy(TOOLKIT/'mods/key/LICENSE-MPL-2.0',licenses/'Mozilla-MPL-2.0.txt')
if (RUNTIME_SOURCE/'mods/doom/vendor/LICENSE').is_file():copy(RUNTIME_SOURCE/'mods/doom/vendor/LICENSE',licenses/'Doom-GPL-2.0.txt')
if (RUNTIME_SOURCE/'mods/ota/vendor/LICENCE.md').is_file():copy(RUNTIME_SOURCE/'mods/ota/vendor/LICENCE.md',licenses/'Monocypher-BSD-CC0.txt')
copy(Path(sys.base_prefix)/'LICENSE.txt',licenses/'Python-PSF.txt')
copy(TOOLKIT/'mods/ui/fonts/OFL.txt',licenses/'Barlow-OFL.txt')
for name,relative in (('miniz-MIT.txt','miniz/LICENSE'),('jsmn-MIT.txt','jsmn/LICENSE'),('sha256-public-domain.txt','sha256/README.md')):
    dependency=TOOLKIT/'mods/audio/vendor'/relative
    if dependency.is_file():copy(dependency,licenses/name)
for path in (TOOLKIT/'mods/audio/licenses').iterdir():
    if path.is_file():copy(path,licenses/('cdj3k-mods-'+path.name))
for name in ('LICENSE-MIT','LICENSE-APACHE'):
    old=licenses/('dr-libs-'+name)
    if old.exists() and digest(old)==digest(TOOLKIT/'mods/audio/licenses'/name):old.unlink()
openssl_notice=licenses/'OpenSSL-4.0.0-APACHE.txt'
if not openssl_notice.exists():
    openssl_notice.write_bytes(urllib.request.urlopen('https://raw.githubusercontent.com/openssl/openssl/openssl-4.0.0/LICENSE.txt',timeout=30).read())
registry=json.loads((TOOLKIT/'builder/models.json').read_text())
for name,record in registry['licenses'].items():(licenses/(name+'.txt')).write_text(record['text'],encoding='utf8')
# Source obligations are carried with the exact dependencies used by this build.
for distribution in ('pycdlib','cryptography','cffi','pyinstaller'):
    dist=importlib.metadata.distribution(distribution)
    for entry in dist.files or []:
        if any(token in entry.name.lower() for token in ('license','copying','notice')):
            path=Path(dist.locate_file(entry))
            if path.is_file():copy(path,licenses/'dependencies'/distribution/str(entry).replace('..','_'))
    if distribution=='pycdlib':
        for entry in dist.files or []:
            if str(entry).startswith('pycdlib/') and entry.suffix=='.py':copy(dist.locate_file(entry),licenses/'source'/entry)
source=resources/'source'/'xdj-xz-toolkit';source.mkdir(parents=True,exist_ok=True)
for entry in ('runtime.c','runtime.h','ui_runtime.c','ui_runtime.h','settings.c','settings.h','runtime_smoke.c','build.py','xz-relocations.ld'):
    copy(RUNTIME_SOURCE/'mods'/entry,source/'mods'/entry)
for folder in ('cue','audio','key','ui','tests','doom','ota'):
    for path in (RUNTIME_SOURCE/'mods'/folder).rglob('*'):
        if path.is_file() and (path.suffix in ('.c','.h','.ld','.md','.txt','.py','.json','.hex') or path.name.startswith(('LICENSE','COPYING'))):
            copy(path,source/'mods'/path.relative_to(RUNTIME_SOURCE/'mods'))
for name in ('generate.py','source.json','BarlowSemiCondensed-Medium.ttf'):
    copy(RUNTIME_SOURCE/'mods/ui/fonts'/name,source/'mods/ui/fonts'/name)
for name in ('xz_directfb_hook.c','mods_bridge.h','orchestrator.sh'):
    copy(RUNTIME_SOURCE/'vendor/tools/xz_runtime'/name,source/'vendor/tools/xz_runtime'/name)
for folder in ('build/dfb-generated','build/directfb-1.4-src/include','build/directfb-1.4-src/lib'):
    for path in (RUNTIME_SOURCE/'vendor'/folder).rglob('*.h'):copy(path,source/'vendor'/path.relative_to(RUNTIME_SOURCE/'vendor'))
if a.runtime_bundle:
    with zipfile.ZipFile(a.runtime_bundle/'source.zip') as archive:
        for entry in archive.infolist():
            target=(source/entry.filename).resolve()
            if not target.is_relative_to(source.resolve()) or (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError('Unsafe runtime source archive member')
        archive.extractall(source)
if a.runtime_bundle and (runtime/'xz-updater').is_file():
    subprocess.run([sys.executable,str(RUNTIME_SOURCE/'mods/ota/publish.py'),'--bundle',str(a.runtime_bundle),
                    '--output',str(resources/'ota-release'),'--label','XZ-Mods-local-runtime'],check=True)
uv_record={'version':'0.11.33','url':'https://github.com/astral-sh/uv/releases/download/0.11.33/uv-x86_64-pc-windows-msvc.zip',
    'archive_sha256':'c253ce868ad48d29327b661452ce184c9e333e6d6f5bc8d6fcfbf4dd52b83442'}
if not (resources/'uv.exe').is_file():
    archive=urllib.request.urlopen(uv_record['url'],timeout=30).read()
    if hashlib.sha256(archive).hexdigest()!=uv_record['archive_sha256']:raise ValueError('UV release checksum mismatch')
    with zipfile.ZipFile(io.BytesIO(archive)) as bundle:
        names=[name for name in bundle.namelist() if Path(name).name=='uv.exe']
        if len(names)!=1:raise ValueError('Unexpected UV archive layout')
        (resources/'uv.exe').write_bytes(bundle.read(names[0]))
for name in ('LICENSE-MIT','LICENSE-APACHE'):
    target=licenses/('uv-'+name)
    if not target.exists():target.write_bytes(urllib.request.urlopen('https://raw.githubusercontent.com/astral-sh/uv/0.11.33/'+name).read())
uv_record['executable_sha256']=digest(resources/'uv.exe')
(resources/'uv-source.json').write_text(json.dumps(uv_record,indent=2)+'\n')
subprocess.run([sys.executable,str(TOOLKIT/'builder/build_audio_helper.py'),'--zig',str(a.zig.resolve()),'--output',str(resources/'xz-audio-helper.exe')],check=True)
subprocess.run([sys.executable,str(TOOLKIT/'builder/build_overcue_check.py'),'--zig',str(a.zig.resolve()),'--output',str(resources/'xz-overcue-check.exe')],check=True)
if a.skip_backend:
    if not (resources/'backend/xz-mods-service.exe').is_file():raise ValueError('Build the standalone backend before skipping its rebuild')
else:subprocess.run([sys.executable,str(APP/'tools/build_backend.py'),'--toolkit',str(TOOLKIT)],check=True)
for path in resources.rglob('*'):
    if path.is_file() and (path.suffix.lower() in ('.key','.upd','.pth','.ckpt','.safetensors') or path.name in ('rbp','rbp.patched','autoexec.bin','imagedata.dat','XDJXZ_v126.zip')):
        raise ValueError('Public package contains a prohibited firmware/key asset: '+path.name)
print(json.dumps({'resources':str(resources),'firmware_included':False,'boot_keys_included':False,'vjtools_included':False,'models_included':False}))
