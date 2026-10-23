/* Actual posix_spawn + compiled helper + fake client. No networking APIs invoked. */
#define _GNU_SOURCE
#include "../linux.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
static uint64_t now(void *p){(void)p;struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(unsigned long)t.tv_nsec/1000000;}
static struct xz_net_identity identity={.name="fixture0",.ifindex=7,.identity_epoch=1};
static void wait_ended(pid_t pid){for(unsigned i=0;i<300;i++){int s;if(waitpid(pid,&s,WNOHANG)==pid)return;struct timespec t={0,10000000};nanosleep(&t,NULL);}assert(!"owned child survived parent lifetime");}
static void start(struct xz_net_linux_io *io,struct xz_net_linux_os **os,const char *client,const char *event,uint64_t *token){
    struct xz_net_linux_config config={client,event,1};assert(!xz_net_linux_os_create(os,io,&config));struct xz_net_control control={NULL,now(NULL)+3000,now,NULL};assert(!io->client_start(io->context,&identity,token,&control));
    struct xz_net_lease lease;enum xz_net_result r;do{r=io->client_poll(io->context,*token,&lease,20,&control);}while(r==XZ_NET_PENDING&&now(NULL)<control.deadline_ms);
    assert(r==XZ_NET_OK&&lease.kind==XZ_NET_LEASE_BOUND&&lease.seconds==3600&&lease.address.ip.octet[3]==80);
}
static pid_t child_of(pid_t pid){char path[100];snprintf(path,sizeof path,"/proc/%ld/task/%ld/children",(long)pid,(long)pid);FILE *f=fopen(path,"r");assert(f);long child=0;assert(fscanf(f,"%ld",&child)==1);fclose(f);return (pid_t)child;}
int main(int argc,char **argv){
    assert(argc==3);assert(!prctl(PR_SET_CHILD_SUBREAPER,1));
    pid_t unrelated=fork();assert(unrelated>=0);if(!unrelated){for(;;)pause();}
    struct xz_net_linux_io io;struct xz_net_linux_os *os;uint64_t token;start(&io,&os,argv[1],argv[2],&token);
    struct xz_net_control c={NULL,now(NULL)+3000,now,NULL};assert(io.client_stop(io.context,token+1,&c)==XZ_NET_STALE);assert(!kill(unrelated,0));assert(!io.client_stop(io.context,token,&c));xz_net_linux_os_destroy(os);assert(!kill(unrelated,0));
    /* Runtime death and runtime exec both close its close-on-exec lifetime writer. */
    for(int exec_case=0;exec_case<2;exec_case++){
        int report[2],go[2];assert(!pipe(report)&&!pipe(go));pid_t parent=fork();assert(parent>=0);
        if(!parent){close(report[0]);close(go[1]);start(&io,&os,argv[1],argv[2],&token);pid_t supervisor=child_of(getpid()),client=child_of(supervisor);pid_t ids[2]={supervisor,client};assert(write(report[1],ids,sizeof ids)==sizeof ids);char byte;assert(read(go[0],&byte,1)==1);if(exec_case){execl("/bin/true","true",(char *)NULL);_exit(127);}for(;;)pause();}
        close(report[1]);close(go[0]);pid_t ids[2];assert(read(report[0],ids,sizeof ids)==sizeof ids);assert(write(go[1],"x",1)==1);if(!exec_case)assert(!kill(parent,SIGKILL));wait_ended(parent);wait_ended(ids[0]);
        /* Supervisor normally reaps client. If SIGKILL wins its grace window, subreaper owns it. */
        for(unsigned i=0;i<300&&!kill(ids[1],0);i++){int status;waitpid(ids[1],&status,WNOHANG);struct timespec t={0,10000000};nanosleep(&t,NULL);}assert(kill(ids[1],0)&&errno==ESRCH);
        close(report[0]);close(go[1]);assert(!kill(unrelated,0));
    }
    /* Wrong expected parent exits before any client launch. */
    pid_t race=fork();assert(race>=0);if(!race){execl(argv[2],argv[2],"--run","1","fixture0",(char *)NULL);_exit(127);}int status;assert(waitpid(race,&status,0)==race&&WIFEXITED(status)&&WEXITSTATUS(status)==126);
    assert(!kill(unrelated,0));assert(!kill(unrelated,SIGTERM));assert(waitpid(unrelated,&status,0)==unrelated);
    puts("PASS real launcher/event pipe: fixed argv, strict lease, explicit stop, parent death, parent exec, startup race, unrelated PID survives");
}
