/* SPDX-License-Identifier: MIT */
#include "tracklist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECENT 32
#define HELD 16
#define EOL "\r\n"

struct entry {
 uint32_t start_ms;             /* recording time of the first audible moment, clamped to 0 */
 int continued;                 /* the run started before this file */
 int deck;
 uint32_t committed_at;         /* monotonic, for hold_max_ms */
 char artist[XZ_TL_TEXT], title[XZ_TL_TEXT];
};
struct run {
 int active;                    /* audible now, or within gap_ms of its last audible moment */
 uint32_t start_ms, last_ms;    /* monotonic: first and last audible moment */
 uint32_t heard_ms, heard_file_ms;
};
struct deck {
 int loaded;
 uint64_t key; uint32_t load_id;
 char title[XZ_TL_TEXT], artist[XZ_TL_TEXT];
 uint32_t listed_seq;           /* the file this load is listed in, 0 none */
 struct run run;
 int air; uint32_t air_at; int air_seen;   /* last readable on-air value and when */
};
struct recent { uint64_t key; uint32_t last_ms, seq; };
struct file {
 int open, resumed, timeless;   /* timeless: resumed without a known time, nothing more is listed */
 uint32_t seq, generation;
 uint32_t origin_ms;            /* monotonic time of the file's 00:00:00 */
 uint32_t last_elapsed_ms;
 int air_mode;
 char wav[XZ_TL_PATH], txt[XZ_TL_PATH + 8], cue[XZ_TL_PATH + 8];
 char head[1024];               /* the header, kept for at_end mode */
 struct entry held[HELD]; unsigned held_n;
 struct entry list[XZ_TL_ENTRIES]; unsigned n;
};
struct xz_tl {
 struct xz_tl_config cfg;
 struct deck deck[XZ_TL_DECKS];
 struct recent recent[RECENT];
 struct file f;
 uint32_t next_seq;
 int have_prev; uint32_t prev_now;
 int rec_seen, rec_known, recording; uint32_t generation;
 int want_open, want_resume, handled; uint32_t want_origin, handled_generation;
 int rec_lost; uint32_t rec_lost_at;   /* the recorder unreadable since rec_lost_at, while a file is open */
 int skip_running;                     /* xz_tl_skip_running: mark the running recording handled at the next sample */
 struct xz_tl_op ops[XZ_TL_OPS]; unsigned op_head, op_n;
 uint32_t entries, files, ops_dropped, entries_dropped;
};

void xz_tl_default_config(struct xz_tl_config *c){
 c->heard_ms=30000u;c->heard_ms_no_air=60000u;c->gap_ms=8000u;c->repeat_ms=60000u;c->tail_ms=10000u;
 c->hold_max_ms=120000u;c->air_hold_ms=5000u;c->step_max_ms=1000u;c->rec_hold_ms=5000u;c->cue_sheet=1;c->at_end=1;
}
size_t xz_tl_size(void){return sizeof(struct xz_tl);}
void xz_tl_init(struct xz_tl *t,const struct xz_tl_config *c){
 memset(t,0,sizeof *t);
 if(c)t->cfg=*c;else xz_tl_default_config(&t->cfg);
 t->next_seq=1;
}
void xz_tl_op_free(struct xz_tl_op *op){if(op){free(op->text);op->text=NULL;op->length=0;}}

