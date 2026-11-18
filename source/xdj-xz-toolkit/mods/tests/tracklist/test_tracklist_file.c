/* SPDX-License-Identifier: MIT */
/* Host tests for the tracklist file writer on a real temporary folder, and an end-to-end run: core -> writer ->
   files on disk. Failures (full stick, missing recording, link in place of the file) are injected. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../../tracklist.h"
#include "../../tracklist_file.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures, checks;
#define CHECK(c) do{checks++;if(!(c)){failures++;fprintf(stderr,"%s:%d: CHECK failed: %s\n",__FILE__,__LINE__,#c);}}while(0)

static char root[256], folder[300];
static char *slurp(const char *path){
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 static char buf[65536];size_t n=fread(buf,1,sizeof buf-1,f);fclose(f);buf[n]=0;return buf;
}
static void touch(const char *path){int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd>=0){(void)!write(fd,"RIFF",4);close(fd);}}
static void op_make(struct xz_tl_op *op,enum xz_tl_op_kind kind,uint32_t seq,const char *base_name,const char *ext,const char *text){
 memset(op,0,sizeof *op);op->kind=kind;op->file_seq=seq;
 snprintf(op->wav,sizeof op->wav,"%s/%s.WAV",folder,base_name);
 snprintf(op->path,sizeof op->path,"%s/%s.%s",folder,base_name,ext);
 op->text=strdup(text);op->length=strlen(text);
}
static enum xz_tl_write_result run(struct xz_tl_writer *w,enum xz_tl_op_kind kind,uint32_t seq,const char *b,const char *ext,const char *text){
 struct xz_tl_op op;op_make(&op,kind,seq,b,ext,text);enum xz_tl_write_result r=xz_tl_write(w,&op);xz_tl_op_free(&op);return r;
}
static int full_after;     /* bytes the "stick" still takes */
static ssize_t full_write(int fd,const void *p,size_t n){
 if(full_after<=0){errno=ENOSPC;return -1;}
 size_t take=n<(size_t)full_after?n:(size_t)full_after;ssize_t r=write(fd,p,take);if(r>0)full_after-=(int)r;return r;
}

