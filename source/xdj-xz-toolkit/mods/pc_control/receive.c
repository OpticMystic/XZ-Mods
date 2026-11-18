/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* One bounded RAM packet from an explicit peer. The caller supplies stdout;
 * the network peer cannot choose a path, command, or second transfer. */
int main(int argc,char **argv){
 if(argc!=4)return 2;
 char *end;unsigned long remaining=strtoul(argv[1],&end,10);
 if(*end||remaining<96||remaining>96u+540000u*5u)return 2;
 struct sockaddr_in local={0},peer={0};struct in_addr expected;
 if(inet_pton(AF_INET,argv[2],&local.sin_addr)!=1||inet_pton(AF_INET,argv[3],&expected)!=1)return 2;
 local.sin_family=AF_INET;local.sin_port=htons(50009);
 int listener=socket(AF_INET,SOCK_STREAM,0),one=1;
 if(listener<0)return 1;
 setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
 struct timeval timeout={20,0};
 setsockopt(listener,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 if(bind(listener,(struct sockaddr*)&local,sizeof(local))||listen(listener,1)){close(listener);return 1;}
 socklen_t length=sizeof(peer);int client=accept(listener,(struct sockaddr*)&peer,&length);close(listener);
 if(client<0)return 1;
 if(peer.sin_addr.s_addr!=expected.s_addr){close(client);return 1;}
 setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 unsigned char buffer[32768];
 while(remaining){
  size_t want=remaining<sizeof(buffer)?remaining:sizeof(buffer);
  ssize_t got=read(client,buffer,want);
  if(got<0&&errno==EINTR)continue;
  if(got<=0){close(client);return 1;}
  size_t done=0;
  while(done<(size_t)got){
   ssize_t wrote=write(STDOUT_FILENO,buffer+done,(size_t)got-done);
   if(wrote<0&&errno==EINTR)continue;
   if(wrote<=0){close(client);return 1;}
   done+=(size_t)wrote;
  }
  remaining-=(unsigned long)got;
 }
 fsync(STDOUT_FILENO);close(client);return 0;
}
