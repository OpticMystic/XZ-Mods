#ifdef NDEBUG
#error Shell acceptance requires active assertions
#endif
#include "four_deck_fixtures.h"
#include "shell_settings.h"
#include "shell_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct fx_track track;
static uint16_t pixels[804*480],shown[804*480];
static uint16_t rgb565(uint32_t c){return (uint16_t)(((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31));}
static void render(const struct xz_shell *u,const struct xz_shell_model *m){
 for(int i=0;i<804*480;i++)pixels[i]=0xa55a;
 assert(xz_shell_render(u,m,pixels,804));
 for(int y=0;y<480;y++)for(int x=800;x<804;x++)assert(pixels[y*804+x]==0xa55a);
}
static void test_setting(void){
 struct xz_shell u;struct xz_shell_model m;struct xz_shell_setting_item it;
 struct xz_shell_action out[XZ_SHELL_ACTIONS];xz_shell_init(&u);fx_model_four_players(&m,&track);
 assert(sh_settings_item(&u,&m,XZ_SHELL_ITEM_WAVE_TITLES,&it)&&it.known&&it.writable&&it.current==0);
 assert(!strcmp(it.label,"WAVEFORM TITLES")&&it.choice_count==2&&!strcmp(it.choices[1],"HIDE"));
 assert(sh_settings_set(&u,XZ_SHELL_ITEM_WAVE_TITLES,1,out)==1);
 assert(out[0].kind==XZ_SHELL_ACT_SET_SETTING&&out[0].id==XZ_SHELL_ITEM_WAVE_TITLES&&out[0].value==1);
 assert(!sh_indicator_shown(&u,XZ_SHELL_IND_WAVE_TITLES)&&sh_indicator_shown(&u,XZ_SHELL_IND_BYPASS));
 assert(sh_settings_item(&u,&m,XZ_SHELL_ITEM_WAVE_TITLES,&it)&&it.current==1);
 assert(!xz_shell_restore_setting(&u,XZ_SHELL_ITEM_WAVE_TITLES,2));
 assert(!xz_shell_restore_setting(&u,XZ_SHELL_ITEM_WAVE_TITLES,-1));
 assert(xz_shell_restore_setting(&u,XZ_SHELL_ITEM_WAVE_TITLES,0)&&!u.display.hidden);
}
static void test_ruler_and_icons(void){
 static const struct xz_shell_beat beats[]={{2000,1},{2250,2},{2500,3},{2750,4},{3000,1}};
 for(int count=2;count<=4;count+=2)for(int sides=0;sides<4;sides++)for(int order=0;order<2;order++){
  struct xz_shell u;struct xz_shell_model m,view;xz_shell_init(&u);fx_model_four_players(&m,&track);
  u.play_decks=count;u.zoom=3;u.deck=1;m.sides_extra=(uint8_t)sides;m.wave_order=order;
  for(int d=0;d<4;d++){
   m.deck[d].known=XZ_SHELL_KNOWN(XZ_SHELL_F_LOADED)|XZ_SHELL_KNOWN(XZ_SHELL_F_TITLE)|
    XZ_SHELL_KNOWN(XZ_SHELL_F_GRID)|XZ_SHELL_KNOWN(XZ_SHELL_F_ELAPSED)|XZ_SHELL_KNOWN(XZ_SHELL_F_TOTAL);
   m.deck[d].loaded=1;m.deck[d].title="TRACK";m.deck[d].elapsed_ms=2000;m.deck[d].elapsed_at_ms=0;
   m.deck[d].grid=beats;m.deck[d].grid_count=5;m.deck[d].total_ms=10000;
  }
  xz_shell_play_projection(&u,&m,&view);render(&u,&m);memcpy(shown,pixels,sizeof shown);
  for(int d=0;d<4;d++)if(sh_play_visible(&view,d)){
   struct xz_shell_rect tag=xz_shell_play_rect(&view,XZ_SHELL_R_DECK_TAG,d);
   int x=98+700/2+48; /* One source-grid beat ahead at zoom 3. */
   assert(pixels[(tag.y+1)*804+x]==rgb565(xz_blend(SH_BLACK,SH_WHITE,112)));
   assert(pixels[(tag.y+20)*804+x]==rgb565(SH_WHITE));
   if(sh_control_active(&m,d)){
    int right=d&1,filled=30+7+right*12,hollow=30+7+(!right)*12;
    assert(pixels[(tag.y+5)*804+filled]==rgb565(SH_WHITE));
    assert(pixels[(tag.y+5)*804+hollow]==rgb565(SH_BLUE));
    assert(pixels[(tag.y+1)*804+30]==rgb565(SH_WHITE)&&pixels[(tag.y+1)*804+56]==rgb565(SH_WHITE));
   }
  }
  assert(xz_shell_restore_setting(&u,XZ_SHELL_ITEM_WAVE_TITLES,1));render(&u,&m);
  int changed=0;
  for(int d=0;d<4;d++)if(sh_play_visible(&view,d)){
   struct xz_shell_rect tag=xz_shell_play_rect(&view,XZ_SHELL_R_DECK_TAG,d);int x=98+700/2+48;
   assert(pixels[(tag.y+1)*804+x]==rgb565(SH_WHITE)); /* Waveform ruler takes the title space. */
   for(int y=tag.y;y<tag.y+20;y++)for(int xx=104;xx<180;xx++)changed+=pixels[y*804+xx]!=shown[y*804+xx];
  }
  assert(changed>0&&u.deck==1&&m.sides_extra==sides&&m.deck_count==4);
 }
}
static void test_title_clip(void){
 for(int count=2;count<=4;count+=2)for(int sides=0;sides<4;sides++)for(int position=0;position<2;position++){
  struct xz_shell u;struct xz_shell_model m,view;xz_shell_init(&u);fx_model_four_players(&m,&track);
  u.play_decks=count;m.sides_extra=(uint8_t)sides;u.display.wave_position=(enum xz_shell_wave_position)position;
  for(int d=0;d<4;d++)m.deck[d].title="A very long track title that must stop before the playhead [Extended Club Mix]";
  xz_shell_play_projection(&u,&m,&view);render(&u,&m);memcpy(shown,pixels,sizeof shown);
  for(int d=0;d<4;d++)m.deck[d].title="";
  render(&u,&m);
  int head=98+(position==XZ_SHELL_POSITION_LEFT?700*27/100:700/2);
  for(int d=0;d<4;d++)if(sh_play_visible(&view,d)){
   struct xz_shell_rect tag=xz_shell_play_rect(&view,XZ_SHELL_R_DECK_TAG,d);int changed=0;
   for(int y=tag.y;y<tag.y+20;y++)for(int x=104;x<798;x++){
    if(x>=head-10)assert(shown[y*804+x]==pixels[y*804+x]);
    else changed+=shown[y*804+x]!=pixels[y*804+x];
   }
   assert(changed>0);
  }
 }
 puts("PASS long waveform titles leave playhead and future beats clear in both positions/views");
}
int main(void){fx_track_build(&track,7);test_setting();test_ruler_and_icons();test_title_clip();puts("PASS title SHOW/HIDE, full upper beat alignment, selected pairs/orders and bracketed single-filled control icons");}
