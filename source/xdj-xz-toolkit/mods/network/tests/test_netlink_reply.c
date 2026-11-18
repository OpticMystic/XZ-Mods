/* SPDX-License-Identifier: MIT. Actual netlink operation with recorded OS replies. */
#include "../linux_os.c"
#include <assert.h>

static unsigned char packet[256];
static size_t packet_size;
static uint32_t sender;
static uint64_t tick;
static unsigned closes,reads;
int __wrap_socket(int domain,int type,int protocol){assert(domain==AF_NETLINK&&protocol==NETLINK_ROUTE);(void)type;return 101;}
int __wrap_bind(int fd,const struct sockaddr *a,socklen_t n){assert(fd==101&&a->sa_family==AF_NETLINK&&n==sizeof(struct sockaddr_nl));return 0;}
ssize_t __wrap_sendto(int fd,const void *p,size_t n,int flags,const struct sockaddr *a,socklen_t z){
    assert(fd==101&&p&&a->sa_family==AF_NETLINK&&z==sizeof(struct sockaddr_nl));(void)flags;return (ssize_t)n;
}
int __wrap_poll(struct pollfd *p,nfds_t n,int timeout){assert(n==1&&p[0].fd==101&&timeout==20);tick++;p[0].revents=POLLIN;return 1;}
int __wrap___poll_chk(struct pollfd *p,nfds_t n,int timeout,size_t size){assert(size>=n*sizeof(*p));return __wrap_poll(p,n,timeout);}
ssize_t __wrap_recvfrom(int fd,void *p,size_t n,int flags,struct sockaddr *a,socklen_t *z){
    assert(fd==101&&n>=packet_size&&*z==sizeof(struct sockaddr_nl));(void)flags;
    memcpy(p,packet,packet_size);struct sockaddr_nl *s=(struct sockaddr_nl *)a;s->nl_family=AF_NETLINK;s->nl_pid=sender;reads++;return (ssize_t)packet_size;
}
ssize_t __wrap___recvfrom_chk(int fd,void *p,size_t n,size_t capacity,int flags,struct sockaddr *a,socklen_t *z){
    assert(capacity>=n);return __wrap_recvfrom(fd,p,n,flags,a,z);
}
int __wrap_close(int fd){assert(fd==101);closes++;return 0;}
static uint64_t test_now(void *p){(void)p;return tick++;}
static void reply(size_t at,uint32_t sequence,int error){
    struct { struct nlmsghdr n;struct nlmsgerr e; } value={0};
    value.n.nlmsg_len=NLMSG_LENGTH(sizeof value.e);value.n.nlmsg_type=NLMSG_ERROR;value.n.nlmsg_seq=sequence;value.e.error=error;
    assert(at+sizeof value<=sizeof packet);memcpy(packet+at,&value,sizeof value);packet_size=at+sizeof value;
}
static void expect(enum xz_net_result wanted){
    tick=closes=reads=0;struct nlmsghdr request={.nlmsg_len=sizeof request};
    struct xz_net_control c={.deadline_ms=5,.now_ms=test_now};
    assert(netlink(&request,&c)==wanted);assert(closes==1&&reads>0&&reads<=5);
}
int main(void){
    reply(0,1,0);expect(XZ_NET_OK);
    reply(0,1,-EEXIST);expect(XZ_NET_CONFLICT);
    reply(0,1,-EPERM);expect(XZ_NET_VERIFY_FAILED);
    reply(0,2,0);expect(XZ_NET_TIMEOUT);
    reply(0,1,0);sender=12;expect(XZ_NET_TIMEOUT);sender=0;
    packet_size=sizeof(struct nlmsghdr)-1;expect(XZ_NET_TIMEOUT);
    struct nlmsghdr first={.nlmsg_len=17,.nlmsg_type=NLMSG_NOOP,.nlmsg_seq=1};
    memset(packet,0,sizeof packet);memcpy(packet,&first,sizeof first);packet_size=17;expect(XZ_NET_TIMEOUT);
    reply(NLMSG_ALIGN(17),1,0);expect(XZ_NET_OK);
    first.nlmsg_len=UINT32_MAX;memcpy(packet,&first,sizeof first);packet_size=sizeof first;expect(XZ_NET_TIMEOUT);
    first.nlmsg_len=0;memcpy(packet,&first,sizeof first);expect(XZ_NET_TIMEOUT);
    reply(0,1,0);packet_size--;expect(XZ_NET_TIMEOUT);
    first.nlmsg_len=sizeof first;memcpy(packet,&first,sizeof first);reply(sizeof first,1,0);expect(XZ_NET_OK);
    puts("PASS actual netlink ACK: success/conflict/error, peer/sequence, truncated and aligned multi-message replies");return 0;
}
