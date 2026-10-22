"""Owned C waveform reduction and exact native RGB packet fixtures. Offline only."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path)
p.add_argument('--native-proof',type=Path,default=Path('E:/Codex/xz-jog-protocol-20261022/offline-protocol/native-wave-packets.json'))
p.add_argument('--fixtures',type=Path,default=Path('E:/Codex/xz-jog-offline-20261021/display/c-adapter'))
p.add_argument('--red-runtime',choices=['content','commit','age'])
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);root=Path(__file__).resolve().parents[1]
def linux(p):
    s=p.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
native=json.loads(a.native_proof.read_text());expected=b''.join(bytes.fromhex(c['packet_hex']) for c in native['cases'])
results=[]
if a.red_runtime:
    mutant=a.out/'mutant';(mutant/'tests').mkdir(parents=True)
    source=(root/'jog_display_runtime.c').read_text()
    needle,replacement={
        'content':('current_content(side_idx)==content','1 /* content fence removed */'),
        'commit':('struct xz_ds_selection final_selection;','xz_jd_commit(&jd_state[side_idx],&transaction);\n struct xz_ds_selection final_selection;'),
        'age':('<=XZ_MASTER_MAX_AGE_MS','<=150u')
    }[a.red_runtime]
    assert source.count(needle)==(2 if a.red_runtime=='age' else 1)
    (mutant/'jog_display_runtime.c').write_text(source.replace(needle,replacement))
    (mutant/'tests/test_jog_assets_runtime.c').write_text((root/'tests/test_jog_assets_runtime.c').read_text())
    for arch,cc in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
        binary=a.out/('red-'+arch)
        flags=['-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-pthread','-I'+linux(root),'-I'+linux(a.fixtures)]
        commands=[[cc,*flags,linux(mutant/'tests/test_jog_assets_runtime.c'),linux(root/'jog_display.c'),linux(root/'jog_display_wave.c'),'-o',linux(binary)],
                  (['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(binary)]]
        for i,cmd in enumerate(commands):
            r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
            (a.out/f'{arch}-{i}.log').write_text(r.stdout+r.stderr);results.append(dict(command=cmd,returncode=r.returncode))
            if i:assert r.returncode and 'Assertion' in r.stderr,r.stdout+r.stderr
            else:assert not r.returncode,r.stdout+r.stderr
        print(arch+' RED rejected '+a.red_runtime+' mutant')
    (a.out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    raise SystemExit(0)
for arch,cc in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
    binary=a.out/('wave-'+arch);stream=a.out/(arch+'-packets.bin')
    flags=['-std=c11','-O2','-g','-UNDEBUG','-Wall','-Wextra','-Werror']
    flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie'] if arch=='host' else ['-march=armv7-a']
    commands=[[cc,*flags,linux(root/'tests/test_jog_wave.c'),linux(root/'jog_display_wave.c'),'-o',linux(binary)],
             (['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(binary),linux(stream)]]
    for i,cmd in enumerate(commands):
        r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
        (a.out/f'{arch}-{i}.log').write_text(r.stdout+r.stderr);results.append(dict(command=cmd,returncode=r.returncode))
        assert not r.returncode,r.stdout+r.stderr
        print(r.stdout,end='')
    binary=a.out/('assets-'+arch)
    commands=[[cc,*flags,'-pthread','-I'+linux(a.fixtures),linux(root/'tests/test_jog_assets_runtime.c'),
               linux(root/'jog_display.c'),linux(root/'jog_display_wave.c'),'-o',linux(binary)],
              (['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(binary)]]
    for i,cmd in enumerate(commands):
        r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
        (a.out/f'{arch}-assets-{i}.log').write_text(r.stdout+r.stderr);results.append(dict(command=cmd,returncode=r.returncode))
        assert not r.returncode,r.stdout+r.stderr
        print(r.stdout,end='')
    assert stream.read_bytes()==expected
    print(arch+' PASS all 292 packets exactly match native producer fixtures')
    binary=a.out/('schedule-'+arch)
    commands=[[cc,*flags,'-I'+linux(a.fixtures),linux(root/'tests/test_jog_wave_schedule.c'),
               linux(root/'jog_display.c'),linux(root/'jog_display_wave.c'),'-o',linux(binary)],
              (['qemu-arm','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else [])+[linux(binary)]]
    for i,cmd in enumerate(commands):
        r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,text=True,timeout=60)
        (a.out/f'{arch}-schedule-{i}.log').write_text(r.stdout+r.stderr);results.append(dict(command=cmd,returncode=r.returncode))
        assert not r.returncode,r.stdout+r.stderr
        print(r.stdout,end='')
(a.out/'results.json').write_text(json.dumps(dict(results=results,native_proof_sha256=hashlib.sha256(a.native_proof.read_bytes()).hexdigest(),hardware_acceptance=False),indent=2)+'\n')
