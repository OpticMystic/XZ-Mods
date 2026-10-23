/* SPDX-License-Identifier: MIT. Transaction logic; all OS calls use the injected boundary. */
#include "linux.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct xz_net_linux {
    struct xz_net_linux_io io;
    struct xz_net_snapshot last;
    uint64_t epoch,generation;
};
struct xz_net_backend_ticket {
    struct xz_net_trial trial;
    struct xz_net_adapter before;
    struct xz_net_address address;
    struct xz_net_identity advertised_adapter;
    struct xz_net_address advertised;
    struct xz_net_address owned_address[XZ_NET_ADDRESSES];
    unsigned owned_count;
    int advertisement_pinned;
    struct xz_net_ipv4 gateway;
    uint64_t client,expires,checked_at;
    unsigned metric;
    int created_address,created_route,client_started;
    int uncertain_address,uncertain_route;
};
static enum xz_net_result err(struct xz_net_error *e,enum xz_net_result r,const char *s) {
    if(e){e->code=r;snprintf(e->detail,sizeof e->detail,"%s",s);}return r;
}
static int ip_equal(struct xz_net_ipv4 a,struct xz_net_ipv4 b){return !memcmp(a.octet,b.octet,4);}
static int addr_equal(struct xz_net_address a,struct xz_net_address b){return a.prefix==b.prefix&&ip_equal(a.ip,b.ip);}
static const struct xz_net_preference *preference(const struct xz_net_backend_ticket *t){
    for(unsigned i=0;i<t->trial.policy.count;i++)if(xz_net_identity_equal(&t->trial.policy.order[i].adapter,&t->trial.selected))return &t->trial.policy.order[i];return NULL;
}
static struct xz_net_adapter *adapter(struct xz_net_snapshot *s,const struct xz_net_identity *id){
    for(unsigned i=0;i<s->count;i++)if(xz_net_identity_equal(&s->adapter[i].id,id))return &s->adapter[i];return NULL;
}
static int has_address(const struct xz_net_adapter *a,struct xz_net_address ip){
    if(a)for(unsigned i=0;i<a->address_count;i++)if(addr_equal(a->address[i],ip))return 1;return 0;
}
static int ours(const struct xz_net_backend_ticket *t,struct xz_net_address ip){for(unsigned i=0;i<t->owned_count;i++)if(addr_equal(t->owned_address[i],ip))return 1;return 0;}
static uint64_t hash(const void *p,size_t n){const unsigned char *b=p;uint64_t h=1469598103934665603ULL;while(n--)h=(h^*b++)*1099511628211ULL;return h?h:1;}
static uint64_t os_now(void *p){struct xz_net_linux *l=p;return l->io.now_ms(l->io.context);}
static enum xz_net_result detect(void *p,const struct xz_net_control *c,struct xz_net_snapshot *out,struct xz_net_error *e){
    struct xz_net_linux *l=p;struct xz_net_snapshot s={0};
    if(xz_net_control_expired(c))return err(e,c->cancelled&&c->cancelled(c->context)?XZ_NET_CANCELLED:XZ_NET_TIMEOUT,"Detection cancelled or deadline expired");
    enum xz_net_result r=l->io.inventory(l->io.context,c,&s,e);if(r)return r;
    if(s.count>XZ_NET_ADAPTERS)return err(e,XZ_NET_INVALID,"OS returned too many adapters");
    for(unsigned i=0;i<s.count;i++){
        struct xz_net_adapter *a=&s.adapter[i];uint64_t epoch=0;
        for(unsigned j=0;j<l->last.count;j++){
            const struct xz_net_identity *old=&l->last.adapter[j].id;
            if(a->id.ifindex==old->ifindex&&!memcmp(a->id.name,old->name,16)&&a->id.mac_known==old->mac_known&&(old->mac_known!=XZ_NET_YES||!memcmp(a->id.mac,old->mac,6)))epoch=old->identity_epoch;
        }
        a->id.identity_epoch=epoch?epoch:++l->epoch;
        if(xz_net_validate_identity(&a->id)||a->address_count>XZ_NET_ADDRESSES)return err(e,XZ_NET_INVALID,"OS returned an invalid interface identity or address count");
        a->state_revision=0;a->state_revision=hash(a,sizeof *a);
    }
    s.generation=0;s.observed_ms=0;struct xz_net_snapshot old=l->last;old.generation=0;old.observed_ms=0;
    if(memcmp(&s,&old,sizeof s))l->generation++;
    if(!l->generation)l->generation=1;
    s.generation=l->generation;s.observed_ms=os_now(l);l->last=s;*out=s;return XZ_NET_OK;
}
static enum xz_net_result baseline(struct xz_net_linux *l,struct xz_net_backend_ticket *t,const struct xz_net_control *c,struct xz_net_snapshot *s,struct xz_net_error *e){
    enum xz_net_result r=detect(l,c,s,e);if(r)return r;struct xz_net_adapter *a=adapter(s,&t->trial.selected);
    if(!a||a->addresses_truncated||a->link_up==XZ_NET_NO)return err(e,XZ_NET_CONFLICT,"Selected adapter changed or lost its link");
    for(unsigned i=0;i<t->before.address_count;i++)if(!has_address(a,t->before.address[i]))return err(e,XZ_NET_CONFLICT,"A native address changed; it was left untouched");
    for(unsigned i=0;i<a->address_count;i++)if(!has_address(&t->before,a->address[i])&&!ours(t,a->address[i]))return err(e,XZ_NET_CONFLICT,"An external address appeared; trial paused");
    if(t->before.gateway_present&&(!a->gateway_present||!ip_equal(t->before.gateway,a->gateway)))return err(e,XZ_NET_CONFLICT,"Native gateway changed; it was left untouched");
    return XZ_NET_OK;
}
static enum xz_net_result prepare(void *p,const struct xz_net_trial *request,const struct xz_net_control *c,struct xz_net_backend_ticket **out,struct xz_net_error *e){
    struct xz_net_linux *l=p;struct xz_net_snapshot s;enum xz_net_result r=detect(l,c,&s,e);if(r)return r;
    r=xz_net_validate_trial(request,&s,e);if(r)return r;
    struct xz_net_backend_ticket *t=calloc(1,sizeof *t);if(!t)return err(e,XZ_NET_NEEDS_BACKEND,"Network journal allocation failed");
    t->trial=*request;t->before=*adapter(&s,&request->selected);t->metric=42760;
    for(unsigned i=0;i<request->policy.count;i++)if(xz_net_identity_equal(&request->policy.order[i].adapter,&request->selected))t->metric+=i;
    *out=t;return XZ_NET_OK;
}
static enum xz_net_result add_address(struct xz_net_linux *l,struct xz_net_backend_ticket *t,struct xz_net_address ip,const struct xz_net_control *c,struct xz_net_error *e){
    struct xz_net_snapshot s;enum xz_net_result r=baseline(l,t,c,&s,e);if(r)return r;
    struct xz_net_adapter *a=adapter(&s,&t->trial.selected);
    for(unsigned i=0;i<a->address_count;i++)if(ip_equal(a->address[i].ip,ip.ip)&&!addr_equal(a->address[i],ip))return err(e,XZ_NET_CONFLICT,"This IP already exists with a different prefix");
    t->address=ip;
    if(has_address(a,ip))return XZ_NET_OK;
    if(a->address_count==XZ_NET_ADDRESSES||t->owned_count==XZ_NET_ADDRESSES)return err(e,XZ_NET_INCOMPLETE_SNAPSHOT,"Address or journal limit reached; existing addresses were kept");
    if(xz_net_control_expired(c))return err(e,XZ_NET_CANCELLED,"Apply cancelled before address creation");
    r=l->io.address(l->io.context,1,&t->trial.selected,&ip,c);
    if(r){if(r==XZ_NET_TIMEOUT)t->uncertain_address=1;return err(e,r,"Exclusive additive address operation failed or was not acknowledged");}
    t->owned_address[t->owned_count++]=ip;t->created_address=1;return XZ_NET_OK;
}
static enum xz_net_result add_route(struct xz_net_linux *l,struct xz_net_backend_ticket *t,struct xz_net_ipv4 gateway,const struct xz_net_control *c,struct xz_net_error *e){
    struct xz_net_snapshot s;enum xz_net_result r=baseline(l,t,c,&s,e);if(r)return r;
    if(xz_net_control_expired(c))return err(e,XZ_NET_CANCELLED,"Apply cancelled before route creation");
    if(l->io.route_present(l->io.context,&t->trial.selected,gateway,t->metric))return err(e,XZ_NET_CONFLICT,"Requested MODS route metric already exists");
    t->gateway=gateway;r=l->io.route(l->io.context,1,&t->trial.selected,gateway,t->metric,c);
    if(r){if(r==XZ_NET_TIMEOUT)t->uncertain_route=1;return err(e,r,"Additive gateway operation failed or was not acknowledged");}t->created_route=1;return XZ_NET_OK;
}
static enum xz_net_result accept_lease(struct xz_net_linux *l,struct xz_net_backend_ticket *t,const struct xz_net_lease *lease,const struct xz_net_control *c,struct xz_net_error *e){
    if(strcmp(lease->interface,t->trial.selected.name)||lease->kind<XZ_NET_LEASE_BOUND||lease->kind>XZ_NET_LEASE_RENEW)return err(e,XZ_NET_INVALID,"DHCP report does not match the selected adapter");
    unsigned char wire[64];struct xz_net_lease checked;if(xz_net_lease_encode(lease,wire)||xz_net_lease_decode(wire,&checked))return err(e,XZ_NET_INVALID,"Invalid lease report");
    struct xz_net_snapshot s;enum xz_net_result r=baseline(l,t,c,&s,e);if(r)return r;
    struct xz_net_trial validation=t->trial;validation.expected_generation=s.generation;validation.expected_state_revision=adapter(&s,&t->trial.selected)->state_revision;
    struct xz_net_preference *q=NULL;for(unsigned i=0;i<validation.policy.count;i++)if(xz_net_identity_equal(&validation.policy.order[i].adapter,&t->trial.selected))q=&validation.policy.order[i];
    q->mode=XZ_NET_STATIC;q->static_ip.address=lease->address;q->static_ip.dhcp=XZ_NET_STATIC_KEEP_DHCP;q->static_ip.gateway_mode=validation.policy.request_route&&lease->has_gateway?XZ_NET_GATEWAY_SET:XZ_NET_GATEWAY_KEEP;q->static_ip.gateway=lease->gateway;
    r=xz_net_validate_trial(&validation,&s,e);if(r)return r;
    struct xz_net_address previous=t->address;int previous_owned=ours(t,previous);
    r=add_address(l,t,lease->address,c,e);if(r)return r;t->address.origin=XZ_NET_ORIGIN_DHCP_LEASE;
    if(t->trial.policy.request_route&&lease->has_gateway&&!t->created_route){r=add_route(l,t,lease->gateway,c,e);if(r)return r;}
    if(previous_owned&&!addr_equal(previous,lease->address)){
        struct xz_net_snapshot readback;r=detect(l,c,&readback,e);if(r)return r;
        if(!has_address(adapter(&readback,&t->trial.selected),lease->address))return err(e,XZ_NET_VERIFY_FAILED,"Renewed address failed readback");
        if(!has_address(adapter(&readback,&t->trial.selected),previous))return err(e,XZ_NET_CONFLICT,"Old owned lease changed during renewal");
        r=l->io.address(l->io.context,0,&t->trial.selected,&previous,c);if(r)return err(e,r,"Old owned lease removal failed");
        for(unsigned i=0;i<t->owned_count;i++)if(addr_equal(t->owned_address[i],previous)){memmove(t->owned_address+i,t->owned_address+i+1,(t->owned_count-i-1)*sizeof *t->owned_address);t->owned_count--;break;}
        if(t->advertisement_pinned&&xz_net_identity_equal(&t->advertised_adapter,&t->trial.selected))t->advertised=t->address;
    }
    t->expires=os_now(l)+(uint64_t)lease->seconds*1000;return XZ_NET_OK;
}
static enum xz_net_result apply(void *p,struct xz_net_backend_ticket *t,const struct xz_net_control *c,struct xz_net_error *e){
    struct xz_net_linux *l=p;const struct xz_net_preference *q=preference(t);struct xz_net_snapshot s;
    enum xz_net_result r=baseline(l,t,c,&s,e);if(r)return r;
    if(q->mode==XZ_NET_KEEP){struct xz_net_adapter *a=adapter(&s,&t->trial.selected);if(!a->address_count)return err(e,XZ_NET_NO_ADAPTER,"Selected adapter has no address to advertise");t->address=a->address[0];return XZ_NET_OK;}
    if(q->mode==XZ_NET_STATIC){
        r=add_address(l,t,q->static_ip.address,c,e);if(r)return r;if(t->created_address)t->address.origin=XZ_NET_ORIGIN_STATIC_CONFIRMED;
        if(q->static_ip.gateway_mode==XZ_NET_GATEWAY_SET)return add_route(l,t,q->static_ip.gateway,c,e);
        return XZ_NET_OK; /* OFF means no MODS child; embedded ownership remains unknown. */
    }
    if(t->before.competing_client)return err(e,XZ_NET_CONFLICT,"A competing visible DHCP client is running");
    r=l->io.client_start(l->io.context,&t->trial.selected,&t->client,c);if(r)return err(e,r,"Cannot start the configured udhcpc/event helper");t->client_started=1;
    while(!xz_net_control_expired(c)){
        struct xz_net_lease lease={0};r=l->io.client_poll(l->io.context,t->client,&lease,100,c);
        if(r==XZ_NET_PENDING)continue;if(r)return err(e,r,"Owned DHCP client failed before a lease");
        if(lease.kind==XZ_NET_LEASE_DECONFIG)continue;
        if(lease.kind==XZ_NET_LEASE_FAILURE)return err(e,XZ_NET_VERIFY_FAILED,"Owned DHCP client reported acquisition failure");
        return accept_lease(l,t,&lease,c,e);
    }
    return err(e,c->cancelled&&c->cancelled(c->context)?XZ_NET_CANCELLED:XZ_NET_TIMEOUT,"Owned DHCP acquisition stopped; baseline retained");
}
static enum xz_net_result verify(void *p,struct xz_net_backend_ticket *t,const struct xz_net_control *c,struct xz_net_snapshot *s,struct xz_net_error *e){
    struct xz_net_linux *l=p;enum xz_net_result r=baseline(l,t,c,s,e);if(r)return r;
    struct xz_net_adapter *a=adapter(s,&t->trial.selected);
    if(!has_address(a,t->address))return err(e,XZ_NET_VERIFY_FAILED,"Requested local address is absent on readback");
    if(t->created_route&&!l->io.route_present(l->io.context,&t->trial.selected,t->gateway,t->metric))return err(e,XZ_NET_CONFLICT,"Owned route changed; native routes left untouched");
    if(t->client_started&&os_now(l)>=t->expires)return err(e,XZ_NET_TIMEOUT,"Owned DHCP lease expired");
    a->owned_dhcp_enabled=t->client_started?XZ_NET_YES:XZ_NET_NO;a->dhcp_enabled=XZ_NET_UNKNOWN;
    for(unsigned i=0;i<a->address_count;i++)if(addr_equal(a->address[i],t->address))a->address[i].origin=t->address.origin;
    if(!t->advertisement_pinned){
        for(unsigned i=0;i<t->trial.policy.count;i++){
            struct xz_net_adapter *candidate=adapter(s,&t->trial.policy.order[i].adapter);
            if(!candidate||candidate->link_up==XZ_NET_NO||candidate->addresses_known!=XZ_NET_YES||!candidate->address_count)continue;
            t->advertised_adapter=candidate->id;t->advertised=xz_net_identity_equal(&candidate->id,&t->trial.selected)?t->address:candidate->address[0];t->advertisement_pinned=1;break;
        }
        if(!t->advertisement_pinned)return err(e,XZ_NET_VERIFY_FAILED,"No preferred adapter has a usable advertised address");
    }else if(!has_address(adapter(s,&t->advertised_adapter),t->advertised))return err(e,XZ_NET_CONFLICT,"Advertised address changed; review priority again");
    t->checked_at=os_now(l);return XZ_NET_OK;
}
static enum xz_net_result restore(void *p,struct xz_net_backend_ticket *t,const struct xz_net_control *c,struct xz_net_snapshot *out,struct xz_net_error *e){
    struct xz_net_linux *l=p;int conflict=0;enum xz_net_result r;
    if(t->client_started){r=l->io.client_stop(l->io.context,t->client,c);if(r)return err(e,r,"Owned DHCP child did not stop within rollback budget");t->client_started=0;}
    struct xz_net_snapshot s;r=detect(l,c,&s,e);if(r)return r;struct xz_net_adapter *a=adapter(&s,&t->trial.selected);
    if(!a)return err(e,XZ_NET_CONFLICT,"Adapter disappeared; no replacement adapter was changed");
    if(t->created_route){
        if(l->io.route_present(l->io.context,&t->trial.selected,t->gateway,t->metric)){
            r=l->io.route(l->io.context,0,&t->trial.selected,t->gateway,t->metric,c);if(r)return err(e,r,"Owned route removal failed");
        }else conflict=1;t->created_route=0;
    }
    while(t->owned_count){struct xz_net_address owned=t->owned_address[t->owned_count-1];
        if(has_address(a,owned)){r=l->io.address(l->io.context,0,&t->trial.selected,&owned,c);if(r)return err(e,r,"Owned address removal failed");}
        else conflict=1;t->owned_count--;
    }t->created_address=0;
    r=detect(l,c,out,e);if(r)return r;a=adapter(out,&t->trial.selected);
    if(!a)return err(e,XZ_NET_CONFLICT,"Adapter disappeared after restoration");
    a->owned_dhcp_enabled=XZ_NET_NO;
    if(t->uncertain_address){if(has_address(a,t->address))return err(e,XZ_NET_ROLLBACK_FAILED,"Unacknowledged address is present; ownership uncertain, kept for inspection");t->uncertain_address=0;}
    if(t->uncertain_route){if(l->io.route_present(l->io.context,&t->trial.selected,t->gateway,t->metric))return err(e,XZ_NET_ROLLBACK_FAILED,"Unacknowledged route is present; ownership uncertain, kept for inspection");t->uncertain_route=0;}
    for(unsigned i=0;i<t->before.address_count;i++)if(!has_address(a,t->before.address[i]))conflict=1;
    return conflict?err(e,XZ_NET_CONFLICT,"External state changed; conditional rollback kept native changes"):XZ_NET_OK;
}
static void describe(void *p,struct xz_net_backend_ticket *t,struct xz_net_view *v){
    (void)p;v->advertised_known=1;v->advertised_adapter=t->advertised_adapter;v->advertised=t->advertised;
    char ip[16];xz_net_ipv4_text(t->advertised.ip,ip);snprintf(v->detail,sizeof v->detail,"ADVERTISE %s %s/%u; MODS DHCP %s; native DHCP unknown",t->advertised_adapter.name,ip,t->advertised.prefix,t->client_started?"ON":"OFF");
}
static enum xz_net_result maintain(void *p,struct xz_net_backend_ticket *t,const struct xz_net_control *c,struct xz_net_view *v){
    struct xz_net_linux *l=p;struct xz_net_error periodic_error={0};
    if(!t->client_started&&v->phase!=XZ_NET_TRIAL_READY)return XZ_NET_PENDING;
    if(os_now(l)>=t->checked_at+1000){enum xz_net_result checked=verify(l,t,c,&v->actual,&periodic_error);if(checked){snprintf(v->detail,sizeof v->detail,"%s",periodic_error.detail);return checked;}describe(l,t,v);}
    if(!t->client_started)return XZ_NET_PENDING;
    struct xz_net_lease lease={0};enum xz_net_result r=l->io.client_poll(l->io.context,t->client,&lease,0,c);struct xz_net_error e={0};
    if(r==XZ_NET_OK){
        if(lease.kind==XZ_NET_LEASE_DECONFIG||lease.kind==XZ_NET_LEASE_FAILURE){snprintf(v->detail,sizeof v->detail,"Owned DHCP lease lost; baseline retained");return XZ_NET_VERIFY_FAILED;}
        r=accept_lease(l,t,&lease,c,&e);if(r){snprintf(v->detail,sizeof v->detail,"%s",e.detail);return r;}
        r=verify(l,t,c,&v->actual,&e);if(r){snprintf(v->detail,sizeof v->detail,"%s",e.detail);return r;}describe(l,t,v);return XZ_NET_OK;
    }
    if(r!=XZ_NET_PENDING){snprintf(v->detail,sizeof v->detail,"Owned DHCP client exited or sent an invalid report");return r;}
    if(os_now(l)>=t->expires){snprintf(v->detail,sizeof v->detail,"Owned DHCP lease expired; revert this trial");return XZ_NET_TIMEOUT;}return XZ_NET_PENDING;
}
static void release(void *p,struct xz_net_backend_ticket *t){(void)p;free(t);}
enum xz_net_result xz_net_linux_create(struct xz_net_linux **out,struct xz_net_backend *b,const struct xz_net_linux_io *io){
    if(!out||!b||!io||!io->now_ms||!io->inventory||!io->address||!io->route||!io->route_present||!io->client_start||!io->client_poll||!io->client_stop)return XZ_NET_INVALID;
    struct xz_net_linux *l=calloc(1,sizeof *l);if(!l)return XZ_NET_NEEDS_BACKEND;l->io=*io;
    *b=(struct xz_net_backend){.context=l,.now_ms=os_now,.detect=detect,.prepare=prepare,.apply=apply,.verify=verify,.restore=restore,.maintain=maintain,.describe=describe,.release=release};*out=l;return XZ_NET_OK;
}
void xz_net_linux_destroy(struct xz_net_linux *l){free(l);}
