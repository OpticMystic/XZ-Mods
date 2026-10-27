"""Compile real POSIX storage and run only against temporary directories, never USBs."""
from pathlib import Path
import argparse,json,os,subprocess,tempfile

ROOT=Path(__file__).resolve().parent
def main():
 p=argparse.ArgumentParser();p.add_argument('--zig');p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 a.out.mkdir(parents=True,exist_ok=True)
 compiler=[a.zig,'cc','-target','x86_64-linux-gnu'] if a.zig else ['cc','-Wno-misleading-indentation']
 receipts=[]
 for name,extra in [('storage',['test_diary.c']),('hooks',['test_hooks.c','../audio/vendor/sha256/sha256.c'])]:
  binary=a.out/('test-'+name)
  cmd=compiler+['-std=c11','-O2','-Wall','-Wextra','-Werror','-UNDEBUG',*[str(ROOT/f) for f in ['format.c','store.c',*extra]],'-lpthread','-o',str(binary)]
  r=subprocess.run(cmd,capture_output=True);(a.out/(name+'-compile.log')).write_bytes(r.stdout+r.stderr)
  if r.returncode:print(r.stderr.decode(errors='replace'));return r.returncode
  output=a.out/'sticks';output.mkdir(exist_ok=True)
  if os.name=='nt':
   def linux(path):return '/mnt/'+path.resolve().drive[0].lower()+path.resolve().as_posix()[2:]
   run=['wsl','-d','Ubuntu','--exec',linux(binary),linux(output)]
  else:run=[str(binary),str(output)]
  r=subprocess.run(run,capture_output=True,timeout=240);(a.out/(name+'.log')).write_bytes(r.stdout+r.stderr)
  print((r.stdout+r.stderr).decode(errors='replace'))
  receipts.append({'compile':cmd,'run':run,'exit':r.returncode});(a.out/'receipt.json').write_text(json.dumps(receipts,indent=2))
  if r.returncode:return r.returncode
 return 0
if __name__=='__main__':raise SystemExit(main())
