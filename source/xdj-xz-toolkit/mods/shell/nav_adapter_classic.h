#include "../ui/classic_column.h"
#include "../ui/classic_mixer.h"
static int nav_full_composition;
static struct xz_shell_deck nav_full_native[2];
void nav_classic_full(const struct xz_shell_model *s){nav_full_composition=1;memcpy(nav_full_native,s->deck,sizeof nav_full_native);}
#include "../ui/extra_lane.h"
/* The same CLASSIC surface and touch hook used by the runtime fixture tests. */
void nav_classic_start(struct xz_ui_model *out){
 model=*out;model.ui_interface=0;native_active=0;native_four=XZ_FOUR_STACKED;
 xz_native_touch_init(&touch,&ui,&model,apply,NULL);stock_touch=stock;touch.visible=0;fixture_detail=-1;fdr_fixture_decks();
 for(int d=0;d<2;d++){
  struct xz_extra_deck *k=&fixture_extra.deck[d];
  snprintf(k->title,sizeof k->title,"HOST FIXTURE DECK %d",d+3);
  k->known=~0u&((1u<<XZ_EXTRA_FIELD_COUNT)-1);k->elapsed_ms=32000;k->total_ms=300000;
  static struct xz_extra_column cols[1500];
  for(int i=0;i<1500;i++)cols[i]=(struct xz_extra_column){(unsigned char)(30+i%180),(unsigned char)(20+i%90),(unsigned char)(10+i%40)};
  k->wave=(struct xz_extra_wave){cols,1500,300000,500};
  k->bpm_x100=k->track_bpm_x100=12800;k->vinyl=k->quantize=1;
 }
}
void nav_classic_touch(int x,int y,int down){static unsigned char self[128];contact(self,(unsigned)x,(unsigned)y,down);}
void nav_classic_draw(uint16_t *pixels,const uint16_t *stock,unsigned now){
 static uint16_t wave[XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT];
 memset(wave,0,sizeof wave);
 /* Invert the existing BELOW / HALF photo's row map, as preview_four_deck.py does. */
 for(int d=0;d<2;d++)for(int src=0;src<132;src++){
  int best=0,distance=1000;
  for(int row=0;row<108;row++){int mapped=row<10?row:row>=98?132-(108-row):10+(row-10)*112/88;int delta=abs(mapped-src);if(delta<distance){distance=delta;best=row;}}
  int target=d?136+src:src,sy=18+d*132+best;
  memcpy(wave+target*XZ_WAVE_WIDTH,stock+sy*800+131,XZ_WAVE_WIDTH*2);
  if(sy<24)for(int x=151;x<536;x++)wave[target*XZ_WAVE_WIDTH+x]=0;
 }
 xz_four_layout_pixels(wave,XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT,XZ_WAVE_WIDTH,native_four);
 struct xz_four_frame frame={.decks=&fixture_extra,.order=native_four,.now_ms=now,.column_us=XZ_EXTRA_LANE_COLUMN_US,.background=1,.detail=fixture_detail,.notice=fixture_notice};
 xz_four_frame_draw(wave,XZ_WAVE_WIDTH*XZ_WAVE_HEIGHT,&frame,NULL);
 memcpy(pixels,stock,800*480*2);
 for(int y=0;y<XZ_WAVE_HEIGHT;y++)memcpy(pixels+(y+18)*800+131,wave+y*XZ_WAVE_WIDTH,XZ_WAVE_WIDTH*2);
 if(nav_full_composition){
 struct xz_classic_column_view col={.layout=XZ_LAYOUT_FOUR};
 for(int d=0;d<4;d++){col.deck[d].live=1;col.deck[d].source=1;snprintf(col.deck[d].key,8,"Am");}
 xz_classic_column_draw(pixels,800,800*480,&col);

 struct xz_classic_mixer_view mix={.native=nav_full_native,.extra=&fixture_extra,.now_ms=now,.layout=XZ_LAYOUT_FOUR};
 xz_classic_mixer_draw(pixels,800,800*480,&mix);
 }
}

int nav_classic_detail(void){return fixture_detail;}
