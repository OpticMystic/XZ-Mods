/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "shell_playlists.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32 /* host tests only */
#include <direct.h>
#include <io.h>
#define fsync _commit
#define mkdir(path,mode) _mkdir(path)
#endif

#define HEADER "xzmods-playlists 1"
#define FILE_CAP (4u*1024u*1024u)

void xz_shpl_init(struct xz_shpl_set *s) { memset(s,0,sizeof *s);s->next_id=XZ_SHPL_FIRST_CREATED; }
void xz_shpl_free(struct xz_shpl_set *s) {
 if(!s)return;
 for(uint32_t i=0;i<s->n;i++)free(s->p[i].tracks);
 free(s->p);xz_shpl_init(s);
}
static int reserve(uint32_t **a, uint32_t *cap, uint32_t need) {
 if(need<=*cap)return 0;
 uint32_t next=*cap?*cap:16;while(next<need)next*=2;
 uint32_t *g=realloc(*a,(size_t)next*sizeof *g);if(!g)return -1;
 *a=g;*cap=next;return 0;
}
static struct xz_shpl *record(struct xz_shpl_set *s, uint32_t id) {
 for(uint32_t i=0;i<s->n;i++)if(s->p[i].id==id)return &s->p[i];
 if(s->n==s->cap){
  uint32_t next=s->cap?s->cap*2:16;struct xz_shpl *g=realloc(s->p,(size_t)next*sizeof *g);
  if(!g)return NULL;
  s->p=g;s->cap=next;
 }
 struct xz_shpl *r=&s->p[s->n++];memset(r,0,sizeof *r);r->id=id;return r;
}
const struct xz_shpl *xz_shpl_find(const struct xz_shpl_set *s, uint32_t id) {
 if(!s)return NULL;
 for(uint32_t i=0;i<s->n;i++)if(s->p[i].id==id)return &s->p[i];
 return NULL;
}
int xz_shpl_copy(struct xz_shpl_set *d, const struct xz_shpl_set *s) {
 xz_shpl_init(d);
 if(!s||!s->n){if(s)d->next_id=s->next_id;return 0;}
 if(!(d->p=calloc(s->n,sizeof *d->p)))return -1;
 d->cap=s->n;d->next_id=s->next_id;
 for(uint32_t i=0;i<s->n;i++){
  d->p[i]=s->p[i];d->p[i].tracks=NULL;d->p[i].cap=0;d->n=i+1;
  if(s->p[i].count&&(reserve(&d->p[i].tracks,&d->p[i].cap,s->p[i].count)))return xz_shpl_free(d),-1;
  if(s->p[i].count)memcpy(d->p[i].tracks,s->p[i].tracks,(size_t)s->p[i].count*sizeof(uint32_t));
 }
 return 0;
}
static void set_name(char out[XZ_SHELL_NAME_MAX], const char *in) {
 size_t n=0;
 /* One line per record: no control characters in a name. */
 for(;in&&in[n]&&n<XZ_SHELL_NAME_MAX-1;n++)out[n]=(unsigned char)in[n]<32?' ':in[n];
 out[n]=0;
}
int xz_shpl_apply(struct xz_shpl_set *s, const struct xz_shell_playlist_edit *e, const uint32_t *base, uint32_t base_count,
                  const char *base_name, uint32_t *created) {
 if(!s||!e)return -1;
 struct xz_shpl *r;
 switch(e->op){
 case XZ_SHELL_PL_CREATE:{
  if(!e->name[0])return -1;
  if(!(r=record(s,s->next_id)))return -1;
  r->created=1;r->has_tracks=1;r->parent=e->playlist;set_name(r->name,e->name);
  if(created)*created=r->id;
  s->next_id++;return 0;
 }
 case XZ_SHELL_PL_RENAME:
  if(!e->name[0]||!(r=record(s,e->playlist))||r->deleted)return -1;
  set_name(r->name,e->name);r->renamed=1;return 0;
 case XZ_SHELL_PL_DELETE:
  if(!(r=record(s,e->playlist)))return -1;
  r->deleted=1;free(r->tracks);r->tracks=NULL;r->count=r->cap=0;return 0;
 case XZ_SHELL_PL_ADD_TRACK:case XZ_SHELL_PL_REMOVE_TRACK:case XZ_SHELL_PL_MOVE_TRACK:{
  const struct xz_shpl *had=xz_shpl_find(s,e->playlist);
  if(had&&had->deleted)return -1;
  if(!(r=record(s,e->playlist)))return -1;
  if(!r->has_tracks){
   /* First track edit of a stick playlist: it keeps its own order from here on. */
   if(base_count&&(!base||reserve(&r->tracks,&r->cap,base_count)))return -1;
   if(base_count)memcpy(r->tracks,base,(size_t)base_count*sizeof *base);
   r->count=base_count;r->has_tracks=1;
   if(!r->renamed&&base_name)set_name(r->name,base_name);
  }
  if(e->op==XZ_SHELL_PL_ADD_TRACK){
   if(!e->track||r->count>=XZ_SHPL_TRACKS_MAX||reserve(&r->tracks,&r->cap,r->count+1))return -1;
   r->tracks[r->count++]=e->track;return 0;
  }
  /* The row given, or the track's first row when the list changed under the shell. */
  int32_t at=e->from>=0&&(uint32_t)e->from<r->count&&r->tracks[e->from]==e->track?e->from:-1;
  for(uint32_t i=0;at<0&&i<r->count;i++)if(r->tracks[i]==e->track)at=(int32_t)i;
  if(at<0)return -1;
  if(e->op==XZ_SHELL_PL_REMOVE_TRACK){
   memmove(r->tracks+at,r->tracks+at+1,(size_t)(r->count-(uint32_t)at-1)*sizeof *r->tracks);r->count--;return 0;
  }
  int32_t to=e->to<0?0:(uint32_t)e->to>=r->count?(int32_t)r->count-1:e->to;
  uint32_t t=r->tracks[at];
  if(to>at)memmove(r->tracks+at,r->tracks+at+1,(size_t)(to-at)*sizeof *r->tracks);
  else if(to<at)memmove(r->tracks+to+1,r->tracks+to,(size_t)(at-to)*sizeof *r->tracks);
  r->tracks[to]=t;return 0;
 }
 default:return -1;
 }
}
/* Growable text buffer. */
struct text { char *s; size_t n, cap; int failed; };
static void put(struct text *t, const char *s, size_t n) {
 if(t->failed)return;
 if(t->n+n+1>t->cap){
  size_t next=t->cap?t->cap:4096;while(next<t->n+n+1)next*=2;
  char *g=realloc(t->s,next);if(!g){t->failed=1;return;}
  t->s=g;t->cap=next;
 }
 memcpy(t->s+t->n,s,n);t->n+=n;t->s[t->n]=0;
}
static void putf(struct text *t, const char *fmt, uint32_t a, uint32_t b) {
 char line[64];int n=snprintf(line,sizeof line,fmt,a,b);if(n>0)put(t,line,(size_t)n);
}
char *xz_shpl_serialize(const struct xz_shpl_set *s, size_t *length) {
 struct text t={0};
 put(&t,HEADER "\n",sizeof HEADER);
 putf(&t,"next %u%.0u\n",s?s->next_id:XZ_SHPL_FIRST_CREATED,0);
 for(uint32_t i=0;s&&i<s->n;i++){
  const struct xz_shpl *r=&s->p[i];
  char flags[5];int f=0;
  if(r->deleted)flags[f++]='d';
  if(r->created)flags[f++]='c';
  if(r->renamed)flags[f++]='r';
  if(r->has_tracks)flags[f++]='t';
  if(!f)flags[f++]='-';
  flags[f]=0;
  putf(&t,"p %u %u ",r->id,r->parent);put(&t,flags,strlen(flags));put(&t," ",1);put(&t,r->name,strlen(r->name));put(&t,"\n",1);
  if(r->has_tracks){
   putf(&t,"t %u%.0u",r->id,0);
   for(uint32_t k=0;k<r->count;k++)putf(&t," %u%.0u",r->tracks[k],0);
   put(&t,"\n",1);
  }
 }
 if(t.failed){free(t.s);return NULL;}
 if(length)*length=t.n;
 return t.s;
}
static int number(const char **p, const char *end, uint32_t *out) {
 const char *s=*p;uint64_t v=0;int any=0;
 while(s<end&&*s==' ')s++;
 while(s<end&&*s>='0'&&*s<='9'){v=v*10+(uint64_t)(*s-'0');if(v>UINT32_MAX)return -1;s++;any=1;}
 if(!any)return -1;
 *p=s;*out=(uint32_t)v;return 0;
}
int xz_shpl_parse(struct xz_shpl_set *s, const char *text, size_t n) {
 xz_shpl_init(s);
 const char *end=text+n,*line=text;
 if(n<sizeof HEADER-1||memcmp(text,HEADER,sizeof HEADER-1))return -1;
 while(line<end){
  const char *eol=memchr(line,'\n',(size_t)(end-line));if(!eol)eol=end;
  const char *p=line+2;uint32_t id,parent,track;
  if(eol-line>5&&!memcmp(line,"next ",5)){p=line+5;if(!number(&p,eol,&id)&&id>=XZ_SHPL_FIRST_CREATED)s->next_id=id;}
  else if(eol-line>2&&!memcmp(line,"p ",2)&&!number(&p,eol,&id)&&!number(&p,eol,&parent)){
   while(p<eol&&*p==' ')p++;
   struct xz_shpl *r=record(s,id);if(!r){xz_shpl_free(s);return -1;}
   r->parent=parent;
   for(;p<eol&&*p!=' ';p++){
    if(*p=='d')r->deleted=1;else if(*p=='c')r->created=1;else if(*p=='r')r->renamed=1;else if(*p=='t')r->has_tracks=1;
   }
   if(p<eol)p++;
   size_t len=(size_t)(eol-p);if(len>XZ_SHELL_NAME_MAX-1)len=XZ_SHELL_NAME_MAX-1;
   memcpy(r->name,p,len);r->name[len]=0;
   if(r->created&&id>=s->next_id)s->next_id=id+1;
  }else if(eol-line>2&&!memcmp(line,"t ",2)&&!number(&p,eol,&id)){
   struct xz_shpl *r=record(s,id);if(!r){xz_shpl_free(s);return -1;}
   r->count=0;
   while(r->count<XZ_SHPL_TRACKS_MAX&&!number(&p,eol,&track)){
    if(reserve(&r->tracks,&r->cap,r->count+1)){xz_shpl_free(s);return -1;}
    r->tracks[r->count++]=track;
   }
  }
  line=eol+1;
 }
 return 0;
}
static int path_of(char *out, size_t cap, const char *root, const char *name) {
 return snprintf(out,cap,"%s/XZMODS%s%s",root,name[0]?"/":"",name)<(int)cap?0:-1;
}
int xz_shpl_load(struct xz_shpl_set *s, const char *root) {
 char path[1024];xz_shpl_init(s);
 if(path_of(path,sizeof path,root,"playlists.txt"))return -1;
 FILE *f=fopen(path,"rb");
 if(!f)return errno==ENOENT?0:-1;
 char *text=NULL;long n=-1;int rc=-1;
 if(!fseek(f,0,SEEK_END))n=ftell(f);
 if(n>0&&(unsigned long)n<=FILE_CAP&&!fseek(f,0,SEEK_SET)&&(text=malloc((size_t)n))&&fread(text,1,(size_t)n,f)==(size_t)n)
  rc=xz_shpl_parse(s,text,(size_t)n);
 else if(n==0)rc=0;
 free(text);fclose(f);return rc;
}
int xz_shpl_save(const struct xz_shpl_set *s, const char *root) {
 char dir[1024],path[1024],temp[1024];size_t n=0;
 if(path_of(dir,sizeof dir,root,"")||path_of(path,sizeof path,root,"playlists.txt")||path_of(temp,sizeof temp,root,"playlists.txt.tmp")){errno=ENAMETOOLONG;return -1;}
 if(mkdir(dir,0755)&&errno!=EEXIST)return -1;
 char *text=xz_shpl_serialize(s,&n);
 if(!text){errno=ENOMEM;return -1;}
 FILE *f=fopen(temp,"wb");
 if(!f){int e=errno;free(text);errno=e;return -1;}
 int ok=fwrite(text,1,n,f)==n&&!fflush(f)&&!fsync(fileno(f));
 int e=errno;
 if(fclose(f))ok=0;
 free(text);
 if(!ok){remove(temp);errno=e?e:EIO;return -1;}
 /* rename is the commit: a crash before it leaves the old file, after it the new one. */
#ifdef _WIN32
 remove(path); /* host tests: rename does not replace on Windows */
#endif
 if(rename(temp,path)){e=errno;remove(temp);errno=e;return -1;}
 return 0;
}
