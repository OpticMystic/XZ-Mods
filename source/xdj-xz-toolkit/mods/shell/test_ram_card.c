#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
/* The RAM card on MODERN's play screen (shell.h ram_only, ui/ram_notice.h): a deck playing its whole track from RAM
   after the music USB went out shows the card in its lane while it has no waveform, keeps every other readout, and
   loses the card as soon as a waveform is back or the RAM copy ends. Two decks, four decks and the mixer order. */
#include "four_deck_fixtures.h"
#include "shell_internal.h"
#include "../ui/layout_cycle.h"
#include "../ui/ram_notice.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct fx_track track;
static uint16_t with_card[800*480],without[800*480],normal[800*480];
static uint16_t rgb565(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static void render(const struct xz_shell *u,const struct xz_shell_model *m,uint16_t *out){
 assert(xz_shell_render(u,m,out,800));
}
/* What the runtime publishes once the USB is out: the reads from the USB are gone; the player's own stay. */
static void usb_out(struct xz_shell_model *m,int d,int ram){
 struct xz_shell_deck *k=&m->deck[d];
 k->known&=~(XZ_SHELL_KNOWN(XZ_SHELL_F_WAVE)|XZ_SHELL_KNOWN(XZ_SHELL_F_OVERVIEW)|XZ_SHELL_KNOWN(XZ_SHELL_F_STEM_WAVE)
  |XZ_SHELL_KNOWN(XZ_SHELL_F_GRID)|XZ_SHELL_KNOWN(XZ_SHELL_F_MEMORY_CUES));
 memset(&k->wave,0,sizeof k->wave);memset(&k->overview,0,sizeof k->overview);memset(&k->stems,0,sizeof k->stems);
 k->grid=NULL;k->grid_count=0;k->memory_count=0;
 k->known|=XZ_SHELL_KNOWN(XZ_SHELL_F_RAM_ONLY);k->ram_only=ram;
}
static int card_pixels(const uint16_t *f,struct xz_shell_rect r){
 int n=0;uint16_t fill=rgb565(XZ_CALM_FILL),edge=rgb565(XZ_CALM_EDGE);
 for(int y=r.y;y<r.y+r.h;y++)for(int x=r.x;x<r.x+r.w;x++)n+=f[y*800+x]==fill||f[y*800+x]==edge;
 return n;
}
/* Every pixel outside deck d's lane is the same with and without the card; inside it, the card is there. */
static void card_only_in_lane(const struct xz_shell_model *m,int d){
 struct xz_shell_rect lane=xz_shell_play_rect(m,XZ_SHELL_R_LANE,d);
 int changed=0;
 for(int y=0;y<480;y++)for(int x=0;x<800;x++){
  int inside=x>=lane.x&&x<lane.x+lane.w&&y>=lane.y&&y<lane.y+lane.h;
  if(with_card[y*800+x]!=without[y*800+x]){assert(inside);changed++;}
 }
 assert(changed>4000&&card_pixels(with_card,lane)>4000);
}
static int cases;
static void layout(int preference,int four){
 struct xz_shell u;struct xz_shell_model m;xz_shell_init(&u);
 int decks=four?4:2;
 for(int d=0;d<decks;d++){
  if(four)fx_model_four(&m,&track);else fx_model_full(&m,&track);
  m.layout_preference=preference;m.play_view_count=decks;m.wave_order=preference==XZ_LAYOUT_MIXER+1; /* the PLAY projection xz_shell_play_rect expects */
  render(&u,&m,normal);
  /* USB out, no RAM copy: no card (the lane is just empty). */
  usb_out(&m,d,0);render(&u,&m,without);
  assert(!card_pixels(without,xz_shell_play_rect(&m,XZ_SHELL_R_LANE,d)));
  /* USB out with the whole track in RAM: the card, and nothing else on screen changes. */
  m.deck[d].ram_only=1;render(&u,&m,with_card);
  card_only_in_lane(&m,d);
  /* The other decks never show it. */
  for(int o=0;o<decks;o++)if(o!=d)assert(!card_pixels(with_card,xz_shell_play_rect(&m,XZ_SHELL_R_LANE,o)));
  /* The waveform is back (the same USB, or a track loaded from another): no card, the screen as before. */
  struct xz_shell_model back;
  if(four)fx_model_four(&back,&track);else fx_model_full(&back,&track);
  back.layout_preference=preference;back.play_view_count=decks;back.wave_order=m.wave_order;back.deck[d].ram_only=1;
  render(&u,&back,with_card);assert(!memcmp(with_card,normal,sizeof normal));
  /* The RAM copy ended (a new track loaded): no card. */
  m.deck[d].ram_only=0;render(&u,&m,with_card);assert(!memcmp(with_card,without,sizeof without));
  /* The runtime does not know (no RAM service): no card. */
  m.deck[d].ram_only=1;m.deck[d].known&=~XZ_SHELL_KNOWN(XZ_SHELL_F_RAM_ONLY);
  render(&u,&m,with_card);assert(!memcmp(with_card,without,sizeof without));
  /* A deck error keeps its own banner there instead. */
  m.deck[d].known|=XZ_SHELL_KNOWN(XZ_SHELL_F_RAM_ONLY)|XZ_SHELL_KNOWN(XZ_SHELL_F_ALERT);
  m.deck[d].alert=(struct xz_shell_alert){XZ_SHELL_ALERT_READ_ERROR,"E-8302","CANNOT PLAY TRACK"};
  render(&u,&m,with_card);assert(!card_pixels(with_card,xz_shell_play_rect(&m,XZ_SHELL_R_LANE,d)));
  cases++;
 }
}
int main(void){
 fx_track_build(&track,7);
 layout(XZ_LAYOUT_TWO+1,0);
 layout(XZ_LAYOUT_FOUR+1,1);
 layout(XZ_LAYOUT_MIXER+1,1);
 assert(xz_ram_notice_height(116,10)==52&&xz_ram_notice_height(56,8)==40&&xz_ram_notice_height(20,8)==30);
 printf("PASS RAM card: %d deck/layout cases; card only in the affected lane, every other pixel kept; gone when the waveform or a new track returns\n",cases);
 return 0;
}