/* ---- text ---- */
static size_t clean(char *out,size_t cap,const char *in){
 size_t at=0;if(!cap)return 0;
 const unsigned char *p=(const unsigned char *)(in?in:"");
 while(*p){
  unsigned c=*p,need=c<0x80?0:(c&0xe0)==0xc0?1:(c&0xf0)==0xe0?2:(c&0xf8)==0xf0?3:4;
  int ok=need<4&&!(need==1&&c<0xc2)&&!(need==3&&c>0xf4);
  for(unsigned i=1;ok&&i<=need;i++)ok=(p[i]&0xc0)==0x80;
  if(ok&&need==2){unsigned v=(c&15)<<12|(p[1]&63)<<6;ok=v>=0x800&&!(v>=0xd800&&v<=0xdfff);}
  if(ok&&need==3){unsigned v=(c&7)<<18|(p[1]&63)<<12;ok=v>=0x10000&&v<=0x10ffff;}
  if(!ok){if(at+2>cap)break;out[at++]='?';p++;continue;}
  if(!need){if(at+2>cap)break;out[at++]=(char)(c<0x20||c==0x7f?' ':c);p++;continue;}
  if(at+need+2>cap)break;
  for(unsigned i=0;i<=need;i++)out[at++]=(char)p[i];
  p+=need+1;
 }
 while(at&&out[at-1]==' ')at--;
 out[at]=0;
 size_t lead=0;while(out[lead]==' ')lead++;
 if(lead){memmove(out,out+lead,at-lead+1);at-=lead;}
 return at;
}
size_t xz_tl_name(char *out,size_t cap,const char *artist,const char *title,int deck){
 char a[XZ_TL_TEXT],t[XZ_TL_TEXT];
 size_t na=clean(a,sizeof a,artist),nt=clean(t,sizeof t,title);
 int n;
 if(na&&nt)n=snprintf(out,cap,"%s - %s",a,t);
 else if(nt)n=snprintf(out,cap,"%s",t);
 else if(na)n=snprintf(out,cap,"%s - (untitled)",a);
 else n=snprintf(out,cap,"Unknown track (DECK %d)",deck+1);
 if(n<0||!cap)return 0;
 if((size_t)n<cap)return (size_t)n;
 /* Cut: drop a last character that snprintf left incomplete. */
 size_t at=cap-1,lead=at;
 while(lead&&((unsigned char)out[lead-1]&0xc0)==0x80)lead--;
 if(lead){
  unsigned c=(unsigned char)out[lead-1];size_t want=c<0x80?1:(c&0xe0)==0xc0?2:(c&0xf0)==0xe0?3:4;
  if(at-(lead-1)<want)at=lead-1;
 }
 out[at]=0;return at;
}
void xz_tl_clock(char out[16],uint32_t ms){
 uint32_t s=ms/1000u;snprintf(out,16,"%02u:%02u:%02u",s/3600u,s/60u%60u,s%60u);
}
int xz_tl_sibling(char *out,size_t cap,const char *wav,const char *ext_upper){
 size_t n=wav?strlen(wav):0;
 if(n<5||wav[n-4]!='.'||!ext_upper||strlen(ext_upper)!=3)return -1;
 const char *e=wav+n-3;
 if((e[0]|32)!='w'||(e[1]|32)!='a'||(e[2]|32)!='v')return -1;
 const char *slash=strrchr(wav,'/');if(slash&&slash+1>=wav+n-4)return -1;   /* no base name */
 if(n+1>cap)return -1;
 memcpy(out,wav,n-3);
 int lower=e[0]=='w';
 for(int i=0;i<3;i++)out[n-3+(size_t)i]=(char)(lower?ext_upper[i]|32:ext_upper[i]);
 out[n]=0;return 0;
}
static const char *base(const char *path){const char *s=strrchr(path,'/');return s?s+1:path;}

/* ---- ops ---- */
static void queue(struct xz_tl *t,enum xz_tl_op_kind kind,const char *path,const char *text,size_t length){
 if(t->op_n>=XZ_TL_OPS){t->ops_dropped++;return;}
 char *copy=malloc(length+1);if(!copy){t->ops_dropped++;return;}
 memcpy(copy,text,length);copy[length]=0;
 struct xz_tl_op *op=&t->ops[(t->op_head+t->op_n)%XZ_TL_OPS];
 memset(op,0,sizeof *op);
 op->kind=kind;op->file_seq=t->f.seq;op->text=copy;op->length=length;
 snprintf(op->path,sizeof op->path,"%s",path);snprintf(op->wav,sizeof op->wav,"%s",t->f.wav);
 t->op_n++;
}
int xz_tl_take(struct xz_tl *t,struct xz_tl_op *out){
 if(!t->op_n)return 0;
 *out=t->ops[t->op_head];memset(&t->ops[t->op_head],0,sizeof t->ops[t->op_head]);
 t->op_head=(t->op_head+1)%XZ_TL_OPS;t->op_n--;return 1;
}
void xz_tl_status(const struct xz_tl *t,struct xz_tl_status *s){
 memset(s,0,sizeof *s);s->file_open=t->f.open&&!t->f.timeless;s->on_air_mode=t->f.air_mode;
 s->entries=t->entries;s->files=t->files;s->ops_dropped=t->ops_dropped;s->entries_dropped=t->entries_dropped;
}

