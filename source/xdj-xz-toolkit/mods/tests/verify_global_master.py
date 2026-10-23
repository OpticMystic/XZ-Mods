"""Replay the own-C MASTER observer; native interfaces are controlled test callbacks."""
from pathlib import Path
import argparse,hashlib,json,subprocess

p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);p.add_argument('--arch',choices=['host','arm','all'],default='all');p.add_argument('--case');a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
def linux(path):
    s=path.resolve().as_posix();return '/mnt/'+s[0].lower()+s[2:]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
files=['sync_link_runtime.c','sync_link_runtime.h','sync_link_plan.c','sync_link_plan.h','sync_master.h','extra_decks.h','runtime.h','clock_compat.h','sync_observation.h',
       'tests/test_sync_link_runtime.c','tests/test_sync_link_plan.c','tests/test_global_master.c','tests/test_master_lifecycle.c']
receipt={'inputs':{n:sha(root/n) for n in files},'runs':[],'hardware':False}
cases={'runtime':[None],'plan':[None],
       'global':['pending','local','guards','operator','stale','reentrant','inflight','concurrent','steady','paused','admission','missing','restart-request'],
       'lifecycle':['stop-start','exhaustion','cache-exhaustion','pinned-restart','no-legacy-fallback','busy-authority-token','clock-collision']}
if a.case:
    cases={suite:[a.case] for suite,selected in cases.items() if a.case in selected}
    if not cases:p.error('unknown case')
def run(label,cmd):
    r=subprocess.run(['wsl','-d','Ubuntu','--exec',*cmd],capture_output=True,timeout=90)
    (out/(label+'.stdout')).write_bytes(r.stdout);(out/(label+'.stderr')).write_bytes(r.stderr)
    receipt['runs'].append({'label':label,'command':cmd,'exit':r.returncode})
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(label,r.returncode,r.stdout.decode(errors='replace')[-120:].strip(),flush=True)
    return r.returncode
for arch,cc in [('host','gcc'),('arm','arm-linux-gnueabi-gcc')]:
    if a.arch not in ['all',arch]:continue
    flags=['-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-pthread']
    flags+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer','-no-pie'] if arch=='host' else ['-mcpu=cortex-a9','-mfpu=vfpv3','-mfloat-abi=softfp']
    for suite,selected in cases.items():
        test='test_global_master.c' if suite=='global' else 'test_master_lifecycle.c' if suite=='lifecycle' else 'test_sync_link_'+suite+'.c'
        binary=out/(suite+'-'+arch)
        cmd=[cc,*flags,'-I'+linux(root/'tests'),linux(root/'tests'/test)]
        if suite not in ['plan','lifecycle']:cmd.append(linux(root/'sync_link_runtime.c'))
        cmd +=[linux(root/'sync_link_plan.c'),'-lm','-o',linux(binary)]
        if run(suite+'-'+arch+'-compile',cmd):continue
        for case in selected:
            prefix=['qemu-arm','-cpu','cortex-a9','-L','/usr/arm-linux-gnueabi'] if arch=='arm' else ['env','ASAN_OPTIONS=detect_leaks=1:abort_on_error=1','UBSAN_OPTIONS=halt_on_error=1']
            run(suite+'-'+arch+('-'+case if case else ''),prefix+[linux(binary)]+([case] if case else []))
assert receipt['inputs']=={n:sha(root/n) for n in files}
raise SystemExit(int(any(r['exit'] for r in receipt['runs'])))