static void test_ops(void){
 struct xz_tl_writer w;xz_tl_writer_init(&w);w.forbid_dev_set=0;   /* the temp folder shares the root fs here */
 char wav[400],txt[400];snprintf(wav,sizeof wav,"%s/REC001.WAV",folder);snprintf(txt,sizeof txt,"%s/REC001.TXT",folder);
 /* No recording beside it: nothing is created. */
 CHECK(run(&w,XZ_TL_OP_OPEN,1,"REC001","TXT","head\r\n")==XZ_TL_FAILED&&access(txt,F_OK)!=0&&w.last_errno==ENOENT);
 CHECK(run(&w,XZ_TL_OP_APPEND,1,"REC001","TXT","line\r\n")==XZ_TL_SKIPPED);
 touch(wav);
 /* A stale list from a deleted recording with the same number is replaced. */
 FILE *f=fopen(txt,"wb");fputs("stale\r\n",f);fclose(f);
 CHECK(run(&w,XZ_TL_OP_OPEN,2,"REC001","TXT","head\r\n")==XZ_TL_WROTE);
 CHECK(run(&w,XZ_TL_OP_APPEND,2,"REC001","TXT","00:00:00  A - B\r\n")==XZ_TL_WROTE);
 CHECK(run(&w,XZ_TL_OP_CLOSE,2,"REC001","TXT","\r\nEnd.\r\n")==XZ_TL_WROTE);
 CHECK(run(&w,XZ_TL_OP_WHOLE,2,"REC001","CUE","cue\r\n")==XZ_TL_WROTE);
 CHECK(run(&w,XZ_TL_OP_WHOLE,2,"REC001","CUE","cue2\r\n")==XZ_TL_WROTE);
 CHECK(!strcmp(slurp(txt),"head\r\n00:00:00  A - B\r\n\r\nEnd.\r\n"));
 char cue[400];snprintf(cue,sizeof cue,"%s/REC001.CUE",folder);CHECK(!strcmp(slurp(cue),"cue2\r\n"));
 CHECK(w.syncs>=6);
 /* RESUME appends, and creates the file when it is missing. */
 CHECK(run(&w,XZ_TL_OP_RESUME,3,"REC001","TXT","resumed\r\n")==XZ_TL_WROTE);
 CHECK(!strcmp(slurp(txt),"head\r\n00:00:00  A - B\r\n\r\nEnd.\r\nresumed\r\n"));
 char wav2[400],txt2[400];snprintf(wav2,sizeof wav2,"%s/REC002.WAV",folder);snprintf(txt2,sizeof txt2,"%s/REC002.TXT",folder);
 touch(wav2);
 CHECK(run(&w,XZ_TL_OP_RESUME,4,"REC002","TXT","resumed\r\n")==XZ_TL_WROTE&&!strcmp(slurp(txt2),"resumed\r\n"));
 /* A link in place of the list is never followed. */
 char wav3[400],txt3[400],elsewhere[400];snprintf(wav3,sizeof wav3,"%s/REC003.WAV",folder);snprintf(txt3,sizeof txt3,"%s/REC003.TXT",folder);
 snprintf(elsewhere,sizeof elsewhere,"%s/elsewhere.txt",root);touch(wav3);
 CHECK(!symlink(elsewhere,txt3));
 CHECK(run(&w,XZ_TL_OP_OPEN,5,"REC003","TXT","head\r\n")==XZ_TL_FAILED&&access(elsewhere,F_OK)!=0);
 /* The stick fills during an append: the file is cut back to whole lines and the file's later ops are skipped. */
 char wav4[400],txt4[400];snprintf(wav4,sizeof wav4,"%s/REC004.WAV",folder);snprintf(txt4,sizeof txt4,"%s/REC004.TXT",folder);touch(wav4);
 CHECK(run(&w,XZ_TL_OP_OPEN,6,"REC004","TXT","head\r\n")==XZ_TL_WROTE);
 w.write_fn=full_write;full_after=5;
 CHECK(run(&w,XZ_TL_OP_APPEND,6,"REC004","TXT","00:01:00  half a line\r\n")==XZ_TL_FAILED&&w.last_errno==ENOSPC);
 CHECK(!strcmp(slurp(txt4),"head\r\n"));
 w.write_fn=NULL;
 CHECK(run(&w,XZ_TL_OP_APPEND,6,"REC004","TXT","later\r\n")==XZ_TL_SKIPPED);
 /* The next recording starts clean. */
 CHECK(run(&w,XZ_TL_OP_OPEN,7,"REC004","TXT","new\r\n")==XZ_TL_WROTE&&!strcmp(slurp(txt4),"new\r\n"));
 /* The recording's folder is on the unit's own filesystem (the stick went and its mount point stayed): refused. */
 struct stat st;stat(folder,&st);w.forbid_dev=st.st_dev;w.forbid_dev_set=1;
 CHECK(run(&w,XZ_TL_OP_APPEND,8,"REC004","TXT","x\r\n")==XZ_TL_FAILED&&w.last_errno==EXDEV);
 w.forbid_dev_set=0;
 /* The recording was removed between ops: nothing is written. */
 unlink(wav4);
 CHECK(run(&w,XZ_TL_OP_APPEND,9,"REC004","TXT","x\r\n")==XZ_TL_FAILED&&!strcmp(slurp(txt4),"new\r\n"));
 /* While stock is still writing RECTMP.WAV, the final name does not exist yet: the list is written anyway. */
 char tmp[400],txt5[400];snprintf(tmp,sizeof tmp,"%s/RECTMP.WAV",folder);snprintf(txt5,sizeof txt5,"%s/REC005.TXT",folder);touch(tmp);
 CHECK(run(&w,XZ_TL_OP_OPEN,11,"REC005","TXT","head\r\n")==XZ_TL_WROTE&&!strcmp(slurp(txt5),"head\r\n"));
 unlink(tmp);
 /* A path whose list would land in another folder than its recording is refused. */
 struct xz_tl_op op;op_make(&op,XZ_TL_OP_OPEN,10,"REC001","TXT","x");snprintf(op.path,sizeof op.path,"%s/REC001.TXT",root);
 CHECK(xz_tl_write(&w,&op)==XZ_TL_FAILED&&w.last_errno==EINVAL);xz_tl_op_free(&op);
}