/* ---- helpers ---- */
static uint64_t ident_key(const struct deck *d){
 if(d->key)return d->key;
 if(!d->title[0]&&!d->artist[0])return 0;
 uint64_t h=1469598103934665603ull;                  /* FNV-1a over artist NUL title, top bit marks text */
 for(const char *p=d->artist;*p;p++)h=(h^(unsigned char)*p)*1099511628211ull;
 h=(h^0xff)*1099511628211ull;
 for(const char *p=d->title;*p;p++)h=(h^(unsigned char)*p)*1099511628211ull;
 return h|(1ull<<63);
}
static struct recent *recent_find(struct xz_tl *t,uint64_t key,int make,uint32_t now){
 if(!key)return NULL;
 struct recent *oldest=&t->recent[0];
 for(unsigned i=0;i<RECENT;i++){
  if(t->recent[i].key==key)return &t->recent[i];
  if(!t->recent[i].key){if(oldest->key)oldest=&t->recent[i];}
  else if(oldest->key&&(uint32_t)(now-t->recent[i].last_ms)>(uint32_t)(now-oldest->last_ms))oldest=&t->recent[i];
 }
 if(!make)return NULL;
 memset(oldest,0,sizeof *oldest);oldest->key=key;oldest->last_ms=now;return oldest;
}
static int effective_air(const struct xz_tl *t,const struct deck *d,uint32_t now){
 return d->air_seen&&(uint32_t)(now-d->air_at)<=t->cfg.air_hold_ms?d->air:-1;
}
static int air_mode(const struct xz_tl *t,uint32_t now){
 for(int i=0;i<XZ_TL_DECKS;i++)if(effective_air(t,&t->deck[i],now)>=0)return 1;
 return 0;
}
static uint32_t threshold(const struct xz_tl *t,uint32_t now){return air_mode(t,now)?t->cfg.heard_ms:t->cfg.heard_ms_no_air;}
static uint32_t file_time(const struct file *f,uint32_t mono,int *before){
 int32_t d=(int32_t)(mono-f->origin_ms);
 if(before)*before=d<0;
 return d<0?0u:(uint32_t)d;
}
static void line(struct xz_tl *t,enum xz_tl_op_kind kind,const char *path,const char *text){
 queue(t,kind,path,text,strlen(text));
}

/* ---- entries ---- */
static void commit(struct xz_tl *t,int deck,uint32_t start_mono,uint32_t now){
 struct deck *d=&t->deck[deck];d->listed_seq=t->f.seq;
 struct recent *r=recent_find(t,ident_key(d),1,now);if(r){r->seq=t->f.seq;r->last_ms=now;}
 if(t->f.held_n>=HELD){t->entries_dropped++;return;}
 struct entry *e=&t->f.held[t->f.held_n++];memset(e,0,sizeof *e);
 int before;e->start_ms=file_time(&t->f,start_mono,&before);e->continued=before;e->deck=deck;e->committed_at=now;
 memcpy(e->artist,d->artist,sizeof e->artist);memcpy(e->title,d->title,sizeof e->title);
}
static int entry_line(const struct entry *e,char *text,size_t cap){
 char clock[16],name[XZ_TL_LINE-64];
 xz_tl_clock(clock,e->start_ms);xz_tl_name(name,sizeof name,e->artist,e->title,e->deck);
 return snprintf(text,cap,"%s  %s%s" EOL,clock,name,e->continued?"  (continued)":"");
}
static void write_entry(struct xz_tl *t,const struct entry *e){
 char text[XZ_TL_LINE];entry_line(e,text,sizeof text);
 if(!t->cfg.at_end)line(t,XZ_TL_OP_APPEND,t->f.txt,text);
 if(t->f.n<XZ_TL_ENTRIES)t->f.list[t->f.n++]=*e;else t->entries_dropped++;
 t->entries++;
}
static int entry_before(const struct entry *a,const struct entry *b){
 return a->start_ms!=b->start_ms?a->start_ms<b->start_ms:a->deck<b->deck;
}
static void sort_held(struct file *f){
 for(unsigned i=1;i<f->held_n;i++){
  struct entry e=f->held[i];unsigned j=i;
  while(j&&entry_before(&e,&f->held[j-1])){f->held[j]=f->held[j-1];j--;}
  f->held[j]=e;
 }
}
/* Writes held entries in time order. all: the file ends, nothing is pending any more. */
static void flush(struct xz_tl *t,uint32_t now,int all){
 struct file *f=&t->f;if(!f->held_n)return;
 sort_held(f);
 uint32_t earliest=UINT32_MAX;
 if(!all)for(int i=0;i<XZ_TL_DECKS;i++){
  const struct deck *d=&t->deck[i];
  if(d->run.active&&d->listed_seq!=f->seq){uint32_t s=file_time(f,d->run.start_ms,NULL);if(s<earliest)earliest=s;}
 }
 unsigned done=0;
 while(done<f->held_n){
  const struct entry *e=&f->held[done];
  if(!all&&e->start_ms>earliest&&(uint32_t)(now-e->committed_at)<t->cfg.hold_max_ms)break;
  write_entry(t,e);done++;
 }
 memmove(f->held,f->held+done,(f->held_n-done)*sizeof *f->held);f->held_n-=done;
}

