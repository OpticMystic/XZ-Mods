/* SPDX-License-Identifier: MIT */
/* CLASSIC 2 DECKS follows the sides (FOUR_DECK.md) through the real touch_hook and xz_mods_render_v1: a half the
   wave painted as DECK 3 or 4 owns its wave rows, its half of the side column and its bottom panel, so stock never
   sees those contacts as DECK 1 or 2 touches; the panel's overview searches the software deck under NEEDLE LOCK;
   the other side stays stock; route loss mid-contact keeps the contact owned to release and then gives the half
   back; the overlay, the DECKS button and the key slots follow the same painted pair; a stock half keeps its stem
   rows. Included once by test_runtime_controls.c. */
static unsigned fixture_two_pair;
unsigned xz_native_two_pair(void){return fixture_two_pair;}
static unsigned fixture_pair_frames,fixture_pair_frame_pair;static int fixture_pair_frame_eligible;
int xz_classic_mixer_runtime_pair_frame(uint16_t *p,size_t stride,size_t count,int eligible,int theme,uint32_t now,
 const struct xz_classic_mixer_key keys[2],unsigned pair){
    (void)p;(void)stride;(void)count;(void)theme;(void)now;(void)keys;
    fixture_pair_frames++;fixture_pair_frame_pair=pair;fixture_pair_frame_eligible=eligible;return eligible&&pair;
}
static void tpr_tap(void *self,unsigned x,unsigned y){contact(self,x,y,1);contact(self,x,y,0);}
static void two_pair_runtime_check(void *self){
    int saved_active=native_active,saved_four=native_four,saved_interface=model.ui_interface,saved_stems=model.stems_overlay;
    native_active=1;native_four=-1;model.ui_interface=XZ_UI_INTERFACE_CLASSIC;xz_deck_layout_set(XZ_LAYOUT_TWO);
    fdr_fixture_decks();fixture_extra.left_on_3=1;fixture_extra.deck[0].playing=0; /* DECK 3 paused, DECK 4 playing */
    fixture_event_count=0;

    /* Left half on DECK 3: its wave rows, column half and panel never reach stock or retarget the browse deck. */
    fixture_two_pair=1;ui.deck=inline_ui.deck=1;
    int before=stock_calls;
    tpr_tap(self,400,18+60);tpr_tap(self,140,18+131);tpr_tap(self,60,30);tpr_tap(self,60,150);tpr_tap(self,200,380);tpr_tap(self,10,470);
    assert(stock_calls==before&&ui.deck==1&&!fixture_event_count);
    /* The right half is stock: its wave rows, column and panel reach stock as before. */
    tpr_tap(self,400,18+132);assert(stock_calls==before+2);
    tpr_tap(self,60,200);assert(stock_calls==before+4&&ui.deck==1);
    tpr_tap(self,600,380);assert(stock_calls==before+6);
    puts("PASS 2 DECKS pair touch: a DECK 3 half owns its wave rows, column half and panel; the stock half keeps its");

    /* Needle search on the left panel's overview: a paused DECK 3 seeks to the touched column, a drag follows. */
    struct xz_classic_mixer_rect r=xz_classic_half_overview_rect(0);
    fixture_event_count=0;before=stock_calls;
    tpr_tap(self,(unsigned)(r.x+150),(unsigned)(r.y+10));
    assert(fixture_event_count==1&&fixture_events[0].deck==0&&fixture_events[0].kind==XZ_EXTRA_IN_SEEK&&
           fixture_events[0].seek_ms==xz_classic_half_seek_ms(0,r.x+150,300000));
    contact(self,(unsigned)(r.x+10),(unsigned)(r.y+40),1);contact(self,(unsigned)(r.x+200),(unsigned)(r.y+40),1);
    contact(self,(unsigned)(r.x+600),(unsigned)(r.y+40),1);contact(self,(unsigned)(r.x+600),(unsigned)(r.y+40),0);
    assert(fixture_event_count==4&&fixture_events[3].seek_ms==xz_classic_half_seek_ms(0,r.x+r.w-1,300000)&&stock_calls==before);
    /* Right on DECK 4 too: a playing deck refuses with the NEEDLE LOCK line and seeks nothing. */
    fixture_two_pair=3;fixture_extra.right_on_4=1;async_notice.text[0]=0;
    struct xz_classic_mixer_rect rr=xz_classic_half_overview_rect(1);
    tpr_tap(self,(unsigned)(rr.x+100),(unsigned)(rr.y+10));
    assert(fixture_event_count==4&&strstr(async_notice.text,"DECK 4")&&stock_calls==before);
    tpr_tap(self,400,18+200);tpr_tap(self,60,200);tpr_tap(self,600,380);assert(stock_calls==before);
    puts("PASS 2 DECKS pair needle search: paused DECK 3 seeks and drags clamp to the overview, playing DECK 4 refuses with a notice");

    /* Route loss mid-contact: the contact the half owned stays owned to its release; the next touch is stock's. */
    contact(self,400,18+60,1);fixture_two_pair=0;fixture_extra.left_on_3=fixture_extra.right_on_4=0;
    contact(self,(unsigned)(r.x+40),(unsigned)(r.y+10),1);contact(self,(unsigned)(r.x+40),(unsigned)(r.y+10),0);
    assert(stock_calls==before&&fixture_event_count==4);
    tpr_tap(self,400,18+60);assert(stock_calls==before+2);
    tpr_tap(self,60,30);assert(stock_calls==before+4&&ui.deck==0);
    puts("PASS 2 DECKS pair route loss: an owned contact stays owned to release without seeking, then both halves are stock again");

    /* Stem rows: a half on DECK 3 has none (the DECKS button band is the button's again); the stock half keeps its. */
    model.stems_overlay=XZ_STEM_LAYOUT_OUTER;
    fixture_two_pair=0;assert(stem_row_at_locked(300,30)==0&&stem_row_at_locked(300,18+250)==1);
    fixture_two_pair=1;assert(stem_row_at_locked(300,30)==-1&&stem_row_at_locked(300,18+250)==1);
    fixture_two_pair=2;assert(stem_row_at_locked(300,30)==0&&stem_row_at_locked(300,18+250)==-1);
    model.stems_overlay=saved_stems;

    /* Key slots address the software deck only where the wave painted it. */
    struct xz_extra_decks e=fixture_extra;e.left_on_3=e.right_on_4=1;
    fixture_two_pair=1;assert(xz_classic_key_target(classic_key_extra(&e,1),0)==2&&xz_classic_key_target(classic_key_extra(&e,1),1)==1);
    e.left_on_3=e.right_on_4=1;fixture_two_pair=0;assert(!classic_key_extra(&e,1));
    e.left_on_3=e.right_on_4=1;fixture_two_pair=3;assert(xz_classic_key_target(classic_key_extra(&e,1),1)==3);
    native_four=XZ_FOUR_STACKED;e.left_on_3=e.right_on_4=1;fixture_two_pair=0;assert(classic_key_extra(&e,1)==&e);native_four=-1;

    /* The overlay frame and the DECKS button take the painted pair; MODS and MODERN get no pair frame. */
    static uint16_t frame_pixels[800*480];int saved_button=model.deck_view_button;model.deck_view_button=1;
    fixture_extra.route_up=1;fixture_extra.left_on_3=1;fixture_two_pair=1;fixture_pair_frames=0;
    assert(xz_mods_render_v1(frame_pixels,800,480,800,NULL));
    assert(fixture_pair_frames==1&&fixture_pair_frame_pair==1&&fixture_pair_frame_eligible&&badge_frame_cache.dv_pair==1);
    fixture_two_pair=0;assert(xz_mods_render_v1(frame_pixels,800,480,800,NULL));
    assert(fixture_pair_frames==2&&!fixture_pair_frame_pair&&!fixture_pair_frame_eligible&&badge_frame_cache.dv_pair==0);
    fixture_two_pair=1;touch.visible=1;xz_mods_render_v1(frame_pixels,800,480,800,NULL);assert(fixture_pair_frames==2);
    touch.visible=0;model.fb_takeover=1;xz_mods_render_v1(frame_pixels,800,480,800,NULL);
    assert(fixture_pair_frames==3&&!fixture_pair_frame_eligible);model.fb_takeover=0;
    xz_deck_layout_set(XZ_LAYOUT_FOUR);xz_mods_render_v1(frame_pixels,800,480,800,NULL);assert(fixture_pair_frames==3);
    xz_deck_layout_set(XZ_LAYOUT_TWO);model.deck_view_button=saved_button;
    puts("PASS 2 DECKS pair overlay: the pair frame and DECKS button follow the painted pair, not over MODS, a takeover or four-deck views");

    fixture_two_pair=0;memset(&fixture_extra,0,sizeof fixture_extra);fixture_event_count=0;
    native_active=saved_active;native_four=saved_four;model.ui_interface=saved_interface;
}
