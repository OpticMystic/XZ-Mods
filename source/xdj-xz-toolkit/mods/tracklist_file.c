/* SPDX-License-Identifier: MIT */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "tracklist_file.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void xz_tl_writer_init(struct xz_tl_writer *w){
 memset(w,0,sizeof *w);
 struct stat st;
 if(!stat("/",&st)){w->forbid_dev=st.st_dev;w->forbid_dev_set=1;}
}
static int split(const char *path,char *dir,size_t cap,const char **name){
 const char *slash=strrchr(path,'/');
 if(!slash||slash==path||!slash[1])return -1;
 size_t n=(size_t)(slash-path);if(n>=cap)return -1;
 memcpy(dir,path,n);dir[n]=0;*name=slash+1;return 0;
}
static ssize_t do_write(struct xz_tl_writer *w,int fd,const void *p,size_t n){
 return w->write_fn?w->write_fn(fd,p,n):write(fd,p,n);
}
static int do_sync(struct xz_tl_writer *w,int fd){if(w->no_sync)return 0;w->syncs++;return w->sync_fn?w->sync_fn(fd):fsync(fd);}
static int write_all(struct xz_tl_writer *w,int fd,const char *p,size_t n){
 while(n){
  ssize_t done=do_write(w,fd,p,n);
  if(done<0&&errno==EINTR)continue;
  if(done<=0){if(!done)errno=ENOSPC;return -1;}
  p+=done;n-=(size_t)done;w->writes++;
 }
 return 0;
}
static enum xz_tl_write_result fail(struct xz_tl_writer *w,const struct xz_tl_op *op,int error){
 w->failed=1;w->failed_seq=op->file_seq;w->last_errno=error?error:EIO;w->failures++;
 return XZ_TL_FAILED;
}
enum xz_tl_write_result xz_tl_write(struct xz_tl_writer *w,const struct xz_tl_op *op){
 w->ops++;
 if(w->failed&&w->failed_seq==op->file_seq){w->skipped++;return XZ_TL_SKIPPED;}
 char dir[XZ_TL_PATH+8],wav_dir[XZ_TL_PATH];const char *name,*wav;
 if(split(op->path,dir,sizeof dir,&name)||split(op->wav,wav_dir,sizeof wav_dir,&wav)||strcmp(dir,wav_dir))
  return fail(w,op,EINVAL);
 int dfd=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
 if(dfd<0)return fail(w,op,errno);
 struct stat st;int error=0,fd=-1;off_t before=-1;
 if(fstat(dfd,&st)){error=errno;goto done;}
 if(w->forbid_dev_set&&st.st_dev==w->forbid_dev){error=EXDEV;goto done;}   /* the stick is gone */
 /* A recording beside it: the final name, or the file stock writes while it records (recorder/SEAM.md: the
    WavWriter writes RECTMP.WAV and renames it to RECnnn.WAV when the file closes). */
 struct stat ws;
 int present=!fstatat(dfd,wav,&ws,AT_SYMLINK_NOFOLLOW)&&S_ISREG(ws.st_mode)&&ws.st_dev==st.st_dev;
 if(!present)present=!fstatat(dfd,XZ_TL_RECORDING_NAME,&ws,AT_SYMLINK_NOFOLLOW)&&S_ISREG(ws.st_mode)&&ws.st_dev==st.st_dev;
 if(!present){error=ENOENT;goto done;}
 int flags=O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC;
 flags|=op->kind==XZ_TL_OP_OPEN||op->kind==XZ_TL_OP_WHOLE?O_TRUNC:O_APPEND;
 fd=openat(dfd,name,flags,0644);
 if(fd<0){error=errno;goto done;}
 struct stat fs;
 if(fstat(fd,&fs)){error=errno;goto done;}
 if(!S_ISREG(fs.st_mode)||fs.st_nlink!=1){error=EINVAL;goto done;}
 before=fs.st_size;
 if(write_all(w,fd,op->text,op->length)||do_sync(w,fd)){
  error=errno;
  if(before>=0&&ftruncate(fd,before)==0)(void)do_sync(w,fd);              /* keep whole lines */
  goto done;
 }
 if((op->kind==XZ_TL_OP_OPEN||op->kind==XZ_TL_OP_RESUME||op->kind==XZ_TL_OP_WHOLE)&&before==0&&do_sync(w,dfd)){error=errno;goto done;}
done:
 if(fd>=0&&close(fd)&&!error)error=errno;
 close(dfd);
 return error?fail(w,op,error):XZ_TL_WROTE;
}
