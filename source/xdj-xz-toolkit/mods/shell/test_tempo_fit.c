#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* The tempo readout fits its frame (the owner, 2026-11-16: a four-digit percentage ran over the border). Pinned:
   - the value in the unit's steps (ui/tempo_text.h): two decimals in 6, 10 and 16 %, one in WIDE, so -100 %
     reads -100.0 and nothing is wider than "+16.00";
   - on the PLAY strips (two decks and four lanes), for every range, both signs at the range's end, MASTER TEMPO
     on and off and SYNC on, the value and its % stay 4 px or more clear of the BPM box (two decks) and of the MT
     and SYNC chips (four decks), and inside the strip. */
#include "four_deck_fixtures.h"
#include "shell_internal.h"
#include "../ui/tempo_text.h"
#include "../ui/layout_cycle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { W=XZ_SHELL_WIDTH, H=XZ_SHELL_HEIGHT };
static struct fx_track track;
static uint16_t a[W*H],b[W*H];
static void render(const struct xz_shell *u,const struct xz_shell_model *m,uint16_t *p){
 memset(p,0,sizeof a);assert(xz_shell_render(u,m,p,W));
}
static void text_is(int32_t x100,int known,int range,const char *want){
 char t[16];xz_tempo_text(t,sizeof t,x100,known,range);
 if(strcmp(t,want)){fprintf(stderr,"tempo %d range %d: \"%s\", want \"%s\"\n",(int)x100,range,t,want);assert(0);}
}
static void test_text(void){
 text_is(0,1,6,"0.00");text_is(598,1,6,"+5.98");text_is(-600,1,6,"-6.00");
 text_is(-995,1,10,"-9.95");text_is(1000,1,10,"+10.00");text_is(-1600,1,16,"-16.00");text_is(1595,1,16,"+15.95");
 text_is(-10000,1,100,"-100.0");text_is(10000,1,100,"+100.0");text_is(9950,1,100,"+99.5");text_is(-50,1,100,"-0.5");
 text_is(0,1,100,"0.0");text_is(2,1,100,"0.0");text_is(12000,0,0,"+100.0");text_is(-10000,0,0,"-100.0");
 text_is(-1288,0,0,"-12.88");
 char t[16];int widest=0;
 static const int ranges[4]={6,10,16,100};
 for(int r=0;r<4;r++)for(int32_t v=-ranges[r]*100;v<=ranges[r]*100;v+=r==3?50:r==0?2:5){
  xz_tempo_text(t,sizeof t,v,1,ranges[r]);assert(strlen(t)<=6);
  if(sh_num_width(SH_NUM_MID,t)>widest)widest=sh_num_width(SH_NUM_MID,t);
 }
 assert(widest==sh_num_width(SH_NUM_MID,"+16.00"));
 puts("PASS tempo text: unit steps per range, WIDE to -100.0, never wider than +16.00");
}
/* Columns where the strip differs between the tempo shown and unknown: the value, its % and the dashes. */
static void value_columns(struct xz_shell_rect s,int y0,int y1,int *lo,int *hi){
 *lo=W;*hi=-1;
 for(int y=y0;y<y1;y++)for(int x=s.x;x<s.x+s.w;x++)if(a[y*W+x]!=b[y*W+x]){if(x<*lo)*lo=x;if(x>*hi)*hi=x;}
}
static void check(struct xz_shell *u,struct xz_shell_model *m,int deck,int range,int32_t x100,int mt){
 struct xz_shell_model view;xz_shell_play_projection(u,m,&view);
 int four=sh_play_deck_count(&view)==4;
 struct xz_shell_deck *d=&m->deck[deck];
 d->tempo_range=range;d->tempo_x100=x100;d->master_tempo=mt;d->sync=1;
 d->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO)|XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO_RANGE)|XZ_SHELL_KNOWN(XZ_SHELL_F_MASTER_TEMPO)|XZ_SHELL_KNOWN(XZ_SHELL_F_SYNC);
 render(u,m,a);
 d->known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO);render(u,m,b);d->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_TEMPO);
 struct xz_shell_rect s=xz_shell_play_rect(&view,XZ_SHELL_R_STRIP,deck);
 /* The value row: two decks y+52..+72 beside the BPM box at x+8+298; four lanes y+76..+96 before the MT
    chip at x+6+92. */
 int lo,hi,y0=four?s.y+76:s.y+52,y1=y0+20,limit=four?s.x+6+92:s.x+8+298;
 value_columns(s,y0,y1,&lo,&hi);
 char t[16];xz_tempo_text(t,sizeof t,x100,1,range);
 if(!(hi>=0&&hi<=limit-5&&lo>=s.x)){fprintf(stderr,"%s deck %d \"%s\": value columns %d..%d, limit %d\n",four?"four":"two",deck+1,t,lo,hi,limit);assert(0);}
 /* Nothing of the readout lands on the box or chip column itself. */
 for(int y=y0;y<y1;y++)assert(a[y*W+limit]==b[y*W+limit]);
 /* Four lanes: the MT chip as drawn (its outline, red or grey) starts 4 px or more after the %. */
 if(four){
  uint16_t ink=(uint16_t)(mt?((SH_RED>>8)&0xf800)|((SH_RED>>5)&0x7e0)|((SH_RED>>3)&31):((0x5a5a5a>>8)&0xf800)|((0x5a5a5a>>5)&0x7e0)|((0x5a5a5a>>3)&31));
  int chip=-1;
  for(int x=hi+1;x<s.x+s.w&&chip<0;x++)for(int y=s.y+80;y<s.y+96;y++)if(a[y*W+x]==ink){chip=x;break;}
  if(!(chip>=0&&chip-hi-1>=4)){fprintf(stderr,"four deck %d \"%s\": %% ends at %d, MT chip at %d\n",deck+1,t,hi,chip);assert(0);}
 }
}
static void each(struct xz_shell *u,struct xz_shell_model *m){
 struct xz_shell_model view;xz_shell_play_projection(u,m,&view);
 static const int ranges[4]={6,10,16,100};
 for(int deck=0;deck<sh_deck_count(m);deck++){
  int on=0;for(int slot=0;slot<sh_play_deck_count(&view);slot++)on|=sh_play_deck_count(&view)==4?slot==deck:sh_play_deck(&view,slot)==deck;
  if(!on||sh_play_placeholder(&view,deck))continue;
  struct xz_shell_deck saved=m->deck[deck];
  for(int r=0;r<4;r++)for(int sign=-1;sign<=1;sign+=2)for(int mt=0;mt<2;mt++){
   check(u,m,deck,ranges[r],sign*ranges[r]*100,mt);
   check(u,m,deck,ranges[r],sign*(ranges[r]*100-(r==3?50:5)),mt);
  }
  m->deck[deck]=saved;
 }
}
int main(void){
 struct xz_shell u;struct xz_shell_model m;
 test_text();
 fx_track_build(&track,7);
 xz_shell_init(&u);fx_model_full(&m,&track);m.stems_known=1;each(&u,&m);
 xz_shell_init(&u);fx_model_four_players(&m,&track);m.stems_known=1;m.layout_preference=XZ_LAYOUT_TWO+1;m.sides_extra=1;each(&u,&m);
 xz_shell_init(&u);fx_model_four_players(&m,&track);m.stems_known=1;m.layout_preference=XZ_LAYOUT_FOUR+1;each(&u,&m);
 xz_shell_init(&u);fx_model_four_players(&m,&track);m.stems_known=1;m.layout_preference=XZ_LAYOUT_MIXER+1;each(&u,&m);
 puts("PASS tempo readout: every range, sign, MASTER TEMPO and SYNC state fits its strip with 4 px to spare, two decks and four lanes");
 return 0;
}
