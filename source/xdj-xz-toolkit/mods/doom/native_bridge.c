/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "native_bridge.h"
#include "wad_catalog.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static struct xz_game_native bridge;
static pthread_mutex_t browser_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct xz_wad_catalog catalog;
static int catalog_scanning,base_index=-1,map_index=-1;
static void *scan_games(void *unused) {
    (void)unused;
    struct xz_wad_catalog *next=calloc(1,sizeof(*next));
    if(next) {
        if(access("/dev/shm/doom1.wad",R_OK)==0) xz_wad_inspect("/dev/shm/doom1.wad",&next->files[next->count++]);
        FILE *mounts=fopen("/proc/mounts","r");char line[2048];
        if(mounts) { while(fgets(line,sizeof(line),mounts)) {
            char *save,*device=strtok_r(line," \t",&save),*root=strtok_r(NULL," \t",&save),*type=strtok_r(NULL," \t",&save);
            if(!device || !root || !type || strcmp(type,"vfat"))continue;
            if(strncmp(root,"/media/usb1/",12)&&strncmp(root,"/media/usb2/",12))continue;
            if(strchr(root,'\\') || strstr(root,".."))continue;
            xz_wad_scan(next,root);
        } fclose(mounts); }
    }
    pthread_mutex_lock(&browser_mutex);
    if(next) {
        char previous_base[1024]={0},previous_map[1024]={0};
        if(base_index>=0)snprintf(previous_base,sizeof(previous_base),"%s",catalog.files[base_index].path);
        if(map_index>=0)snprintf(previous_map,sizeof(previous_map),"%s",catalog.files[map_index].path);
        catalog=*next;base_index=map_index=-1;
        for(unsigned i=0;i<catalog.count;i++) {
            if(catalog.files[i].kind==XZ_WAD_BASE && (base_index<0 || !strcmp(previous_base,catalog.files[i].path)))base_index=(int)i;
            if(previous_map[0]&&!strcmp(previous_map,catalog.files[i].path))map_index=(int)i;
        }
    }
    catalog_scanning=0;pthread_mutex_unlock(&browser_mutex);free(next);return NULL;
}
void xz_doom_browser_scan(void) {
    pthread_mutex_lock(&browser_mutex);
    if(!catalog_scanning) {pthread_t thread;catalog_scanning=1;
        if(pthread_create(&thread,NULL,scan_games,NULL)==0)pthread_detach(thread);else catalog_scanning=0;
    }
    pthread_mutex_unlock(&browser_mutex);
}
void xz_doom_browser_read(int offset,struct xz_doom_browser *out) {
    memset(out,0,sizeof(*out));pthread_mutex_lock(&browser_mutex);
    out->scanning=catalog_scanning;out->total=(int)catalog.count;out->base=base_index;out->map=map_index;
    out->chex=base_index>=0&&catalog.files[base_index].chex;
    if(offset<0)offset=0;
    if(offset>=out->total)offset=out->total>0?(out->total-1)/6*6:0;
    out->offset=offset;
    for(int i=offset;i<out->total&&i<offset+6;i++) {
        int row=out->count++;struct xz_wad_file *file=&catalog.files[i];
        const char *source=!strncmp(file->path,"/media/usb1/",12)?"USB1":!strncmp(file->path,"/media/usb2/",12)?"USB2":"BUNDLED";
        snprintf(out->labels[row],96,"%s: %.84s",source,file->name);
        snprintf(out->details[row],64,"%s%s",i==base_index?"BASE: ":i==map_index?"MAP: ":"",file->reason);
    }
    char reason[96];out->can_play=xz_wad_compatible(base_index>=0?&catalog.files[base_index]:NULL,map_index>=0?&catalog.files[map_index]:NULL,reason)==0;
    snprintf(out->selection,96,"%.42s + %.42s",base_index>=0?catalog.files[base_index].name:"NO BASE GAME",map_index>=0?catalog.files[map_index].name:"NO ADD-ON");
    snprintf(out->status,96,"%s",catalog_scanning?"SEARCHING BOTH USB PORTS":catalog.truncated?"SEARCH LIMIT REACHED / USE USB WADS FOLDER":reason);
    pthread_mutex_unlock(&browser_mutex);
}
int xz_doom_browser_select(int index,char reason[96]) {
    int result=-1;pthread_mutex_lock(&browser_mutex);
    if(index>=0 && (unsigned)index<catalog.count) {
        struct xz_wad_file *file=&catalog.files[index];
        if(file->kind==XZ_WAD_BASE) {base_index=index;map_index=-1;result=0;}
        else if(file->kind==XZ_WAD_MAP && xz_wad_compatible(base_index>=0?&catalog.files[base_index]:NULL,file,reason)==0) {map_index=index;result=0;}
        else if(file->kind!=XZ_WAD_MAP)snprintf(reason,96,"%s",file->reason);
        if(!result)snprintf(reason,96,"SELECTED: %.85s",file->name);
    }else snprintf(reason,96,"RESCAN USB GAME FILES");
    pthread_mutex_unlock(&browser_mutex);return result;
}
void xz_doom_browser_clear_map(void) {pthread_mutex_lock(&browser_mutex);map_index=-1;pthread_mutex_unlock(&browser_mutex);}
int xz_doom_choose_game(int chex) {
    int result=-1;pthread_mutex_lock(&browser_mutex);
    if(base_index>=0&&catalog.files[base_index].chex==!!chex)result=0;
    else for(unsigned i=0;i<catalog.count;i++)if(catalog.files[i].kind==XZ_WAD_BASE&&catalog.files[i].chex==!!chex){base_index=(int)i;map_index=-1;result=0;break;}
    pthread_mutex_unlock(&browser_mutex);if(result)xz_doom_browser_scan();return result;
}
uint32_t xz_game_milliseconds(void) {
    struct timespec t;
    if(syscall(SYS_clock_gettime,CLOCK_MONOTONIC,&t)) return 0;
    return (uint32_t)((uint64_t)t.tv_sec*1000+(uint32_t)t.tv_nsec/1000000);
}
int xz_game_active(const struct xz_game_input *s,uint32_t now) {
    return s && s->magic==XZ_GAME_INPUT_MAGIC && s->version==1 &&
        __atomic_load_n(&s->owner,__ATOMIC_ACQUIRE) &&
        (uint32_t)(now-__atomic_load_n(&s->heartbeat,__ATOMIC_ACQUIRE))<750;
}
int xz_game_decode(struct xz_game_native *b,int deck,const void *input,uint32_t now) {
    if(!b || !b->input || !input || deck<0 || deck>1) return 0;
    const unsigned char *p=input; uint16_t key; memcpy(&key,p+8,2);
    unsigned op=p[11]&15; int active=xz_game_active(b->input,now);
    /* Exact 1.26: signed delta at +12, signed speed at +16. +20 is
     * an absolute unsigned counter and must never determine direction. */
    if(key==0x4305 && op==4 && active) {
        int32_t delta; float speed; memcpy(&delta,p+12,4); memcpy(&speed,p+16,4);
        int32_t direction=delta<0?-1:delta>0?1: speed>=-128 && speed<0?-1:speed>0 && speed<=128?1:0;
        __atomic_store_n(&b->input->jog[deck],(uint32_t)direction,__ATOMIC_RELAXED);
        __atomic_store_n(&b->input->jog_time[deck],now,__ATOMIC_RELEASE); return 1;
    }
    int index=key==0x4101?0:key==0x4102?1:key==0x4306?10:key>=0x4119&&key<=0x4120?(int)key-0x4119+2:-1;
    if(index<0) return 0;
    uint32_t bit=1u<<(unsigned)index;
    if(op==2 || op==3) {
        if(!(b->owned[deck]&bit)) return 0;
        b->owned[deck]&=~bit;
        __atomic_fetch_and(&b->input->buttons[deck],~bit,__ATOMIC_RELEASE); return 1;
    }
    if(active && op==0) {
        b->owned[deck]|=bit;
        __atomic_fetch_or(&b->input->buttons[deck],bit,__ATOMIC_RELEASE); return 1;
    }
    return active || (b->owned[deck]&bit) ? 1 : 0;
}
int xz_doom_bridge_start(void) {
    int fd=open(XZ_GAME_INPUT_PATH,O_RDWR|O_CREAT|O_NOFOLLOW,0600);
    struct stat info;
    if(fd<0) return -1;
    if(fstat(fd,&info) || !S_ISREG(info.st_mode) || info.st_uid!=geteuid() ||
       ftruncate(fd,sizeof(struct xz_game_input))) { close(fd); return -1; }
    bridge.input=mmap(NULL,sizeof(*bridge.input),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    close(fd);
    if(bridge.input==MAP_FAILED) { bridge.input=NULL; return -1; }
    /* A stale game must never retain ownership after a DJ app restart. */
    memset(bridge.input,0,sizeof(*bridge.input));
    bridge.input->version=1; bridge.input->magic=XZ_GAME_INPUT_MAGIC;
    if(access("/dev/shm/doom1.wad",R_OK)==0) {
        pthread_mutex_lock(&browser_mutex);
        if(!catalog.count&&xz_wad_inspect("/dev/shm/doom1.wad",&catalog.files[0])==0&&catalog.files[0].kind==XZ_WAD_BASE){catalog.count=1;base_index=0;}
        pthread_mutex_unlock(&browser_mutex);
    }
    return 0;
}
int xz_doom_physical_key(void *self,const void *input) {
    if(!self) return 0;
    unsigned channel=((const unsigned char *)self)[0x26];
    return xz_game_decode(&bridge,channel==1?0:channel==2?1:-1,input,xz_game_milliseconds());
}
static int game_paths(char binary[1024],char wad[1024]) {
    const char *usb=getenv("XZ_MODS_USB");
    if(!usb || usb[0]!='/' || strstr(usb,"..") || strchr(usb,'\n')) return -1;
    if(snprintf(binary,1024,"/dev/shm/xz-doom")>=1024 ||
       snprintf(wad,1024,"%s/VJTOOLS/doom/doom1.wad",usb)>=1024) return -1;
    if(access(wad,R_OK)) snprintf(wad,1024,"/dev/shm/doom1.wad");
    return 0;
}
int xz_doom_ready(void) {
    char binary[1024],wad[1024];
    if(!bridge.input || game_paths(binary,wad) || access(binary,X_OK))return 0;
    pthread_mutex_lock(&browser_mutex);int selected=base_index>=0,ready=selected;
    if(ready)ready=access(catalog.files[base_index].path,R_OK)==0;
    pthread_mutex_unlock(&browser_mutex);
    return selected?ready:access(wad,R_OK)==0;
}
static void *reap_game(void *value) {
    pid_t pid=(pid_t)(uintptr_t)value;
    while(waitpid(pid,NULL,0)<0 && errno==EINTR) {}
    return NULL;
}
int xz_doom_launch(void) {
    char binary[1024],wad[1024],map[1024]={0};int family=XZ_WAD_DOOM;
    if(!bridge.input || game_paths(binary,wad) || access(binary,X_OK)) return -1;
    pthread_mutex_lock(&browser_mutex);
    if(base_index>=0) {
        snprintf(wad,sizeof(wad),"%s",catalog.files[base_index].path);family=catalog.files[base_index].family;
        if(map_index>=0)snprintf(map,sizeof(map),"%s",catalog.files[map_index].path);
    }
    pthread_mutex_unlock(&browser_mutex);
    struct xz_wad_file base,addon;char reason[96];
    if(xz_wad_inspect(wad,&base) || (map[0]&&xz_wad_inspect(map,&addon)) || xz_wad_compatible(&base,map[0]?&addon:NULL,reason))return -1;
    family=base.family;
    if(xz_game_active(bridge.input,xz_game_milliseconds())) return -1;
    /* posix_spawn performs no application-side work after fork in the
     * multithreaded DJ process. Disable all preload hooks in the game. */
    extern char **environ;
    size_t count=0; while(environ[count]) count++;
    char **environment=calloc(count+2,sizeof(*environment));
    if(!environment) return -1;
    size_t out=0;
    for(size_t i=0;i<count;i++) if(strncmp(environ[i],"LD_PRELOAD=",11)) environment[out++]=environ[i];
    environment[out]="LD_PRELOAD=";
    const struct xz_wad_file *level=map[0]?&addon:&base;char episode[16],number[16];
    snprintf(episode,sizeof(episode),"%d",level->first_episode?level->first_episode:1);
    snprintf(number,sizeof(number),"%d",level->first_map?level->first_map:1);
    char patch[1024]={0};
    if(base.chex){const char *slash=strrchr(wad,'/');size_t length=slash?(size_t)(slash-wad+1):0;
        if(length+9>=sizeof(patch)){free(environment);return -1;}
        memcpy(patch,wad,length);snprintf(patch+length,sizeof(patch)-length,"chex.deh");
        if(access(patch,R_OK)){free(environment);return -1;}
    }
    char *args[24]={binary,"-nogui","-nosound","-iwad",wad,"-config","/dev/shm/xz-doom.cfg"};
    int argc=7;
    if(map[0]) {args[argc++]="-file";args[argc++]=map;}
    if(base.chex){args[argc++]="-gameversion";args[argc++]="chex";args[argc++]="-deh";args[argc++]=patch;}
    args[argc++]="-warp";if(family!=XZ_WAD_DOOM2)args[argc++]=episode;args[argc++]=number;
    args[argc++]="-xz-seconds";args[argc++]="0";args[argc]=NULL;
    pid_t pid; int result=posix_spawn(&pid,binary,NULL,NULL,args,environment);
    free(environment);
    if(!result) { pthread_t thread;
        if(pthread_create(&thread,NULL,reap_game,(void *)(uintptr_t)pid)==0) pthread_detach(thread);
        else { kill(pid,SIGTERM); while(waitpid(pid,NULL,0)<0 && errno==EINTR) {} return -1; }
    }
    return result?-1:0;
}
int xz_doom_running(void) { return xz_game_active(bridge.input,xz_game_milliseconds()); }
__attribute__((visibility("default"))) int xz_mods_game_active_v1(void) { return xz_doom_running(); }
