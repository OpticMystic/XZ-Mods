/* SPDX-License-Identifier: MIT */
/* DECK VIEW through the real ui_runtime.c hooks (deck_view.h, route_card.h): the CLASSIC button on every
   play screen, the picker by touch, encoder and BACK, a press never acting on an unpainted view, the card's
   RESCAN and TWO DECKS from what the wave transaction painted, the encoder only while the card shows, the
   side column's tags, and MODERN's request reaching the same single entry point. Included once by
   test_runtime_controls.c after the rekordbox and Classic mixer fixtures. */
static uint32_t fixture_card,fixture_card_input;
static int fixture_card_focus=-2,fixture_rescans,fixture_shell_view=-1;
static struct xz_extra_route_status fixture_route;
uint32_t xz_native_four_card(void){return fixture_card;}
void xz_native_four_card_input(uint32_t now,int focus){fixture_card_input=now;fixture_card_focus=focus;}
struct xz_extra_route_status xz_extra_decks_route_status(void){return fixture_route;}
void xz_extra_decks_rescan(void){fixture_rescans++;}
int xz_shell_runtime_take_deck_view(void){int view=fixture_shell_view;fixture_shell_view=-1;return view;}
static uint16_t dv_frame[800*480];
static void dv_paint(void){memset(dv_frame,0,sizeof dv_frame);assert(xz_mods_render_v1(dv_frame,800,480,800,NULL));}
static void dv_tap(void *self,int x,int y){contact(self,(unsigned)x,(unsigned)y,1);contact(self,(unsigned)x,(unsigned)y,0);}
static void dv_back(void){unsigned char input[16]={0};input[8]=8;input[9]=0x42;menu_back_key_hook(NULL,input);input[11]=2;menu_back_key_hook(NULL,input);}
static int dv_push(int press){uint32_t entry[3]={press?1u:2u,0,0};return encoder_push_hook(entry,NULL,NULL,NULL);}
static uint32_t dv_card(int order,int state){return XZ_NATIVE_CARD_SHOWN|(uint32_t)order<<1|(uint32_t)state<<4;}
static int dv_button_inked(void){
    int n=0;for(int y=0;y<18;y++)for(int x=162;x<278;x++)n+=dv_frame[y*800+x]!=0;return n;
}
static void deck_view_runtime_check(void *self){
    int saved_interface=model.ui_interface,saved_four=native_four,saved_active=native_active,saved_visible=fixture_mixer_visible;
    model.ui_interface=XZ_UI_INTERFACE_CLASSIC;touch.visible=0;model.fb_takeover=0;native_play=1;native_active=1;native_four=-1;
    fixture_card=0;fixture_route=(struct xz_extra_route_status){0};fixture_route.state=XZ_EXTRA_ROUTE_NOT_DETECTED;
    memset(&dv,0,sizeof dv);xz_deck_layout_set(XZ_LAYOUT_TWO);
    int calls=stock_calls;
    /* The button is on every Classic play screen, 2 DECKS included; touch reaches y 39. */
    dv_paint();assert(dv.painted&&!dv.painted_open&&dv_button_inked()>200);
    dv_tap(self,170,39);assert(dv.open&&stock_calls==calls);
    /* The picker's picture applies through the one entry point; the next tap waits for its paint. */
    dv_paint();assert(dv.painted_open);
    struct xz_dv_rect tile=xz_deck_view_tile(&xz_deck_view_classic,XZ_LAYOUT_FOUR);
    unsigned repaints=fixture_repaints;
    dv_tap(self,tile.x+20,tile.y+20);assert(xz_deck_layout_get()==XZ_LAYOUT_FOUR&&!dv.open&&fixture_repaints>repaints);
    dv_tap(self,170,10);assert(!dv.open&&stock_calls==calls);
    dv_paint();dv_tap(self,170,10);assert(dv.open);
    /* Outside closes and goes nowhere; the empty panel does nothing. */
    dv_paint();dv_tap(self,xz_deck_view_classic.picker.x+6,xz_deck_view_classic.picker.y+10);assert(dv.open);
    dv_tap(self,700,400);assert(!dv.open&&stock_calls==calls);
    /* The encoder owns the open picker: turn, push applies; BACK closes. */
    dv_paint();dv_tap(self,170,10);dv_paint();
    assert(encoder_rotate_hook(1)==1&&dv.tile==XZ_LAYOUT_MIXER);
    assert(dv_push(1)==1&&xz_deck_layout_get()==XZ_LAYOUT_MIXER&&!dv.open);
    dv_paint();dv_tap(self,170,10);dv_paint();assert(dv.open);
    dv_back();assert(!dv.open&&xz_deck_layout_get()==XZ_LAYOUT_MIXER);
    dv_paint();assert(!dv.painted_open);
    /* The card the wave transaction painted: RESCAN wakes the connector. */
    native_four=XZ_FOUR_MIXER;native_active=0;
    struct xz_four_layout g;assert(xz_four_layout_get(XZ_FOUR_MIXER,&g));
    fixture_card=dv_card(XZ_FOUR_MIXER,XZ_EXTRA_ROUTE_NOT_DETECTED);
    struct xz_route_card_rect rescan=xz_route_card_button(&xz_route_card_classic,(int)g.lane_y[3],XZ_ROUTE_CARD_HIT_PRIMARY);
    struct xz_route_card_rect two=xz_route_card_button(&xz_route_card_classic,(int)g.lane_y[3],XZ_ROUTE_CARD_HIT_TWO);
    int rescans=fixture_rescans;
    dv_tap(self,131+rescan.x+10,18+rescan.y+10);assert(fixture_rescans==rescans+1&&stock_calls==calls);
    /* A state the card has not painted yet is never pressed. */
    fixture_route.state=XZ_EXTRA_ROUTE_WAITING_NATIVE_STOP;
    dv_tap(self,131+rescan.x+10,18+rescan.y+10);assert(fixture_rescans==rescans+1&&stock_calls==calls);
    fixture_route.state=XZ_EXTRA_ROUTE_NOT_DETECTED;
    /* The card's empty parts take the touch and do nothing, also over the detail panel's lanes. */
    dv_tap(self,131+100,18+(int)g.lane_y[2]+30);assert(fixture_detail==-1&&stock_calls==calls);
    /* The encoder while the card shows: push focuses RESCAN, turn moves to TWO DECKS, push applies. */
    assert(dv_push(1)==1&&dv.focus==DV_FOCUS_PRIMARY&&fixture_card_focus==XZ_ROUTE_CARD_HIT_PRIMARY);
    assert(dv_push(0)==1&&dv.focus==DV_FOCUS_PRIMARY);
    assert(encoder_rotate_hook(1)==1&&dv.focus==DV_FOCUS_TWO&&fixture_card_focus==XZ_ROUTE_CARD_HIT_TWO);
    assert(encoder_rotate_hook(1)==1&&dv.focus==DV_FOCUS_BUTTON);
    assert(encoder_rotate_hook(-1)==1&&dv.focus==DV_FOCUS_TWO);
    assert(dv_push(1)==1&&xz_deck_layout_get()==XZ_LAYOUT_TWO&&dv.focus==DV_FOCUS_NONE&&fixture_card_focus==-1);
    /* BACK leaves the card's focus. */
    xz_deck_layout_set(XZ_LAYOUT_MIXER);
    assert(dv_push(1)==1&&dv.focus==DV_FOCUS_PRIMARY);dv_back();assert(dv.focus==DV_FOCUS_NONE);
    /* TWO DECKS by touch. */
    dv_tap(self,131+two.x+10,18+two.y+10);assert(xz_deck_layout_get()==XZ_LAYOUT_TWO&&stock_calls==calls);
    /* No card (decks 3 and 4 ready, or 2 DECKS): the encoder stays the player's. */
    fixture_card=0;xz_deck_layout_set(XZ_LAYOUT_FOUR);native_four=XZ_FOUR_STACKED;
    assert(!dv_push(1)&&dv.focus==DV_FOCUS_NONE);
    /* The side column in a four-deck view: DECK 1's tag is the stock column's own tap at (60,40). */
    xz_deck_layout_set(XZ_LAYOUT_MIXER);native_four=XZ_FOUR_MIXER;fixture_mixer_visible=1;fixture_detail=-1;
    contact(self,40,22+68+20,1);assert(stock_calls==++calls&&last_stock.x==60&&last_stock.y==40&&last_stock.down);
    contact(self,40,22+68+20,0);assert(stock_calls==++calls&&!last_stock.down);
    contact(self,40,22+136+20,1);assert(stock_calls==++calls&&last_stock.x==60&&last_stock.y==180);
    contact(self,40,22+136+20,0);assert(stock_calls==++calls);
    /* DECK 3's tag (first in mixer order) opens its detail like its channel title, not while NOT CONNECTED. */
    dv_tap(self,40,30);assert(fixture_detail==0&&stock_calls==calls);
    dv_tap(self,40,30);assert(fixture_detail==-1);
    fixture_card=dv_card(XZ_FOUR_MIXER,XZ_EXTRA_ROUTE_NOT_DETECTED);
    dv_tap(self,40,30);assert(fixture_detail==-1&&stock_calls==calls);
    fixture_card=0;fixture_mixer_visible=saved_visible;
    /* The top band (top_bar.h). An OUTER stem row under DECKS keeps its touches below y 17; the band stays DECKS. */
    xz_deck_layout_set(XZ_LAYOUT_TWO);native_four=-1;native_active=1;native_play=1;memset(&dv,0,sizeof dv);
    int saved_layout=model.stems_overlay;model.stems_overlay=XZ_STEM_LAYOUT_OUTER_HALF;dv_paint();
    contact(self,170,30,1);assert(inline_contact&&!dv.open&&!dv.contact);contact(self,170,30,0);assert(!inline_contact&&!dv.open);
    dv_paint();dv_tap(self,170,10);assert(dv.open);dv_paint();dv_tap(self,700,400);assert(!dv.open);
    model.stems_overlay=XZ_STEM_LAYOUT_BELOW;dv_paint();dv_tap(self,170,30);assert(dv.open);dv_paint();dv_tap(self,700,400);assert(!dv.open);
    /* VJ.TOOLS ends at y 17: stock DECK 1 select ({0,18,115,140}) gets y 18 on, and the deck it selects is seen. */
    int saved_enabled=model.connection.enabled,saved_connected=model.connection.connected;
    model.connection.enabled=model.connection.connected=1;ui.deck=inline_ui.deck=1;calls=stock_calls;
    dv_paint();dv_tap(self,60,XZ_TOP_BAR_H);assert(stock_calls==calls+2&&ui.deck==0&&!touch.capture);
    /* STEMS answers to y 39 on the play screen, to y 23 elsewhere. */
    int layout=model.stems_overlay;calls=stock_calls;
    dv_tap(self,700,XZ_TOP_BAR_DEEP_H-1);assert(model.stems_overlay!=layout&&stock_calls==calls);
    native_play=0;layout=model.stems_overlay;
    dv_tap(self,700,XZ_TOP_BAR_TOUCH_H);assert(model.stems_overlay==layout&&stock_calls==calls+2);
    dv_tap(self,700,XZ_TOP_BAR_TOUCH_H-1);assert(model.stems_overlay!=layout);
    native_play=1;model.stems_overlay=saved_layout;model.connection.enabled=saved_enabled;model.connection.connected=saved_connected;
    /* MODERN asks through the shell; the same entry point applies it under ui_mutex. */
    fixture_shell_view=XZ_LAYOUT_FOUR;fake_requests|=XZ_SHELL_UI_DECK_VIEW;shell_ui_requests_locked();
    assert(xz_deck_layout_get()==XZ_LAYOUT_FOUR&&fixture_shell_view==-1);
    xz_deck_layout_set(XZ_LAYOUT_TWO);memset(&dv,0,sizeof dv);
    model.ui_interface=saved_interface;native_four=saved_four;native_active=saved_active;native_play=1;
    puts("PASS DECK VIEW CLASSIC: button on every play screen, picker by touch/encoder/BACK, no press on an unpainted view or card state, RESCAN and TWO DECKS from the painted card, encoder only while the card shows, column tags, top band: OUTER stem row keeps its touches under DECKS, stock DECK 1 select from y 18, STEMS to y 39 on the play screen, MODERN request through the one entry point");
}
