"""Replay the unmodified firmware duration getters with an ejected source under ARM QEMU."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
from elftools.elf.elffile import ELFFile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
firmware = HERE.parents[1] / 'vendor/decrypted_iso/pdj/extracted/pdj/rbp'
assert hashlib.sha256(firmware.read_bytes()).hexdigest() == '6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2'
with firmware.open('rb') as stream:
    elf = ELFFile(stream)
    text = elf.get_section_by_name('.text')
    data, base = text.data(), text['sh_addr']
    symbols = {s.name: s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    names = ('Pub_Total_GetMusicMaxTime_mSec', 'UiGetPlayTotalTime', '_ZN2ui11StatWatcher16getPlayTotalTimeE9ENUM_DECK')
    functions = []
    for name in names:
        symbol = symbols[name]
        address, size = symbol['st_value'], symbol['st_size']
        functions.append((address, data[address-base:address-base+size]))

code = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
'''
code += '#include "' + (HERE / 'presentation.c').as_posix() + '"\n'
code += r'''
static uint32_t retained[7]={0x10200,2,6,4},current[7],epoch=1;
static int active=1,retire_on_get;
int xz_ram_music_retained_epoch(unsigned deck,uint32_t record[7],uint32_t *generation){
 if(deck>1||!active)return 0;memcpy(record,retained,28);if(generation)*generation=epoch;return 1;
}
int xz_ram_music_owned_epoch(unsigned deck,uint32_t record[7],uint32_t *generation){return xz_ram_music_retained_epoch(deck,record,generation);}
int xz_ram_music_retained(unsigned deck,uint32_t record[7]){return xz_ram_music_retained_epoch(deck,record,NULL);}
int xz_ram_music_snapshot(int deck,uint32_t generation,const uint32_t record[7]){(void)deck;(void)generation;(void)record;return 0;}
int xz_read_memory(uint32_t address,void *output,size_t n){memcpy(output,(void *)(uintptr_t)address,n);return 0;}
int xz_hook_arm(uint32_t a,const unsigned char *g,void *replacement,void **original){(void)a;(void)g;(void)replacement;(void)original;return -1;}
static void music_get(unsigned deck,void *output){assert(deck<2);memcpy(output,current,28);if(retire_on_get){epoch++;retire_on_get=0;}}
static uint32_t sentinel_time(unsigned deck){(void)deck;return UINT32_MAX;}
static void map(uintptr_t address,size_t size){assert(mmap((void*)address,size,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void*)address);}
static void put(uintptr_t address,uint32_t value){memcpy((void*)address,&value,4);}
int main(void){
 map(0xa9000,4096);map(0xc7000,4096);map(0x249000,4096);
 map(0x1c89000,4096);map(0x2247000,16384);map(0x20000000,4096);
'''
for address, blob in functions:
    code += '{const unsigned char bytes[]={' + ','.join(hex(b) for b in blob) + '};'
    code += 'memcpy((void*)' + hex(address) + ',bytes,sizeof(bytes));__builtin___clear_cache((char*)' + hex(address) + ',(char*)' + hex(address+len(blob)) + ');}\n'
code += r'''
 put(0x1c8981c,0x20000000);put(0x20000038,0x20000100);
 put(0x2000010c,0x20000200);put(0x20000110,0x20000300);
 put(0x20000208,125000);put(0x20000308,130000);
 stock_music_get=music_get;stock_max_time=(uint32_t(*)(unsigned))(uintptr_t)0xa904c;
 uint32_t (*engine_time)(unsigned)=(uint32_t(*)(unsigned))(uintptr_t)0xc7ea0;
 assert(engine_time(0)==125000&&engine_time(1)==130000);
 assert(stock_max_time(0)==0&&stock_max_time(1)==0);
 const uintptr_t display[]={0xec440,0xec48c,0xec52c,0xeec2c,0x1425e0};
 for(unsigned d=0;d<2;d++)for(unsigned i=0;i<5;i++)assert(max_time_for_caller(d,display[i])==(d?130000:125000));
 const uintptr_t excluded[]={0xa36cc,0xb1fb4,0x194a7c,0x194ac0,0x196800,0x196a54,0x196ba0};
 for(unsigned i=0;i<7;i++)assert(max_time_for_caller(0,excluded[i])==0);
 current[0]=0x10200;current[1]=99;assert(max_time_for_caller(0,display[0])==0);memset(current,0,28);
 retire_on_get=1;assert(max_time_for_caller(0,display[0])==0);
 put(0x20000208,0);assert(max_time_for_caller(0,display[0])==0);
 put(0x20000208,UINT32_MAX);assert(max_time_for_caller(0,display[0])==0);
 stock_max_time=sentinel_time;assert(max_time_for_caller(0,display[0])==UINT32_MAX);
 puts("PASS unmodified ARM firmware: engine total125000/130000ms, ejected stock duration0, retained display duration correct; replacement, epoch, excluded callers and invalid time stay stock");
 return 0;
}
'''
(out / 'native-duration.c').write_text(code)
binary = out / 'native-duration-arm'
subprocess.run([str(args.zig.resolve()), 'cc', '-target', 'arm-linux-gnueabi.2.13', '-mcpu=cortex_a9', '-O2', '-std=c11', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation', str(out/'native-duration.c'), '-o', str(binary)], check=True)
def linux(path):
    return '/mnt/' + path.drive[0].lower() + path.as_posix()[2:] if os.name == 'nt' else str(path)
prefix = ['wsl.exe', '-d', 'Ubuntu', '--'] if os.name == 'nt' else []
result = subprocess.run(prefix + ['qemu-arm', '-cpu', 'cortex-a9', '-L', '/usr/arm-linux-gnueabi', linux(binary)], capture_output=True, text=True)
print(result.stdout + result.stderr, end='')
(out/'native-duration-proof.json').write_text(json.dumps(dict(passed=result.returncode==0,exit_code=result.returncode,stdout=result.stdout,stderr=result.stderr,hardware_touched=False,firmware_functions=[hex(a) for a,_ in functions],source_sha256=hashlib.sha256((HERE/'presentation.c').read_bytes()).hexdigest()),indent=2)+'\n')
raise SystemExit(result.returncode)
