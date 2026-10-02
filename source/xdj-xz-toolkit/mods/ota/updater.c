/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "vendor/monocypher-ed25519.h"
#include "trusted_key.h"
#include "../audio/vendor/sha256/sha256.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_BUNDLE (24u*1024u*1024u)
static const char *names[4]={"libxz-mods.so","libxz-receiver.so","xz-doom","source.zip"};
static const char *ram_names[3]={"libxz-mods.so","libxz-directfb-hook.so","xz-doom"};
static const unsigned char app_sha256[32]={0x3a,0x7c,0x6c,0x50,0x7e,0xa6,0x7b,0x24,0x84,0xd0,0xcb,0xc5,0x81,0x36,0xa4,0x95,0x37,0x4c,0x8e,0xde,0x5e,0xee,0xf1,0xff,0xdf,0xfc,0xfe,0x86,0xc7,0xa3,0xda,0xe3};
struct manifest {unsigned char raw[576],hash[32];uint32_t sequence,sizes[4];char label[33],id[74];};
struct pointer {uint32_t sequence;char digest[65],mode;};
static const char *usb,*ram="/dev/shm";
static char root[1100];
static int failure(const char *message) {fprintf(stderr,"XZ_OTA_ERROR %s\n",message);return 1;}
static uint32_t little(const unsigned char *p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void hex(const unsigned char *bytes,size_t size,char *out) {static const char digits[]="0123456789abcdef";for(size_t i=0;i<size;i++){out[i*2]=digits[bytes[i]>>4];out[i*2+1]=digits[bytes[i]&15];}out[size*2]=0;}
static int exact(int fd,void *data,size_t size) {unsigned char *p=data;while(size){ssize_t n=read(fd,p,size);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;p+=n;size-=(size_t)n;}return 0;}
static int write_all(int fd,const void *data,size_t size) {const unsigned char *p=data;while(size){ssize_t n=write(fd,p,size);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;p+=n;size-=(size_t)n;}return 0;}
static int zeros(const unsigned char *p,size_t n) {for(size_t i=0;i<n;i++)if(p[i])return 0;return 1;}
static int manifest_parse(struct manifest *m) {
    if(memcmp(m->raw,"XZOTA1\0\0",8)||little(m->raw+8)!=1||little(m->raw+12)<XZ_OTA_MIN_SEQUENCE||
       memcmp(m->raw+16,"xdj-xz/1.26\0",12)||!zeros(m->raw+28,20)||
       memcmp(m->raw+48,app_sha256,32)||!memchr(m->raw+80,0,32)||little(m->raw+112)!=4||!zeros(m->raw+500,12))return -1;
    if(crypto_ed25519_check(m->raw+512,xz_ota_public_key,m->raw,512))return -1;
    m->sequence=little(m->raw+12);memcpy(m->label,m->raw+80,32);m->label[32]=0;
    for(unsigned i=0;i<32&&m->label[i];i++)if(m->label[i]<32||m->label[i]>126)return -1;
    uint64_t total=576;
    for(unsigned i=0;i<4;i++) {
        const unsigned char *entry=m->raw+116+i*96;size_t length=strlen(names[i]);
        if(memcmp(entry,names[i],length)||!zeros(entry+length,32-length)||!zeros(entry+68,28))return -1;
        m->sizes[i]=little(entry+32);if(!m->sizes[i]||m->sizes[i]>(i==3?16u:2u)*1024u*1024u)return -1;
        total+=m->sizes[i];
    }
    if(total>MAX_BUNDLE)return -1;
    SHA256_CTX sha;sha256_init(&sha);sha256_update(&sha,m->raw,512);sha256_final(&sha,m->hash);
    char digest[65];hex(m->hash,32,digest);snprintf(m->id,sizeof(m->id),"%08x-%s",m->sequence,digest);
    return 0;
}
static int hash_file(const char *path,uint32_t expected,unsigned char digest[32]) {
    int flags=O_RDONLY|O_NOFOLLOW;
    /* Only generated /proc/<pid>/exe paths follow the kernel's executable link. */
    if(!strncmp(path,"/proc/",6)&&strlen(path)>10&&!strcmp(path+strlen(path)-4,"/exe"))flags=O_RDONLY;
    int fd=open(path,flags);struct stat stat;unsigned char buffer[32768];SHA256_CTX sha;
    if(fd<0)return -1;
    if(fstat(fd,&stat)||!S_ISREG(stat.st_mode)||(expected&&(uint64_t)stat.st_size!=expected)){close(fd);return -1;}
    sha256_init(&sha);ssize_t n;
    while((n=read(fd,buffer,sizeof(buffer)))!=0){if(n<0&&errno==EINTR)continue;if(n<0){close(fd);return -1;}sha256_update(&sha,buffer,(size_t)n);}
    sha256_final(&sha,digest);close(fd);return 0;
}
static int mkdir_safe(const char *path) {struct stat stat;if(lstat(path,&stat)==0)return S_ISDIR(stat.st_mode)&&!S_ISLNK(stat.st_mode)?0:-1;return mkdir(path,0700);}
static int setup_root(void) {
    if(!usb || usb[0]!='/' || strstr(usb,"..") || strchr(usb,'\n') || strlen(usb)>1000)return -1;
    struct stat stat;if(lstat(usb,&stat)||!S_ISDIR(stat.st_mode)||S_ISLNK(stat.st_mode))return -1;
    char directory[1100];snprintf(directory,sizeof(directory),"%s/VJTOOLS",usb);
    if(mkdir_safe(directory))return -1;
    snprintf(root,sizeof(root),"%s/VJTOOLS/ota",usb);if(mkdir_safe(root))return -1;
    snprintf(directory,sizeof(directory),"%s/slots",root);return mkdir_safe(directory);
}
static int sync_directory(const char *path) {int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd<0)return -1;int result=fsync(fd);close(fd);return result;}
static int pointer_read(const char *name,struct pointer *p) {
    char path[1200],text[100];snprintf(path,sizeof(path),"%s/%s",root,name);
    int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0){memset(p,0,sizeof(*p));return errno==ENOENT?0:-1;}
    ssize_t size=read(fd,text,sizeof(text)-1);close(fd);if(size<=0)return -1;text[size]=0;
    unsigned seq;char mode;int used=0;
    if(sscanf(text,"%8x %64[0-9a-f] %c\n%n",&seq,p->digest,&mode,&used)!=3 || (size_t)used!=(size_t)size || strlen(p->digest)!=64 || (mode!='N'&&mode!='R'))return -1;
    p->sequence=seq;p->mode=mode;return 0;
}
static int pointer_write(const char *name,const struct pointer *p) {
    char path[1200],temporary[1200],text[100];snprintf(path,sizeof(path),"%s/%s",root,name);snprintf(temporary,sizeof(temporary),"%s/.pointer-XXXXXX",root);
    int fd=mkstemp(temporary);if(fd<0)return -1;
    int size=snprintf(text,sizeof(text),"%08x %s %c\n",p->sequence,p->digest,p->mode);
    int result=write_all(fd,text,(size_t)size)||fsync(fd);if(close(fd))result=-1;
    if(!result)result=rename(temporary,path);if(result)unlink(temporary);else result=sync_directory(root);return result;
}
static void pointer_from(struct manifest *m,struct pointer *p) {p->sequence=m->sequence;p->mode='N';hex(m->hash,32,p->digest);}
static int same(const struct pointer *a,const struct pointer *b) {return a->sequence==b->sequence&&!strcmp(a->digest,b->digest);}
static int slot_path(const struct pointer *p,char path[1200]) {return snprintf(path,1200,"%s/slots/%08x-%s",root,p->sequence,p->digest)>=1200?-1:0;}
static int verify_slot(const struct pointer *p,struct manifest *m) {
    char directory[1200],path[1400];if(!p->sequence||slot_path(p,directory))return -1;
    struct stat stat;if(lstat(directory,&stat)||!S_ISDIR(stat.st_mode)||S_ISLNK(stat.st_mode))return -1;
    snprintf(path,sizeof(path),"%s/manifest.bin",directory);int fd=open(path,O_RDONLY|O_NOFOLLOW);
    if(fd<0)return -1;int result=fstat(fd,&stat)||stat.st_size!=576||exact(fd,m->raw,576);close(fd);
    if(result||manifest_parse(m))return -1;
    struct pointer parsed;pointer_from(m,&parsed);if(!same(p,&parsed))return -1;
    for(unsigned i=0;i<4;i++){unsigned char digest[32];snprintf(path,sizeof(path),"%s/%s",directory,names[i]);
        if(hash_file(path,m->sizes[i],digest)||memcmp(digest,m->raw+116+i*96+36,32))return -1;
        if(i<3){unsigned char elf[40];fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return -1;result=exact(fd,elf,sizeof(elf));close(fd);
            if(result||memcmp(elf,"\x7f""ELF\x01\x01",6)||elf[18]!=40||elf[19]||little(elf+36)&0x400)return -1;}
    }
    return 0;
}
static int url_parse(const char *url,char host[256],char port[8]) {
    if(strncmp(url,"http://",7)||strlen(url)>300)return -1;
    const char *start=url+7,*colon=strchr(start,':');size_t n=colon?(size_t)(colon-start):strlen(start);
    if(!n||n>255)return -1;
    for(size_t i=0;i<n;i++)if(!((start[i]>='0'&&start[i]<='9')||(start[i]>='A'&&start[i]<='Z')||(start[i]>='a'&&start[i]<='z')||start[i]=='.'||start[i]=='-'))return -1;
    memcpy(host,start,n);host[n]=0;snprintf(port,8,"%s",colon?colon+1:"80");
    if(colon&&strlen(colon+1)>5)return -1;
    char *end;unsigned long value=strtoul(port,&end,10);return *end||value==0||value>65535?-1:0;
}
static int http_get(const char *url,const char *resource,size_t maximum,size_t *length) {
    char host[256],port[8];if(url_parse(url,host,port))return -1;
    struct addrinfo hints={0},*addresses=NULL;hints.ai_socktype=SOCK_STREAM;hints.ai_family=AF_INET;
    if(getaddrinfo(host,port,&hints,&addresses))return -1;
    int fd=-1;struct timeval timeout={10,0};
    for(struct addrinfo *p=addresses;p;p=p->ai_next){fd=socket(p->ai_family,p->ai_socktype,p->ai_protocol);if(fd<0)continue;
        setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
        int flags=fcntl(fd,F_GETFL,0);fcntl(fd,F_SETFL,flags|O_NONBLOCK);
        int connected=connect(fd,p->ai_addr,p->ai_addrlen);
        if(connected<0&&errno==EINPROGRESS){struct pollfd wait={.fd=fd,.events=POLLOUT};int error=0;socklen_t size=sizeof(error);
            if(poll(&wait,1,5000)>0&&getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)==0&&error==0)connected=0;}
        fcntl(fd,F_SETFL,flags);if(!connected)break;close(fd);fd=-1;}
    freeaddrinfo(addresses);if(fd<0)return -1;
    char request[512];int n=snprintf(request,sizeof(request),"GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",resource,host);
    if(write_all(fd,request,(size_t)n)){close(fd);return -1;}
    char header[4096];size_t used=0;
    while(used<sizeof(header)-1){if(exact(fd,header+used,1)){close(fd);return -1;}used++;if(used>=4&&!memcmp(header+used-4,"\r\n\r\n",4))break;}
    header[used]=0;
    if(used>=sizeof(header)-1 || (strncmp(header,"HTTP/1.0 200 ",13)&&strncmp(header,"HTTP/1.1 200 ",13))){close(fd);return -1;}
    const char *size_header=strcasestr(header,"\r\nContent-Length:");
    if(!size_header||strcasestr(header,"\r\nTransfer-Encoding:")||strcasestr(size_header+2,"\r\nContent-Length:")){close(fd);return -1;}
    char *end;unsigned long size=strtoul(size_header+18,&end,10);
    if(!size||size>maximum||strncmp(end,"\r\n",2)){close(fd);return -1;}*length=(size_t)size;return fd;
}
static int discover(char url[320]) {
    int fd=socket(AF_INET,SOCK_DGRAM,0),one=1;if(fd<0)return -1;
    setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&one,sizeof(one));struct timeval timeout={2,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(50009),.sin_addr={.s_addr=INADDR_BROADCAST}};
    struct ifreq interface={0};snprintf(interface.ifr_name,sizeof(interface.ifr_name),"eth0");
    if(ioctl(fd,SIOCGIFBRDADDR,&interface)==0)address.sin_addr=((struct sockaddr_in *)&interface.ifr_broadaddr)->sin_addr;
    const char *request="XZOTA_DISCOVER_1";sendto(fd,request,strlen(request),0,(struct sockaddr *)&address,sizeof(address));
    char reply[340];ssize_t n=recv(fd,reply,sizeof(reply)-1,0);close(fd);if(n<9)return -1;reply[n]=0;
    if(strncmp(reply,"XZOTA1 ",7)||strlen(reply+7)>=320)return -1;
    snprintf(url,320,"%s",reply+7);char host[256],port[8];return url_parse(url,host,port);
}
static int newer(struct manifest *m) {
    struct pointer floor,active,pending,incoming,base;pointer_from(m,&incoming);
    if(pointer_read("accepted",&floor)||pointer_read("active",&active)||pointer_read("pending",&pending)||pointer_read("base",&base))return -1;
    if(m->sequence<floor.sequence || (m->sequence==floor.sequence&&!same(&incoming,&floor)))return -1;
    if(pending.sequence>m->sequence)return -1;
    if(pending.sequence==m->sequence&&!same(&pending,&incoming))return -1;
    if(same(&incoming,&active)&&base.mode!='R')return 0;return 1;
}
static void remove_incoming(const char *directory) {
    char path[1400];for(unsigned i=0;i<4;i++){snprintf(path,sizeof(path),"%s/%s",directory,names[i]);unlink(path);}
    snprintf(path,sizeof(path),"%s/manifest.bin",directory);unlink(path);rmdir(directory);
}
static int fetch_manifest(const char *url,struct manifest *m) {
    size_t size;int fd=http_get(url,"/manifest.bin",576,&size);if(fd<0)return -1;
    int result=size!=576||exact(fd,m->raw,576);close(fd);return result||manifest_parse(m)?-1:0;
}
static int download(const char *url) {
    struct manifest m;if(fetch_manifest(url,&m))return failure("UPDATE SIGNATURE OR SERVER CHECK FAILED");
    int allowed=newer(&m);if(allowed<0)return failure("OLDER OR CONFLICTING UPDATE REJECTED");
    if(!allowed){printf("CURRENT %s\n",m.label);return 0;}
    uint64_t total=576;for(unsigned i=0;i<4;i++)total+=m.sizes[i];struct statvfs space;
    if(statvfs(root,&space)||(uint64_t)space.f_bavail*space.f_frsize<total+1024*1024)return failure("USB NEEDS MORE FREE SPACE");
    size_t bytes;int network=http_get(url,"/latest.xzu",MAX_BUNDLE,&bytes);if(network<0)return failure("UPDATE DOWNLOAD FAILED");
    unsigned char signed_header[576];if(bytes!=total||exact(network,signed_header,576)||memcmp(signed_header,m.raw,576)){close(network);return failure("UPDATE CHANGED DURING DOWNLOAD");}
    char temporary[1200];snprintf(temporary,sizeof(temporary),"%s/.incoming-XXXXXX",root);
    if(!mkdtemp(temporary)){close(network);return failure("USB STAGING FAILED");}
    int bad=0;char path[1400];unsigned char buffer[32768];
    for(unsigned i=0;i<4&&!bad;i++) {
        snprintf(path,sizeof(path),"%s/%s",temporary,names[i]);int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0){bad=1;break;}
        uint32_t remaining=m.sizes[i];SHA256_CTX sha;sha256_init(&sha);
        while(remaining){size_t count=remaining<sizeof(buffer)?remaining:sizeof(buffer);if(exact(network,buffer,count)||write_all(fd,buffer,count)){bad=1;break;}sha256_update(&sha,buffer,count);remaining-=(uint32_t)count;}
        unsigned char digest[32];sha256_final(&sha,digest);if(memcmp(digest,m.raw+116+i*96+36,32)||fsync(fd))bad=1;if(close(fd))bad=1;
    }
    close(network);
    if(!bad){snprintf(path,sizeof(path),"%s/manifest.bin",temporary);int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)bad=1;else{bad=write_all(fd,m.raw,576)||fsync(fd);close(fd);}}
    struct pointer candidate;pointer_from(&m,&candidate);char directory[1200];slot_path(&candidate,directory);
    if(!bad&&sync_directory(temporary))bad=1;
    if(!bad&&rename(temporary,directory)) {struct manifest existing;if(errno!=EEXIST&&errno!=ENOTEMPTY)bad=1;else if(verify_slot(&candidate,&existing))bad=1;else remove_incoming(temporary);}
    if(bad) {remove_incoming(temporary);return failure("INCOMPLETE OR CORRUPT UPDATE REJECTED");}
    if(pointer_write("pending",&candidate))return failure("COULD NOT SAVE PENDING UPDATE");
    snprintf(path,sizeof(path),"%s/base",root);unlink(path);sync_directory(root);
    printf("STAGED %s / APPLY AT NEXT BOOT\n",m.label);return 0;
}
static int copy_verified(const char *source,const char *destination,const unsigned char expected[32]) {
    char temporary[1400];snprintf(temporary,sizeof(temporary),"%s.next-XXXXXX",destination);int out=mkstemp(temporary),in=open(source,O_RDONLY|O_NOFOLLOW);
    if(out<0||in<0){if(out>=0){close(out);unlink(temporary);}if(in>=0)close(in);return -1;}
    unsigned char buffer[32768],digest[32];SHA256_CTX sha;sha256_init(&sha);int result=0;ssize_t n;
    while((n=read(in,buffer,sizeof(buffer)))) {if(n<0&&errno==EINTR)continue;if(n<0||write_all(out,buffer,(size_t)n)){result=-1;break;}sha256_update(&sha,buffer,(size_t)n);}
    sha256_final(&sha,digest);if(expected&&memcmp(digest,expected,32))result=-1;
    if(fchmod(out,0700)||fsync(out))result=-1;close(in);if(close(out))result=-1;
    if(!result)result=rename(temporary,destination);if(result)unlink(temporary);return result;
}
static int restore_ram(void) {
    char source[1400],destination[1400];int result=0;
    for(unsigned i=0;i<3;i++){snprintf(source,sizeof(source),"%s/xz-ota-base/%s",ram,ram_names[i]);snprintf(destination,sizeof(destination),"%s/%s",ram,ram_names[i]);
        if(access(source,R_OK)==0&&copy_verified(source,destination,NULL))result=-1;}
    return result;
}
static int apply(void) {
    struct pointer candidate,failed,floor,previous,active,base;struct manifest m;
    if(pointer_read("base",&base))return failure("USB RECOVERY POINTER INVALID");
    if(base.mode=='R')return 2;
    if(pointer_read("pending",&candidate))return failure("PENDING POINTER INVALID");
    if(!candidate.sequence&&pointer_read("active",&candidate))return failure("ACTIVE POINTER INVALID");
    if(!candidate.sequence)return 2;
    if(pointer_read("accepted",&floor)||pointer_read("previous",&previous)||pointer_read("active",&active))return failure("UPDATE HISTORY INVALID");
    if(candidate.mode=='R') {if(!same(&candidate,&previous)&&!same(&candidate,&active))return failure("UNAPPROVED ROLLBACK REFUSED");}
    else if(!same(&candidate,&active)&&(candidate.sequence<floor.sequence||(candidate.sequence==floor.sequence&&!same(&candidate,&floor))))return failure("STALE PENDING UPDATE REFUSED");
    if(pointer_read("failed",&failed)||same(&candidate,&failed)||verify_slot(&candidate,&m))return failure("UNVERIFIED OR FAILED SLOT REFUSED");
    char directory[1200],source[1400],destination[1400],backup[1400];slot_path(&candidate,directory);
    snprintf(backup,sizeof(backup),"%s/xz-ota-base",ram);if(mkdir_safe(backup))return failure("RAM BACKUP FAILED");
    for(unsigned i=0;i<3;i++) {
        snprintf(destination,sizeof(destination),"%s/%s",ram,ram_names[i]);snprintf(source,sizeof(source),"%s/%s",backup,ram_names[i]);
        if(access(destination,R_OK)==0 && copy_verified(destination,source,NULL))return failure("BASE RUNTIME BACKUP FAILED");
    }
    int bad=0;
    for(unsigned i=0;i<3&&!bad;i++){snprintf(source,sizeof(source),"%s/%s",directory,names[i]);snprintf(destination,sizeof(destination),"%s/%s",ram,ram_names[i]);bad=copy_verified(source,destination,m.raw+116+i*96+36);}
    if(!bad) {
        char smoke[1400],mods[1400],receiver[1400];snprintf(smoke,sizeof(smoke),"%s/xz-runtime-smoke",ram);snprintf(mods,sizeof(mods),"%s/libxz-mods.so",ram);snprintf(receiver,sizeof(receiver),"%s/libxz-directfb-hook.so",ram);
        pid_t pid=fork();if(pid==0){alarm(15);unsetenv("LD_PRELOAD");execl(smoke,smoke,mods,receiver,(char *)NULL);_exit(127);}int status=0;
        if(pid<0 || waitpid(pid,&status,0)<0 || !WIFEXITED(status)||WEXITSTATUS(status))bad=1;
    }
    if(bad){restore_ram();pointer_write("failed",&candidate);return failure("LOAD CHECK FAILED / BASE RUNTIME RESTORED");}
    if(pointer_write("trial",&candidate)){restore_ram();return failure("TRIAL STATE FAILED");}
    printf("TRIAL %s\n",m.label);return 0;
}
static int accept_trial(const char *pid_text) {
    char *end;unsigned long pid=strtoul(pid_text,&end,10);if(*end||!pid||pid>INT32_MAX)return failure("INVALID APPLICATION PID");
    struct pointer trial,active,floor;struct manifest m;
    if(pointer_read("trial",&trial)||verify_slot(&trial,&m)||pointer_read("active",&active)||pointer_read("accepted",&floor))return failure("TRIAL CANNOT BE ACCEPTED");
    char path[1400];snprintf(path,sizeof(path),"/proc/%lu/exe",pid);unsigned char digest[32];
    if(hash_file(path,0,digest)||memcmp(digest,app_sha256,32))return failure("APPLICATION TARGET DOES NOT MATCH");
    snprintf(path,sizeof(path),"/proc/%lu/maps",pid);FILE *maps=fopen(path,"r");char line[2048];unsigned found=0;
    if(!maps)return failure("APPLICATION MAPS UNAVAILABLE");
    while(fgets(line,sizeof(line),maps)){if(strstr(line,"/dev/shm/libxz-mods.so"))found|=1;if(strstr(line,"/dev/shm/libxz-directfb-hook.so"))found|=2;}fclose(maps);
    if(found!=3)return failure("APPLICATION DID NOT LOAD THE TRIAL RUNTIME");
    for(unsigned i=0;i<3;i++){snprintf(path,sizeof(path),"%s/%s",ram,ram_names[i]);if(hash_file(path,m.sizes[i],digest)||memcmp(digest,m.raw+116+i*96+36,32))return failure("RAM RUNTIME CHANGED");}
    if(active.sequence&&!same(&active,&trial)&&pointer_write("previous",&active))return failure("PREVIOUS SLOT SAVE FAILED");
    if(trial.sequence>floor.sequence&&pointer_write("accepted",&trial))return failure("ACCEPTED SEQUENCE SAVE FAILED");
    if(pointer_write("active",&trial))return failure("ACTIVE SLOT SAVE FAILED");
    snprintf(path,sizeof(path),"%s/pending",root);unlink(path);snprintf(path,sizeof(path),"%s/trial",root);unlink(path);sync_directory(root);
    printf("ACCEPTED %s\n",m.label);return 0;
}
static int reject(void) {struct pointer trial;if(pointer_read("trial",&trial)||!trial.sequence)return 2;if(restore_ram()||pointer_write("failed",&trial))return 1;printf("RESTORED BASE RUNTIME\n");return 0;}
static int rollback(void) {
    struct pointer previous;struct manifest m;if(pointer_read("previous",&previous))return failure("PREVIOUS SLOT POINTER INVALID");
    if(!previous.sequence) {
        struct pointer base={.mode='R'};memset(base.digest,'0',64);base.digest[64]=0;
        if(pointer_write("base",&base))return failure("USB RECOVERY SAVE FAILED");
        printf("ROLLBACK STAGED USB VERSION / APPLY AT NEXT BOOT\n");return 0;
    }
    if(verify_slot(&previous,&m))return failure("NO VERIFIED PREVIOUS SLOT");
    previous.mode='R';if(pointer_write("pending",&previous))return failure("ROLLBACK SAVE FAILED");printf("ROLLBACK STAGED %s / APPLY AT NEXT BOOT\n",m.label);return 0;
}
int main(int argc,char **argv) {
    alarm(90);
    if(argc<3)return failure("USE COMMAND USB [URL OR PID]");usb=argv[2];if(argc>4&&argv[4][0]=='/')ram=argv[4];
    if(setup_root())return failure("INSERT WRITABLE BOOT USB");
    if(!strcmp(argv[1],"identity")&&argc>3) {
        char *end;unsigned long pid=strtoul(argv[3],&end,10);if(*end||!pid||pid>INT32_MAX)return 1;
        char path[100],text[65];unsigned char digest[32];snprintf(path,sizeof(path),"/proc/%lu/exe",pid);
        if(hash_file(path,0,digest)){perror("application identity");return 1;}hex(digest,32,text);printf("APPLICATION_SHA256 %s\n",text);return 0;
    }
    if(!strcmp(argv[1],"verify")&&argc>3){struct pointer p;struct manifest m;int fd=open(argv[3],O_RDONLY|O_NOFOLLOW);if(fd<0)return 1;int bad=exact(fd,m.raw,576);close(fd);if(bad||manifest_parse(&m))return failure("SIGNATURE REJECTED");pointer_from(&m,&p);printf("VERIFIED %u %s\n",p.sequence,m.label);return 0;}
    if(!strcmp(argv[1],"apply"))return apply();if(!strcmp(argv[1],"accept")&&argc>3)return accept_trial(argv[3]);
    if(!strcmp(argv[1],"reject"))return reject();if(!strcmp(argv[1],"rollback"))return rollback();
    if(!strcmp(argv[1],"check")||!strcmp(argv[1],"stage")) {
        char url[320];if(argc>3)snprintf(url,sizeof(url),"%s",argv[3]);else if(discover(url))return failure("NO UPDATE SERVER / OPEN NETWORK SETUP");
        if(!strcmp(argv[1],"stage"))return download(url);
        struct manifest m;if(fetch_manifest(url,&m))return failure("UPDATE SIGNATURE OR SERVER CHECK FAILED");int available=newer(&m);
        if(available<0)return failure("OLDER OR CONFLICTING UPDATE REJECTED");printf("%s %s\n",available?"AVAILABLE":"CURRENT",m.label);return 0;
    }
    return failure("UNKNOWN UPDATE COMMAND");
}
