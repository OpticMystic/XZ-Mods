/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "store.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

int xz_diary_store_init(struct xz_diary_store *s,const char *path,unsigned long mount_id){
 struct stat st;memset(s,0,sizeof *s);
 if(!path||strlen(path)>=sizeof s->root||lstat(path,&st)||!S_ISDIR(st.st_mode))return -1;
 strcpy(s->root,path);s->device=st.st_dev;s->inode=st.st_ino;s->mount_id=mount_id;return 0;
}
void xz_diary_store_queue(struct xz_diary_store *s,const char *line,size_t n){
 if(s->disabled||!n)return;
 if(n>XZ_DIARY_LINE_BYTES||line[n-1]!='\n'){if(s->dropped<1000000)s->dropped++;return;}
 while(n>sizeof s->pending-s->used){
  char *end=memchr(s->pending,'\n',s->used);unsigned discard=end?(unsigned)(end-s->pending)+1:s->used;
  s->used-=discard;memmove(s->pending,s->pending+discard,s->used);if(s->dropped<1000000)s->dropped++;
 }
 memcpy(s->pending+s->used,line,n);s->used+=(unsigned)n;
}
static int guard(struct xz_diary_store *s){
 struct stat st;
 if(lstat(s->root,&st)||!S_ISDIR(st.st_mode)||st.st_dev!=s->device||st.st_ino!=s->inode)return -1;
 return s->guard?s->guard(s->context,s):1;
}
static int writable(struct xz_diary_store *s,int fd){
 uint64_t free_bytes;int ro;struct statvfs v;
 if(s->space){if(s->space(s->context,fd,&free_bytes,&ro)){s->pause_reason=XZ_DIARY_IO_ERROR;return 0;}}
 else {if(fstatvfs(fd,&v)){s->pause_reason=XZ_DIARY_IO_ERROR;return 0;}free_bytes=(uint64_t)v.f_bavail*v.f_frsize;ro=!!(v.f_flag&ST_RDONLY);}
 s->pause_reason=ro?XZ_DIARY_READ_ONLY:free_bytes<XZ_DIARY_MIN_FREE+XZ_DIARY_BATCH_BYTES+65536u?XZ_DIARY_SPACE_LOW:XZ_DIARY_NONE;
 return s->pause_reason==XZ_DIARY_NONE;
}
static int child_dir(int parent,const char *name,int create){
 if(create&&mkdirat(parent,name,0755)&&errno!=EEXIST)return -1;
 return openat(parent,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
}
static void filename(char out[32],unsigned i){snprintf(out,32,"segment-%02u.jsonl",i);}
static int scan(struct xz_diary_store *s,int dir,unsigned *pick){
 int empty=-1,oldest=-1;unsigned gen[XZ_DIARY_SEGMENTS]={0};time_t times[XZ_DIARY_SEGMENTS]={0};
 unsigned maximum=0;
 for(unsigned i=0;i<XZ_DIARY_SEGMENTS;i++){
  char name[32],head[2049];struct stat st;filename(name,i);
  if(fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)){if(errno!=ENOENT)return -1;if(empty<0)empty=(int)i;continue;}
  if(!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<0||st.st_size>(off_t)XZ_DIARY_SEGMENT_BYTES)return -1;
  int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)return -1;
  struct stat pinned;int valid=!fstat(fd,&pinned)&&pinned.st_ino==st.st_ino&&pinned.st_dev==st.st_dev;
  ssize_t n=valid?read(fd,head,sizeof head-1):-1;close(fd);if(n<0)return -1;head[n]=0;
  /* Generation is private ring metadata. Never replay old content into new records. */
  char *line=strchr(head,'\n');if(line)*line=0;
  char *key=strstr(head,"\"segment_generation\":");
  if(key){char *end;unsigned long value=strtoul(key+21,&end,10);if(value>INT_MAX||end==key+21||(*end!=','&&*end!='}'))return -1;gen[i]=(unsigned)value;}
  if(gen[i]>maximum)maximum=gen[i];times[i]=st.st_mtime;
  if(oldest<0||gen[i]<gen[oldest]||(gen[i]==gen[oldest]&&times[i]<times[oldest]))oldest=(int)i;
 }
 if(maximum>=INT_MAX)return -1;
 s->generation=maximum+1;*pick=(unsigned)(empty>=0?empty:oldest);return empty>=0?0:1;
}
static int write_once(struct xz_diary_store *s,int fd,const void *data,size_t n){
 s->writes++;ssize_t wrote=s->write_once?s->write_once(s->context,fd,data,n):write(fd,data,n);
 if(wrote>0)s->written+=(uint64_t)wrote;return wrote==(ssize_t)n?0:-1;
}
static int sync_once(struct xz_diary_store *s,int fd){s->syncs++;return s->sync_once?s->sync_once(s->context,fd):fsync(fd);}
int xz_diary_store_flush(struct xz_diary_store *s,const struct xz_diary_session *session,uint32_t now){
 if(s->disabled)return -1;
 if(!s->used)return 0;
 int safe=guard(s);if(safe<0){s->disabled=1;s->used=0;s->pause_reason=XZ_DIARY_SOURCE_CHANGED;return -1;}
 if(!safe){s->pause_reason=XZ_DIARY_READ_BUSY;return 0;}
 if(s->attempted&&(uint32_t)(now-s->last_attempt)<XZ_DIARY_INTERVAL_MS)return 0;
 s->attempted=1;s->last_attempt=now;
 int root=-1,parent=-1,dir=-1,fd=-1,result=0,fresh=0;struct stat st;
 root=open(s->root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(root<0){result=-1;goto finish;}
 if(fstat(root,&st)||st.st_dev!=s->device||st.st_ino!=s->inode){result=-1;goto finish;}
 if(!writable(s,root))goto finish;
 if((safe=guard(s))<=0){result=safe;goto finish;}
 parent=child_dir(root,"VJ.Tools",1);if(parent<0){result=-1;goto finish;}
 if(fstat(parent,&st)||st.st_dev!=s->device){result=-1;goto finish;}
 if((safe=guard(s))<=0){result=safe;goto finish;}
 dir=child_dir(parent,"XZ-Mods-diary",1);if(dir<0){result=-1;goto finish;}
 if(fstat(dir,&st)||st.st_dev!=s->device){result=-1;goto finish;}
 char name[32],batch[XZ_DIARY_BATCH_BYTES+2048];unsigned bytes=0;
 if(!s->active||s->bytes+s->used>XZ_DIARY_SEGMENT_BYTES){
  unsigned pick;int remove=scan(s,dir,&pick);if(remove<0){result=-1;goto finish;}
  filename(name,pick);
  if((safe=guard(s))<=0){result=safe;goto finish;}
  if(remove&&unlinkat(dir,name,0)){result=-1;goto finish;}
  int n=xz_diary_header(batch,sizeof batch,session,s->generation);if(n<0){result=-1;goto finish;}
  bytes=(unsigned)n;
  if((safe=guard(s))<=0){result=safe;goto finish;}
  fd=openat(dir,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC,0600);
  if(fd<0){result=-1;goto finish;}
  s->segment=pick;s->bytes=0;fresh=1;
 }else{
  filename(name,s->segment);
  if((safe=guard(s))<=0){result=safe;goto finish;}
  fd=openat(dir,name,O_WRONLY|O_APPEND|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
  if(fd<0){result=-1;goto finish;}
 }
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_dev!=s->device||
    st.st_size!=(off_t)s->bytes||(!fresh&&st.st_ino!=s->file_inode)){result=-1;goto finish;}
 s->file_inode=st.st_ino;
 /* Header and records share one write, capped at 8 KiB. Keep complete lines. */
 unsigned consume=s->used;
 if(bytes+consume>XZ_DIARY_BATCH_BYTES){consume=XZ_DIARY_BATCH_BYTES-bytes;while(consume&&s->pending[consume-1]!='\n')consume--;}
 if(!consume){result=-1;goto finish;}
 memcpy(batch+bytes,s->pending,consume);bytes+=consume;
 if(!writable(s,root))goto finish;
 if((safe=guard(s))<=0){result=safe;goto finish;}
 if(write_once(s,fd,batch,bytes)){result=-1;goto finish;}
 s->bytes+=bytes;s->active=1;s->used-=consume;memmove(s->pending,s->pending+consume,s->used);
 if((safe=guard(s))<=0){result=safe;goto finish;}
 if(sync_once(s,fd)){result=-1;goto finish;}
 if(fresh){if((safe=guard(s))<=0){result=safe;goto finish;}if(sync_once(s,dir)){result=-1;goto finish;}}
 result=1;
finish:
 if(fd>=0&&close(fd))result=-1;
 if(dir>=0)close(dir);if(parent>=0)close(parent);if(root>=0)close(root);
 if(result<0){s->disabled=1;s->used=0;s->pause_reason=XZ_DIARY_IO_ERROR;}
 return result;
}
