/* Real service + real Linux transaction logic, recording OS boundary. No OS network calls. */
#define _POSIX_C_SOURCE 200809L
#include "../linux.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/syscall.h>
#ifdef NDEBUG
#error Assertions required
#endif
struct fake {
    struct xz_net_snapshot state;
    uint64_t ms;
    int adds,deletes,routes,child,starts,stops,polls,route_exists,fail_route,fail_delete,change_native,no_lease,hold,fail_inventory,ack_timeout,renew;
    struct xz_net_ipv4 gateway;
    unsigned metric;
};
int __real_clock_gettime(clockid_t,struct timespec *);
int __wrap_clock_gettime(clockid_t clock,struct timespec *out){assert(clock==CLOCK_MONOTONIC);return __real_clock_gettime(clock,out);}
long __real_syscall(long,...);
long __wrap_syscall(long number,...){assert(number==SYS_clock_gettime);va_list ap;va_start(ap,number);int clock=va_arg(ap,int);void *out=va_arg(ap,void *);va_end(ap);assert(clock==CLOCK_MONOTONIC);return __real_syscall(number,clock,out);}
static uint64_t clock_ms(void *p){return __atomic_load_n(&((struct fake *)p)->ms,__ATOMIC_ACQUIRE);}
static enum xz_net_result inventory(void *p,const struct xz_net_control *c,struct xz_net_snapshot *out,struct xz_net_error *e){
    (void)c;(void)e;struct fake *f=p;if(__atomic_exchange_n(&f->fail_inventory,0,__ATOMIC_ACQ_REL))return XZ_NET_NEEDS_BACKEND;
    *out=f->state;out->adapter[0].owned_dhcp_enabled=f->child?XZ_NET_YES:XZ_NET_NO;return XZ_NET_OK;
}
static enum xz_net_result address(void *p,int add,const struct xz_net_identity *id,const struct xz_net_address *a,const struct xz_net_control *c){
    (void)c;struct fake *f=p;assert(!strcmp(id->name,"fixture0"));struct xz_net_adapter *s=&f->state.adapter[0];
    if(add){for(unsigned i=0;i<s->address_count;i++)if(!memcmp(s->address[i].ip.octet,a->ip.octet,4))return XZ_NET_CONFLICT;assert(s->address_count<4);s->address[s->address_count++]=*a;f->adds++;if(f->change_native)s->address[0].ip.octet[3]=77;if(f->ack_timeout)return XZ_NET_TIMEOUT;}
    else{if(f->fail_delete)return XZ_NET_VERIFY_FAILED;int found=0;for(unsigned i=0;i<s->address_count;i++)if(s->address[i].prefix==a->prefix&&!memcmp(s->address[i].ip.octet,a->ip.octet,4)){memmove(s->address+i,s->address+i+1,(s->address_count-i-1)*sizeof *s->address);memset(&s->address[--s->address_count],0,sizeof *s->address);found=1;break;}assert(found);f->deletes++;}return XZ_NET_OK;
}
static enum xz_net_result route(void *p,int add,const struct xz_net_identity *id,struct xz_net_ipv4 gateway,unsigned metric,const struct xz_net_control *c){
    (void)id;(void)c;struct fake *f=p;if(f->fail_route)return XZ_NET_VERIFY_FAILED;f->routes++;f->route_exists=add;f->gateway=gateway;f->metric=metric;return XZ_NET_OK;
}
static int route_present(void *p,const struct xz_net_identity *id,struct xz_net_ipv4 gateway,unsigned metric){(void)id;struct fake *f=p;return f->route_exists&&metric==f->metric&&!memcmp(gateway.octet,f->gateway.octet,4);}
static enum xz_net_result start(void *p,const struct xz_net_identity *id,uint64_t *token,const struct xz_net_control *c){(void)c;assert(!strcmp(id->name,"fixture0"));struct fake *f=p;assert(!f->child);f->child=1;f->starts++;*token=123;return XZ_NET_OK;}
static enum xz_net_result poll_client(void *p,uint64_t token,struct xz_net_lease *lease,unsigned wait,const struct xz_net_control *c){
    (void)c;struct fake *f=p;assert(token==123);f->polls++;
    if(__atomic_load_n(&f->hold,__ATOMIC_ACQUIRE)){struct timespec t={0,1000000};nanosleep(&t,NULL);return XZ_NET_PENDING;}
    __atomic_add_fetch(&f->ms,wait?wait:1,__ATOMIC_RELAXED);
    int renew=__atomic_exchange_n(&f->renew,0,__ATOMIC_ACQ_REL);
    if(!renew&&(f->no_lease||f->polls>1))return XZ_NET_PENDING;
    memset(lease,0,sizeof *lease);lease->kind=renew?XZ_NET_LEASE_RENEW:XZ_NET_LEASE_BOUND;strcpy(lease->interface,"fixture0");lease->address.prefix=24;assert(!xz_net_parse_ipv4(renew?"192.0.2.81":"192.0.2.80",&lease->address.ip));lease->seconds=3600;return XZ_NET_OK;
}
static enum xz_net_result stop(void *p,uint64_t token,const struct xz_net_control *c){(void)c;struct fake *f=p;assert(token==123);f->child=0;f->stops++;return XZ_NET_OK;}
static void init(struct fake *f){
    memset(f,0,sizeof *f);f->ms=100;f->state.count=2;
    for(unsigned i=0;i<2;i++){
        struct xz_net_adapter *a=&f->state.adapter[i];snprintf(a->id.name,sizeof a->id.name,"fixture%u",i);a->id.ifindex=i+7;
        a->admin_up=a->link_up=a->addresses_known=a->gateway_known=XZ_NET_YES;a->caps=XZ_NET_CAP_STATIC_ADD|XZ_NET_CAP_GATEWAY|XZ_NET_CAP_DHCP_START|XZ_NET_CAP_DHCP_STOP|XZ_NET_CAP_RESTORE|XZ_NET_CAP_BINDING;
        a->address_count=1;assert(!xz_net_parse_ipv4("192.0.2.10",&a->address[0].ip));a->address[0].ip.octet[3]=(uint8_t)(10+i);a->address[0].prefix=24;
    }
}
static struct xz_net_view wait_request(struct xz_net_service *s,uint64_t request){
    struct xz_net_view v;for(unsigned i=0;i<3000;i++){
        if(xz_net_service_try_read(s,&v)==XZ_NET_OK&&v.request_id==request&&v.phase!=XZ_NET_DETECTING&&v.phase!=XZ_NET_PREFLIGHT&&v.phase!=XZ_NET_APPLYING&&v.phase!=XZ_NET_VERIFYING&&v.phase!=XZ_NET_RESTORING)return v;
        struct timespec t={0,1000000};nanosleep(&t,NULL);
    }assert(!"worker did not finish");memset(&v,0,sizeof v);return v;
}
static void submit(struct xz_net_service *s,struct xz_net_command *c){for(unsigned i=0;i<1000;i++){enum xz_net_result r=xz_net_service_submit(s,c);if(r==XZ_NET_OK)return;assert(r==XZ_NET_BUSY);struct timespec t={0,1000000};nanosleep(&t,NULL);}assert(0);}
static struct xz_net_trial trial(struct xz_net_view *v,enum xz_net_mode mode){
    struct xz_net_trial t={0};t.policy.schema=1;t.policy.count=v->actual.count;
    for(unsigned i=0;i<t.policy.count;i++)t.policy.order[i].adapter=v->actual.adapter[i].id;
    t.selected=v->actual.adapter[0].id;t.expected_generation=v->actual.generation;t.expected_state_revision=v->actual.adapter[0].state_revision;
    t.policy.order[0].mode=mode;t.policy.order[0].static_ip.address.prefix=24;assert(!xz_net_parse_ipv4("192.0.2.50",&t.policy.order[0].static_ip.address.ip));return t;
}
static void scenario(unsigned which){
    struct fake f;init(&f);if(which==1)f.fail_route=1;if(which==2)f.change_native=1;if(which==3)f.no_lease=1;if(which==4)f.state.adapter[0].caps&=~XZ_NET_CAP_DHCP_START;if(which==5)f.fail_delete=1;if(which==10)f.ack_timeout=1;
    struct xz_net_linux_io io={&f,clock_ms,inventory,address,route,route_present,start,poll_client,stop};struct xz_net_linux *linux;struct xz_net_backend b;assert(!xz_net_linux_create(&linux,&b,&io));
    struct xz_net_service *service;assert(!xz_net_service_start(&service,&b));assert(!f.adds&&!f.starts);
    struct xz_net_command c={.kind=XZ_NET_CMD_DETECT,.request_id=1};submit(service,&c);struct xz_net_view v=wait_request(service,1);assert(v.actual.count==2&&!f.adds&&!f.starts);assert(v.actual.adapter[0].dhcp_enabled==XZ_NET_UNKNOWN);
    c=(struct xz_net_command){.kind=XZ_NET_CMD_APPLY_TRIAL,.request_id=2};c.body.apply=trial(&v,which==3||which==4||which==6||which==7||which==8||which==9||which==13?XZ_NET_DHCP:XZ_NET_STATIC);
    if(which==12){struct xz_net_preference swap=c.body.apply.policy.order[0];c.body.apply.policy.order[0]=c.body.apply.policy.order[1];c.body.apply.policy.order[1]=swap;}
    if(which==1)c.body.apply.policy.order[0].static_ip.gateway_mode=XZ_NET_GATEWAY_SET,assert(!xz_net_parse_ipv4("192.0.2.1",&c.body.apply.policy.order[0].static_ip.gateway));
    if(which==7)__atomic_store_n(&f.hold,1,__ATOMIC_RELEASE);
    submit(service,&c);
    if(which==7){
        for(unsigned i=0;i<2000;i++){if(!xz_net_service_try_read(service,&v)&&v.phase==XZ_NET_APPLYING)break;struct timespec t={0,1000000};nanosleep(&t,NULL);}
        struct xz_net_command cancel={.kind=XZ_NET_CMD_CANCEL,.request_id=8,.body.trial_id=2};submit(service,&cancel);
    }
    v=wait_request(service,2);
    if(which==11){
        assert(v.applied_known);f.state.adapter[0].address_count=1;memset(&f.state.adapter[0].address[1],0,sizeof f.state.adapter[0].address[1]);__atomic_add_fetch(&f.ms,2000,__ATOMIC_RELEASE);
        for(unsigned i=0;i<2000;i++){if(!xz_net_service_try_read(service,&v)&&!v.applied_known)break;struct timespec t={0,1000000};nanosleep(&t,NULL);}assert(!v.applied_known&&f.state.adapter[0].address_count==1);
    }else if(which==13){
        assert(v.applied_known&&v.advertised.ip.octet[3]==80);__atomic_store_n(&f.renew,1,__ATOMIC_RELEASE);
        for(unsigned i=0;i<2000;i++){if(!xz_net_service_try_read(service,&v)&&v.advertised.ip.octet[3]==81)break;struct timespec t={0,1000000};nanosleep(&t,NULL);}assert(v.applied_known&&v.advertised.ip.octet[3]==81&&f.adds==2&&f.deletes==1);
        c=(struct xz_net_command){.kind=XZ_NET_CMD_REVERT,.request_id=3,.body.trial_id=2};submit(service,&c);v=wait_request(service,3);assert(v.result==XZ_NET_ROLLED_BACK&&f.deletes==2&&f.stops==1);
    }else if(which==10){
        assert(v.result==XZ_NET_ROLLBACK_FAILED&&v.rollback_available&&f.adds==1&&!f.deletes&&f.state.adapter[0].address_count==2);
        f.state.adapter[0].address_count=1;memset(&f.state.adapter[0].address[1],0,sizeof f.state.adapter[0].address[1]);
        c=(struct xz_net_command){.kind=XZ_NET_CMD_REVERT,.request_id=3,.body.trial_id=2};submit(service,&c);v=wait_request(service,3);assert(v.result==XZ_NET_ROLLED_BACK&&!f.deletes);
    }else if(which==8||which==9){
        assert(v.result==XZ_NET_OK&&f.child&&f.starts==1);__atomic_store_n(&f.fail_inventory,1,__ATOMIC_RELEASE);
        c=(struct xz_net_command){.kind=which==8?XZ_NET_CMD_VERIFY:XZ_NET_CMD_DETECT,.request_id=3,.body.trial_id=2};submit(service,&c);v=wait_request(service,3);
        assert(!f.child&&f.stops==1&&f.state.adapter[0].address_count==1&&!v.applied_known);
    }else if(which==0||which==5||which==6||which==12){
        assert(v.result==XZ_NET_OK&&v.applied_known&&v.advertised_known&&v.rollback_available&&f.adds==1&&f.state.adapter[0].address_count==2);
        assert(f.state.adapter[0].address[0].ip.octet[3]==10);assert(v.actual.adapter[0].dhcp_enabled==XZ_NET_UNKNOWN);
        if(which==12)assert(!strcmp(v.advertised_adapter.name,"fixture1")&&v.advertised.ip.octet[3]==11);
        if(which==6)assert(f.starts==1&&v.actual.adapter[0].owned_dhcp_enabled==XZ_NET_YES);
        c=(struct xz_net_command){.kind=XZ_NET_CMD_REVERT,.request_id=3,.body.trial_id=2};submit(service,&c);v=wait_request(service,3);
        if(which==5){assert(v.result==XZ_NET_ROLLBACK_FAILED&&v.rollback_available);f.fail_delete=0;c.request_id=4;submit(service,&c);v=wait_request(service,4);}
        assert(v.result==XZ_NET_ROLLED_BACK&&f.state.adapter[0].address_count==1&&!v.applied_known);if(which==6)assert(!f.child&&f.stops==1);
    }else if(which==1){assert(v.result==XZ_NET_VERIFY_FAILED&&f.adds==1&&f.deletes==1&&f.state.adapter[0].address[0].ip.octet[3]==10);}
    else if(which==2){assert(v.result==XZ_NET_CONFLICT&&f.deletes==1&&f.state.adapter[0].address[0].ip.octet[3]==77);}
    else if(which==3){assert(v.result==XZ_NET_TIMEOUT&&f.starts==1&&f.stops==1&&!f.adds);}
    else if(which==4){assert(v.result==XZ_NET_NEEDS_DHCP_CLIENT&&!f.starts&&!f.adds);}
    else if(which==7){assert(v.result==XZ_NET_CANCELLED&&f.starts==1&&f.stops==1&&!f.adds);}
    assert(xz_net_service_stop(service,clock_ms(&f)+10000)==XZ_NET_OK);xz_net_linux_destroy(linux);
}
static void validation(void){
    struct xz_net_ipv4 ip;const char *bad[]={"","1.2.3","1.2.3.4;id","-1.2.3.4","1.2.3.256","01.2.3.4","1.2.3.4\n"," 1.2.3.4","1.2.3.4.5"};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++)assert(xz_net_parse_ipv4(bad[i],&ip)==XZ_NET_INVALID);
    assert(!xz_net_parse_ipv4("169.254.168.59",&ip));uint8_t prefix;assert(!xz_net_parse_mask("255.255.0.0",&prefix)&&prefix==16);assert(!xz_net_parse_mask("/31",&prefix)&&prefix==31);assert(!xz_net_parse_mask("32",&prefix)&&prefix==32);
    assert(xz_net_parse_mask("255.0.255.0",&prefix)==XZ_NET_INVALID);assert(xz_net_parse_mask("33",&prefix)==XZ_NET_INVALID);
    struct xz_net_identity id={.name="-bad",.ifindex=1,.identity_epoch=1};assert(xz_net_validate_identity(&id));strcpy(id.name,"a;reboot");assert(xz_net_validate_identity(&id));
    struct xz_net_lease l={.kind=XZ_NET_LEASE_BOUND,.interface="fixture0",.address={.ip={{192,0,2,55}},.prefix=24},.seconds=100};unsigned char wire[64];assert(!xz_net_lease_encode(&l,wire));struct xz_net_lease b;assert(!xz_net_lease_decode(wire,&b));wire[63]=1;assert(xz_net_lease_decode(wire,&b));
}
int main(void){validation();for(unsigned i=0;i<14;i++)scenario(i);puts("PASS real service/Linux backend: additive apply, priority advertisement, native preservation/conflict, conditional rollback failure/retry, lost ACK retained truthfully, DHCP lease/renewal/timeout/missing client, periodic stale-state invalidation, failed live VERIFY/DETECT cleanup, reserved cancellation, monotonic waits, strict boundaries");}
