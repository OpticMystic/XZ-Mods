/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "pin.h"
static uint64_t pending,held;static int allow=1;
int xz_ram_assets_reserve(uint64_t n){if(!allow)return 0;pending+=n;return 1;}
void xz_ram_assets_publish(uint64_t n){assert(pending>=n);pending-=n;held+=n;}
void xz_ram_assets_cancel(uint64_t n){assert(pending>=n);pending-=n;}
void xz_ram_assets_release(uint64_t n){assert(held>=n);held-=n;}
int main(void){
    FILE *files[7];char names[7][96];unsigned char bytes[70000],readback[70000];struct stat old[7];struct xz_ram_file_pin pins[7]={{0}};
    for(unsigned i=0;i<7;i++){
        snprintf(names[i],sizeof(names[i]),"/tmp/xz-ram-pin-%ld-%u",(long)getpid(),i);
        files[i]=fopen(names[i],"w+b");assert(files[i]);
        for(size_t j=0;j<sizeof(bytes);j++)bytes[j]=(unsigned char)(j*43+i);
        assert(fwrite(bytes,1,sizeof(bytes),files[i])==sizeof(bytes)&&!fflush(files[i]));
        assert(!fseek(files[i],1234,SEEK_SET)&&!fstat(fileno(files[i]),&old[i]));
    }
    allow=0;assert(!xz_ram_files_pin(files,pins,7));assert(!pending&&!held);
    for(unsigned i=0;i<7;i++){struct stat now;assert(!fstat(fileno(files[i]),&now));assert(now.st_dev==old[i].st_dev&&now.st_ino==old[i].st_ino);}
    allow=1;assert(xz_ram_files_pin(files,pins,7)==1&&!pending&&held==7*73728u);
    for(unsigned i=0;i<7;i++){
        assert(ftell(files[i])==1234);assert(!unlink(names[i]));assert(!fseek(files[i],0,SEEK_SET));
        assert(fread(readback,1,sizeof(readback),files[i])==sizeof(readback));
        for(size_t j=0;j<sizeof(bytes);j++)assert(readback[j]==(unsigned char)(j*43+i));
        assert(!fseek(files[i],69666,SEEK_SET)&&fread(readback,1,1024,files[i])==334);
        assert(!fclose(files[i]));xz_ram_file_unpin(&pins[i]);
    }
    assert(!pending&&!held);puts("PASS seven RAM-backed stem files, budget rejection, preserved cursors, source deletion, seek/EOF and full retirement");
}