/* End to end: the core's ops for a short set, carried out on disk. */
static void test_end_to_end(int at_end){
 struct xz_tl_writer w;xz_tl_writer_init(&w);w.forbid_dev_set=0;
 struct xz_tl *t=malloc(xz_tl_size());struct xz_tl_config c;xz_tl_default_config(&c);c.at_end=at_end;xz_tl_init(t,&c);
 unsigned during=0;              /* operations carried out while stock was still writing RECTMP.WAV */
 /* As stock records: RECTMP.WAV while recording, renamed to the final name when the file closes. */
 char wav[400],tmp[400];snprintf(wav,sizeof wav,"%s/REC100.WAV",folder);snprintf(tmp,sizeof tmp,"%s/RECTMP.WAV",folder);touch(tmp);
 struct xz_tl_input in;memset(&in,0,sizeof in);in.rec.known=1;
 for(int i=0;i<XZ_TL_DECKS;i++){in.deck[i].known=1;in.deck[i].on_air=0;}
 uint32_t now=5000,start=0;
 for(int tick=0;tick<400;tick++,now+=500){
  if(tick==10){in.rec.recording=1;in.rec.generation=9;snprintf(in.rec.file,sizeof in.rec.file,"%s",wav);start=now;}
  if(tick==20){struct xz_tl_deck_in *d=&in.deck[1];d->loaded=1;d->track_key=(1ull<<32)|5;d->text_known=1;
   snprintf(d->artist,XZ_TL_TEXT,"Artist");snprintf(d->title,XZ_TL_TEXT,"Title");d->playing=1;d->on_air=1;}
  if(tick==300){in.rec.recording=0;CHECK(!rename(tmp,wav));}
  in.now_ms=now;in.rec.elapsed_known=in.rec.recording;in.rec.elapsed_ms=in.rec.recording?now-start:0;
  xz_tl_step(t,&in);
  struct xz_tl_op op;while(xz_tl_take(t,&op)){CHECK(xz_tl_write(&w,&op)==XZ_TL_WROTE);during+=in.rec.recording;xz_tl_op_free(&op);}
 }
 char txt[400];snprintf(txt,sizeof txt,"%s/REC100.TXT",folder);
 CHECK(!strcmp(slurp(txt),"REC100.WAV tracklist (XZ Mods)\r\n"
  "Times are from the start of REC100.WAV. A track is listed once it has been on air for 30 seconds.\r\n\r\n"
  "00:00:05  Artist - Title\r\n\r\nEnd of recording at 00:02:24.\r\n"));
 char cue[400];snprintf(cue,sizeof cue,"%s/REC100.CUE",folder);
 CHECK(strstr(slurp(cue),"FILE \"REC100.WAV\" WAVE")&&strstr(slurp(cue),"INDEX 01 00:05:00"));
  CHECK(at_end?during==0:during==2);   /* live: the header and the line, beside RECTMP.WAV */
 unlink(wav);unlink(txt);unlink(cue);
 free(t);
}

int main(void){
 snprintf(root,sizeof root,"/tmp/xz-tracklist-test-%d",(int)getpid());
 if(mkdir(root,0755)){perror("mkdir");return 2;}
 snprintf(folder,sizeof folder,"%s/PIONEER REC",root);
 if(mkdir(folder,0755)){perror("mkdir");return 2;}
 test_ops();test_end_to_end(0);test_end_to_end(1);
 char cmd[600];snprintf(cmd,sizeof cmd,"rm -rf '%s'",root);if(system(cmd)){}
 printf("%s: %d checks, %d failures\n",failures?"FAIL":"PASS",checks,failures);
 return failures?1:0;
}
