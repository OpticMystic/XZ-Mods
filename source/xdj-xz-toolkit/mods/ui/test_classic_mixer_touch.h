/* SPDX-License-Identifier: MIT */
static int fixture_mixer_visible;
int xz_classic_mixer_runtime_visible(void){return fixture_mixer_visible;}
int xz_classic_mixer_runtime_layout(void){return xz_deck_layout_get();}
void xz_classic_mixer_runtime_hide(void){fixture_mixer_visible=0;}
int xz_classic_mixer_runtime_native_point(int deck,int lx,int ly,int *x,int *y){
 if(deck<0||deck>1||lx<0||lx>299||ly<0||ly>33)return 0;
 *x=7+400*deck+lx;*y=423+ly;return 1;
}
static void classic_mixer_touch_check(void *self){
 int old_four=native_four,old_active=native_active;
 xz_deck_layout_set(XZ_LAYOUT_MIXER);native_four=XZ_FOUR_MIXER;native_active=0;fixture_mixer_visible=1;fixture_detail=-1;
 fdr_fixture_decks();fixture_event_count=0;
 int calls=stock_calls;
 fdr_tap(self,80,320);assert(fixture_detail==0&&stock_calls==calls);
 fdr_tap(self,690,320);assert(fixture_detail==1&&stock_calls==calls);
 fixture_detail=-1;
 contact(self,80,320,1);contact(self,80,370,0);assert(fixture_detail==-1);
 for(unsigned x=50;x<800;x+=200)fdr_tap(self,x,370);
 assert(stock_calls==calls&&fixture_event_count==0);
 /* DECK 1 is the second channel: capture survives a drag into channel 4. */
 contact(self,206,440,1);assert(stock_calls==++calls&&last_stock.x==7&&last_stock.y==423&&last_stock.down);
 contact(self,770,470,1);assert(stock_calls==++calls&&last_stock.x==306&&last_stock.y==453);
 fdr_release_cleared(self);assert(stock_calls==++calls&&!last_stock.down&&last_stock.x==306);
 contact(self,406,440,1);assert(stock_calls==++calls&&last_stock.x==407&&last_stock.y==423);
 contact(self,593,473,0);assert(stock_calls==++calls&&!last_stock.down);
 /* DECK 3 is the first channel. Only it receives a paused software seek. */
 contact(self,6,450,1);assert(fixture_event_count==1&&fixture_events[0].deck==0&&fixture_events[0].seek_ms==0);
 contact(self,193,450,1);assert(fixture_event_count==2&&fixture_events[1].seek_ms==300000);
 fdr_release_cleared(self);assert(stock_calls==calls);
 fdr_tap(self,690,450);assert(fixture_event_count==2&&stock_calls==calls&&async_notice.text[0]);
 /* The overview of the playing DECK 4 never turns into a native DECK 2 touch. */
 fixture_extra.deck[1].playing=0;fixture_extra.deck[1].held_playing=1;fixture_extra.deck[1].platter_contact=0;
 fdr_tap(self,690,450);assert(fixture_event_count==2);
 fixture_extra.deck[1].platter_contact=1;fdr_tap(self,690,450);assert(fixture_event_count==3&&fixture_events[2].deck==1);
 /* A native gesture ends once when the route disappears; no later event jumps to another deck. */
 contact(self,250,450,1);assert(stock_calls==++calls&&last_stock.down);
 native_four=-1;contact(self,770,451,1);assert(stock_calls==++calls&&!last_stock.down);
 contact(self,770,451,0);assert(stock_calls==calls);
 fixture_mixer_visible=0;
 contact(self,250,450,1);assert(stock_calls==++calls&&last_stock.x==250&&last_stock.y==450);
 contact(self,250,450,0);calls=stock_calls;
 /* A gesture that began on an upper software lane stays there when it reaches the bottom. */
 native_four=XZ_FOUR_STACKED;fixture_mixer_visible=1;fixture_detail=-1;
 contact(self,500,180,1);contact(self,500,450,1);contact(self,500,450,0);
 assert(stock_calls==calls&&!mixer_contact.active&&fixture_event_count==3);
 /* MODS opened while holding a native overview releases stock before taking ownership. */
 contact(self,250,450,1);assert(stock_calls==++calls&&last_stock.down);
 touch.visible=1;contact(self,250,451,1);assert(stock_calls==++calls&&!last_stock.down);
 contact(self,250,451,0);assert(stock_calls==calls);touch.visible=0;
 /* Every higher-priority owner cancels a held native gesture once, before it sees input. */
 for(int takeover=0;takeover<5;takeover++){
  play_key.open=play_key.down=0;model.ui_interface=XZ_UI_INTERFACE_CLASSIC;arcade_present=0;
  xz_deck_layout_set(XZ_LAYOUT_MIXER);native_four=XZ_FOUR_MIXER;
  contact(self,250,450,1);assert(stock_calls==++calls&&last_stock.down);
  if(takeover==0)play_key.open=1;
  if(takeover==1)model.ui_interface=XZ_UI_INTERFACE_MODERN;
  if(takeover==2)arcade_present=1;
  if(takeover==3)xz_deck_layout_set(XZ_LAYOUT_FOUR);
  if(takeover==4){model.fb_takeover=1;model.connection.connected=1;}
  contact(self,250,451,1);assert(stock_calls==++calls&&!last_stock.down&&mixer_contact.cancelled);
  contact(self,250,452,1);assert(stock_calls==calls);
  fdr_release_cleared(self);assert(stock_calls==calls&&!mixer_contact.active);
  play_key.open=play_key.down=0;model.ui_interface=XZ_UI_INTERFACE_CLASSIC;arcade_present=0;
  model.fb_takeover=model.connection.connected=0;
 }
 /* Number order keeps the mixer's channels 3 1 2 4 (decision D3b): DECK 1 is still the second channel and
    DECK 3 the first, with captured drag ownership. */
 xz_deck_layout_set(XZ_LAYOUT_FOUR);native_four=XZ_FOUR_STACKED;
 contact(self,206,440,1);assert(stock_calls==++calls&&last_stock.x==7);
 contact(self,790,473,1);assert(stock_calls==++calls&&last_stock.x==306);
 fdr_release_cleared(self);assert(stock_calls==++calls&&!last_stock.down);
 fixture_detail=-1;fdr_tap(self,50,320);assert(fixture_detail==0);
 fdr_tap(self,450,320);assert(fixture_detail==0&&stock_calls==calls);
 xz_deck_layout_set(XZ_LAYOUT_FOUR);fixture_mixer_visible=0;fixture_detail=-1;native_four=old_four;native_active=old_active;
 puts("PASS Classic mixer touches: 3,1,2,4 channels in both orders, titles, captured native overview mapping, software needle lock, crossing channels, route revocation, MODS, upper-lane ownership and two-deck passthrough");
}
