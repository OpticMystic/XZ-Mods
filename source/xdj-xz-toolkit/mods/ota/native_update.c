/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "native_update.h"
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static struct xz_update_model state={.status="CHECK FOR NETWORK UPDATES"};
static int ready(void) {const char *usb=getenv("XZ_MODS_USB");return usb&&usb[0]=='/'&&access(usb,W_OK)==0&&access("/dev/shm/xz-updater",X_OK)==0;}
void xz_update_read(struct xz_update_model *out) {pthread_mutex_lock(&mutex);*out=state;out->ready=ready();pthread_mutex_unlock(&mutex);}
static void *run(void *argument) {
    int operation=(int)(uintptr_t)argument;const char *mode=operation==1?"stage":operation==2?"rollback":"check";
    const char *usb=getenv("XZ_MODS_USB");char url[320]={0},config[1100];
    snprintf(config,sizeof(config),"%s/VJTOOLS/ota-server.txt",usb);
    int file=open(config,O_RDONLY|O_NOFOLLOW);
    if(file>=0){ssize_t n=read(file,url,sizeof(url)-1);close(file);if(n>0){url[n]=0;url[strcspn(url,"\r\n")]=0;}}
    char *args[]={"/dev/shm/xz-updater",(char *)mode,(char *)usb,url[0]?url:NULL,NULL};
    extern char **environ;size_t count=0;while(environ[count])count++;
    char **env=calloc(count+2,sizeof(*env));size_t out=0;
    if(env){for(size_t i=0;i<count;i++)if(strncmp(environ[i],"LD_PRELOAD=",11))env[out++]=environ[i];env[out]="LD_PRELOAD=";}
    int pipefd[2]={-1,-1},result=-1,status=0;char text[4096]={0};pid_t pid=0;
    if(env&&pipe(pipefd)==0) {
        posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions,pipefd[1],1);posix_spawn_file_actions_adddup2(&actions,pipefd[1],2);
        posix_spawn_file_actions_addclose(&actions,pipefd[0]);posix_spawn_file_actions_addclose(&actions,pipefd[1]);
        result=posix_spawn(&pid,args[0],&actions,NULL,args,env);posix_spawn_file_actions_destroy(&actions);close(pipefd[1]);pipefd[1]=-1;
        if(!result){size_t used=0;char buffer[512];ssize_t n;while((n=read(pipefd[0],buffer,sizeof(buffer)))>0){size_t keep=(size_t)n;if(keep>sizeof(text)-1-used)keep=sizeof(text)-1-used;memcpy(text+used,buffer,keep);used+=keep;}while(waitpid(pid,&status,0)<0&&errno==EINTR){} }
    }
    if(pipefd[0]>=0)close(pipefd[0]);if(pipefd[1]>=0)close(pipefd[1]);free(env);
    pthread_mutex_lock(&mutex);
    if(result||!WIFEXITED(status)||WEXITSTATUS(status))state.phase=XZ_UPDATE_ERROR;
    else if(!strncmp(text,"AVAILABLE ",10))state.phase=XZ_UPDATE_AVAILABLE;
    else if(!strncmp(text,"CURRENT ",8))state.phase=XZ_UPDATE_CURRENT;
    else if(!strncmp(text,"STAGED ",7)||!strncmp(text,"ROLLBACK STAGED ",16))state.phase=XZ_UPDATE_STAGED;
    else state.phase=XZ_UPDATE_IDLE;
    char *message=text;if(!strncmp(message,"XZ_OTA_ERROR ",13))message+=13;message[strcspn(message,"\r\n")]=0;
    snprintf(state.status,sizeof(state.status),"%.127s",*message?message:"UPDATE CHECK COULD NOT RUN");pthread_mutex_unlock(&mutex);return NULL;
}
void xz_update_request(int operation) {
    pthread_mutex_lock(&mutex);
    if(state.phase!=XZ_UPDATE_WORKING){
        if(!ready()){state.phase=XZ_UPDATE_ERROR;snprintf(state.status,sizeof(state.status),"OPEN SETUP / PREPARE THE UPDATED USB LOADER");}
        else {pthread_t thread;state.phase=XZ_UPDATE_WORKING;snprintf(state.status,sizeof(state.status),"%s",operation==1?"DOWNLOADING AND VERIFYING UPDATE":operation==2?"SELECTING PREVIOUS VERIFIED VERSION":"FINDING SIGNED UPDATE SERVER");
            if(pthread_create(&thread,NULL,run,(void *)(uintptr_t)operation)==0)pthread_detach(thread);else state.phase=XZ_UPDATE_ERROR;
        }
    }
    pthread_mutex_unlock(&mutex);
}
