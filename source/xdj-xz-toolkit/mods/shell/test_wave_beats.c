#ifdef NDEBUG
#error Beat alignment acceptance requires assertions
#endif
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
enum { STRIDE=804, BEATS=129, COLS=80000 };
static uint16_t frame[STRIDE*480], reference[STRIDE*480];
static struct xz_shell_column wave[4][COLS];
static struct xz_shell_beat grid[4][BEATS];
static unsigned checks, failures;
static int originals[4]={12000,15000,10000,16000};
static int kind,context[9];
static char titles[4][80];
static const char *output;
static int rgb(unsigned c){return (int)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static void check(int ok,const char *what){checks++;if(!ok){failures++;if(failures<=40)fprintf(stderr,"RED %s [%d,%d,%d,%d,%d,%d,%d,%d,%d]\n",what,context[0],context[1],context[2],context[3],context[4],context[5],context[6],context[7],context[8]);}}
static int time_at(int d,int beat){return (int)(((int64_t)beat*6000000+originals[d]/2)/originals[d]);}
static void model(struct xz_shell_model *m){
 memset(m,0,sizeof *m);m->deck_count=4;m->clock_ms=1000;
 m->capabilities=XZ_SHELL_CAP_GRID;m->stem_layout_known=1;m->stem_layout=XZ_SHELL_STEMS_HIDDEN;
 for(int d=0;d<4;d++){
  struct xz_shell_deck *k=&m->deck[d];
  k->known=XZ_SHELL_KNOWN(XZ_SHELL_F_LOADED)|XZ_SHELL_KNOWN(XZ_SHELL_F_ELAPSED)|
   XZ_SHELL_KNOWN(XZ_SHELL_F_TOTAL)|XZ_SHELL_KNOWN(XZ_SHELL_F_PLAYING)|
   XZ_SHELL_KNOWN(XZ_SHELL_F_TRACK_BPM)|XZ_SHELL_KNOWN(XZ_SHELL_F_BPM)|
   XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO)|XZ_SHELL_KNOWN(XZ_SHELL_F_GRID)|
   XZ_SHELL_KNOWN(XZ_SHELL_F_SYNC)|XZ_SHELL_KNOWN(XZ_SHELL_F_SYNC_STATUS)|XZ_SHELL_KNOWN(XZ_SHELL_F_TITLE);
  snprintf(titles[d],sizeof titles[d],"ORIGINAL %d.%02d / EFFECTIVE 120",originals[d]/100,originals[d]%100);k->title=titles[d];
  k->loaded=1;k->total_ms=80000;k->elapsed_ms=time_at(d,16);k->track_bpm_x100=originals[d];
  k->bpm_x100=12000;k->tempo_x100=(int)(120000000LL/originals[d])-10000;
  k->sync=1;k->sync_status=XZ_SHELL_SYNC_LOCKED;k->grid=grid[d];k->grid_count=BEATS;
  for(int i=0;i<BEATS;i++){
   int ms=kind==1&&i>17?time_at(d,17)+(time_at(d,i)-time_at(d,17))/2:time_at(d,i);
   grid[d][i]=(struct xz_shell_beat){ms,(uint8_t)(1+i%4)};
  }
 }
}
static struct xz_shell_rect lane(struct xz_shell *u,struct xz_shell_model *m,int d){
 struct xz_shell_model view;xz_shell_play_projection(u,m,&view);return xz_shell_play_rect(&view,XZ_SHELL_R_LANE,d);
}
static void render(struct xz_shell *u,struct xz_shell_model *m){
 for(unsigned i=0;i<sizeof frame/sizeof *frame;i++)frame[i]=0xa55a;
 assert(xz_shell_render(u,m,frame,STRIDE));
 for(int y=0;y<480;y++)for(int x=800;x<STRIDE;x++)assert(frame[y*STRIDE+x]==0xa55a);
}
static void save(const char *name){
 if(!output)return;
 char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",output,name);FILE *f=fopen(path,"wb");assert(f);
 fprintf(f,"P6\n800 480\n255\n");for(int y=0;y<480;y++)for(int x=0;x<800;x++){
  unsigned p=frame[y*STRIDE+x];unsigned char q[3]={(unsigned char)(((p>>11)&31)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
  assert(fwrite(q,1,3,f)==3);
 }assert(!fclose(f));
}
static int phase_ms(int d,int beat,int quarters){
 return grid[d][beat].ms+(grid[d][beat+1].ms-grid[d][beat].ms)*quarters/4;
}
static void geometry_matrix(void){
 const int pps[]={32,48,64,96,128,192,256},tempos[]={0,600,-600,10000};
 const int batches[2][4]={{12000,15000,10000,16000},{12800,10000,12000,15000}};
 for(int batch=0;batch<2;batch++)for(int view=2;view<=4;view+=2)for(int pair=0;pair<(view==2?4:1);pair++)
 for(int order=0;order<2;order++)for(int position=0;position<2;position++)for(int zoom=0;zoom<7;zoom++)for(int stem=0;stem<2;stem++)for(kind=0;kind<3;kind++){
  context[0]=batch;context[1]=view;context[2]=pair;context[3]=order;context[4]=position;context[5]=zoom;context[6]=stem;context[7]=kind;
  memcpy(originals,batches[batch],sizeof originals);
  struct xz_shell u;struct xz_shell_model m,v;xz_shell_init(&u);u.play_decks=view;u.zoom=zoom;u.deck=3;
  u.display.wave_position=(enum xz_shell_wave_position)position;u.display.wave_color=stem?XZ_SHELL_WAVE_STEMS:XZ_SHELL_WAVE_3BAND;
  model(&m);m.sides_extra=(uint8_t)pair;m.wave_order=order;
  memset(wave,0,sizeof wave);
  for(int d=0;d<4;d++){
   struct xz_shell_deck *k=&m.deck[d];
   if(kind==2)k->known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_GRID);
   k->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_WAVE)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_WAVE);
   k->wave=(struct xz_shell_wave){wave[d],COLS,80000};k->stems=k->wave;
   wave[d][grid[d][18].ms]=(struct xz_shell_column){255,255,255};
  }
  xz_shell_play_projection(&u,&m,&v);
  int ph=98+(position==XZ_SHELL_POSITION_LEFT?700*27/100:350),ppb=pps[zoom]/2;
  for(int snapshot=0;snapshot<3;snapshot++){
   context[8]=snapshot;
   for(int d=0;d<4;d++)m.deck[d].elapsed_ms=phase_ms(d,snapshot==1?17:16,snapshot==2?1:0);
   render(&u,&m);
   for(int d=0;d<4;d++)if(sh_play_visible(&v,d)){
    struct xz_shell_rect r=lane(&u,&m,d);int cy=r.y+20+(r.h-20)/2;
    int delta=snapshot==1?ppb:snapshot==2?(ppb*7+2)/4:2*ppb;
    int hit=0;for(int x=ph+delta-1;x<=ph+delta+1;x++)if(x>=98&&x<798)hit|=frame[cy*STRIDE+x]!=0;
    check(hit,"all source/stem peaks share beat coordinate and completed-audio motion");
    if(kind!=2){
     int tick=ph+(snapshot==2?(ppb*3+2)/4:ppb);
     check(frame[(r.y+r.h-1)*STRIDE+tick]==rgb(SH_WHITE),"actual real beat tick shares common scale and honest quarter phase");
     if(snapshot==0&&ph+4*ppb<797)check(frame[(r.y+r.h-1)*STRIDE+ph+4*ppb]==rgb(SH_RED),"real downbeats remain red");
    }
   }
   if(!snapshot){
    memcpy(reference,frame,sizeof frame);
    for(unsigned t=0;t<4;t++){
     for(int d=0;d<4;d++){m.deck[d].tempo_x100=tempos[t];m.deck[d].bpm_x100=7000+(int)t*2300;}
     render(&u,&m);
     for(int d=0;d<4;d++)if(sh_play_visible(&v,d)){
      struct xz_shell_rect r=lane(&u,&m,d);int different=0;
      for(int y=r.y;y<r.y+r.h;y++)for(int x=98;x<798;x++)different|=frame[y*STRIDE+x]!=reference[y*STRIDE+x];
      check(!different,"paused0/+6/-6/+100 and effectiveBPM do not resize geometry");
     }
    }
   }
  }
 }
}
static void marker_and_truth_checks(void){
 memcpy(originals,(int[]){12000,15000,12800,16000},sizeof originals);kind=1;
 struct xz_shell u;struct xz_shell_model m;xz_shell_init(&u);u.play_decks=4;u.zoom=3;model(&m);
 for(int d=0;d<4;d++){
  struct xz_shell_deck *k=&m.deck[d];k->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_MEMORY_CUES)|XZ_SHELL_KNOWN(XZ_SHELL_F_HOT_CUES)|XZ_SHELL_KNOWN(XZ_SHELL_F_LOOP)|XZ_SHELL_KNOWN(XZ_SHELL_F_CUE_POINT);
  k->memory_count=1;k->memory_cue[0]=(struct xz_shell_cue){1,grid[d][17].ms,0xff3c3c,0};
  k->hot_cue[0]=(struct xz_shell_cue){1,grid[d][18].ms,0x00ff00,0};k->cue_ms=grid[d][19].ms;
  k->loop=(struct xz_shell_loop){1,4,1,phase_ms(d,16,2),grid[d][20].ms};
 }
 render(&u,&m);save("variable-grid-cues-loop");
 for(int d=0;d<4;d++){
  struct xz_shell_rect r=lane(&u,&m,d);
  check(frame[(r.y+r.h-1)*STRIDE+496]==rgb(SH_MEMORY),"memory cue uses actual variable-grid source timestamp");
  check(frame[(r.y+r.h-20)*STRIDE+544]==rgb(xz_blend(SH_BLACK,0x00ff00,140)),"hotcue uses same mapping across grid transition");
  check(frame[(r.y+r.h-1)*STRIDE+472]==rgb(SH_LOOP)&&frame[(r.y+r.h-1)*STRIDE+640]==rgb(SH_LOOP),"loop boundaries use common beat coordinate");
  check(frame[(r.y+r.h-7)*STRIDE+592]==rgb(SH_LOOP),"ordinary cue uses common beat coordinate");
 }
 kind=0;model(&m);m.deck[1].elapsed_ms=phase_ms(1,16,1);render(&u,&m);save("locked-quarter-beat-is-visible");
 struct xz_shell_rect r=lane(&u,&m,1);
 check(frame[(r.y+r.h-1)*STRIDE+484]==rgb(SH_WHITE)&&frame[(r.y+r.h-1)*STRIDE+496]!=rgb(SH_WHITE),"locked flag does not fake true quarter-beat phase");
 memcpy(reference,frame,sizeof frame);
 for(int d=0;d<4;d++){m.deck[d].sync=0;m.deck[d].sync_status=XZ_SHELL_SYNC_TEMPO_ONLY;m.deck[d].sync_master_deck=3;}
 render(&u,&m);
 for(int d=0;d<4;d++){
  struct xz_shell_rect q=lane(&u,&m,d);int different=0;
  for(int y=q.y+20;y<q.y+q.h;y++)for(int x=98;x<798;x++)different|=frame[y*STRIDE+x]!=reference[y*STRIDE+x];
  check(!different,"SYNC off/request/tempo-only/locked flags cannot project position");
 }
 model(&m);m.deck[0].elapsed_ms=phase_ms(0,16,1);m.deck[1].elapsed_ms=phase_ms(1,16,2);
 m.deck[0].sync_status=m.deck[1].sync_status=XZ_SHELL_SYNC_TEMPO_ONLY;render(&u,&m);save("bpm-only-following-drifts");
 r=lane(&u,&m,0);check(frame[(r.y+r.h-1)*STRIDE+484]==rgb(SH_WHITE),"BPM-only first actual quarter-beat motion remains visible");
 r=lane(&u,&m,1);check(frame[(r.y+r.h-1)*STRIDE+472]==rgb(SH_WHITE),"different actual effective speed remains visibly different");
 model(&m);render(&u,&m);memcpy(reference,frame,sizeof frame);
 for(int d=0;d<4;d++)m.deck[d].total_ms=120000+d*9999;
 render(&u,&m);
 for(int d=0;d<4;d++){
  struct xz_shell_rect q=lane(&u,&m,d);int different=0;
  for(int y=q.y;y<q.y+q.h;y++)for(int x=98;x<798;x++)different|=frame[y*STRIDE+x]!=reference[y*STRIDE+x];
  check(!different,"main zoom does not depend on total track length");
 }
}
static void observed_clock_and_seek_checks(void){
 memcpy(originals,(int[]){12000,15000,10000,16000},sizeof originals);kind=0;
 struct xz_shell u;struct xz_shell_model m;xz_shell_init(&u);u.play_decks=4;u.zoom=3;model(&m);
 for(int snapshot=0;snapshot<3;snapshot++){
  m.clock_ms=1000+(uint32_t)snapshot*20;
  for(int d=0;d<4;d++){
   struct xz_shell_deck *k=&m.deck[d];k->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_MOTION)|XZ_SHELL_KNOWN(XZ_SHELL_F_PLAYBACK_RATE);
   k->moving=1;k->playing=1;k->elapsed_at_ms=1000;k->playback_rate_ppm=(int32_t)(12000LL*1000000/originals[d]);
  }
  render(&u,&m);
  for(int d=0;d<4;d++){
   struct xz_shell_rect r=lane(&u,&m,d);int x=496-(snapshot*20*96+500)/1000;
   check(frame[(r.y+r.h-1)*STRIDE+x]==rgb(SH_WHITE),"signed observed audio rate advances all originals at common real beat speed");
  }
 }
 /* Main-lane inverse coordinates are tested independently in the axis suite.
    The existing touch seek belongs to the whole-track percentage overview. */
 for(int view=2;view<=4;view+=2)for(int pair=0;pair<4;pair++)for(int order=0;order<2;order++){
  struct xz_shell_model projected;model(&m);m.capabilities|=XZ_SHELL_CAP_SEEK;m.sides_extra=(uint8_t)pair;m.wave_order=order;u.play_decks=view;
  xz_shell_play_projection(&u,&m,&projected);
  for(int d=0;d<4;d++)if(sh_play_visible(&projected,d)){
   struct xz_shell_rect r=xz_shell_play_rect(&projected,XZ_SHELL_R_OVERVIEW,d);
   int x=r.x+r.w/2;struct xz_shell_action actions[XZ_SHELL_ACTIONS];
   struct xz_shell_event event={.kind=XZ_SHELL_TOUCH_DOWN,.x=x,.y=r.y+r.h/2};
   size_t n=xz_shell_input(&u,&m,&event,actions);int found=0;
   for(size_t i=0;i<n;i++)if(actions[i].kind==XZ_SHELL_ACT_SEEK){found=1;check(actions[i].deck==d&&actions[i].value==xz_shell_overview_ms(&projected,m.deck[d].total_ms,x,d),"touch overview inverse remains source percentage on selected logical deck");}
   check(found,"overview touch keeps real seek action discoverable");
   event.kind=XZ_SHELL_TOUCH_UP;xz_shell_input(&u,&m,&event,actions);
  }
 }
}
static void malformed_canvas_checks(void){
 for(int fault=0;fault<4;fault++){
  struct xz_shell u;struct xz_shell_model m;xz_shell_init(&u);u.play_decks=4;u.zoom=3;kind=0;model(&m);
  memset(wave,0,sizeof wave);
  for(int d=0;d<4;d++){
   struct xz_shell_deck *k=&m.deck[d];k->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_WAVE);k->wave=(struct xz_shell_wave){wave[d],COLS,80000};
   wave[d][grid[d][18].ms]=(struct xz_shell_column){255,255,255};
   if(fault==0)grid[d][18].ms=grid[d][17].ms;
   if(fault==1)grid[d][18].ms=grid[d][17].ms-20;
   if(fault==2)grid[d][18].beat_in_bar=0;
   if(fault==3)grid[d][18].beat_in_bar=4;
  }
  render(&u,&m);memcpy(reference,frame,sizeof frame);
  for(int d=0;d<4;d++)m.deck[d].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_GRID);
  render(&u,&m);
  for(int d=0;d<4;d++){
   struct xz_shell_rect r=lane(&u,&m,d);int cy=r.y+20+(r.h-20)/2,different=0;
   for(int y=cy-3;y<=cy+3;y++)for(int x=98;x<798;x++)different|=frame[y*STRIDE+x]!=reference[y*STRIDE+x];
   check(!different,"malformed/gapped grid uses one stable originalBPM fallback for real peaks");
  }
 }
}
static void grid_edges_and_transition_checks(void){
 memcpy(originals,(int[]){12000,15000,12800,10000},sizeof originals);
 for(int edge=0;edge<4;edge++)for(int stem=0;stem<2;stem++)for(int zoom=0;zoom<7;zoom++){
  const int ppb_values[]={16,24,32,48,64,96,128};int ppb=ppb_values[zoom];
  struct xz_shell u;struct xz_shell_model m;xz_shell_init(&u);u.play_decks=4;u.zoom=zoom;
  u.display.wave_color=stem?XZ_SHELL_WAVE_STEMS:XZ_SHELL_WAVE_3BAND;kind=edge==0?1:0;model(&m);memset(wave,0,sizeof wave);
  for(int d=0;d<4;d++){
   struct xz_shell_deck *k=&m.deck[d];int head,peak;
   if(edge==0){head=phase_ms(d,18,1);peak=grid[d][20].ms;}
   else if(edge==1){
    int offset=4000+d*37;for(int i=0;i<BEATS;i++)grid[d][i].ms+=offset;
    head=grid[d][0].ms;peak=head-3*(grid[d][1].ms-grid[d][0].ms)/2;
   }else if(edge==2){
    int span=grid[d][128].ms-grid[d][127].ms;head=grid[d][128].ms+span;peak=head+span;
   }else{
    head=grid[d][0].ms;peak=grid[d][1].ms;
   }
   k->elapsed_ms=head;k->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_WAVE)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_WAVE);
   k->wave=(struct xz_shell_wave){wave[d],COLS,80000};k->stems=k->wave;
   assert(peak>=0&&peak<COLS);wave[d][peak]=(struct xz_shell_column){255,255,255};
  }
  render(&u,&m);
  for(int d=0;d<4;d++){
   struct xz_shell_rect r=lane(&u,&m,d);int cy=r.y+20+(r.h-20)/2;
   int x=448+(edge==0?(ppb*7+2)/4:edge==1?-3*ppb/2:ppb),hit=0;
   for(int j=x-1;j<=x+1;j++)hit|=frame[cy*STRIDE+j]!=0;
   check(hit,"actual source/stem peaks survive transition, before-first, after-last and source-start mapping");
  }
  if(zoom==3&&stem==1&&edge<3){char name[80];snprintf(name,sizeof name,"source-edge-or-transition-%d",edge);save(name);}
 }
}
int main(int argc,char **argv){
 if(argc>2)return 2;output=argc==2?argv[1]:NULL;struct xz_shell u;struct xz_shell_model m;
 xz_shell_init(&u);u.play_decks=4;u.zoom=3;model(&m);render(&u,&m);
 for(int d=0;d<4;d++){struct xz_shell_rect r=lane(&u,&m,d);check(frame[(r.y+r.h-1)*STRIDE+496]==rgb(SH_WHITE),"equal true beat phase uses48px per beat at96pps for every original BPM");}
 save("equal-beat-phase-grid");
 for(int d=0;d<4;d++){
  struct xz_shell_deck *k=&m.deck[d];k->known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_GRID);
  k->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_WAVE)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_WAVE);
  k->wave=(struct xz_shell_wave){wave[d],COLS,80000};k->stems=k->wave;
  wave[d][time_at(d,18)]=(struct xz_shell_column){255,255,255};
 }
 /* The geometry must also carry the source peaks. Restore the actual grid,
    suppress only its ink by using the low-level lane in later extensions. */
 for(int d=0;d<4;d++)m.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_GRID);
 render(&u,&m);save("equal-beat-phase-impulses");
 for(int d=0;d<4;d++){
  struct xz_shell_rect r=lane(&u,&m,d);int cy=r.y+20+(r.h-20)/2;
  int hit=0;for(int x=543;x<=546;x++)hit|=frame[cy*STRIDE+x]!=0;
  check(hit,"two-beat source impulse appears96px ahead for every original BPM");
 }
 for(int d=0;d<4;d++)m.deck[d].elapsed_ms=time_at(d,17);
 render(&u,&m);save("one-audio-beat-later");
 for(int d=0;d<4;d++){
  struct xz_shell_rect r=lane(&u,&m,d);int cy=r.y+20+(r.h-20)/2;
  int hit=0;for(int x=495;x<=498;x++)hit|=frame[cy*STRIDE+x]!=0;
  check(hit,"one completed audio beat moves each source impulse exactly48px");
 }
 geometry_matrix();marker_and_truth_checks();observed_clock_and_seek_checks();malformed_canvas_checks();grid_edges_and_transition_checks();
 printf("%s beat_alignment_checks=%u failures=%u\n",failures?"RED":"PASS",checks,failures);return failures?1:0;
}
