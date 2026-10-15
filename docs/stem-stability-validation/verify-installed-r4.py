import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--process-id',type=int,required=True);a=p.parse_args()
root=Path('E:/Codex/xz-stem-fix-20261015');candidate=root/'app-e9e9fd3';installed=Path(os.environ['LOCALAPPDATA'])/'XZ Mods'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files={p.relative_to(candidate/'resources').as_posix():sha(p) for p in (candidate/'resources').rglob('*') if p.is_file()}
actual={p.relative_to(installed/'resources').as_posix():sha(p) for p in (installed/'resources').rglob('*') if p.is_file()}
assert files==actual,{'different':[p for p,v in files.items() if actual.get(p)!=v][:10],'extra':list(set(actual)-set(files))[:10]}
exe=installed/'xz-mods-builder.exe';built=candidate/'src-tauri/target/x86_64-pc-windows-msvc/release/xz-mods-builder.exe';payload=root/'installer-payload-r4/xz-mods-builder.exe'
assert built.read_bytes().count(b'TYPE_VAR_UNK')==1
assert payload.read_bytes()==built.read_bytes().replace(b'TYPE_VAR_UNK',b'TYPE_VAR_NSS')
assert sha(exe)==sha(payload)
selection=Path(os.environ['LOCALAPPDATA'])/'com.opticmystic.xzmods/mod-versions/selection.json';before=(root/'selection-before-r4.json').read_bytes();assert selection.read_bytes()==before
result=subprocess.run([str(installed/'resources/backend/xz-mods-service.exe')],input=json.dumps({'method':'list_mod_versions'}),capture_output=True,text=True,env={**os.environ,'XZ_BUILDER_RESOURCES':str(installed/'resources'),'XZ_BUILDER_DATA':str(Path(os.environ['LOCALAPPDATA'])/'com.opticmystic.xzmods')},timeout=60)
assert result.returncode==0,result.stderr
response=json.loads(result.stdout.splitlines()[-1]);assert response['ok'],response
versions=response['result'];assert versions['bundled_id']=='ab9cec84aa4a65a42ace7257dd648029b991224694a85e7f045f082631b58862';assert versions['selected']['id']==json.loads(before)['id'] and versions['selected']['pinned'];assert selection.read_bytes()==before
proof={'installed_path':str(exe),'process_id':a.process_id,'installed_exe_sha256':sha(exe),'installer_payload_sha256':sha(payload),'packaging_patch':'Only Tauri bundle-type marker UNK to NSS, exactly three bytes; installer payload extracted with7zip matches installed exe','resources_verified':len(files),'extra_resources':0,'runtime_sha256':sha(installed/'resources/runtime/libxz-mods.so'),'receiver_sha256':sha(installed/'resources/runtime/libxz-receiver.so'),'bundled_candidate_id':versions['bundled_id'],'preserved_selection':versions['selected'],'controller_changed':False,'physical_usb_written':False,'hardware_acceptance':'pending','published':False}
(root/'installed-r4-verification.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(proof,indent=2))
