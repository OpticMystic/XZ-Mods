"""Replay native waveform read-borrow release, clear request and blackout under ARM QEMU."""
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
parser.add_argument('--source', type=Path, default=HERE/'presentation.c')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
firmware = HERE.parents[1]/'vendor/decrypted_iso/pdj/extracted/pdj/rbp'
assert hashlib.sha256(firmware.read_bytes()).hexdigest() == '6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2'
with firmware.open('rb') as stream:
    elf = ELFFile(stream)
    text = elf.get_section_by_name('.text')
    data, base = text.data(), text['sh_addr']
    blobs = [(a,data[a-base:a-base+n]) for a,n in ((0x13a7f4,164),(0x13a898,28),(0xeb9e0,120))]
    release = data[0x144790-base:0x1447a4-base]

code = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
'''
code += '#include "'+args.source.resolve().as_posix()+'"\n'
code += r'''
static uint32_t identity[7]={0x10200,28,13,4};
int xz_ram_music_owned_epoch(unsigned d,uint32_t r[7],uint32_t *e){if(d!=0)return 0;memcpy(r,identity,28);if(e)*e=2;return 1;}
int xz_ram_music_retained_epoch(unsigned d,uint32_t r[7],uint32_t *e){return xz_ram_music_owned_epoch(d,r,e);}
int xz_ram_music_retained(unsigned d,uint32_t r[7]){return xz_ram_music_owned_epoch(d,r,NULL);}
int xz_ram_music_snapshot(int d,uint32_t e,const uint32_t r[7]){(void)d;(void)e;(void)r;return 0;}
int xz_read_memory(uint32_t a,void *out,size_t n){memcpy(out,(void*)(uintptr_t)a,n);return 0;}
int xz_hook_arm(uint32_t a,const unsigned char *g,void *r,void **o){(void)a;(void)g;(void)r;(void)o;return -1;}
static void get_music(unsigned d,void *out){(void)d;memset(out,0,28);}
static void map(uintptr_t a,size_t n){assert(mmap((void*)a,n,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void*)a);}
static void put(uintptr_t a,uint32_t v){memcpy((void*)a,&v,4);}
static uint32_t word(uintptr_t a){uint32_t v;memcpy(&v,(void*)a,4);return v;}
static void stub(uintptr_t a){put(a,0xe3a00000);put(a+4,0xe12fff1e);}
int main(void){
 map(0x13a000,4096);map(0x13c000,4096);map(0xeb000,4096);map(0x144000,4096);
 map(0x222c000,4096);map(0x2251000,0x20000);map(0x20000000,0x4000);
 stub(0x13c338);stub(0x13c350);stub(0xeb6a8);
'''
for address, blob in blobs:
    code += '{const unsigned char b[]={' + ','.join(hex(b) for b in blob) + '};memcpy((void*)'+hex(address)+',b,sizeof(b));}\n'
# Preserve the caller LR because the native renderer uses LR as its cache pointer.
prefix = [0xe24dd098,0xe58de094,0xe58d0090]
suffix = [0xe59de094,0xe28dd098,0xe12fff1e]
import struct
wrapper = struct.pack('<3I',*prefix)+release+struct.pack('<3I',*suffix)
code += '{const unsigned char b[]={' + ','.join(hex(b) for b in wrapper) + '};memcpy((void*)0x144700,b,sizeof(b));}\n'
code += r'''
 __builtin___clear_cache((char*)0xeb000,(char*)0xec000);
 __builtin___clear_cache((char*)0x13a000,(char*)0x13d000);
 __builtin___clear_cache((char*)0x144000,(char*)0x145000);
 put(0x227080c,1);memcpy((void*)0x2251f7c,identity,8);put(0x2251f84,1);put(0x2251f88,0x20000000);
 put(0x20000004,4);memcpy((void*)0x2000002c,identity,8);
 put(0x20000024,1);put(0x20000e44,0x20002000);put(0x20000e48,128);
 memset((void*)0x20002000,0x5a,128);
 ((void(*)(void*))(uintptr_t)0x144700)((void*)0x20000000);
 assert(word(0x20000004)==0&&word(0x20000024)==1&&word(0x20000e44)==0x20002000);
 stock_music_get=get_music;stock_ex_wave_clear=(void(*)(unsigned,unsigned))(uintptr_t)0x13a7f4;
 ex_wave_clear_for_caller(0,1,0xa24c8);
 ((unsigned(*)(unsigned))(uintptr_t)0xeb9e0)(0);
 if(word(0x227080c)!=1){fprintf(stderr,"RED native renderer released borrow, then retained wave blackout occurred despite intact payload\n");return 1;}
 assert(word(0x222c6b0)==0&&word(0x20000e44)==0x20002000&&*(unsigned char*)0x20002000==0x5a);
 ex_wave_clear_for_caller(0,1,0xdead);
 ((unsigned(*)(unsigned))(uintptr_t)0xeb9e0)(0);
 assert(word(0x227080c)==0);
 puts("PASS actual ARM borrow release + native clear/blackout: retained current payload remains visible; unqualified clear stays stock");
 return 0;
}
'''
(out/'native-borrow.c').write_text(code)
binary = out/'native-borrow-arm'
subprocess.run([str(args.zig.resolve()),'cc','-target','arm-linux-gnueabi.2.13','-mcpu=cortex_a9','-O2','-std=c11','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(out/'native-borrow.c'),'-o',str(binary)],check=True)
def linux(path):
    return '/mnt/'+path.drive[0].lower()+path.as_posix()[2:] if os.name=='nt' else str(path)
prefix = ['wsl.exe','-d','Ubuntu','--'] if os.name=='nt' else []
result = subprocess.run(prefix+['qemu-arm','-cpu','cortex-a9','-L','/usr/arm-linux-gnueabi',linux(binary)],capture_output=True,text=True)
print(result.stdout+result.stderr,end='')
(out/'native-borrow-proof.json').write_text(json.dumps(dict(passed=result.returncode==0,exit_code=result.returncode,stdout=result.stdout,stderr=result.stderr,source_sha256=hashlib.sha256(args.source.read_bytes()).hexdigest(),hardware_touched=False),indent=2)+'\n')
raise SystemExit(result.returncode)
