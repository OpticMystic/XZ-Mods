/* SPDX-License-Identifier: MIT. udhcpc -s executable; records only, never configures networking. */
#define _POSIX_C_SOURCE 200809L
#include "linux.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#ifndef XZ_NET_CLIENT_PATH
#define XZ_NET_CLIENT_PATH "/dev/shm/xz-network-udhcpc"
#endif
#ifndef XZ_NET_EVENT_PATH
#define XZ_NET_EVENT_PATH "/dev/shm/xz-network-lease-event"
#endif
static volatile sig_atomic_t stopping;
static int positive(const char *,uint32_t *);
static void stop_signal(int signal){(void)signal;stopping=1;}
static uint64_t milliseconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(unsigned long)t.tv_nsec/1000000;}
static int run_client(int argc,char **argv){
    uint32_t expected;struct stat pipe_state,client_state,event_state;
    if(argc!=4||positive(argv[2],&expected)||expected>(uint32_t)0x7fffffff||getppid()!=(pid_t)expected)return 126;
    struct xz_net_identity id={.ifindex=1,.identity_epoch=1};
    if(strlen(argv[3])>=16)return 126;strcpy(id.name,argv[3]);
    const char *marker=getenv("XZ_NETWORK_EVENT_FD");
    if(xz_net_validate_identity(&id)||!marker||strcmp(marker,XZ_NET_STRING(XZ_NET_EVENT_FD))||fstat(4,&pipe_state)||!S_ISFIFO(pipe_state.st_mode)||fstat(XZ_NET_EVENT_FD,&pipe_state)||!S_ISFIFO(pipe_state.st_mode))return 126;
    if(stat(XZ_NET_CLIENT_PATH,&client_state)||stat(XZ_NET_EVENT_PATH,&event_state)||!S_ISREG(client_state.st_mode)||!S_ISREG(event_state.st_mode)||((client_state.st_mode|event_state.st_mode)&(S_ISUID|S_ISGID)))return 126;
    struct sigaction action={0};action.sa_handler=stop_signal;sigemptyset(&action.sa_mask);sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);
    if(prctl(PR_SET_PDEATHSIG,SIGTERM)||getppid()!=(pid_t)expected)return 126;
    long maximum=sysconf(_SC_OPEN_MAX);if(maximum<0||maximum>65536)maximum=65536;
    for(int fd=3;fd<maximum;fd++)if(fd!=XZ_NET_EVENT_FD&&fd!=XZ_NET_LIFETIME_FD)close(fd);
    if(stopping||getppid()!=(pid_t)expected)return 126;
    pid_t supervisor=getpid(),child=fork();if(child<0)return 126;
    if(!child){
        if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=supervisor)_exit(126);
        close(4);char *args[]={XZ_NET_CLIENT_PATH,"-f","-n","-a","-t","3","-T","2","-i",id.name,"-s",XZ_NET_EVENT_PATH,NULL};
        char *environment[]={"PATH=/usr/sbin:/usr/bin:/sbin:/bin",XZ_NET_EVENT_ENV,NULL};
        execve(XZ_NET_CLIENT_PATH,args,environment);_exit(127);
    }
    close(XZ_NET_EVENT_FD);int status=0,term_sent=0;uint64_t kill_at=0;
    for(;;){
        pid_t ended=waitpid(child,&status,WNOHANG);
        if(ended==child)return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
        if(ended<0&&errno==ECHILD)return 126;
        struct pollfd lifetime={4,POLLIN|POLLHUP,0};int ready=poll(&lifetime,1,20);
        if(ready>0){char byte;ssize_t n=read(4,&byte,1);if(n<=0||lifetime.revents&(POLLHUP|POLLERR|POLLNVAL))stopping=1;}
        if(getppid()!=(pid_t)expected)stopping=1;
        if(stopping&&!term_sent){
            /* waitpid above proves this PID is our unreaped child; never load a PID from a file. */
            if(kill(child,SIGTERM)&&errno!=ESRCH)return 126;term_sent=1;kill_at=milliseconds()+250;
        }
        if(term_sent==1&&milliseconds()>=kill_at){if(kill(child,SIGKILL)&&errno!=ESRCH)return 126;term_sent=2;}
    }
}
static int positive(const char *s,uint32_t *out) {
    if(!s||!*s||strlen(s)>10)return -1;uint64_t n=0;
    for(;*s;s++){if(*s<'0'||*s>'9')return -1;n=n*10+(unsigned)(*s-'0');if(n>0xffffffffu)return -1;}
    if(!n)return -1;*out=(uint32_t)n;return 0;
}
int main(int argc,char **argv) {
    if(argc>=2&&!strcmp(argv[1],"--run"))return run_client(argc,argv);
    const char *marker=getenv("XZ_NETWORK_EVENT_FD"),*iface=getenv("interface");struct stat st;
    if(argc!=2||!marker||strcmp(marker,XZ_NET_STRING(XZ_NET_EVENT_FD))||!iface||strlen(iface)>=16||fstat(XZ_NET_EVENT_FD,&st)||!S_ISFIFO(st.st_mode))return 2;
    struct xz_net_lease l={0};memcpy(l.interface,iface,strlen(iface));
    if(!strcmp(argv[1],"bound"))l.kind=XZ_NET_LEASE_BOUND;
    else if(!strcmp(argv[1],"renew"))l.kind=XZ_NET_LEASE_RENEW;
    else if(!strcmp(argv[1],"deconfig"))l.kind=XZ_NET_LEASE_DECONFIG;
    else if(!strcmp(argv[1],"nak")||!strcmp(argv[1],"leasefail"))l.kind=XZ_NET_LEASE_FAILURE;
    else return 2;
    if(l.kind<=XZ_NET_LEASE_RENEW){
        const char *ip=getenv("ip"),*mask=getenv("subnet"),*router=getenv("router");
        if(xz_net_parse_ipv4(ip,&l.address.ip)||xz_net_parse_mask(mask,&l.address.prefix)||positive(getenv("lease"),&l.seconds))return 2;
        if(router&&*router){
            size_t n=strnlen(router,256);if(n>=256)return 2;char text[256];memcpy(text,router,n+1);char *save=NULL,*p=strtok_r(text," ",&save);unsigned count=0;
            while(p){struct xz_net_ipv4 g;if(++count>8||xz_net_parse_ipv4(p,&g))return 2;if(count==1){l.gateway=g;l.has_gateway=1;}p=strtok_r(NULL," ",&save);}
        }
    }
    unsigned char record[64];if(xz_net_lease_encode(&l,record))return 2;
    int flags=fcntl(XZ_NET_EVENT_FD,F_GETFL);if(flags<0||fcntl(XZ_NET_EVENT_FD,F_SETFL,flags|O_NONBLOCK))return 3;
    ssize_t n;do{n=write(XZ_NET_EVENT_FD,record,sizeof record);}while(n<0&&errno==EINTR);return n==(ssize_t)sizeof record?0:3;
}
