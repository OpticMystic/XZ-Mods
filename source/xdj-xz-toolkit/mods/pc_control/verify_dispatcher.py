"""Execute the real hook dispatcher against mapped synthetic ARM prologues."""
from pathlib import Path
import argparse,json,re,subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
source=args.source.read_text()
def function(name):
    start=source.index(name+'(')
    start=source.rfind('\n',0,start)+1
    opened=source.index('{',start);depth=1;at=opened+1
    while depth:
        depth+=(source[at]=='{')-(source[at]=='}');at+=1
    return source[start:at]
hook_start=source.index('struct code_hook {');hook_end=source.index('};',hook_start)+2
hook_declaration=re.search(r'static struct code_hook code_hooks\[[^\]]+\];',source).group(0)
program='''#define _GNU_SOURCE
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
'''+source[hook_start:hook_end]+'\n'+hook_declaration+'''
static unsigned code_hook_count;static int application_verified=1;
'''+function('range_protection')+'\n'+function('xz_hook_arm')+'''
int main(void){
 const uint32_t addresses[5]={0xe52d0,0xe8194,0xe8604,0x27a3f4,0x1fab28};
 const unsigned char guards[5][8]={{0xf0,0x4f,0x2d,0xe9,0x00,0x50,0xa0,0xe1},
  {0x03,0x00,0x50,0xe3,0xf0,0x41,0x2d,0xe9},{0x03,0x00,0x50,0xe3,0xf0,0x47,0x2d,0xe9},
  {0x22,0x30,0xd0,0xe5,0xf0,0x47,0x2d,0xe9},{0xf0,0x4f,0x2d,0xe9,0x80,0x52,0xa0,0xe1}};
 for(unsigned i=0;i<5;i++){
  uintptr_t page=addresses[i]&~(uintptr_t)4095;
  if(i!=2)assert(mmap((void*)page,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==(void*)page);
  assert(!mprotect((void*)page,4096,PROT_READ|PROT_WRITE|PROT_EXEC));
  memcpy((void*)(uintptr_t)addresses[i],guards[i],8);
  assert(!mprotect((void*)page,4096,PROT_READ|PROT_EXEC));
  void *original=0;unsigned char wrong[8];memcpy(wrong,guards[i],8);wrong[0]^=1;
  assert(xz_hook_arm(addresses[i],wrong,(void*)0x12345678,&original)==-1&&!original);
  assert(xz_hook_arm(addresses[i],guards[i],(void*)0x12345678,&original)==0&&original);
  assert(!memcmp(original,guards[i],8));
  uint32_t *patched=(void*)(uintptr_t)addresses[i];assert(patched[0]==0xe51ff004&&patched[1]==0x12345678);
  uint32_t *trampoline=original;assert(trampoline[2]==0xe51ff004&&trampoline[3]==addresses[i]+8);
 }
 assert(code_hook_count==5);
 void *original=0;assert(xz_hook_arm(0x123400,guards[0],(void*)0x12345678,&original)==-1);
 while(code_hook_count<sizeof(code_hooks)/sizeof(code_hooks[0])){
  assert(!mprotect((void*)(uintptr_t)(addresses[0]&~4095u),4096,PROT_READ|PROT_WRITE|PROT_EXEC));
  memcpy((void*)(uintptr_t)addresses[0],guards[0],8);
  assert(!mprotect((void*)(uintptr_t)(addresses[0]&~4095u),4096,PROT_READ|PROT_EXEC));
  assert(!xz_hook_arm(addresses[0],guards[0],(void*)0x12345678,&original));
 }
 original=0;assert(xz_hook_arm(addresses[0],guards[0],(void*)0x12345678,&original)==-1&&!original);
 puts("PASS real hook dispatcher accepts four guarded PC callbacks and existing waveform hook; wrong/unknown guards rejected");
}
'''
(out/'dispatcher-test.c').write_text(program)
binary=out/'dispatcher-test'
subprocess.run(['gcc','-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-fPIE','-pie',str(out/'dispatcher-test.c'),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
(out/'verification.json').write_text(json.dumps({'real_dispatcher_executed':True,'guarded_pc_callbacks':4,
 'existing_wave_hook':True,'wrong_and_unknown_guards_rejected':True,'real_capacity_exhaustion_rejected':True,'controller_touched':False},indent=2)+'\n')
