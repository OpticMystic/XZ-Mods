"""Run actual bridge identity tests with in-memory I/O; never access a device."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess

ROOT=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--arm',action='store_true');p.add_argument('--zig',default=os.environ.get('XZ_ZIG')); a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
for name in ['bridge.c','contract.h','port_layout.h','test_binding.c']:shutil.copyfile(ROOT/name,out/name)
crypto=ROOT.parents[2]/'mods/audio/vendor/sha256'
for name in ['sha256.c','sha256.h']:shutil.copyfile(crypto/name,out/name)
contract=(out/'contract.h').read_text()
for pin,data in [('RBP_SHA',b'synthetic application'),('RUNTIME_SHA',b'synthetic runtime'),('RECEIVER_SHA',b'synthetic receiver'),('MODULE_SHA',b'synthetic module')]:
    contract=re.sub(r'(#define '+pin+r' ")[^"]+',r'\g<1>'+hashlib.sha256(data).hexdigest(),contract)
(out/'contract.h').write_text(contract)
# Keep qualification, admission, route decisions and status publication unchanged.
# Only replace process execution and event/debounce I/O in the scratch copy.
bridge=(out/'bridge.c').read_text()
boundaries={
    'command': 'return m_command(program, arg, option, extra, selection, music);',
    'event_socket': 'return m_event_socket();',
    'event_wait': 'm_event_wait(fd, milliseconds);',
    'debounce_wait': 'return m_debounce_wait(milliseconds);',
}
for name,body in boundaries.items():
    pattern=r'(static (?:int|void) '+name+r'\([^)]*\)\n\{)\n.*?\n\}'
    bridge,count=re.subn(pattern,lambda m:m[1]+'\n    '+body+'\n}',bridge,flags=re.S)
    assert count==1, name
(out/'bridge.c').write_text(bridge)
cc='arm-linux-gnueabi-gcc' if a.arm else 'gcc'
flags=['-std=c11','-Wall','-Wextra','-Werror','-O1','-g']+(['-static','-mcpu=cortex-a9','-mfloat-abi=soft'] if a.arm else ['-fsanitize=address,undefined','-fno-sanitize-recover=all','-no-pie'])
binary=out/'test-binding'
runs=[]
def run(stage,command):
    done=subprocess.run(command,capture_output=True,timeout=120)
    (out/(stage+'.log')).write_bytes(done.stdout+done.stderr)
    runs.append({'stage':stage,'command':command,'exit':done.returncode})
    print(stage,done.returncode,flush=True)
    if done.returncode: raise RuntimeError('See '+str(out/(stage+'.log')))
    return done.stdout.decode()

versions=[]
try:
    run('compile',[cc,*flags,str(out/'test_binding.c'),str(out/'sha256.c'),'-o',str(binary)])
    run('run',(['qemu-arm','-cpu','cortex-a9'] if a.arm else [])+[str(binary)])
    if a.arm:
        if not a.zig: raise RuntimeError('--arm requires --zig or XZ_ZIG for the GLIBC 2.4 import gate')
        zig=a.zig
        if re.match(r'^[A-Za-z]:',zig):
            zig=subprocess.check_output(['wslpath','-u',zig],text=True).strip()
        def target_path(path):
            return subprocess.check_output(['wslpath','-w',str(path)],text=True).strip() if zig.endswith('.exe') else str(path)
        # Compile the unmodified production source separately, with synthetic pins.
        production=out/'bridge-production.c';shutil.copyfile(ROOT/'bridge.c',production)
        helper=out/'xz-auto-bridge'
        run('arm-import-build',[zig,'cc','-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9',
            '-O2','-std=c11','-Wall','-Wextra','-Werror','-fno-pie','-no-pie',
            '-I'+target_path(out),target_path(production),target_path(out/'sha256.c'),'-o',target_path(helper)])
        symbols=run('arm-imports',['arm-linux-gnueabi-readelf','--dyn-syms','--wide',str(helper)])
        versions=sorted(set(re.findall(r'GLIBC_([0-9.]+)',symbols)))
        assert versions and all(tuple(map(int,v.split('.'))) <= (2,4) for v in versions), versions
        run('arm-self-test',['qemu-arm','-cpu','cortex-a9','/usr/arm-linux-gnueabi/lib/ld-linux.so.3',
            '--library-path','/usr/arm-linux-gnueabi/lib',str(helper),'--self-test'])
        print('PASS GLIBC 2.4 import gate:', ', '.join(versions),flush=True)
finally:
    (out/'receipt.json').write_text(json.dumps({'arm':a.arm,'hardware':False,
        'source_sha256':hashlib.sha256((ROOT/'bridge.c').read_bytes()).hexdigest(),
        'fixture_boundaries':list(boundaries),'glibc_versions':versions,'runs':runs},indent=2)+'\n')
