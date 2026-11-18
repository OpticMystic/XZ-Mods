/* SPDX-License-Identifier: MIT
 * Own one diagnostic child. A lost laptop connection cannot leave it running. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static unsigned long long milliseconds(void) {
    struct timespec value;
    if(clock_gettime(CLOCK_MONOTONIC,&value))return 0;
    return (unsigned long long)value.tv_sec*1000+value.tv_nsec/1000000;
}
static volatile sig_atomic_t interrupted;
static void interrupt(int signal_number){(void)signal_number;interrupted=1;}
int main(int argc,char **argv) {
    if(argc<3)return 2;
    char *end;unsigned long seconds=strtoul(argv[1],&end,10);
    if(*end || seconds<1 || seconds>300 || argv[2][0]!='/')return 2;
    pid_t child=fork();
    if(child<0)return 2;
    if(!child){
        struct rlimit cpu={120,125};setrlimit(RLIMIT_CPU,&cpu);
        struct rlimit core={0,0};setrlimit(RLIMIT_CORE,&core);
        struct rlimit memory={1536u*1024u*1024u,1536u*1024u*1024u};setrlimit(RLIMIT_AS,&memory);
        int score=open("/proc/self/oom_score_adj",O_WRONLY);
        if(score>=0){write(score,"1000",4);close(score);}
        prctl(PR_SET_PDEATHSIG,SIGKILL);
        setpriority(PRIO_PROCESS,0,5);
        execv(argv[2],argv+2);_exit(127);
    }
    signal(SIGHUP,interrupt);signal(SIGINT,interrupt);signal(SIGTERM,interrupt);
    unsigned long long deadline=milliseconds()+seconds*1000;
    int status=0,timed_out=0;
    for(;;){
        pid_t result=waitpid(child,&status,WNOHANG);
        if(result==child)break;
        if(result<0 && errno!=EINTR)return 2;
        unsigned long long current=milliseconds();
        if(!timed_out && (interrupted || current>=deadline)){kill(child,SIGTERM);timed_out=1;deadline=current+3000;}
        else if(timed_out && current>=deadline){kill(child,SIGKILL);while(waitpid(child,&status,0)<0 && errno==EINTR){};break;}
        struct timespec delay={0,100000000};nanosleep(&delay,NULL);
    }
    struct rusage usage;
    if(!getrusage(RUSAGE_CHILDREN,&usage))fprintf(stdout,"XZ_GZ_USAGE peak_rss_kb=%ld\n",usage.ru_maxrss);
    if(timed_out){fprintf(stderr,"XZ_GZ_WATCHDOG native time limit reached\n");return 124;}
    return WIFEXITED(status)?WEXITSTATUS(status):WIFSIGNALED(status)?128+WTERMSIG(status):2;
}
