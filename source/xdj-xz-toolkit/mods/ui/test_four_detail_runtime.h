/* SPDX-License-Identifier: MIT */
/* The classic four-deck DECK 3/4 detail panel through the real touch_hook (ui/extra_detail.h, FOUR_DECK.md
   Touch): lane taps open, switch and close it; its keys act on release and only while still under the
   finger; the strip searches a paused deck only (NEEDLE LOCK, held platter counts as playing); DECK SELECT
   runs once, outside ui_mutex, only toward the other deck; no contact on a DECK 3/4 lane, a gap or the
   panel reaches stock; native lanes right of the panel still map to their rows; revocation, order changes
   and MODS close or cancel; the header strip keeps its taps in either order. Included once by
   test_runtime_controls.c. */
static int fixture_detail=-1,fixture_notice,fixture_notices;
int xz_native_four_detail(void){return fixture_detail;}
void xz_native_four_set_detail(int deck){fixture_detail=deck==0||deck==1?deck:-1;}
void xz_native_four_detail_notice(int notice){fixture_notice=notice;fixture_notices++;}
static struct xz_extra_decks fixture_extra;
int xz_extra_decks_snapshot(struct xz_extra_decks *out){*out=fixture_extra;return fixture_extra.route_up;}
static struct xz_extra_deck_event fixture_events[64];
static int fixture_event_count,fixture_event_accept=1;
int xz_extra_deck_input(const struct xz_extra_deck_event *e){
    if(fixture_event_count<64)fixture_events[fixture_event_count++]=*e;
    return fixture_event_accept;
}
static int fixture_toggles[2],fixture_toggle_result=1,fixture_toggle_under_lock;
int xz_runtime_deck_select_target(int side,int extra){
    /* DECK SELECT takes the deck-select lock and snapshots the players: never under ui_mutex. */
    if(pthread_mutex_trylock(&ui_mutex))fixture_toggle_under_lock++;else pthread_mutex_unlock(&ui_mutex);
    assert((side==0||side==1)&&(extra==0||extra==1));
    int *target=side?&fixture_extra.right_on_4:&fixture_extra.left_on_3;
    if(*target==extra)return 1;
    fixture_toggles[side]++;
    if(fixture_toggle_result==1)*target=extra;
    return fixture_toggle_result;
}

