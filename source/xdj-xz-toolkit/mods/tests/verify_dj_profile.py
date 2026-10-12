"""Check the C DJ Profile reader against the shared fixtures used by the Rust and TypeScript tests."""
from pathlib import Path
import json
import struct
import zlib
import shutil
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
FIXTURES=ROOT.parents[1]/'creative-graph'/'fixtures'/'dj-profile'
def png(width,height,rgba):
    def chunk(kind,body):
        return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body))
    row=b'\x00'+bytes(rgba)*width
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))
            +chunk(b'IDAT',zlib.compress(row*height))+chunk(b'IEND',b''))
with tempfile.TemporaryDirectory(prefix='xz-dj-profile-') as temporary:
    temporary=Path(temporary)
    binary=temporary/'test_dj_profile'
    subprocess.run(['gcc','-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(ROOT),str(ROOT/'tests/test_dj_profile.c'),
        str(ROOT/'dj_profile.c'),str(ROOT/'audio/vendor/sha256/sha256.c'),'-o',str(binary)],check=True)
    cases=json.loads((FIXTURES/'cases.json').read_text(encoding='utf-8'))
    total=mismatches=0
    for kind in ('index','profile'):
        for case in cases[kind]:
            document=temporary/'case.json'
            document.write_text(json.dumps(case['document']),encoding='utf-8')
            got=subprocess.run([str(binary),'case',kind,str(document)],check=True,capture_output=True,text=True).stdout.strip()
            want='valid' if case['valid'] else 'invalid'
            total+=1
            if got!=want:
                mismatches+=1
                print(f'MISMATCH {kind} "{case["name"]}": expected {want}, got {got}')
    print(f'cases: {total-mismatches}/{total} agree with cases.json')
    syntax=subprocess.run([str(binary),'syntax'])
    stick=temporary/'stick'
    shutil.copytree(FIXTURES/'stick-two-profiles',stick)
    result=subprocess.run([str(binary),'stick',str(stick)])
    # The worker that feeds the MODS header, on two copies of the stick.
    runtime=temporary/'test_dj_runtime'
    subprocess.run(['gcc','-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-pthread',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(ROOT),str(ROOT/'tests/test_dj_runtime.c'),
        str(ROOT/'dj_profile.c'),str(ROOT/'dj_profile_write.c'),str(ROOT/'dj_folders.c'),
        str(ROOT/'audio/vendor/sha256/sha256.c'),'-o',str(runtime)],check=True)
    # Folder profiles and the rekordbox stick colour.
    folders=temporary/'test_dj_folders'
    subprocess.run(['gcc','-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(ROOT),str(ROOT/'tests/test_dj_folders.c'),
        str(ROOT/'dj_folders.c'),str(ROOT/'dj_profile.c'),str(ROOT/'dj_profile_write.c'),
        str(ROOT/'audio/vendor/sha256/sha256.c'),'-o',str(folders)],check=True)
    (temporary/'folders').mkdir()
    shutil.copytree(FIXTURES/'stick-two-profiles',temporary/'folders-stick')
    folder_result=subprocess.run([str(folders),str(temporary/'folders'),str(temporary/'folders-stick')])
    usb=[temporary/'usb1',temporary/'usb2']
    for copy in usb: shutil.copytree(FIXTURES/'stick-two-profiles',copy)
    # A logo for the XZ editor to pick, and a file that only looks like one.
    (usb[0]/'Logos').mkdir()
    (usb[0]/'Logos'/'me.png').write_bytes(png(64,64,(10,200,90,255)))
    (usb[0]/'not-a-logo.jpg').write_bytes(b'this is not a jpeg at all')
    worker=subprocess.run([str(runtime),*map(str,usb)])
    failed=mismatches or syntax.returncode or result.returncode or worker.returncode or folder_result.returncode
    print('verify_dj_profile:','FAIL' if failed else 'ok')
    sys.exit(1 if failed else 0)