/* ---- files ---- */
static void cue_text(struct xz_tl *t,char **out,size_t *length){
 struct file *f=&t->f;size_t cap=512+(size_t)(f->n<99?f->n:99)*(2*XZ_TL_TEXT+96);
 char *s=malloc(cap);*out=NULL;*length=0;if(!s)return;
 size_t at=0;char name[XZ_TL_PATH],a[XZ_TL_TEXT],ti[XZ_TL_TEXT];
#define PUT(...) do{int w=snprintf(s+at,cap-at,__VA_ARGS__);if(w<0||(size_t)w>=cap-at){free(s);return;}at+=(size_t)w;}while(0)
 snprintf(name,sizeof name,"%s",base(f->wav));
 char stem[XZ_TL_PATH];snprintf(stem,sizeof stem,"%s",name);{char *dot=strrchr(stem,'.');if(dot)*dot=0;}
 PUT("\xef\xbb\xbfREM COMMENT \"XZ Mods tracklist\"" EOL "TITLE \"%s\"" EOL "FILE \"%s\" WAVE" EOL,stem,name);
 for(unsigned i=0;i<f->n&&i<99;i++){
  const struct entry *e=&f->list[i];
  clean(a,sizeof a,e->artist);clean(ti,sizeof ti,e->title);
  for(char *p=a;*p;p++)if(*p=='"')*p='\'';
  for(char *p=ti;*p;p++)if(*p=='"')*p='\'';
  if(!ti[0])snprintf(ti,sizeof ti,a[0]?"(untitled)":"Unknown track (DECK %d)",e->deck+1);
  uint32_t frames=(uint32_t)((uint64_t)e->start_ms*75u/1000u);
  PUT("  TRACK %02u AUDIO" EOL "    TITLE \"%s\"" EOL,i+1,ti);
  if(a[0])PUT("    PERFORMER \"%s\"" EOL,a);
  PUT("    INDEX 01 %02u:%02u:%02u" EOL,frames/75u/60u,frames/75u%60u,frames%75u);
 }
 if(f->n>99)PUT("REM COMMENT \"%u more tracks are in the TXT file\"" EOL,f->n-99);
#undef PUT
 *out=s;*length=at;
}
static void close_file(struct xz_tl *t,uint32_t now,const char *footer,int clean_end){
 struct file *f=&t->f;if(!f->open)return;
 if(!f->timeless){
  /* Tail rule: a run still waiting that was heard long enough inside this file is listed. */
  for(int i=0;i<XZ_TL_DECKS;i++){
   struct deck *d=&t->deck[i];
   if(d->run.active&&d->listed_seq!=f->seq&&d->run.heard_file_ms>=t->cfg.tail_ms){
    struct recent *r=recent_find(t,ident_key(d),0,now);
    if(r&&r->seq==f->seq&&(uint32_t)(now-r->last_ms)<=t->cfg.repeat_ms){d->listed_seq=f->seq;continue;}
    commit(t,i,d->run.start_ms,now);
   }
  }
  flush(t,now,1);
 }
 if(!t->cfg.at_end){if(footer)line(t,XZ_TL_OP_CLOSE,f->txt,footer);}
 else{
  /* at_end: the whole list in one write, now that the file is complete. */
  size_t cap=sizeof f->head+(size_t)f->n*XZ_TL_LINE+(footer?strlen(footer):0)+128,at=0;
  char *s=malloc(cap);
  if(s){
   at+=(size_t)snprintf(s+at,cap-at,"%s",f->head);
   for(unsigned i=0;i<f->n;i++){int w=entry_line(&f->list[i],s+at,cap-at);if(w>0)at+=(size_t)w;}
   if(f->n>=XZ_TL_ENTRIES)at+=(size_t)snprintf(s+at,cap-at,"(Only the first %u tracks are listed.)" EOL,XZ_TL_ENTRIES);
   if(footer)at+=(size_t)snprintf(s+at,cap-at,"%s",footer);
   /* A list resumed after a restart appends: the part saved when the mods stopped is already in the file. */
   queue(t,f->resumed?XZ_TL_OP_RESUME:XZ_TL_OP_WHOLE,f->txt,s,at);free(s);
  }else t->ops_dropped++;
 }
 if(clean_end&&t->cfg.cue_sheet&&!f->resumed&&!f->timeless&&f->n){
  char *text;size_t length;cue_text(t,&text,&length);
  if(text){queue(t,XZ_TL_OP_WHOLE,f->cue,text,length);free(text);}
 }
 memset(f,0,sizeof *f);
}
static void head(struct xz_tl *t,enum xz_tl_op_kind kind,const char *text){
 snprintf(t->f.head,sizeof t->f.head,"%s",text);
 if(!t->cfg.at_end)line(t,kind,t->f.txt,text);
}
static void open_file(struct xz_tl *t,const struct xz_tl_input *in,int resume,uint32_t origin){
 struct file *f=&t->f;memset(f,0,sizeof *f);
 snprintf(f->wav,sizeof f->wav,"%s",in->rec.file);
 if(xz_tl_sibling(f->txt,sizeof f->txt,f->wav,"TXT")||xz_tl_sibling(f->cue,sizeof f->cue,f->wav,"CUE")){
  memset(f,0,sizeof *f);return;                       /* not a WAV path: write nothing */
 }
 f->open=1;f->seq=t->next_seq++;f->generation=in->rec.generation;f->origin_ms=origin;f->resumed=resume;
 f->air_mode=air_mode(t,in->now_ms);t->files++;
 for(int i=0;i<XZ_TL_DECKS;i++)t->deck[i].run.heard_file_ms=0;
 char text[1024],clock[16];const char *name=base(f->wav);
 if(resume){
  if(!in->rec.elapsed_known){
   f->timeless=1;
   snprintf(text,sizeof text,EOL "%s tracklist (XZ Mods): XZ Mods restarted during this recording and could not read" EOL
    "the time into it, so no further tracks are listed." EOL,name);
   head(t,XZ_TL_OP_RESUME,text);return;
  }
  xz_tl_clock(clock,in->rec.elapsed_ms);
  snprintf(text,sizeof text,EOL "%s tracklist (XZ Mods), continued after XZ Mods restarted at %s." EOL
   "%s" EOL EOL,name,clock,t->cfg.at_end?"Tracks heard before that are not in this list.":"Tracks heard just before that may be missing.");
  head(t,XZ_TL_OP_RESUME,text);
 }else{
  int n=snprintf(text,sizeof text,"%s tracklist (XZ Mods)" EOL,name);
  if(f->air_mode)snprintf(text+n,sizeof text-(size_t)n,
   "Times are from the start of %s. A track is listed once it has been on air for %u seconds." EOL EOL,
   name,t->cfg.heard_ms/1000u);
  else snprintf(text+n,sizeof text-(size_t)n,
   "Times are from the start of %s. The mixer's ON AIR state could not be read, so a track is listed" EOL
   "once it has played for %u seconds, even if its channel fader was down." EOL EOL,name,t->cfg.heard_ms_no_air/1000u);
  head(t,XZ_TL_OP_OPEN,text);
 }
 /* Tracks already heard when the file starts need nothing here: the same step's consider() lists every run
    that has reached the threshold, and one that started before origin_ms comes out at 00:00:00, continued. */
}