/* Screen point of a surface point. */
#define FOUR_X(x) (131u+(unsigned)(x))
#define FOUR_Y(y) (18u+(unsigned)(y))
static void fdr_tap(void *self,unsigned x,unsigned y){contact(self,x,y,1);contact(self,x,y,0);}
/* A native release that clears the coordinates, as the stock touch driver may send. */
static void fdr_release_cleared(void *self){contact(self,0,0,0);}
static void fdr_tap_box(void *self,int hit){
    struct xz_extra_detail_box b=xz_extra_detail_box(hit);
    fdr_tap(self,FOUR_X(b.x+b.w/2),FOUR_Y(b.y+b.h/2));
}
static void fdr_fixture_decks(void){
    memset(&fixture_extra,0,sizeof fixture_extra);fixture_extra.route_up=1;
    for(int i=0;i<XZ_EXTRA_DECKS;i++){
        struct xz_extra_deck *d=&fixture_extra.deck[i];
        d->loaded=1;d->known=XZ_EXTRA_KNOWN(XZ_EXTRA_F_PLAYING)|XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL);
        d->total_ms=300000;d->playing=i;   /* DECK 3 paused, DECK 4 playing */
    }
}
static void four_detail_runtime_check(void *self){
    struct xz_four_layout g;assert(xz_four_layout_get(XZ_FOUR_STACKED,&g));
    int saved_active=native_active,saved_four=native_four,saved_metrics=model.metrics_enabled;
    native_active=0;native_four=XZ_FOUR_STACKED;fixture_detail=-1;fdr_fixture_decks();model.metrics_enabled=1;
    int stock_before=stock_calls;unsigned repaints=fixture_repaints;
    unsigned lane3=FOUR_Y(g.lane_y[2]+30),lane4=FOUR_Y(g.lane_y[3]+30),right=FOUR_X(400),left=FOUR_X(100);

    /* Taps on a DECK 3/4 lane open its panel, the other lane switches, the same lane closes; nothing reaches stock. */
    fdr_tap(self,right,lane3);assert(fixture_detail==0&&fixture_repaints>repaints);
    fdr_tap(self,right,lane4);assert(fixture_detail==1);
    fdr_tap(self,right,lane4);assert(fixture_detail==-1);
    contact(self,right,lane3,1);fdr_release_cleared(self);assert(fixture_detail==0);
    fdr_tap(self,right,lane3);assert(fixture_detail==-1);
    assert(stock_calls==stock_before);
    /* A tap that leaves its lane, crosses a gap, or outlives its order or layout is cancelled and still owned. */
    contact(self,right,lane3,1);contact(self,right,FOUR_Y(g.lane_y[0]+20),1);contact(self,right,FOUR_Y(g.lane_y[0]+20),0);
    contact(self,right,lane3,1);contact(self,right,FOUR_Y(g.lane_y[2]-2),1);fdr_release_cleared(self);
    contact(self,right,lane3,1);native_four=XZ_FOUR_MIXER;contact(self,right,lane3,0);native_four=XZ_FOUR_STACKED;
    contact(self,right,lane3,1);native_four=-1;contact(self,right,lane3,0);native_four=XZ_FOUR_STACKED;
    contact(self,right,FOUR_Y(g.lane_y[1]+XZ_FOUR_LANE_HEIGHT+1),1);contact(self,right,FOUR_Y(g.lane_y[1]+XZ_FOUR_LANE_HEIGHT+1),0);
    assert(fixture_detail==-1&&stock_calls==stock_before);
    /* A native lane right of a closed panel still maps to its own row. */
    contact(self,right,FOUR_Y(g.lane_y[1]+40),1);
    assert(stock_calls==stock_before+1&&last_stock.y==FOUR_Y(xz_four_layout_source_row(&g,g.lane_y[1]+40)));
    contact(self,right,FOUR_Y(g.lane_y[1]+40),0);stock_before=stock_calls;

    /* Open: every contact on the panel is the mod's, over native lanes too; native lanes right of it still map. */
    fdr_tap(self,right,lane3);assert(fixture_detail==0);
    fdr_tap(self,left,FOUR_Y(g.lane_y[0]+30));fdr_tap(self,left,FOUR_Y(g.lane_y[1]+30));
    fdr_tap(self,left,lane4);
    assert(fixture_detail==0&&stock_calls==stock_before);
    contact(self,right,FOUR_Y(g.lane_y[0]+30),1);
    assert(stock_calls==stock_before+1&&last_stock.y==FOUR_Y(xz_four_layout_source_row(&g,g.lane_y[0]+30)));
    contact(self,right,FOUR_Y(g.lane_y[0]+30),0);stock_before=stock_calls;

    /* CLOSE acts on release while still under the finger; dragged off it cancels. */
    struct xz_extra_detail_box close=xz_extra_detail_box(XZ_EXTRA_DETAIL_CLOSE);
    contact(self,FOUR_X(close.x+5),FOUR_Y(close.y+5),1);assert(fixture_detail==0);
    contact(self,FOUR_X(close.x+5),FOUR_Y(close.y+60),1);contact(self,FOUR_X(close.x+5),FOUR_Y(close.y+60),0);assert(fixture_detail==0);
    /* A release can carry a different endpoint without an intervening move. It cancels the key. */
    contact(self,FOUR_X(close.x+5),FOUR_Y(close.y+5),1);
    contact(self,FOUR_X(close.x+5),FOUR_Y(close.y+60),0);assert(fixture_detail==0);
    /* Changing the lane order during a panel key contact cancels that contact too. */
    contact(self,FOUR_X(close.x+5),FOUR_Y(close.y+5),1);native_four=XZ_FOUR_MIXER;
    fdr_release_cleared(self);assert(fixture_detail==0);native_four=XZ_FOUR_STACKED;
    contact(self,FOUR_X(close.x+5),FOUR_Y(close.y+5),1);fdr_release_cleared(self);assert(fixture_detail==-1);
    fdr_tap(self,right,lane3);assert(fixture_detail==0);

    /* DECK SELECT: once, outside ui_mutex, only toward the deck the side does not drive yet. */
    fixture_extra.left_on_3=0;
    fdr_tap_box(self,XZ_EXTRA_DETAIL_EXTRA_SIDE);assert(fixture_toggles[0]==1&&fixture_extra.left_on_3==1&&!fixture_toggle_under_lock);
    fdr_tap_box(self,XZ_EXTRA_DETAIL_EXTRA_SIDE);assert(fixture_toggles[0]==1);
    fdr_tap_box(self,XZ_EXTRA_DETAIL_NATIVE_SIDE);assert(fixture_toggles[0]==2&&fixture_extra.left_on_3==0&&!fixture_toggles[1]);
    int notices=fixture_notices;fixture_toggle_result=0;
    fdr_tap_box(self,XZ_EXTRA_DETAIL_EXTRA_SIDE);
    assert(fixture_toggles[0]==3&&fixture_notices==notices+1&&fixture_notice==XZ_EXTRA_DETAIL_NOTICE_SELECT_FAILED);
    fixture_toggle_result=1;
    fdr_tap(self,right,lane4);assert(fixture_detail==1);
    fixture_extra.right_on_4=0;fdr_tap_box(self,XZ_EXTRA_DETAIL_EXTRA_SIDE);
    assert(fixture_toggles[1]==1&&fixture_extra.right_on_4==1&&fixture_toggles[0]==3);
    fixture_extra.right_on_4=0;
    assert(stock_calls==stock_before);

    /* Needle search: the paused DECK 3 searches on press and per column while dragging; the playing DECK 4 is
       locked, as are a held platter and an unknown play state; each refusal is said once per contact. */
    fdr_tap(self,right,lane3);assert(fixture_detail==0);
    struct xz_extra_detail_box strip=xz_extra_detail_box(XZ_EXTRA_DETAIL_OVERVIEW);
    unsigned sy=FOUR_Y(strip.y+strip.h/2);
    fixture_event_count=0;
    contact(self,FOUR_X(strip.x),sy,1);
    assert(fixture_event_count==1&&fixture_events[0].kind==XZ_EXTRA_IN_SEEK&&fixture_events[0].deck==0&&
           fixture_events[0].pressed&&fixture_events[0].seek_ms==0);
    contact(self,FOUR_X(strip.x),sy,1);assert(fixture_event_count==1);
    contact(self,FOUR_X(strip.x+strip.w/2),sy,1);
    assert(fixture_event_count==2&&fixture_events[1].seek_ms==xz_extra_detail_seek_ms(strip.x+strip.w/2,300000));
    contact(self,FOUR_X(strip.x+strip.w+40),FOUR_Y(strip.y-30),1);
    assert(fixture_event_count==3&&fixture_events[2].seek_ms==300000);
    fdr_release_cleared(self);assert(fixture_event_count==3&&fixture_detail==0);
    fdr_tap(self,right,lane4);assert(fixture_detail==1);
    notices=fixture_notices;
    contact(self,FOUR_X(strip.x+10),sy,1);contact(self,FOUR_X(strip.x+90),sy,1);contact(self,FOUR_X(strip.x+90),sy,0);
    assert(fixture_event_count==3&&fixture_notices==notices+1&&fixture_notice==XZ_EXTRA_DETAIL_NOTICE_NEEDLE_PLAYING);
    fixture_extra.deck[1].playing=0;fixture_extra.deck[1].held_playing=1;
    fdr_tap(self,FOUR_X(strip.x+20),sy);assert(fixture_event_count==3&&fixture_notice==XZ_EXTRA_DETAIL_NOTICE_NEEDLE_HELD);
    fixture_extra.deck[1].held_playing=0;fixture_extra.deck[1].known&=~XZ_EXTRA_KNOWN(XZ_EXTRA_F_PLAYING);
    fdr_tap(self,FOUR_X(strip.x+30),sy);assert(fixture_event_count==3&&fixture_notice==XZ_EXTRA_DETAIL_NOTICE_NEEDLE_PLAYING);
    fixture_extra.deck[1].known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_PLAYING);fixture_extra.deck[1].known&=~XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL);
    fdr_tap(self,FOUR_X(strip.x+40),sy);assert(fixture_event_count==3&&fixture_notice==XZ_EXTRA_DETAIL_NOTICE_NO_LENGTH);
    fixture_extra.deck[1].known|=XZ_EXTRA_KNOWN(XZ_EXTRA_F_TOTAL);fixture_extra.deck[1].loaded=0;
    fdr_tap(self,FOUR_X(strip.x+50),sy);assert(fixture_event_count==3&&fixture_notice==XZ_EXTRA_DETAIL_NOTICE_NO_TRACK);
    fixture_extra.deck[1].loaded=1;fixture_event_accept=0;
    fdr_tap(self,FOUR_X(strip.x+60),sy);assert(fixture_event_count==4&&fixture_notice==XZ_EXTRA_DETAIL_NOTICE_REFUSED);
    fixture_event_accept=1;
    /* Current controls distinguish physical TOP hold from coast after release. Only the hold may seek. */
    fixture_extra.deck[1].held_playing=1;fixture_extra.deck[1].platter_contact=1;
    fdr_tap(self,FOUR_X(strip.x+60),sy);assert(fixture_event_count==5);
    fixture_extra.deck[1].platter_contact=0;
    fdr_tap(self,FOUR_X(strip.x+60),sy);assert(fixture_event_count==5&&fixture_notice==XZ_EXTRA_DETAIL_NOTICE_NEEDLE_HELD);
    fixture_extra.deck[1].held_playing=0;fixture_event_count=4;
    /* A strip drag outliving its panel stops searching. */
    contact(self,FOUR_X(strip.x+70),sy,1);assert(fixture_event_count==5);
    fixture_detail=-1;contact(self,FOUR_X(strip.x+120),sy,1);contact(self,FOUR_X(strip.x+120),sy,0);
    assert(fixture_event_count==5&&stock_calls==stock_before);

    /* Revocation: MODS (or any layout loss) closes the panel at the next touch; a contact begun on the panel
       stays owned to its release. */
    fdr_tap(self,right,lane3);assert(fixture_detail==0);
    contact(self,left,FOUR_Y(g.lane_y[0]+30),1);native_four=-1;contact(self,left,FOUR_Y(g.lane_y[0]+31),1);
    assert(fixture_detail==-1);contact(self,left,FOUR_Y(g.lane_y[0]+31),0);
    assert(stock_calls==stock_before);native_four=XZ_FOUR_STACKED;
    fdr_tap(self,right,lane3);assert(fixture_detail==0);
    /* The header strip over the surface keeps its tap even above a DECK 3 lane (MIXER) or the panel. */
    struct xz_four_layout mixer;assert(xz_four_layout_get(XZ_FOUR_MIXER,&mixer)&&mixer.lane_y[2]==0);
    native_four=XZ_FOUR_MIXER;
    contact(self,XZ_METRICS_X+10,20,1);assert(touch.visible&&ui.page==XZ_UI_METRICS_SETUP);contact(self,XZ_METRICS_X+10,20,0);
    contact(self,400,460,1);assert(fixture_detail==-1);contact(self,400,460,0);
    struct xz_ui_action close_mods={.kind=XZ_UI_CLOSE};apply(NULL,&close_mods,1);assert(!touch.visible);
    fdr_tap(self,right,FOUR_Y(mixer.lane_y[2]+30));assert(fixture_detail==0);
    contact(self,FOUR_X(160),20,1);assert(touch.visible&&ui.page==XZ_UI_METRICS_SETUP);contact(self,FOUR_X(160),20,0);
    apply(NULL,&close_mods,1);assert(!touch.visible);
    model.metrics_enabled=0;fixture_detail=0;
    fdr_tap(self,FOUR_X(160),20);assert(!touch.visible&&fixture_detail==0);
    fdr_tap(self,FOUR_X(close.x+30),FOUR_Y(close.y+10));assert(fixture_detail==-1);
    assert(stock_calls==stock_before&&!fixture_toggle_under_lock);

    /* A panel opened by hardware also revokes detail without requiring another touch. */
    fixture_detail=0;touch.visible=1;inline_layout_publish();assert(fixture_detail==-1);touch.visible=0;

    native_active=saved_active;native_four=saved_four;model.metrics_enabled=saved_metrics;fixture_detail=-1;
    ui.page=XZ_UI_CONTROLS;ui.notice[0]=0;
    puts("PASS classic four-deck detail panel: lane taps open/switch/close, keys on release, DECK SELECT outside the UI lock, "
         "NEEDLE LOCK and refusals once per contact, revocation/order change/MODS, header strip, no stock leaks");
}
