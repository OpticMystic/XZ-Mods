/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#define _GNU_SOURCE
#include "../overcue.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static uint64_t pending,held;
int xz_ram_assets_reserve(uint64_t n){pending+=n;return 1;}
void xz_ram_assets_publish(uint64_t n){assert(pending>=n);pending-=n;held+=n;}
void xz_ram_assets_cancel(uint64_t n){assert(pending>=n);pending-=n;}
void xz_ram_assets_release(uint64_t n){assert(held>=n);held-=n;}
int main(int argc,char **argv){
    assert(argc==2);struct xz_oc_assets a;char error[256],folder[4096],path[4096];
    assert(!xz_oc_open(argv[1],&a,error,sizeof(error)));assert(!xz_oc_bundle(argv[1],folder,sizeof(folder)));
    assert(xz_oc_assets_pin(&a)==1&&held>=xz_oc_assets_ram_bytes(&a)&&held-xz_oc_assets_ram_bytes(&a)<7u*4096u&&!pending);
    const char *names[7]={"drums","harmonics","vocal","full-mix","instrumental","vocals-drums","vocals-harmonics"};
    for(unsigned i=0;i<7;i++){
        int n=snprintf(path,sizeof(path),"%s/stems-sidecar-%s.s16le.pgz",folder,names[i]);assert(n>0&&(size_t)n<sizeof(path));assert(!unlink(path));
    }
    assert(!unlink(argv[1]));
    for(unsigned mask=1;mask<=7;mask++)assert(!xz_oc_verify(xz_oc_mix_file(&a,mask)));
    int16_t samples[16];assert(!xz_oc_read(&a.role[0],32766,8,samples));
    for(int i=0;i<5;i++){assert(samples[i*2]==(32766+i)%1000&&samples[i*2+1]==-samples[i*2]);}
    assert(a.wave_count[0]==5&&a.wave[0][4]==31);xz_oc_close(&a);assert(!pending&&!held);
    puts("PASS actual seven compressed stem mixes and wave bins survive source deletion, complete hashes and EOF; all RAM retired");
}