/* ---- the step ---- */
static void update_deck(struct xz_tl *t,int i,const struct xz_tl_deck_in *in,uint32_t now,uint32_t dt){
 struct deck *d=&t->deck[i];
 if(in->on_air>=0){d->air=in->on_air;d->air_at=now;d->air_seen=1;}
 if(!in->known)return;                                 /* runs age out through gap_ms */
 int changed=in->loaded!=d->loaded||(in->loaded&&(in->track_key!=d->key||in->load_id!=d->load_id));
 if(!in->track_key&&in->loaded&&!changed&&in->text_known&&(strcmp(in->title,d->title)||strcmp(in->artist,d->artist)))
  changed=d->title[0]||d->artist[0];                   /* keyless decks: new text is a new track */
 if(changed){
  memset(&d->run,0,sizeof d->run);d->listed_seq=0;
  d->loaded=in->loaded;d->key=in->loaded?in->track_key:0;d->load_id=in->loaded?in->load_id:0;
  d->title[0]=d->artist[0]=0;
 }
 if(in->loaded&&in->text_known){clean(d->title,sizeof d->title,in->title);clean(d->artist,sizeof d->artist,in->artist);}
 int air=effective_air(t,d,now);
 int audible=in->loaded&&in->playing==1&&(air_mode(t,now)?air==1:1);
 struct run *r=&d->run;
 if(audible){
  if(!r->active){memset(r,0,sizeof *r);r->active=1;r->start_ms=now;}
  else{r->heard_ms+=dt;if(t->f.open)r->heard_file_ms+=dt;}
  r->last_ms=now;
  struct recent *x=d->listed_seq?recent_find(t,ident_key(d),0,now):NULL;
  if(x)x->last_ms=now;
 }else if(r->active&&(uint32_t)(now-r->last_ms)>t->cfg.gap_ms)memset(r,0,sizeof *r);
}
static void consider(struct xz_tl *t,int i,uint32_t now){
 struct deck *d=&t->deck[i];struct file *f=&t->f;
 if(!f->open||f->timeless||!d->loaded||!d->run.active||d->listed_seq==f->seq||d->run.heard_ms<threshold(t,now))return;
 struct recent *r=recent_find(t,ident_key(d),0,now);
 if(r&&r->seq==f->seq&&(uint32_t)(now-r->last_ms)<=t->cfg.repeat_ms){d->listed_seq=f->seq;r->last_ms=now;return;}
 commit(t,i,d->run.start_ms,now);
}
void xz_tl_finish(struct xz_tl *t,uint32_t now){
 struct file *f=&t->f;if(!f->open)return;
 char footer[160],clock[16];xz_tl_clock(clock,file_time(f,now,NULL));
 snprintf(footer,sizeof footer,EOL "XZ Mods stopped at about %s; the list ends here." EOL,clock);
 close_file(t,now,footer,0);
}
void xz_tl_turn_off(struct xz_tl *t,uint32_t now){
 struct file *f=&t->f;if(!f->open)return;
 char footer[160],clock[16];xz_tl_clock(clock,file_time(f,now,NULL));
 snprintf(footer,sizeof footer,EOL "RECORDING TRACKLIST was turned off at about %s; the list ends here." EOL,clock);
 close_file(t,now,footer,0);
}
int xz_tl_stick_stopped(struct xz_tl *t,const char *mount,uint32_t now){
 struct file *f=&t->f;size_t n=mount?strlen(mount):0;
 if(!f->open||!n||strncmp(f->wav,mount,n))return 0;
 char footer[200],clock[16];
 if(f->timeless)snprintf(footer,sizeof footer,EOL "The recording ended because its USB stick was stopped (USB STOP)." EOL);
 else{
  xz_tl_clock(clock,file_time(f,now,NULL));
  snprintf(footer,sizeof footer,EOL "The recording ended at about %s because its USB stick was stopped (USB STOP)." EOL,clock);
 }
 /* A clean end: the recording is complete up to the stop. The file is forgotten here, so the recorder going idle
    afterwards finds no open list, and the same generation is never opened again (handled). */
 close_file(t,now,footer,1);
 return 1;
}
const char *xz_tl_open_wav(const struct xz_tl *t){return t->f.open?t->f.wav:"";}
void xz_tl_skip_running(struct xz_tl *t){t->skip_running=1;}
void xz_tl_step(struct xz_tl *t,const struct xz_tl_input *in){
 uint32_t now=in->now_ms,dt=0;
 if(t->have_prev){dt=now-t->prev_now;if(dt>t->cfg.step_max_ms)dt=t->cfg.step_max_ms;}
 t->have_prev=1;t->prev_now=now;
 for(int i=0;i<XZ_TL_DECKS;i++)update_deck(t,i,&in->deck[i],now,dt);

 /* The recorder. */
 const struct xz_tl_recorder *rec=&in->rec;
 struct file *f=&t->f;
 char footer[512],clock[16];
 /* A short gap in the recorder reads leaves the open file and the recorder history as they are; the decks still
    count against the last known clock. Only a gap of rec_hold_ms ends the file. */
 if(f->open&&!rec->known){
  if(!t->rec_lost){t->rec_lost=1;t->rec_lost_at=now;}
  if((uint32_t)(now-t->rec_lost_at)<t->cfg.rec_hold_ms){
   if(!f->timeless){for(int i=0;i<XZ_TL_DECKS;i++)consider(t,i,now);flush(t,now,0);}
   return;
  }
 }else if(rec->known)t->rec_lost=0;
 if(t->skip_running&&rec->known){
  t->skip_running=0;
  if(rec->recording){t->handled=1;t->handled_generation=rec->generation;t->want_open=0;}
 }
 int recording=rec->known&&rec->recording;
 /* A start we saw happen: the recorder was readable and idle, or on another file, at the last sample. */
 int edge=t->rec_seen&&t->rec_known&&recording&&(!t->recording||t->generation!=rec->generation);
 if(f->open&&!rec->known){
  xz_tl_clock(clock,file_time(f,t->rec_lost?t->rec_lost_at:now,NULL));
  snprintf(footer,sizeof footer,EOL "The recorder could no longer be read at about %s; the list ends here." EOL,clock);
  close_file(t,now,footer,0);
 }else if(f->open&&!recording){
  xz_tl_clock(clock,f->last_elapsed_ms);
  if(f->timeless)snprintf(footer,sizeof footer,EOL "End of recording." EOL);
  else snprintf(footer,sizeof footer,EOL "End of recording at %s." EOL,clock);
  close_file(t,now,footer,1);
 }else if(f->open&&rec->generation!=f->generation){
  /* The split happened when the new file's clock was at 0. */
  xz_tl_clock(clock,file_time(f,rec->elapsed_known?now-rec->elapsed_ms:now,NULL));
  snprintf(footer,sizeof footer,EOL "Recording continues in %s (split at %s)." EOL,
   rec->file[0]?base(rec->file):"the next file",clock);
  close_file(t,now,footer,1);
 }
 if(!recording)t->want_open=0;
 else if(!f->open&&(!t->handled||t->handled_generation!=rec->generation)){   /* re-arms a waiting open too */
  /* Fresh when we saw it start or its clock says it just started; otherwise the mods restarted mid-file. */
  t->want_open=1;t->handled=1;t->handled_generation=rec->generation;
  t->want_resume=!(edge||(rec->elapsed_known&&rec->elapsed_ms<5000u));
  t->want_origin=now;
 }
 /* A start that fails at once shows recording for a few ms (recorder/SEAM.md): create the file only when the
    recording has lasted a second. The times still count from the start. */
 if(t->want_open&&rec->file[0]&&((uint32_t)(now-t->want_origin)>=1000u||(rec->elapsed_known&&rec->elapsed_ms>=1000u))){
  t->want_open=0;
  open_file(t,in,t->want_resume,rec->elapsed_known?now-rec->elapsed_ms:t->want_origin);
 }
 t->rec_seen=1;t->rec_known=rec->known;t->recording=recording;t->generation=rec->generation;
 if(f->open){
  if(rec->elapsed_known){f->origin_ms=now-rec->elapsed_ms;f->last_elapsed_ms=rec->elapsed_ms;}
  else f->last_elapsed_ms=file_time(f,now,NULL);
  if(!f->timeless){
   for(int i=0;i<XZ_TL_DECKS;i++)consider(t,i,now);
   flush(t,now,0);
  }
 }
}
