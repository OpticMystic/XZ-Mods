/* SPDX-License-Identifier: MIT. Documented Linux OS boundary. Never called by host fixtures' fake inventory. */
#define _GNU_SOURCE
#include "linux.h"
#include "../clock_compat.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/if_addr.h>
#include <linux/if_packet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct xz_net_linux_os {
    char client_path[256],event_path[256];
    char client_interface[XZ_NET_IFNAME];
    int verified;
    pid_t pid;
    int pipe,lifetime;
    uint64_t token,serial;
    unsigned char partial[64];
    size_t partial_bytes;
};
static uint64_t milliseconds(void *p){(void)p;struct timespec t;if(xz_clock_gettime_compat(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000+(unsigned long)t.tv_nsec/1000000;}
static enum xz_net_result failure(struct xz_net_error *e,enum xz_net_result r,const char *text){if(e){e->code=r;snprintf(e->detail,sizeof e->detail,"%s",text);}return r;}
static int address_equal(struct xz_net_ipv4 a,struct xz_net_ipv4 b){return !memcmp(a.octet,b.octet,4);}
static int valid_identity(const struct xz_net_identity *id){
    if(xz_net_validate_identity(id)||if_nametoindex(id->name)!=id->ifindex)return 0;
    if(id->mac_known==XZ_NET_YES){
        struct ifaddrs *list=NULL;if(getifaddrs(&list))return 0;int match=0;
        for(struct ifaddrs *a=list;a;a=a->ifa_next)if(a->ifa_addr&&a->ifa_addr->sa_family==AF_PACKET&&!strcmp(a->ifa_name,id->name)){
            struct sockaddr_ll *ll=(struct sockaddr_ll *)a->ifa_addr;if(ll->sll_halen==6&&!memcmp(ll->sll_addr,id->mac,6))match=1;
        }
        freeifaddrs(list);
        if(!match)return 0;
    }return 1;
}
static int compare_adapter(const void *p,const void *q){const struct xz_net_adapter *a=p,*b=q;return strcmp(a->id.name,b->id.name);}
static int compare_address(const void *p,const void *q){const struct xz_net_address *a=p,*b=q;int n=memcmp(a->ip.octet,b->ip.octet,4);return n?n:(int)a->prefix-(int)b->prefix;}
static void competing_clients(struct xz_net_linux_os *o,struct xz_net_snapshot *s){
    DIR *d=opendir("/proc");if(!d)return;struct dirent *entry;unsigned scanned=0;
    while((entry=readdir(d))&&scanned<4096){
        char *end=NULL;long pid=strtol(entry->d_name,&end,10);if(!*entry->d_name||*end||pid<=0||pid==o->pid)continue;scanned++;
        char path[80],buf[512];snprintf(path,sizeof path,"/proc/%ld/cmdline",pid);int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)continue;
        ssize_t n=read(fd,buf,sizeof buf);close(fd);if(n<=0||n==(ssize_t)sizeof buf||buf[n-1])continue;
        const char *exe=strrchr(buf,'/');exe=exe?exe+1:buf;
        if(strcmp(exe,"udhcpc")&&strcmp(exe,"dhclient")&&strcmp(exe,"dhcpcd")&&strcmp(exe,"xz-network-udhcpc"))continue;
        if(o->pid>0){char stat_path[80],stat_text[1024];snprintf(stat_path,sizeof stat_path,"/proc/%ld/stat",pid);FILE *sf=fopen(stat_path,"r");
            if(sf){int own=0;if(fgets(stat_text,sizeof stat_text,sf)){char *tail=strrchr(stat_text,')');char state;long parent;if(tail&&sscanf(tail+1," %c %ld",&state,&parent)==2&&parent==o->pid)own=1;}fclose(sf);if(own)continue;}}
        for(size_t at=strlen(buf)+1;at<(size_t)n;at+=strlen(buf+at)+1)
            for(unsigned i=0;i<s->count;i++)if(!strcmp(buf+at,s->adapter[i].id.name))s->adapter[i].competing_client=1;
    }closedir(d);
}
static enum xz_net_result inventory(void *p,const struct xz_net_control *c,struct xz_net_snapshot *s,struct xz_net_error *e){
    struct xz_net_linux_os *o=p;struct ifaddrs *list=NULL;
    if(xz_net_control_expired(c))return failure(e,XZ_NET_TIMEOUT,"Interface inventory timed out");
    if(getifaddrs(&list))return failure(e,XZ_NET_NEEDS_BACKEND,"getifaddrs failed; documented Linux inventory required");
    for(struct ifaddrs *a=list;a;a=a->ifa_next){
        if(!a->ifa_name||(a->ifa_flags&IFF_LOOPBACK))continue;
        if(strlen(a->ifa_name)>=16){s->inventory_truncated=1;continue;}
        unsigned i=0;for(;i<s->count;i++)if(!strcmp(s->adapter[i].id.name,a->ifa_name))break;
        if(i==s->count){if(s->count==XZ_NET_ADAPTERS){s->inventory_truncated=1;continue;}
            struct xz_net_adapter *b=&s->adapter[s->count++];snprintf(b->id.name,sizeof b->id.name,"%s",a->ifa_name);b->id.ifindex=if_nametoindex(a->ifa_name);
            b->admin_up=a->ifa_flags&IFF_UP?XZ_NET_YES:XZ_NET_NO;b->link_up=a->ifa_flags&IFF_RUNNING?XZ_NET_YES:XZ_NET_NO;
            b->addresses_known=XZ_NET_YES;b->gateway_known=XZ_NET_UNKNOWN;b->owned_dhcp_enabled=o->pid>0&&!strcmp(o->client_interface,a->ifa_name)?XZ_NET_YES:XZ_NET_NO;
            if(b->owned_dhcp_enabled==XZ_NET_YES)b->dhcp_owner=XZ_NET_OWNER_MODS;
            b->caps=XZ_NET_CAP_BINDING|XZ_NET_CAP_DHCP_STOP;
            if(geteuid()==0)b->caps|=XZ_NET_CAP_STATIC_ADD|XZ_NET_CAP_GATEWAY|XZ_NET_CAP_RESTORE;
            if(!access(o->client_path,X_OK)&&!access(o->event_path,X_OK)&&o->verified)b->caps|=XZ_NET_CAP_DHCP_START;
            else{b->setup_reason=XZ_NET_NEEDS_DHCP_CLIENT;snprintf(b->detail,sizeof b->detail,"Need %.42s + %.42s; verified udhcpc FD9 contract",o->client_path,o->event_path);}
        }
        struct xz_net_adapter *b=&s->adapter[i];if(!a->ifa_addr)continue;
        if(a->ifa_addr->sa_family==AF_PACKET){struct sockaddr_ll *ll=(struct sockaddr_ll *)a->ifa_addr;if(ll->sll_halen==6){memcpy(b->id.mac,ll->sll_addr,6);b->id.mac_known=XZ_NET_YES;}}
        if(a->ifa_addr->sa_family==AF_INET){
            if(!a->ifa_netmask){b->addresses_known=XZ_NET_UNKNOWN;continue;}
            struct xz_net_address ip={0};memcpy(ip.ip.octet,&((struct sockaddr_in *)a->ifa_addr)->sin_addr,4);
            char mask[16];if(!inet_ntop(AF_INET,&((struct sockaddr_in *)a->ifa_netmask)->sin_addr,mask,sizeof mask)||xz_net_parse_mask(mask,&ip.prefix)){b->addresses_known=XZ_NET_UNKNOWN;continue;}
            int duplicate=0;for(unsigned k=0;k<b->address_count;k++)if(address_equal(b->address[k].ip,ip.ip)&&b->address[k].prefix==ip.prefix)duplicate=1;
            if(!duplicate){if(b->address_count<XZ_NET_ADDRESSES)b->address[b->address_count++]=ip;else b->addresses_truncated=1;}
        }
    }freeifaddrs(list);qsort(s->adapter,s->count,sizeof s->adapter[0],compare_adapter);
    unsigned long best[XZ_NET_ADAPTERS];for(unsigned i=0;i<s->count;i++){best[i]=~0ul;qsort(s->adapter[i].address,s->adapter[i].address_count,sizeof s->adapter[i].address[0],compare_address);}
    FILE *f=fopen("/proc/net/route","r");if(f){
        for(unsigned i=0;i<s->count;i++)s->adapter[i].gateway_known=XZ_NET_YES;
        char line[512];unsigned lines=0;
        while(fgets(line,sizeof line,f)&&++lines<4096){
            char name[16];unsigned long dst,gw,flags,metric,mask;unsigned ref,use;
            if(sscanf(line,"%15s %lx %lx %lx %u %u %lu %lx",name,&dst,&gw,&flags,&ref,&use,&metric,&mask)!=8||dst||mask||!(flags&1))continue;
            for(unsigned i=0;i<s->count;i++)if(!strcmp(s->adapter[i].id.name,name)&&metric<=best[i]){
                struct xz_net_adapter *b=&s->adapter[i];if(metric==best[i]&&b->gateway_present){b->gateway_ambiguous=1;continue;}best[i]=metric;b->gateway_ambiguous=0;b->gateway_present=1;
                for(unsigned k=0;k<4;k++)b->gateway.octet[k]=(uint8_t)(gw>>(8*k));
            }
        }if(lines>=4096)for(unsigned i=0;i<s->count;i++)s->adapter[i].gateway_ambiguous=1;fclose(f);
    }
    competing_clients(o,s);return xz_net_control_expired(c)?failure(e,XZ_NET_TIMEOUT,"Inventory exceeded its deadline"):XZ_NET_OK;
}
static int attribute(struct nlmsghdr *n,size_t cap,unsigned short type,const void *data,size_t bytes){
    size_t at=NLMSG_ALIGN(n->nlmsg_len),need=RTA_LENGTH(bytes);if(at+RTA_ALIGN(need)>cap)return -1;
    struct rtattr *r=(struct rtattr *)((char *)n+at);r->rta_type=type;r->rta_len=(unsigned short)need;memcpy(RTA_DATA(r),data,bytes);n->nlmsg_len=(unsigned)(at+RTA_ALIGN(need));return 0;
}
static enum xz_net_result netlink(struct nlmsghdr *n,const struct xz_net_control *c){
    int fd=socket(AF_NETLINK,SOCK_RAW|SOCK_CLOEXEC|SOCK_NONBLOCK,NETLINK_ROUTE);if(fd<0)return XZ_NET_NEEDS_BACKEND;
    struct sockaddr_nl local={.nl_family=AF_NETLINK},kernel={.nl_family=AF_NETLINK};
    if(bind(fd,(struct sockaddr *)&local,sizeof local)){close(fd);return XZ_NET_NEEDS_BACKEND;}
    n->nlmsg_seq=1;
    if(xz_net_control_expired(c)){close(fd);return XZ_NET_TIMEOUT;}
    if(sendto(fd,n,n->nlmsg_len,MSG_DONTWAIT,(struct sockaddr *)&kernel,sizeof kernel)!=(ssize_t)n->nlmsg_len){close(fd);return XZ_NET_VERIFY_FAILED;}
    enum xz_net_result result=XZ_NET_TIMEOUT;
    while(c->now_ms(c->context)<c->deadline_ms){
        struct pollfd p={fd,POLLIN,0};int waited=poll(&p,1,20);if(waited<0&&errno==EINTR)continue;if(waited<=0)continue;
        char data[4096];struct sockaddr_nl peer={0};socklen_t length=sizeof peer;ssize_t got=recvfrom(fd,data,sizeof data,MSG_DONTWAIT,(struct sockaddr *)&peer,&length);
        if(got<=0||peer.nl_pid)continue;
        size_t remaining=(size_t)got;char *cursor=data;
        while(remaining>=sizeof(struct nlmsghdr)){
            struct nlmsghdr *reply=(struct nlmsghdr *)cursor;
            if(reply->nlmsg_len<sizeof(*reply)||reply->nlmsg_len>remaining)break;
            if(reply->nlmsg_seq==1&&reply->nlmsg_type==NLMSG_ERROR&&reply->nlmsg_len>=NLMSG_LENGTH(sizeof(struct nlmsgerr))){
                int error=((struct nlmsgerr *)NLMSG_DATA(reply))->error;
                result=!error?XZ_NET_OK:error==-EEXIST||error==-EADDRNOTAVAIL||error==-ESRCH?XZ_NET_CONFLICT:XZ_NET_VERIFY_FAILED;goto done;
            }
            size_t step=NLMSG_ALIGN(reply->nlmsg_len);if(step>remaining)break;
            cursor+=step;remaining-=step;
        }
    }
done:close(fd);return result;
}
static enum xz_net_result address(void *p,int add,const struct xz_net_identity *id,const struct xz_net_address *ip,const struct xz_net_control *c){
    (void)p;if(!valid_identity(id))return XZ_NET_STALE;
    struct {struct nlmsghdr n;struct ifaddrmsg a;char attrs[128];} msg={0};
    msg.n.nlmsg_len=NLMSG_LENGTH(sizeof msg.a);msg.n.nlmsg_type=add?RTM_NEWADDR:RTM_DELADDR;
    msg.n.nlmsg_flags=NLM_F_REQUEST|NLM_F_ACK|(add?NLM_F_CREATE|NLM_F_EXCL:0);
    msg.a.ifa_family=AF_INET;msg.a.ifa_prefixlen=ip->prefix;msg.a.ifa_index=id->ifindex;
    msg.a.ifa_scope=ip->ip.octet[0]==169&&ip->ip.octet[1]==254?RT_SCOPE_LINK:RT_SCOPE_UNIVERSE;
    if(attribute(&msg.n,sizeof msg,IFA_LOCAL,ip->ip.octet,4)||attribute(&msg.n,sizeof msg,IFA_ADDRESS,ip->ip.octet,4))return XZ_NET_INVALID;
    return netlink(&msg.n,c);
}
static int route_present(void *p,const struct xz_net_identity *id,struct xz_net_ipv4 gateway,unsigned metric){
    (void)p;FILE *f=fopen("/proc/net/route","r");if(!f)return 0;char line[512];unsigned lines=0;int found=0;
    while(fgets(line,sizeof line,f)&&++lines<4096){
        char name[16];unsigned long dst,gw,flags,m,mask;unsigned ref,use;
        if(sscanf(line,"%15s %lx %lx %lx %u %u %lu %lx",name,&dst,&gw,&flags,&ref,&use,&m,&mask)!=8||dst||mask||m!=metric||!(flags&1)||strcmp(name,id->name))continue;
        struct xz_net_ipv4 g;for(unsigned k=0;k<4;k++)g.octet[k]=(uint8_t)(gw>>(8*k));if(address_equal(g,gateway)){found=1;break;}
    }fclose(f);return found;
}
static enum xz_net_result route(void *p,int add,const struct xz_net_identity *id,struct xz_net_ipv4 gateway,unsigned metric,const struct xz_net_control *c){
    (void)p;if(!valid_identity(id))return XZ_NET_STALE;
    struct {struct nlmsghdr n;struct rtmsg r;char attrs[128];} msg={0};
    msg.n.nlmsg_len=NLMSG_LENGTH(sizeof msg.r);msg.n.nlmsg_type=add?RTM_NEWROUTE:RTM_DELROUTE;msg.n.nlmsg_flags=NLM_F_REQUEST|NLM_F_ACK|(add?NLM_F_CREATE|NLM_F_EXCL:0);
    msg.r.rtm_family=AF_INET;msg.r.rtm_table=RT_TABLE_MAIN;msg.r.rtm_protocol=RTPROT_STATIC;msg.r.rtm_scope=RT_SCOPE_UNIVERSE;msg.r.rtm_type=RTN_UNICAST;
    if(attribute(&msg.n,sizeof msg,RTA_OIF,&id->ifindex,sizeof id->ifindex)||attribute(&msg.n,sizeof msg,RTA_GATEWAY,gateway.octet,4)||attribute(&msg.n,sizeof msg,RTA_PRIORITY,&metric,sizeof metric))return XZ_NET_INVALID;
    return netlink(&msg.n,c);
}
static int safe_fd(int original){int fd=fcntl(original,F_DUPFD_CLOEXEC,10);close(original);return fd;}
static enum xz_net_result client_start(void *p,const struct xz_net_identity *id,uint64_t *token,const struct xz_net_control *c){
    struct xz_net_linux_os *o=p;if(o->pid>0)return XZ_NET_BUSY;
    if(!o->verified||access(o->client_path,X_OK)||access(o->event_path,X_OK))return XZ_NET_NEEDS_DHCP_CLIENT;
    if(xz_net_validate_identity(id)||xz_net_control_expired(c))return XZ_NET_INVALID;
    int pipes[2],life[2];if(pipe(pipes))return XZ_NET_NEEDS_BACKEND;if(pipe(life)){close(pipes[0]);close(pipes[1]);return XZ_NET_NEEDS_BACKEND;}
    pipes[0]=safe_fd(pipes[0]);pipes[1]=safe_fd(pipes[1]);if(pipes[0]<0||pipes[1]<0){if(pipes[0]>=0)close(pipes[0]);if(pipes[1]>=0)close(pipes[1]);close(life[0]);close(life[1]);return XZ_NET_NEEDS_BACKEND;}
    life[0]=safe_fd(life[0]);life[1]=safe_fd(life[1]);if(life[0]<0||life[1]<0){close(pipes[0]);close(pipes[1]);if(life[0]>=0)close(life[0]);if(life[1]>=0)close(life[1]);return XZ_NET_NEEDS_BACKEND;}
    posix_spawn_file_actions_t actions;int r=posix_spawn_file_actions_init(&actions),initialized=!r;
    if(!r)r=posix_spawn_file_actions_adddup2(&actions,pipes[1],XZ_NET_EVENT_FD);
    if(!r)r=posix_spawn_file_actions_adddup2(&actions,life[0],XZ_NET_LIFETIME_FD);
    if(!r)r=posix_spawn_file_actions_addclose(&actions,pipes[0]);
    if(!r)r=posix_spawn_file_actions_addclose(&actions,pipes[1]);
    if(!r)r=posix_spawn_file_actions_addclose(&actions,life[0]);
    if(!r)r=posix_spawn_file_actions_addclose(&actions,life[1]);
    char parent[32];snprintf(parent,sizeof parent,"%ld",(long)getpid());
    char *argv[]={o->event_path,"--run",parent,(char *)id->name,NULL};
    char *env[]={"PATH=/usr/sbin:/usr/bin:/sbin:/bin",XZ_NET_EVENT_ENV,NULL};pid_t pid=0;
    if(!r)r=posix_spawn(&pid,o->event_path,&actions,NULL,argv,env);
    if(initialized)posix_spawn_file_actions_destroy(&actions);close(pipes[1]);close(life[0]);if(r){close(pipes[0]);close(life[1]);return XZ_NET_NEEDS_DHCP_CLIENT;}
    int flags=fcntl(pipes[0],F_GETFL);fcntl(pipes[0],F_SETFL,flags|O_NONBLOCK);
    o->pid=pid;o->pipe=pipes[0];o->lifetime=life[1];memcpy(o->client_interface,id->name,16);o->partial_bytes=0;o->token=++o->serial;*token=o->token;return XZ_NET_OK;
}
static enum xz_net_result client_poll(void *p,uint64_t token,struct xz_net_lease *lease,unsigned wait_ms,const struct xz_net_control *c){
    struct xz_net_linux_os *o=p;if(!o->pid||o->token!=token)return XZ_NET_STALE;
    if(xz_net_control_expired(c))return XZ_NET_TIMEOUT;
    uint64_t left=c->deadline_ms-c->now_ms(c->context);if(wait_ms>left)wait_ms=(unsigned)left;
    struct pollfd fd={o->pipe,POLLIN,0};int ready=poll(&fd,1,(int)wait_ms);if(ready<0&&errno!=EINTR)return XZ_NET_VERIFY_FAILED;
    if(ready>0){
        ssize_t n=read(o->pipe,o->partial+o->partial_bytes,64-o->partial_bytes);
        if(n>0){o->partial_bytes+=(size_t)n;if(o->partial_bytes==64){o->partial_bytes=0;return xz_net_lease_decode(o->partial,lease);}}
    }
    int status;pid_t ended=waitpid(o->pid,&status,WNOHANG);
    if(ended==o->pid||(ended<0&&errno==ECHILD)){o->pid=0;close(o->pipe);o->pipe=-1;if(o->lifetime>=0)close(o->lifetime);o->lifetime=-1;return XZ_NET_VERIFY_FAILED;}
    return XZ_NET_PENDING;
}
static enum xz_net_result client_stop(void *p,uint64_t token,const struct xz_net_control *c){
    struct xz_net_linux_os *o=p;if(o->token!=token)return XZ_NET_STALE;if(!o->pid)return XZ_NET_OK;
    if(o->lifetime>=0){close(o->lifetime);o->lifetime=-1;}
    uint64_t kill_at=c->now_ms(c->context)+300;int sent=0;
    while(!xz_net_control_expired(c)){
        int status;pid_t ended=waitpid(o->pid,&status,WNOHANG);
        if(ended==o->pid||(ended<0&&errno==ECHILD)){o->pid=0;close(o->pipe);o->pipe=-1;return XZ_NET_OK;}
        if(ended<0&&errno!=EINTR)return XZ_NET_CONFLICT;
        if(!sent){if(kill(o->pid,SIGTERM)&&errno!=ESRCH)return XZ_NET_CONFLICT;sent=1;}
        if(sent==1&&c->now_ms(c->context)>=kill_at){if(kill(o->pid,SIGKILL)&&errno!=ESRCH)return XZ_NET_CONFLICT;sent=2;}
        struct timespec delay={0,10000000};nanosleep(&delay,NULL);
    }return XZ_NET_TIMEOUT;
}
enum xz_net_result xz_net_linux_os_create(struct xz_net_linux_os **out,struct xz_net_linux_io *io,const struct xz_net_linux_config *config){
    if(!out||!io)return XZ_NET_INVALID;struct xz_net_linux_os *o=calloc(1,sizeof *o);if(!o)return XZ_NET_NEEDS_BACKEND;
    const char *client=config&&config->client_path?config->client_path:"/dev/shm/xz-network-udhcpc";
    const char *event=config&&config->event_path?config->event_path:"/dev/shm/xz-network-lease-event";
    if(client[0]!='/'||event[0]!='/'||strlen(client)>=sizeof o->client_path||strlen(event)>=sizeof o->event_path){free(o);return XZ_NET_INVALID;}
    strcpy(o->client_path,client);strcpy(o->event_path,event);o->verified=config&&config->client_contract_verified;o->pipe=o->lifetime=-1;
    *io=(struct xz_net_linux_io){o,milliseconds,inventory,address,route,route_present,client_start,client_poll,client_stop};*out=o;return XZ_NET_OK;
}
void xz_net_linux_os_destroy(struct xz_net_linux_os *o){if(o&&o->pid<=0){if(o->pipe>=0)close(o->pipe);if(o->lifetime>=0)close(o->lifetime);free(o);}}
