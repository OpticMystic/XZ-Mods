/* Portable focused renderer tests. Include implementation for projection assertions. */
#include "classic_mixer.c"
#ifdef NDEBUG
#error Classic mixer acceptance requires active assertions
#endif
#include <assert.h>
#include <stdlib.h>
static uint16_t pixels[807*480+2],saved[807*480+2],native_wave[300*34];
/* 2 DECKS following the sides: a software deck's stock-look panel. */
static struct xz_extra_column half_wave[3000];
static void halves(void){
 for(int i=0;i<3000;i++)half_wave[i]=(struct xz_extra_column){(uint8_t)(80+i%60),50,30};
 struct xz_extra_decks extra;memset(&extra,0,sizeof extra);extra.route_up=1;
 for(int i=0;i<2;i++){
  struct xz_extra_deck *d=&extra.deck[i];d->known=UINT32_MAX;d->loaded=1;snprintf(d->title,sizeof d->title,"Half %d",i);
  strcpy(d->key,"8A");d->source=1;d->remain=1;d->total_ms=240000;d->elapsed_ms=60000;d->bpm_x100=12800;d->tempo_x100=-150;d->tempo_range=10;
  d->wave=(struct xz_extra_wave){half_wave,3000,240000,400};d->master=i==1;d->hot_cue[0]=(struct xz_extra_cue){1,30000,0,0};d->cue_ms=59000;
 }
 struct xz_classic_mixer_view v={NULL,&extra,1000,0,XZ_LAYOUT_TWO,{NULL,NULL},0};
 for(int side=0;side<2;side++){
  assert(xz_classic_half_panel_at(side*400,309)==side&&xz_classic_half_panel_at(side*400+399,479)==side&&xz_classic_half_panel_at(0,308)==-1);
  struct xz_classic_mixer_rect r=xz_classic_half_overview_rect(side);
  assert(r.x==side*400+10&&r.y==421&&r.w==300&&r.h==46);
  assert(xz_classic_half_seek_ms(side,r.x-5,1000)==0&&xz_classic_half_seek_ms(side,r.x+r.w+5,1000)==1000&&xz_classic_half_seek_ms(side,r.x+150,299)==150);
  for(size_t i=0;i<sizeof pixels/sizeof *pixels;i++)pixels[i]=0x5a5a;
  assert(xz_classic_mixer_half_draw(pixels,807,807*480,&v,2+side));
  unsigned inside=0,wave=0;
  for(int y=0;y<480;y++)for(int x=0;x<807;x++){
   int in=y>=309&&x>=side*400&&x<side*400+400;
   if(!in)assert(pixels[y*807+x]==0x5a5a);else inside+=pixels[y*807+x]!=0x5a5a;
  }
  assert(inside==400u*171u&&pixels[807*480]==0x5a5a);
  /* The overview has the deck's waveform; the title band carries the deck's number box. */
  for(int y=423;y<457;y++)for(int x=r.x;x<r.x+300;x++)wave+=pixels[y*807+x]!=(uint16_t)(((0x08>>3)<<11)|((0x0c>>2)<<5)|(0x08>>3));
  assert(wave>3000);
  uint32_t fill=xz_deck_box_fill(2+side,1,0);uint16_t box=(uint16_t)(((fill>>8)&0xf800)|((fill>>5)&0x7e0)|((fill>>3)&31));
  assert(pixels[(309+8)*807+side*400+(side?3:0)+10]==box);
  memcpy(saved,pixels,sizeof pixels);
  for(size_t i=0;i<sizeof pixels/sizeof *pixels;i++)pixels[i]=0x5a5a;
  xz_classic_mixer_half_identity(pixels,807,807*480,2+side);
  for(int y=0;y<480;y++)for(int x=0;x<807;x++)if(pixels[y*807+x]!=0x5a5a)
   assert(y>=315&&y<339&&x>=side*400+(side?3:0)+8&&x<side*400+(side?3:0)+32);
 }
 /* A master deck's BPM box is orange, a following one white; unknown fields stay blank. */
 for(size_t i=0;i<sizeof pixels/sizeof *pixels;i++)pixels[i]=0x5a5a;
 assert(xz_classic_mixer_half_draw(pixels,807,807*480,&v,3));
 assert(pixels[(309+92)*807+400+313]==(uint16_t)0xfbc0);
 assert(!xz_classic_mixer_half_draw(pixels,807,807*480,&v,1)&&!xz_classic_mixer_half_draw(pixels,807,807*480,&v,4));
 v.extra=NULL;assert(!xz_classic_mixer_half_draw(pixels,807,807*480,&v,2));v.extra=&extra;
 assert(!xz_classic_mixer_half_draw(pixels,799,807*480,&v,2)&&!xz_classic_mixer_half_draw(pixels,807,807*480-1,&v,2));
 assert(xz_classic_half_seek_ms(2,0,1000)==-1&&xz_classic_half_seek_ms(0,0,0)==-1);
 puts("PASS 2 DECKS panel half: exact panel writes, overview waveform, number box, master BPM box, identity box only, seek geometry, guards");
}
static void sync_presentation(void){
 struct xz_shell_deck native[2]={0};struct xz_extra_decks extra={0};
 struct xz_classic_mixer_view v={.native=native,.extra=&extra};
 static const char *const labels[]={"SYNC","SYNC 2","SYNC","BPM 2","","LIMIT","SYNC 2","EXT MASTER","NO BPM"};
 for(int side=0;side<2;side++)for(int status=0;status<=XZ_SHELL_SYNC_NO_BPM;status++){
  native[side].known=N(SYNC)|N(SYNC_STATUS);native[side].sync=1;
  native[side].sync_status=status;native[side].sync_master_deck=1;
  struct panel p=project(&v,side);assert(!strcmp(p.sync,labels[status])&&!strstr(p.sync,"WAIT"));
  native[side].sync=0;assert(!project(&v,side).sync[0]);
 }
 native[0].sync=1;native[0].sync_status=999;assert(!strcmp(project(&v,0).sync,"SYNC"));
 native[0].known=N(SYNC);assert(!strcmp(project(&v,0).sync,"SYNC"));
 for(int side=0;side<2;side++)for(int r=0;r<=XZ_EXTRA_SYNC_TEMPO_FOLLOW;r++)for(int q=0;q<2;q++)for(int lock=0;lock<2;lock++){
  struct xz_extra_deck *d=&extra.deck[side];d->known=E(SYNC)|E(QUANTIZE);d->loaded=1;d->quantize=q;
  d->sync=(struct xz_extra_sync){.requested=1,.locked=lock,.master_deck=0,.reason=r,.tempo_matched=1};
  assert(!strcmp(project(&v,side+2).sync,xz_extra_sync_present(d).label));
 }
 puts("PASS Classic native shell status compatibility and software SYNC presentation parity");
}
int main(void){
 sync_presentation();
 halves();
 const unsigned char *order=xz_deck_layout_order(XZ_LAYOUT_MIXER);
 struct xz_shell_deck native[2]={0};struct xz_extra_decks extra={0};struct xz_classic_mixer_view v={native,&extra,2000,0,XZ_LAYOUT_MIXER,{native_wave,NULL},0};
 for(size_t i=0;i<sizeof pixels/sizeof *pixels;i++)pixels[i]=0x5a5a;
 memcpy(saved,pixels,sizeof pixels);assert(!xz_classic_mixer_draw(pixels,807,807*480,&v));assert(!memcmp(saved,pixels,sizeof pixels));extra.route_up=1;
 assert(!xz_classic_mixer_draw(pixels,799,807*480,&v));assert(!xz_classic_mixer_draw(pixels,SIZE_MAX,807*480,&v));assert(!xz_classic_mixer_draw(pixels,807,807*480-1,&v));assert(!xz_classic_mixer_draw(NULL,807,807*480,&v));
 for(int s=0;s<4;s++){assert(xz_classic_mixer_channel_at(XZ_LAYOUT_MIXER,s*200,311)==order[s]);assert(xz_classic_mixer_channel_at(XZ_LAYOUT_MIXER,s*200+199,479)==order[s]);}
 assert(xz_classic_mixer_channel_at(XZ_LAYOUT_MIXER,800,400)==-1);assert(xz_classic_mixer_channel_at(XZ_LAYOUT_MIXER,0,310)==-1);assert(xz_classic_mixer_channel_at(XZ_LAYOUT_MIXER,-1,400)==-1);assert(xz_classic_mixer_channel_at(XZ_LAYOUT_MIXER,0,480)==-1);
 for(int d=0;d<4;d++){struct xz_classic_mixer_rect r=xz_classic_mixer_overview_rect(XZ_LAYOUT_MIXER,d);assert(r.x==(d==2?0:d==0?1:d==1?2:3)*200+6&&r.y==440&&r.w==188&&r.h==34);assert(xz_classic_mixer_seek_ms(XZ_LAYOUT_MIXER,d,r.x-10,1000)==0);assert(xz_classic_mixer_seek_ms(XZ_LAYOUT_MIXER,d,r.x+187,1000)==1000);assert(xz_classic_mixer_seek_ms(XZ_LAYOUT_MIXER,d,r.x+93,1000)==497);}
 int x,y;assert(xz_classic_mixer_native_point(XZ_LAYOUT_MIXER,0,206,440,&x,&y)&&x==0&&y==0);assert(xz_classic_mixer_native_point(XZ_LAYOUT_MIXER,0,393,473,&x,&y)&&x==299&&y==33);assert(!xz_classic_mixer_native_point(XZ_LAYOUT_MIXER,2,6,440,&x,&y));assert(!xz_classic_mixer_native_point(XZ_LAYOUT_MIXER,0,205,440,&x,&y));assert(xz_classic_mixer_seek_ms(XZ_LAYOUT_MIXER,-1,0,1000)==-1);
 /* Decision D3b: the channels mirror the physical mixer, 3 1 2 4, in both four-deck views; only the upper
    lanes follow DECK VIEW. */
 for(int layout=XZ_LAYOUT_FOUR;layout<=XZ_LAYOUT_MIXER;layout++){
  struct xz_four_layout lanes;assert(xz_four_layout_get(layout==XZ_LAYOUT_MIXER?XZ_FOUR_MIXER:XZ_FOUR_STACKED,&lanes));
  for(int slot=0;slot<4;slot++){
   int id=order[slot];
   assert(xz_classic_mixer_channel_at(layout,slot*200+10,450)==id);
   struct xz_classic_mixer_rect r=xz_classic_mixer_overview_rect(layout,id);
   assert(r.x==slot*200+6&&lanes.lane_y[xz_deck_layout_order(layout)[slot]]==(unsigned)slot*68);
   assert(xz_classic_mixer_seek_ms(layout,id,r.x+187,1234)==1234);
   if(id<2){int nx,ny;assert(xz_classic_mixer_native_point(layout,id,r.x+187,r.y+33,&nx,&ny)&&nx==299&&ny==33);}
  }
 }
 assert(xz_classic_mixer_channel_at(XZ_LAYOUT_TWO,100,450)==-1);
 assert(!xz_classic_mixer_overview_rect(XZ_LAYOUT_TWO,0).w);
 v.layout=XZ_LAYOUT_TWO;memcpy(saved,pixels,sizeof pixels);
 assert(!xz_classic_mixer_draw(pixels,807,807*480,&v)&&!memcmp(saved,pixels,sizeof pixels));v.layout=XZ_LAYOUT_MIXER;
 struct panel p=project(&v,0);assert(!p.load_known&&!p.time_known&&!p.acue_known&&!p.bpm_known);p=project(&v,2);assert(!p.acue_known&&!p.time_known);
 native[0].known=N(LOADED)|N(TITLE)|N(TIME_MODE)|N(ELAPSED)|N(TOTAL)|N(BPM)|N(AUTO_CUE);native[0].loaded=1;native[0].title="WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";native[0].time_mode=XZ_SHELL_TIME_REMAIN;native[0].total_ms=300000;native[0].elapsed_ms=42000;native[0].bpm_x100=12800;native[0].auto_cue=1;
 extra.deck[0].known=E(TITLE)|E(TIME_MODE)|E(ELAPSED)|E(TOTAL)|E(BPM);extra.deck[0].loaded=1;extra.deck[0].remain=1;extra.deck[0].auto_cue=1;extra.deck[0].total_ms=300000;extra.deck[0].elapsed_ms=42000;extra.deck[0].bpm_x100=12800;
 struct panel a=project(&v,0),b=project(&v,2);assert(a.loaded==b.loaded&&a.time_known==b.time_known&&a.elapsed==b.elapsed&&a.bpm==b.bpm&&a.acue_known==b.acue_known&&a.acue==b.acue);
 assert(xz_classic_mixer_draw(pixels,807,807*480,&v));
 for(int yy=378;yy<406;yy++)for(int xx=6;xx<194;xx++)assert(pixels[yy*807+xx]==pixels[yy*807+200+xx]);
 native[0].known|=N(MASTER_TEMPO)|N(QUANTIZE)|N(TEMPO_RANGE)|N(TEMPO)|N(KEY)|N(SOURCE)|N(MASTER);
 native[0].master_tempo=1;native[0].quantize_on=1;native[0].tempo_range=100;native[0].tempo_x100=-1450;native[0].source=XZ_SHELL_SRC_USB2;native[0].master=1;native[0].master_scope=XZ_EXTRA_MASTER_SCOPE_SOFTWARE;
 p=project(&v,0);assert(p.mt_known&&p.mt&&p.quant_known&&p.quant&&p.range==100&&p.tempo==-1450&&p.source==2&&p.master&&p.master_scope==XZ_EXTRA_MASTER_SCOPE_SOFTWARE);
 extra.deck[0].known|=E(MASTER_TEMPO)|E(QUANTIZE)|E(TEMPO_RANGE)|E(TEMPO);extra.deck[0].master_tempo=1;extra.deck[0].quantize=1;extra.deck[0].tempo_range=100;extra.deck[0].tempo_x100=-1450;
 p=project(&v,2);assert(p.mt_known&&p.mt&&p.quant_known&&p.quant&&p.range==100&&p.tempo==-1450);
 extra.deck[0].known|=E(PLAYING);extra.deck[0].playing=1;extra.deck[0].tempo_x100=0;extra.deck[0].elapsed_at_ms=1900;assert(project(&v,2).elapsed==42100);
 for(int i=0;i<300*34;i++)native_wave[i]=(uint16_t)i;
 uint16_t copy[300*34];memcpy(copy,native_wave,sizeof copy);
 assert(xz_classic_mixer_draw(pixels,807,807*480,&v));
 for(int yy=0;yy<480;yy++)for(int xx=0;xx<807;xx++)if(yy<311||xx>=800)assert(pixels[yy*807+xx]==0x5a5a);
 assert(xz_classic_mixer_seek_ms(XZ_LAYOUT_MIXER,0,INT32_MIN,INT32_MAX)==0);assert(xz_classic_mixer_seek_ms(XZ_LAYOUT_MIXER,0,INT32_MAX,INT32_MAX)==INT32_MAX);
 assert(pixels[807*480]==0x5a5a&&pixels[807*480+1]==0x5a5a);assert(!memcmp(copy,native_wave,sizeof copy));
 /* Native END owns a clean overview, independent of stale captured stock text. */
 native[0].elapsed_ms=native[0].total_ms;
 assert(xz_classic_mixer_draw(pixels,807,807*480,&v));
 struct xz_classic_mixer_rect endbox=xz_classic_mixer_overview_rect(v.layout,0);
 for(int yy=0;yy<34;yy++)for(int xx=0;xx<188;xx++)if(xx<79||xx>=119||yy<11||yy>=23)
  assert(pixels[(endbox.y+yy)*807+endbox.x+xx]==0x0020);
 /* An unlabeled track number must not reappear beside DECK 1/2. */
 native[0].known|=N(TRACK_NO);native[0].track_no=999;
 assert(xz_classic_mixer_draw(pixels,807,807*480,&v));memcpy(saved,pixels,sizeof pixels);
 native[0].track_no=0;assert(xz_classic_mixer_draw(pixels,807,807*480,&v));assert(!memcmp(saved,pixels,sizeof pixels));
 /* All panels share identical blank backgrounds, title bars and overview bounds. */
 for(int s=0;s<4;s++){assert(pixels[311*807+s*200]==pixels[311*807]);assert(pixels[439*807+s*200+4]==pixels[439*807+4]);}
 /* Number order draws the very same channels (D3b). */
 memcpy(saved,pixels,sizeof pixels);v.layout=XZ_LAYOUT_FOUR;assert(xz_classic_mixer_draw(pixels,807,807*480,&v));
 assert(!memcmp(saved,pixels,sizeof pixels));v.layout=XZ_LAYOUT_MIXER;
 /* Decks 3 and 4 not connected: their channels say so in a dim band; decks 1 and 2 stay live. The software
    snapshot is not read (NULL is fine); without placeholders a down route draws nothing. */
 struct xz_classic_mixer_view down=v;down.extra=NULL;down.placeholders=1;
 for(size_t i=0;i<sizeof pixels/sizeof *pixels;i++)pixels[i]=0x5a5a;
 assert(xz_classic_mixer_draw(pixels,807,807*480,&down));
 uint16_t dim=(uint16_t)(((0x16>>3)<<11)|((0x1c>>2)<<5)|(0x26>>3)),band=(uint16_t)(((0x12>>3)<<11)|((0x34>>2)<<5)|(0x61>>3));
 /* Probed right of where titles are cut (x0 + 29 + 166), so only the band shows there. */
 assert(pixels[312*807+197]==dim&&pixels[312*807+600+197]==dim&&pixels[312*807+200+197]==band&&pixels[312*807+400+197]==band);
 /* Every channel's number sits in its badge (deck_colour.h), dimmed for 3 and 4. */
 for(int s=0;s<4;s++){
  int id=xz_classic_mixer_order()[s];uint32_t want=xz_deck_box_fill(id,id<2,0x161c26);
  assert(pixels[314*807+s*200+4]==(uint16_t)(((want>>19)&31)<<11|((want>>10)&63)<<5|((want>>3)&31)));
 }
 int amber=0;for(int yy=313;yy<330;yy++)for(int xx=30;xx<120;xx++)amber+=pixels[yy*807+xx]!=dim;
 assert(amber>30);
 for(int yy=0;yy<480;yy++)for(int xx=0;xx<807;xx++)if(yy<311||xx>=800)assert(pixels[yy*807+xx]==0x5a5a);
 down.placeholders=0;down.extra=&extra;extra.route_up=0;assert(!xz_classic_mixer_draw(pixels,807,807*480,&down));extra.route_up=1;
 puts("classic_mixer: geometry, guards, known fields, position, parity, padding, channels 3 1 2 4 in both orders and NOT CONNECTED channels PASS");return 0;
}


