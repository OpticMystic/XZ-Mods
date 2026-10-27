/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#ifdef NDEBUG
#error RAM acceptance requires active assertions
#endif
#include "file.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static int fault, stage, source;
static uint32_t copied, cancel_at;
static int make_file(char *name){stage=1;return fault==1?-1:mkstemp(name);}
static int allocate_file(int fd,off_t start,off_t bytes){stage=2;return fault==2?ENOSPC:posix_fallocate(fd,start,bytes);}
static void *map_file(void *addr,size_t n,int prot,int flags,int fd,off_t off){stage=3;return fault==3?MAP_FAILED:mmap(addr,n,prot,flags,fd,off);}
static int pin_file(const void *addr,size_t n){stage=4;return fault==4?-1:mlock(addr,n);}
static int protect_file(void *addr,size_t n,int prot){stage=5;return fault==5?-1:mprotect(addr,n,prot);}
#define mkstemp make_file
#define posix_fallocate allocate_file
#define mmap map_file
#define mlock pin_file
#define mprotect protect_file
#include "file.c"
#undef mkstemp
#undef posix_fallocate
#undef mmap
#undef mlock
#undef mprotect
static int read_bytes(void *ctx,uint32_t offset,void *out,uint32_t n){
 (void)ctx;
 if(fault==6)return 0;
 if(fault==7)return -1;
 if(fault==8&&offset>128){unsigned char b=0;assert(pwrite(source,&b,1,0)==1);}
 if(n>31)n=31;
 ssize_t got;do{got=pread(source,out,n,offset);}while(got<0&&errno==EINTR);
 return (int)got;
}
static int cancel(void *ctx){(void)ctx;return copied>=cancel_at;}
static void progress(void *ctx,uint32_t n){(void)ctx;copied=n;}
int main(int argc,char **argv){
 assert(argc==2);source=open(argv[1],O_RDWR|O_CREAT|O_TRUNC,0600);assert(source>=0);
 unsigned char bytes[73729];for(unsigned i=0;i<sizeof bytes;i++)bytes[i]=(unsigned char)(i*43+7);
 struct xz_ram_copy copy={NULL,read_bytes,cancel,progress};
 for(fault=0;fault<=8;fault++){
  assert(pwrite(source,bytes,sizeof bytes,0)==sizeof bytes);
  struct xz_media_identity id;assert(xz_media_identify(source,&id));
  struct xz_ram_file f=XZ_RAM_FILE_INIT;copied=stage=0;cancel_at=UINT32_MAX;
  enum xz_ram_reason r=xz_ram_file_prepare(&f,source,&id,&copy);
  if(!fault){assert(r==XZ_RAM_OK&&copied==sizeof bytes&&!memcmp(bytes,f.mapping,sizeof bytes));}
  else assert(r!=XZ_RAM_OK);
  xz_ram_file_dispose(&f);xz_ram_file_dispose(&f);
  struct xz_ram_usage u;xz_ram_usage(&u);assert(!u.original_pending&&!u.original_held);
 }
 fault=0;
 const uint32_t points[]={0,1,31,32,32767,32768,32769,sizeof bytes-1,sizeof bytes};
 for(unsigned point=0;point<sizeof points/sizeof *points;point++){
  cancel_at=points[point];
  struct xz_media_identity id;assert(xz_media_identify(source,&id));
  struct xz_ram_file f=XZ_RAM_FILE_INIT;copied=0;
  assert(xz_ram_file_prepare(&f,source,&id,&copy)==XZ_RAM_INTERRUPTED);
  xz_ram_file_dispose(&f);
 }
 close(source);assert(!unlink(argv[1]));
 puts("PASS short reads, zero/error reads, source mutation, tmpfs/allocation/map/pin/protection failures and cancellation before allocation, at chunk boundaries and at the final byte");
}
