/* Fake executable for the real supervisor/pipe runner. No sockets or network tools. */
#define _POSIX_C_SOURCE 200809L
#include "../linux.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
int main(int argc,char **argv){
    assert(argc==12&&!strcmp(argv[1],"-f")&&!strcmp(argv[2],"-n")&&!strcmp(argv[3],"-a")&&!strcmp(argv[4],"-t")&&!strcmp(argv[5],"3")&&!strcmp(argv[6],"-T")&&!strcmp(argv[7],"2")&&!strcmp(argv[8],"-i")&&!strcmp(argv[10],"-s"));
    assert(!strcmp(getenv("XZ_NETWORK_EVENT_FD"),XZ_NET_STRING(XZ_NET_EVENT_FD)));
    setenv("interface",argv[9],1);setenv("ip","192.0.2.80",1);setenv("subnet","255.255.255.0",1);setenv("router","192.0.2.1 192.0.2.2",1);setenv("lease","3600",1);
    pid_t report=fork();assert(report>=0);if(!report){execl(argv[11],argv[11],"bound",(char *)NULL);_exit(127);}int status;assert(waitpid(report,&status,0)==report&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    for(;;)pause();
}
