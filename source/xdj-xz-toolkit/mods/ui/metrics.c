/* SPDX-License-Identifier: MIT */
#include "metrics.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

int xz_metrics_temperature_level(int celsius){return celsius>=90?2:celsius>=80?1:0;}
int xz_metrics_fresh(const struct xz_metrics_sample *s,uint32_t now){return s&&s->valid&&(uint32_t)(now-s->sampled_ms)<3500;}
int xz_metrics_hit(int x,int y){return x>=XZ_METRICS_X&&x<XZ_METRICS_X+XZ_METRICS_W&&y>=0&&y<24;}
static uint64_t memory(const char *text,const char *key,int *found){
 size_t n=strlen(key);*found=0;
 for(const char *p=text;p&&*p;){
  if(!strncmp(p,key,n)&&p[n]==':'){
   char *end;unsigned long long value=strtoull(p+n+1,&end,10);
   if(end!=p+n+1){*found=1;return value;}return 0;
  }
  p=strchr(p,'\n');if(p)p++;
 }
 return 0;
}
void xz_metrics_parse(struct xz_metrics_counters *previous,struct xz_metrics_sample *s,const char *stat,const char *mem,const char *temp,const char *type,uint32_t now){
 memset(s,0,sizeof(*s));s->sampled_ms=now;
 struct xz_metrics_counters current={0};
 for(const char *p=stat;p&&*p;){
  char name[16]={0};unsigned long long v[8]={0};
  size_t length=strcspn(p," \t\r\n");
  if(length<sizeof(name))memcpy(name,p,length);
  const char *cursor=p+length;int n=1;
  if(!strncmp(name,"cpu",3))for(int i=0;i<8;i++){
   char *end;v[i]=strtoull(cursor,&end,10);if(end==cursor)break;cursor=end;n++;
  }
  int index=!strcmp(name,"cpu")?0:strlen(name)==4&&name[0]=='c'&&name[1]=='p'&&name[2]=='u'&&name[3]>='0'&&name[3]<='3'?name[3]-'0'+1:-1;
  if(n>=5&&index>=0){
   current.count|=1u<<index;for(int i=0;i<8;i++)current.cpu[index][i]=v[i];
   if(previous->initialized&&(previous->count&(1u<<index))){
    uint64_t total=0,idle=0;int reset=0;
    for(int i=0;i<8;i++){if(current.cpu[index][i]<previous->cpu[index][i])reset=1;else{uint64_t d=current.cpu[index][i]-previous->cpu[index][i];total+=d;if(i==3||i==4)idle+=d;}}
    if(!reset&&total){uint32_t busy=(uint32_t)((total-idle)*1000/total);if(index)s->core_permille[index-1]=busy;else{s->cpu_permille=busy;s->valid|=XZ_METRICS_CPU;}}
   }
  }
  p=strchr(p,'\n');if(p)p++;
 }
 current.initialized=!!current.count;*previous=current;
 int a,b,unused;
 uint64_t total=memory(mem,"MemTotal",&a),free=memory(mem,"MemFree",&b);
 uint64_t cached=memory(mem,"Cached",&unused),shmem=memory(mem,"Shmem",&unused);
 uint64_t cache=(cached>shmem?cached-shmem:0)+memory(mem,"Buffers",&unused)+memory(mem,"SReclaimable",&unused);
 if(a&&b&&total&&free<=total&&total/1024<=UINT32_MAX){
  s->total_mib=(uint32_t)(total/1024);s->free_mib=(uint32_t)(free/1024);s->cache_mib=(uint32_t)(cache/1024);
  s->used_mib=(uint32_t)((total>free+cache?total-free-cache:0)/1024);s->valid|=XZ_METRICS_RAM;
 }
 if(temp&&type&&!strncmp(type,"anatoptz",8)&&strcspn(type,"\r\n")==8){
  char *end;long value=strtol(temp,&end,10);
  if(end!=temp&&strspn(end," \t\r\n")==strlen(end)&&value>=-40&&value<=150){s->temperature_c=(int32_t)value;s->valid|=XZ_METRICS_TEMP;}
 }
}
