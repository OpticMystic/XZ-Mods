/* SPDX-License-Identifier: MIT */
#include "key_strip.h"
#include "native_palette.h"
#include "themes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(p) do{if(!(p)){fprintf(stderr,"FAIL %s line %d\n",#p,__LINE__);exit(1);}}while(0)
static uint16_t original[800*480],frame[800*480],work[800*480];
static int inside(int x,int y,struct xz_key_strip_box b){return x>=b.x&&x<b.x+b.w&&y>=b.y&&y<b.y+b.h;}
static void save(const char *dir,const char *name){if(!dir)return;char path[2048];snprintf(path,sizeof(path),"%s/%s.raw",dir,name);FILE*f=fopen(path,"wb");CHECK(f&&fwrite(work,2,800*480,f)==800*480);fclose(f);}
static struct xz_key_strip_deck deck(const char *a,const char *b,int cents,int synced){
    struct xz_key_strip_deck d={.valid=1,.cents=cents,.synced=synced};
    snprintf(d.original,sizeof(d.original),"%s",a);snprintf(d.effective,sizeof(d.effective),"%s",b);d.moved=strcmp(a,b)!=0;return d;
}
static void render(int theme,struct xz_key_strip_deck d[2],int kind){
    memcpy(work,frame,sizeof(work));struct xz_key_strip_box bounds[2];
    for(int k=0;k<2;k++){
        CHECK(xz_key_strip_area(work,800,theme,k,&bounds[k])&&bounds[k].kind==kind);
        CHECK(xz_key_strip_render_deck(work,800,theme,k,&d[k],1)==bounds[k].w);
    }
    unsigned changed=0;
    for(int y=0;y<480;y++)for(int x=0;x<800;x++)if(work[y*800+x]!=frame[y*800+x]){
        changed++;CHECK(inside(x,y,bounds[0])||inside(x,y,bounds[1]));CHECK(y<312||y>341);
    }
    CHECK(changed);
    if(d[0].sync_master&&d[1].synced&&!d[0].error[0]&&!d[1].error[0]){
        uint16_t bg=work[(bounds[0].y+bounds[0].h-1)*800+bounds[0].x+bounds[0].w-1];
        CHECK(bg==work[(bounds[1].y+bounds[1].h-1)*800+bounds[1].x+bounds[1].w-1]);
        CHECK(bg!=frame[(bounds[0].y+bounds[0].h-1)*800+bounds[0].x+bounds[0].w-1]);
    }
    if(kind==XZ_KEY_STRIP_PLAY){
        for(int y=90;y<119;y++)for(int x=0;x<115;x++)CHECK(work[y*800+x]==frame[y*800+x]);
        for(int y=232;y<262;y++)for(int x=0;x<115;x++)CHECK(work[y*800+x]==frame[y*800+x]);
    }
    memcpy(work,frame,sizeof(work));
    for(int k=0;k<2;k++)CHECK(!xz_key_strip_render_deck(work,800,theme,k,&d[k],0));
    CHECK(!memcmp(work,frame,sizeof(work)));
}
int main(int argc,char **argv){
    CHECK(argc>=3);FILE*f=fopen(argv[1],"rb");CHECK(f&&fread(original,2,800*480,f)==800*480);fclose(f);
    int kind=atoi(argv[2]);CHECK(kind==XZ_KEY_STRIP_PLAY||kind==XZ_KEY_STRIP_BROWSE);const char*out=argc>3?argv[3]:NULL;
    struct xz_key_strip_deck live[2]={deck("9A","2A",-13,0),deck("9A","11A",-43,1)};
    live[0].sync_master=1;
    struct xz_key_strip_deck classic[2]={deck("F#m","Ebm",-50,1),deck("Bbm","Bbm",49,0)};
    struct xz_key_strip_deck errors[2]={deck("1A","9A",0,0),deck("4A","9A",0,0)};
    xz_key_sync_failure_text(XZ_KEY_SYNC_NEED_MASTER,0,errors[0].error);
    xz_key_sync_failure_text(XZ_KEY_SYNC_NO_MASTER_KEY,1,errors[1].error);
    for(int theme=0;theme<XZ_THEME_COUNT;theme++){
        for(size_t i=0;i<800*480;i++)frame[i]=theme?xz_native_palette_pixel(theme,original[i]):original[i];
        render(theme,live,kind);render(theme,classic,kind);render(theme,errors,kind);
    }
    memcpy(frame,original,sizeof(frame));memcpy(work,frame,sizeof(work));
    for(int k=0;k<2;k++)CHECK(xz_key_strip_render_deck(work,800,0,k,&live[k],1));save(out,"live");
    memcpy(work,frame,sizeof(work));for(int k=0;k<2;k++)CHECK(xz_key_strip_render_deck(work,800,0,k,&errors[k],1));save(out,"errors");
    if(kind==XZ_KEY_STRIP_BROWSE){struct xz_browse_head head={1,-1,NULL};CHECK(xz_browse_head_render(work,800,0,&head));save(out,"browse-full");}
    memset(work,0,sizeof(work));for(int k=0;k<2;k++)CHECK(!xz_key_strip_render_deck(work,800,0,k,&live[k],1));
    CHECK(!xz_browse_head_render(work,800,0,NULL));
    memcpy(work,frame,sizeof(work));for(int x=0;x<800;x++)work[314*800+x]=0xffff;
    for(int k=0;k<2;k++)CHECK(!xz_key_strip_render_deck(work,800,0,k,&live[k],1));
    CHECK(xz_browse_head_hit(430,16)==XZ_BROWSE_HEAD_SORT&&xz_browse_head_hit(570,16)==XZ_BROWSE_HEAD_COLUMN);
    printf("PASS %s: 24 themes, countdown/header bounds, titles untouched, off is stock, classic/cents/errors and unknown-screen refusal\n",kind==XZ_KEY_STRIP_PLAY?"play":"browse");
}
