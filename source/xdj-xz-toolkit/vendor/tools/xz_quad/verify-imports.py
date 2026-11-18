from pathlib import Path
import hashlib,json,re,struct,subprocess
p=Path(__file__).parent
out=p/'module'
module=out/'xz_hub_depth.ko'
datafile=out/'versions.bin'
subprocess.run(['arm-linux-gnueabi-objcopy','--dump-section','__versions='+str(datafile),str(module)],check=True)
raw=datafile.read_bytes()
assert len(raw)%64==0
versions={}
for i in range(0,len(raw),64):
 crc=struct.unpack_from('<I',raw,i)[0]
 name=raw[i+4:i+64].split(b'\0',1)[0].decode()
 assert name not in versions
 versions[name]=f'{crc:08x}'
rebuilt=json.loads((out/'rebuilt-crcs.json').read_text())
imports=set(re.findall(r'\bU (\w+)',(out/'imports.txt').read_text()))|{'module_layout'}
assert set(versions)==imports,(set(versions)^imports)
assert all(rebuilt.get(name)==crc for name,crc in versions.items())
assert '3.0.101-2790-gc248ed7-svn2808' in (out/'modinfo.txt').read_text()
record={'module_sha256':hashlib.sha256(module.read_bytes()).hexdigest(),
        'source_sha256':hashlib.sha256((p/'xz_hub_depth.c').read_bytes()).hexdigest(),
        'imports':dict(sorted(versions.items())), 'all_imports_match_recompiled_source':True,
        'live_crc_input':False,'hardware_execution':False}
(out/'exact-imports.json').write_text(json.dumps(record,indent=2)+'\n')
(out/'kernel-crc-contract.inc').write_text(''.join(
 f'    {{"__kcrctab_{name}", 0x{crc}u}},\n' for name,crc in sorted(versions.items())))
print(json.dumps(record,indent=2))
