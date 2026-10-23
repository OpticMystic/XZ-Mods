/* Actual runtime facade + public export with recording service seams. No OS entry point runs. */
#define _POSIX_C_SOURCE 200809L
#include "../runtime.c"
#include <assert.h>
#include <stdio.h>
#include <dlfcn.h>
static struct xz_net_view fixture;
static enum xz_net_result read_result;
static unsigned reads,os_calls;
enum xz_net_result xz_net_service_try_read(struct xz_net_service *s,struct xz_net_view *v){assert(s==(struct xz_net_service *)1);reads++;if(read_result)return read_result;*v=fixture;return XZ_NET_OK;}
enum xz_net_result xz_net_service_submit(struct xz_net_service *s,const struct xz_net_command *c){(void)s;(void)c;os_calls++;return XZ_NET_INVALID;}
enum xz_net_result xz_net_service_start(struct xz_net_service **s,const struct xz_net_backend *b){(void)s;(void)b;os_calls++;return XZ_NET_INVALID;}
enum xz_net_result xz_net_service_stop(struct xz_net_service *s,uint64_t d){(void)s;(void)d;os_calls++;return XZ_NET_INVALID;}
enum xz_net_result xz_net_linux_create(struct xz_net_linux **l,struct xz_net_backend *b,const struct xz_net_linux_io *io){(void)l;(void)b;(void)io;os_calls++;return XZ_NET_INVALID;}
void xz_net_linux_destroy(struct xz_net_linux *l){(void)l;os_calls++;}
enum xz_net_result xz_net_linux_os_create(struct xz_net_linux_os **o,struct xz_net_linux_io *io,const struct xz_net_linux_config *c){(void)o;(void)io;(void)c;os_calls++;return XZ_NET_INVALID;}
void xz_net_linux_os_destroy(struct xz_net_linux_os *o){(void)o;os_calls++;}
static void zero(const char out[16]){for(unsigned i=0;i<16;i++)assert(!out[i]);}
int main(void){
    int (*address)(char[16])=(int (*)(char[16]))dlsym(RTLD_DEFAULT,"xz_mods_network_address_v1");assert(address);char out[16];memset(out,'x',16);assert(!address(out));zero(out);assert(!address(NULL));
    service=(struct xz_net_service *)1;fixture.phase=XZ_NET_TRIAL_READY;fixture.applied_known=fixture.advertised_known=1;fixture.advertised.ip=(struct xz_net_ipv4){{192,0,2,50}};strcpy(fixture.advertised_adapter.name,"fixture0");
    assert(address(out)==1&&!strcmp(out,"192.0.2.50")&&!out[15]);
    fixture.applied_known=0;memset(out,'x',16);assert(!address(out));zero(out);fixture.applied_known=1;
    fixture.advertised_known=0;assert(!address(out));zero(out);fixture.advertised_known=1;
    fixture.phase=XZ_NET_VERIFYING;assert(!address(out));zero(out);fixture.phase=XZ_NET_TRIAL_READY;
    read_result=XZ_NET_BUSY;memset(out,'x',16);assert(!address(out));zero(out);read_result=XZ_NET_OK;
    pthread_mutex_lock(&guard);unsigned before=reads;memset(out,'x',16);assert(!address(out));zero(out);assert(reads==before);pthread_mutex_unlock(&guard);
    assert(!os_calls);service=NULL;
    puts("PASS exported address v1 ABI: dynamic visibility, canonical success, 16-byte clearing, unknown/unverified/busy fallback, nonblocking locks, zero OS/config calls");
}
