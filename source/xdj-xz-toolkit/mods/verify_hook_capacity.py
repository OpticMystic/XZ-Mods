"""Exercise the production ARM hook allocator on an isolated mapped firmware image."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
from elftools.elf.elffile import ELFFile
here=Path(__file__).resolve().parent
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
source=(here/'runtime.c').read_text();start=source.index('int xz_hook_arm(');end=source.index('int xz_runtime_beat_jump(',start);hook=source[start:end]
structure=source[source.index('struct code_hook {'):source.index('struct pad_trace_record')]
protection=source[source.index('static int range_protection('):source.index('static int known_application(')]
addresses=set(int(a,16) for a in re.findall(r'address\s*==\s*(0x[0-9a-f]+)',hook));addresses.update(int(a,16) for a in re.findall(r'\{(0x[0-9a-f]+),\{',(here/'ram/abi.h').read_text()))
firmware=here.parent/'vendor/decrypted_iso/pdj/extracted/pdj/rbp';assert hashlib.sha256(firmware.read_bytes()).hexdigest()=='6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2'
with firmware.open('rb') as f:
 elf=ELFFile(f);text=elf.get_section_by_name('.text');data=text.data();base=text['sh_addr']
entries=','.join('{'+hex(a)+',{'+','.join(hex(b) for b in data[a-base:a-base+8])+'}}' for a in sorted(addresses))
headers='#define _GNU_SOURCE\n#include <assert.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <sys/mman.h>\n#include <unistd.h>\n'
c=headers+(here/'ram/abi.h').read_text()+'\nstatic int application_verified=1;\n'+structure+protection+hook
c+='static const struct {uint32_t address;unsigned char guard[8];} entries[]={'+entries+'};\n'
c+=r"""
int main(void){
 const size_t page=(size_t)sysconf(_SC_PAGESIZE);unsigned n=sizeof(entries)/sizeof(entries[0]);
 for(unsigned i=0;i<n;i++){
  uintptr_t start=entries[i].address & ~(page-1);
  void *mapped=mmap((void*)start,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
  if(mapped==MAP_FAILED){assert(range_protection(start,page)>=0);assert(!mprotect((void*)start,page,PROT_READ|PROT_WRITE));}
  memcpy((void*)(uintptr_t)entries[i].address,entries[i].guard,8);assert(!mprotect((void*)start,page,PROT_READ|PROT_EXEC));
  void *original=NULL;
  if(xz_hook_arm(entries[i].address,entries[i].guard,(void*)(uintptr_t)0x70000000,&original)){
   fprintf(stderr,"FAIL hook %u/%u at0x%x after %u slots: full native startup cannot fit\n",i+1,n,entries[i].address,code_hook_count);return 1;
  }
  assert(original&& !memcmp(original,entries[i].guard,8));
  assert(*(uint32_t*)(uintptr_t)entries[i].address==0xe51ff004);
  assert(*(uint32_t*)(uintptr_t)(entries[i].address+4)==0x70000000);
  assert(range_protection(entries[i].address,8)==(PROT_READ|PROT_EXEC));
 }
 unsigned count=code_hook_count;void *original=NULL;unsigned char wrong[8]={0};
 assert(xz_hook_arm(entries[0].address,wrong,(void*)0x70000000,&original)==-1&&code_hook_count==count);
 assert(xz_hook_arm(0xdeadbeef,wrong,(void*)0x70000000,&original)==-1&&code_hook_count==count);
 for(unsigned i=0;i<count;i++)munmap(code_hooks[i].trampoline,page);
 printf("PASS actual allocator: %u qualified hooks, trampolines, ARM veneers, restored RX pages and rejected unknown/wrong guards\n",n);
 return 0;
}
"""
(out/'allocator-test.c').write_text(c)
def linux(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:] if os.name=='nt' else str(p)
prefix=['wsl.exe','-d','Ubuntu','--'] if os.name=='nt' else []
subprocess.run(prefix+['cc','-std=c11','-O1','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',linux(out/'allocator-test.c'),'-o',linux(out/'allocator-test')],check=True)
result=subprocess.run(prefix+[linux(out/'allocator-test')],capture_output=True,text=True);print(result.stdout+result.stderr,end='')
(out/'hook-capacity-proof.json').write_text(json.dumps(dict(passed=result.returncode==0,exit_code=result.returncode,qualified_hooks=len(addresses),source_sha256=hashlib.sha256((here/'runtime.c').read_bytes()).hexdigest(),stdout=result.stdout,stderr=result.stderr,hardware_touched=False),indent=2)+'\n')
raise SystemExit(result.returncode)
