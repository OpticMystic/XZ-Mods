/* Run only after unshare --user --map-root-user --net. Real additive OS readback/revert. */
#define _POSIX_C_SOURCE 200809L
#include "../linux.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
static int contains(struct xz_net_adapter *a,unsigned last){for(unsigned i=0;i<a->address_count;i++)if(a->address[i].ip.octet[0]==192&&a->address[i].ip.octet[1]==0&&a->address[i].ip.octet[2]==2&&a->address[i].ip.octet[3]==last)return 1;return 0;}
int main(int argc,char **argv){
    assert(argc==2);struct stat own;assert(!stat("/proc/self/ns/net",&own));assert((unsigned long long)own.st_ino!=strtoull(argv[1],NULL,10));
    struct xz_net_linux_os *os;struct xz_net_linux_io io;struct xz_net_linux_config cfg={.client_path="/missing-private-client",.event_path="/missing-private-event"};assert(!xz_net_linux_os_create(&os,&io,&cfg));
    struct xz_net_linux *linux;struct xz_net_backend b;assert(!xz_net_linux_create(&linux,&b,&io));struct xz_net_control c={b.context,b.now_ms(b.context)+5000,b.now_ms,NULL};struct xz_net_snapshot s;struct xz_net_error error={0};assert(!b.detect(b.context,&c,&s,&error));
    assert(s.count==1&&!strcmp(s.adapter[0].id.name,"fixture0")&&contains(&s.adapter[0],10));struct xz_net_trial t={0};t.policy.schema=1;t.policy.count=1;t.policy.order[0].adapter=t.selected=s.adapter[0].id;t.policy.order[0].mode=XZ_NET_STATIC;t.expected_generation=s.generation;t.expected_state_revision=s.adapter[0].state_revision;
    assert(!xz_net_parse_ipv4("192.0.2.50",&t.policy.order[0].static_ip.address.ip));t.policy.order[0].static_ip.address.prefix=24;
    t.policy.order[0].static_ip.gateway_mode=XZ_NET_GATEWAY_SET;assert(!xz_net_parse_ipv4("192.0.2.1",&t.policy.order[0].static_ip.gateway));
    struct xz_net_backend_ticket *ticket;assert(!b.prepare(b.context,&t,&c,&ticket,&error));assert(!b.apply(b.context,ticket,&c,&error));assert(!b.verify(b.context,ticket,&c,&s,&error));assert(contains(&s.adapter[0],10)&&contains(&s.adapter[0],50));
    assert(!b.restore(b.context,ticket,&c,&s,&error));assert(contains(&s.adapter[0],10)&&!contains(&s.adapter[0],50));b.release(b.context,ticket);xz_net_linux_destroy(linux);xz_net_linux_os_destroy(os);puts("PASS isolated netns: actual Linux add IPv4/default route, preserve original, readback, conditional revert");
}
