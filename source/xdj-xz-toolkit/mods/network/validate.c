/* SPDX-License-Identifier: MIT */
#include "network.h"
#include <stdio.h>
#include <string.h>

int xz_net_control_expired(const struct xz_net_control *c) {
    return c->now_ms(c->context) >= c->deadline_ms || (c->cancelled && c->cancelled(c->context));
}
void xz_net_ipv4_text(struct xz_net_ipv4 ip, char out[16]) {
    snprintf(out,16,"%u.%u.%u.%u",ip.octet[0],ip.octet[1],ip.octet[2],ip.octet[3]);
}
enum xz_net_result xz_net_parse_ipv4(const char *s, struct xz_net_ipv4 *out) {
    struct xz_net_ipv4 ip={{0}};
    if(!s||!out)return XZ_NET_INVALID;
    for(unsigned i=0;i<4;i++){
        unsigned n=0,digits=0;const char *start=s;
        while(*s>='0'&&*s<='9'){
            if(++digits>3)return XZ_NET_INVALID;
            n=n*10+(unsigned)(*s++-'0');
        }
        if(!digits||n>255||(digits>1&&*start=='0'))return XZ_NET_INVALID;
        ip.octet[i]=(uint8_t)n;
        if(i<3){if(*s++!='.')return XZ_NET_INVALID;}else if(*s)return XZ_NET_INVALID;
    }
    *out=ip;return XZ_NET_OK;
}
static uint32_t number(struct xz_net_ipv4 a) {
    return (uint32_t)a.octet[0]<<24|(uint32_t)a.octet[1]<<16|(uint32_t)a.octet[2]<<8|a.octet[3];
}
enum xz_net_result xz_net_parse_mask(const char *s,uint8_t *prefix) {
    if(!s||!prefix||!*s)return XZ_NET_INVALID;
    if(!strchr(s,'.')){
        unsigned n=0,k=0;if(*s=='/')s++;
        if(!*s)return XZ_NET_INVALID;
        while(*s){if(*s<'0'||*s>'9'||++k>2)return XZ_NET_INVALID;n=n*10+(unsigned)(*s++-'0');}
        if(n>32)return XZ_NET_INVALID;*prefix=(uint8_t)n;return XZ_NET_OK;
    }
    struct xz_net_ipv4 ip;if(xz_net_parse_ipv4(s,&ip))return XZ_NET_INVALID;
    uint32_t mask=number(ip),inv=~mask;if(inv&(inv+1))return XZ_NET_INVALID;
    unsigned n=0;while(mask){n++;mask<<=1;}*prefix=(uint8_t)n;return XZ_NET_OK;
}
enum xz_net_result xz_net_validate_identity(const struct xz_net_identity *id) {
    if(!id||!id->ifindex||!id->identity_epoch)return XZ_NET_INVALID;
    unsigned n=0;for(;n<XZ_NET_IFNAME&&id->name[n];n++){
        unsigned char c=(unsigned char)id->name[n];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||(n&&(c=='-'||c=='.'))))return XZ_NET_INVALID;
    }
    return n&&n<XZ_NET_IFNAME?XZ_NET_OK:XZ_NET_INVALID;
}
int xz_net_identity_equal(const struct xz_net_identity *a,const struct xz_net_identity *b) {
    return a->ifindex==b->ifindex&&a->identity_epoch==b->identity_epoch&&
        !memcmp(a->name,b->name,XZ_NET_IFNAME)&&a->mac_known==b->mac_known&&
        (a->mac_known!=XZ_NET_YES||!memcmp(a->mac,b->mac,6));
}
static enum xz_net_result error(struct xz_net_error *e,enum xz_net_result code,const char *s) {
    if(e){e->code=code;snprintf(e->detail,sizeof e->detail,"%s",s);}return code;
}
static int host(struct xz_net_address a) {
    uint32_t ip=number(a.ip);if(!a.prefix||a.prefix>32||a.ip.octet[0]==0||a.ip.octet[0]==127||a.ip.octet[0]>=224)return 0;
    if(a.prefix<31){uint32_t tail=ip&(~0u>>a.prefix);if(!tail||tail==(~0u>>a.prefix))return 0;}return 1;
}
enum xz_net_result xz_net_validate_trial(const struct xz_net_trial *t,const struct xz_net_snapshot *s,struct xz_net_error *e) {
    if(!t||!s||s->count>XZ_NET_ADAPTERS||t->policy.schema!=1||!t->policy.count||t->policy.count>XZ_NET_ADAPTERS||t->policy.request_route>1||xz_net_validate_identity(&t->selected))return error(e,XZ_NET_INVALID,"Invalid network policy or adapter identity");
    if(s->inventory_truncated)return error(e,XZ_NET_INCOMPLETE_SNAPSHOT,"Adapter inventory is incomplete; detect again");
    if(t->expected_generation!=s->generation)return error(e,XZ_NET_STALE,"Adapters changed; detect and choose again");
    const struct xz_net_adapter *a=NULL;const struct xz_net_preference *p=NULL;
    for(unsigned i=0;i<s->count;i++)if(xz_net_identity_equal(&s->adapter[i].id,&t->selected))a=&s->adapter[i];
    if(!a)return error(e,XZ_NET_NO_ADAPTER,"Selected adapter disappeared");
    if(a->state_revision!=t->expected_state_revision)return error(e,XZ_NET_STALE,"Address, route or client changed; review again");
    if(a->addresses_known!=XZ_NET_YES||a->addresses_truncated||a->gateway_ambiguous)return error(e,XZ_NET_INCOMPLETE_SNAPSHOT,"Selected adapter state is incomplete");
    if(a->link_up==XZ_NET_NO)return error(e,XZ_NET_LINK_DOWN,"Connect this adapter, then detect again");
    for(unsigned i=0;i<t->policy.count;i++){
        const struct xz_net_preference *q=&t->policy.order[i];
        if(xz_net_validate_identity(&q->adapter)||q->mode<XZ_NET_KEEP||q->mode>XZ_NET_STATIC)return error(e,XZ_NET_INVALID,"Invalid adapter or address mode");
        int found=0;for(unsigned k=0;k<s->count;k++)if(xz_net_identity_equal(&q->adapter,&s->adapter[k].id))found=1;
        if(!found)return error(e,XZ_NET_STALE,"A preferred adapter is no longer present");
        if(!xz_net_identity_equal(&q->adapter,&t->selected)&&q->mode!=XZ_NET_KEEP)return error(e,XZ_NET_INVALID,"Only the reviewed adapter can change in this trial");
        for(unsigned j=0;j<i;j++)if(xz_net_identity_equal(&q->adapter,&t->policy.order[j].adapter))return error(e,XZ_NET_INVALID,"Adapter order contains a duplicate");
        if(xz_net_identity_equal(&q->adapter,&t->selected))p=q;
    }
    if(!p)return error(e,XZ_NET_INVALID,"Selected adapter is absent from priority order");
    if(p->mode==XZ_NET_DHCP){
        if(a->competing_client)return error(e,XZ_NET_CONFLICT,"Another visible DHCP client owns this adapter; it was left running");
        if(!(a->caps&XZ_NET_CAP_DHCP_START))return error(e,XZ_NET_NEEDS_DHCP_CLIENT,a->detail[0]?a->detail:"Install a compatible udhcpc and private lease hook in the RAM loader");
    }
    if(p->mode==XZ_NET_STATIC){
        if(!host(p->static_ip.address)||p->static_ip.gateway_mode<XZ_NET_GATEWAY_KEEP||p->static_ip.gateway_mode>XZ_NET_GATEWAY_SET||p->static_ip.dhcp<XZ_NET_STATIC_KEEP_DHCP||p->static_ip.dhcp>XZ_NET_STATIC_DISABLE_DHCP)return error(e,XZ_NET_INVALID,"Enter a unicast host IPv4 and valid contiguous prefix");
        if(!(a->caps&XZ_NET_CAP_STATIC_ADD))return error(e,XZ_NET_NEEDS_BACKEND,"Additive Linux address support is required; existing address was kept");
        if(p->static_ip.gateway_mode==XZ_NET_GATEWAY_SET){
            struct xz_net_address g={.ip=p->static_ip.gateway,.prefix=p->static_ip.address.prefix};
            uint32_t mask=~0u<<(32-g.prefix);
            if(!host(g)||number(g.ip)==number(p->static_ip.address.ip)||(number(g.ip)&mask)!=(number(p->static_ip.address.ip)&mask))return error(e,XZ_NET_INVALID,"Gateway must be another host in the selected subnet");
            if(!(a->caps&XZ_NET_CAP_GATEWAY))return error(e,XZ_NET_NEEDS_BACKEND,"Safe additive route support is required");
        }
    }
    return error(e,XZ_NET_OK,"");
}
const char *xz_net_result_text(enum xz_net_result r) {
    switch(r){
    case XZ_NET_OK:return "Local state verified";case XZ_NET_BUSY:return "Network worker is busy";
    case XZ_NET_PENDING:return "Network worker is running";case XZ_NET_INVALID:return "Check IPv4, mask and gateway";
    case XZ_NET_STALE:return "Network changed; detect again";case XZ_NET_NEEDS_DETECT:return "Detect adapters first";
    case XZ_NET_NO_ADAPTER:return "No usable adapter reported";case XZ_NET_LINK_DOWN:return "Connect the selected adapter";
    case XZ_NET_INCOMPLETE_SNAPSHOT:return "Network inventory is incomplete";
    case XZ_NET_NEEDS_DHCP_CLIENT:return "Set up the DHCP client in this RAM loader";
    case XZ_NET_UNSAFE_CLIENT:return "This client does not support the private lease contract";
    case XZ_NET_TIMEOUT:return "Network operation timed out";case XZ_NET_CONFLICT:return "External network change detected; native state kept";
    case XZ_NET_ROLLED_BACK:return "Trial failed; owned changes were restored";
    case XZ_NET_ROLLBACK_FAILED:return "Restoration incomplete; inspect actual addresses";
    case XZ_NET_CANCELLED:return "Trial cancelled";case XZ_NET_VERIFY_FAILED:return "Readback did not match the request";
    default:return "Set up network support, then detect again";
    }
}
