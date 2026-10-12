/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "browser.h"
#include "catalog.h"
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>

static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static int running,created,master_valid,display_format;
static int columns[3]={XZ_KEY_COLUMN_TITLE,XZ_KEY_COLUMN_KEY,XZ_KEY_COLUMN_BPM},key_prefix=1,traffic=1,format_preference;
static struct xz_playing_key master;
static struct xz_key_catalog catalogs[2];
static unsigned revision,painted;
__attribute__((visibility("default"))) struct xz_key_browser_proof xz_mods_key_browser_proof_v1={.version=1};
static _Thread_local int primary_scope,scope_format,scope_usb;
static int safe_file(const char *path,struct stat *out){return !lstat(path,out)&&S_ISREG(out->st_mode);}
static int find_database(unsigned slot,char out[1024],struct stat *out_stat){
    char root[32];snprintf(root,sizeof(root),"/media/usb%u",slot+1);
    snprintf(out,1024,"%s/PIONEER/rekordbox/export.pdb",root);
    if(safe_file(out,out_stat))return 1;
    DIR *dir=opendir(root);if(!dir)return 0;struct dirent *entry;int found=0;
    while((entry=readdir(dir))){
        if(entry->d_name[0]=='.')continue;
        char folder[1024];struct stat stat;int n=snprintf(folder,sizeof(folder),"%s/%s",root,entry->d_name);
        if(n<0||(size_t)n>=sizeof(folder)||lstat(folder,&stat)||!S_ISDIR(stat.st_mode))continue;
        n=snprintf(out,1024,"%s/PIONEER/rekordbox/export.pdb",folder);
        if(n<0||n>=1024)continue;
        if(safe_file(out,out_stat)){found=1;break;}
    }
    closedir(dir);return found;
}
static int same_file(const struct stat *a,const struct stat *b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&a->st_mtime==b->st_mtime;}
static void *worker(void *unused){
    (void)unused;struct stat previous[2]={{0}};char previous_path[2][1024]={{0}};
    for(;;){
        pthread_mutex_lock(&mutex);int keep_running=running;pthread_mutex_unlock(&mutex);if(!keep_running)break;
        for(unsigned slot=0;slot<2;slot++){
            char path[1024]={0};struct stat stat={0};int found=find_database(slot,path,&stat);
            if(!strcmp(path,previous_path[slot])&&same_file(&stat,&previous[slot]))continue;
            struct xz_key_catalog next={0};
            if(found&&xz_key_catalog_load(path,&next))found=0;
            struct stat after;
            if(found&&(!safe_file(path,&after)||!same_file(&stat,&after))){xz_key_catalog_free(&next);continue;}
            pthread_mutex_lock(&mutex);struct xz_key_catalog old=catalogs[slot];catalogs[slot]=next;revision++;
            __atomic_store_n(&xz_mods_key_browser_proof_v1.tracks[slot],(uint32_t)next.count,__ATOMIC_RELAXED);pthread_mutex_unlock(&mutex);
            xz_key_catalog_free(&old);previous[slot]=stat;snprintf(previous_path[slot],1024,"%s",path);
        }
        pthread_mutex_lock(&mutex);
        if(running){struct timeval now;gettimeofday(&now,NULL);struct timespec until={now.tv_sec+1,now.tv_usec*1000};pthread_cond_timedwait(&changed,&mutex,&until);}
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}
int xz_key_browser_start(void){
    if(created)return 0;pthread_mutex_lock(&mutex);running=1;pthread_mutex_unlock(&mutex);
    if(pthread_create(&thread,NULL,worker,NULL)){running=0;return -1;}created=1;return 0;
}
void xz_key_browser_stop(void){
    if(!created)return;pthread_mutex_lock(&mutex);running=0;pthread_cond_signal(&changed);pthread_mutex_unlock(&mutex);
    pthread_join(thread,NULL);created=0;pthread_mutex_lock(&mutex);
    for(unsigned slot=0;slot<2;slot++)xz_key_catalog_free(&catalogs[slot]);master_valid=0;pthread_mutex_unlock(&mutex);
}
void xz_key_browser_master(const struct xz_playing_key *key,int format){
    pthread_mutex_lock(&mutex);int valid=key!=NULL;
    if(valid!=master_valid||format!=display_format||(valid&&
        (master.key.root!=key->key.root||master.key.minor!=key->key.minor||(master.cents<-25||master.cents>25)!=(key->cents<-25||key->cents>25))))revision++;
    master_valid=valid;display_format=format;if(valid)master=*key;pthread_mutex_unlock(&mutex);
}
int xz_key_browser_repaint(void){pthread_mutex_lock(&mutex);int result=painted!=revision;painted=revision;pthread_mutex_unlock(&mutex);return result;}
int xz_key_browser_columns_valid(const int value[3]){
    if(!value)return 0;
    for(int i=0;i<3;i++){
        if(value[i]<0||value[i]>=XZ_KEY_COLUMN_COUNT)return 0;
        for(int j=0;j<i;j++)if(value[i]==value[j])return 0;
    }
    return 1;
}
int xz_key_browser_column_choose(int value[3],int slot,int choice){
    if(!xz_key_browser_columns_valid(value)||slot<0||slot>2||choice<0||choice>=XZ_KEY_COLUMN_COUNT)return 0;
    for(int i=0;i<3;i++)if(i!=slot&&value[i]==choice)value[i]=value[slot];
    value[slot]=choice;return 1;
}
void xz_key_browser_configure(const int value[3],int prefix,int colors,int format){
    pthread_mutex_lock(&mutex);
    int next[3]={columns[0],columns[1],columns[2]};
    if(xz_key_browser_columns_valid(value))memcpy(next,value,sizeof(next));
    prefix=!!prefix;colors=!!colors;format=format>=0&&format<=2?format:0;
    if(memcmp(next,columns,sizeof(next))||prefix!=key_prefix||colors!=traffic||format!=format_preference)revision++;
    memcpy(columns,next,sizeof(next));key_prefix=prefix;traffic=colors;format_preference=format;
    pthread_mutex_unlock(&mutex);
}
void xz_key_browser_scope(int enter,int primary,int format,int usb){primary_scope=enter&&primary&&(unsigned)usb<2;scope_format=format;scope_usb=usb;}
static size_t put_ascii(uint16_t *out,size_t used,size_t cap,const char *text){
    for(size_t i=0;text[i]&&used<cap;i++)out[used++]=(unsigned char)text[i];
    return used;
}
size_t xz_key_browser_compose(const int value[3],int key_first,const struct xz_key_browser_fields *f,uint16_t out[264],size_t *key_lead){
    if(key_lead)*key_lead=0;
    if(!xz_key_browser_columns_valid(value)||!f||!out)return 0;
    int order[4],key=f->key&&f->key[0],count=xz_key_browser_order(value,key_first&&key,order);
    const size_t cap=263;size_t used=0;int fields=0,only_title=1;
    for(int i=0;i<count&&used<cap;i++){
        size_t start=used;
        if(fields)used=put_ascii(out,used,cap,"  ");
        size_t mark=used;
        switch(order[i]){
        case XZ_KEY_COLUMN_TITLE:
            for(size_t j=0;f->title&&j<f->title_length&&f->title[j]&&used<cap;j++)out[used++]=f->title[j];
            break;
        case XZ_KEY_COLUMN_KEY:
            if(key){used=put_ascii(out,used,cap,f->key);if(!start&&key_lead)*key_lead=used;}
            break;
        case XZ_KEY_COLUMN_ARTIST:
            for(size_t j=0;f->artist&&j<64&&f->artist[j]&&used<cap;j++)out[used++]=f->artist[j];
            break;
        case XZ_KEY_COLUMN_BPM:
            if(f->tempo&&f->tempo<100000){char bpm[12];snprintf(bpm,sizeof(bpm),"%u.%u",(unsigned)(f->tempo/100),(unsigned)(f->tempo%100/10));used=put_ascii(out,used,cap,bpm);}
            break;
        }
        if(used==mark){used=start;continue;}
        fields++;if(order[i]!=XZ_KEY_COLUMN_TITLE)only_title=0;
    }
    /* Do not leave a truncated high surrogate at the end. */
    if(used&&out[used-1]>=0xd800&&out[used-1]<=0xdbff)used--;
    out[used]=0;
    return only_title?0:used;
}
size_t xz_key_browser_prefix(const uint16_t *title,size_t length,uint16_t out[264],uint16_t prefix[8],uint32_t *color){
    if(primary_scope){__atomic_add_fetch(&xz_mods_key_browser_proof_v1.scoped,1,__ATOMIC_RELAXED);__atomic_store_n(&xz_mods_key_browser_proof_v1.last_length,(uint32_t)length,__ATOMIC_RELAXED);}
    if(!primary_scope||!title||!length||length>255||!color||!prefix)return 0;
    prefix[0]=0;*color=0;
    struct xz_key_entry track;uint16_t artist[64]={0};int value[3];
    pthread_mutex_lock(&mutex);
    memcpy(value,columns,sizeof(value));int lead=key_prefix,colors=traffic;
    int format=format_preference==1?XZ_KEY_CLASSIC:format_preference==2?XZ_KEY_CAMELOT:scope_format;
    int found=xz_key_catalog_track(&catalogs[scope_usb],title,length,&track);
    if(found==1&&track.artist_id)xz_key_catalog_artist(&catalogs[scope_usb],track.artist_id,artist);
    enum xz_key_match match=found==1&&master_valid&&track.key.root>=0?xz_key_match((struct xz_playing_key){.key=track.key},master):XZ_KEY_UNKNOWN;
    pthread_mutex_unlock(&mutex);
    /* An unknown or ambiguous title keeps the stock row: no field is borrowed from another track. */
    if(found!=1){__atomic_add_fetch(&xz_mods_key_browser_proof_v1.unknown,1,__ATOMIC_RELAXED);return 0;}
    char label[8]="";if(track.key.root>=0)xz_music_key_format(track.key,format,label);
    struct xz_key_browser_fields fields={title,length,label[0]?label:NULL,artist,track.tempo};
    size_t key_lead,n=xz_key_browser_compose(value,lead,&fields,out,&key_lead);
    if(!n)return 0;
    if(label[0])__atomic_add_fetch(&xz_mods_key_browser_proof_v1.prefixes,1,__ATOMIC_RELAXED);
    else __atomic_add_fetch(&xz_mods_key_browser_proof_v1.unknown,1,__ATOMIC_RELAXED);
    /* Repaint only a leading key: its glyphs start at the stock cell origin. Traffic off
       keeps the stock row ink, so there is nothing to repaint. */
    if(colors&&key_lead&&key_lead<8){
        *color=match==XZ_KEY_COMPATIBLE?0x52d794:match==XZ_KEY_DETUNED?0xffbd59:0xa8b2bf;
        for(size_t i=0;i<key_lead;i++)prefix[i]=out[i];prefix[key_lead]=0;
    }
    return n;
}
static uint32_t list_generation;
void xz_key_browser_list_drawn(void){__atomic_add_fetch(&list_generation,1,__ATOMIC_RELAXED);}
uint32_t xz_key_browser_list_generation(void){return __atomic_load_n(&list_generation,__ATOMIC_RELAXED);}
const char *xz_key_browser_status(void){
    pthread_mutex_lock(&mutex);int have=catalogs[0].count||catalogs[1].count;pthread_mutex_unlock(&mutex);
    return have?"USB KEYS INDEXED / GREEN MATCHES MASTER / AMBER IS DETUNED":"BROWSER KEYS: EXPORT ANALYZED TRACKS FROM REKORDBOX TO USB";
}

enum { KEY_LINE_BASE = 0x224e19c, KEY_LINE_STRIDE = 0x224 };
static pthread_mutex_t key_line_mutex = PTHREAD_MUTEX_INITIALIZER;
static char key_line_text[2][32];
__attribute__((visibility("default"))) struct xz_key_line_proof xz_mods_key_line_proof_v1 = {.version = 1};

int xz_key_line_deck(uintptr_t text){
    if(text<KEY_LINE_BASE||text>=KEY_LINE_BASE+2u*KEY_LINE_STRIDE)return -1;
    int deck=(int)((text-KEY_LINE_BASE)/KEY_LINE_STRIDE);
    __atomic_add_fetch(&xz_mods_key_line_proof_v1.matched[deck],1,__ATOMIC_RELAXED);
    return deck;
}
void xz_key_line_publish(int deck,const char *text){
    if(deck<0||deck>1)return;
    pthread_mutex_lock(&key_line_mutex);
    snprintf(key_line_text[deck],sizeof(key_line_text[deck]),"%s",text?text:"");
    pthread_mutex_unlock(&key_line_mutex);
}
size_t xz_key_line_adjacent(int deck,const uint16_t *text,size_t length,uint16_t out[96]){
    if(deck<0||deck>1||!text||!out||!length||length>64)return 0;
    char suffix[32];
    pthread_mutex_lock(&key_line_mutex);memcpy(suffix,key_line_text[deck],sizeof(suffix));pthread_mutex_unlock(&key_line_mutex);
    size_t n=0;while(n<sizeof(suffix)-1&&suffix[n])n++;
    if(!n)return 0;
    size_t used=0;
    for(size_t i=0;i<length&&text[i];i++)out[used++]=text[i];
    if(!used)return 0;
    out[used++]=' ';
    for(size_t i=0;i<n&&used<95;i++)out[used++]=(unsigned char)suffix[i];
    out[used]=0;
    __atomic_add_fetch(&xz_mods_key_line_proof_v1.drawn[deck],1,__ATOMIC_RELAXED);
    return used;
}
