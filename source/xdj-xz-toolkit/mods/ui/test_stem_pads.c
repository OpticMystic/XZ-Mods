#ifdef NDEBUG
#error Pad acceptance requires active assertions
#endif
#include "stem_pads.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    struct xz_stem_pads s = {0};
    struct xz_cue_event e = {0, 0, 0, 1, 0, 0, 0, -1, 0};
    int toggle;
    assert(xz_stem_pad_event(&s, &e, 1, &toggle) && toggle == 2 && s.muted[0] == 4);
    assert(xz_stem_pad_event(&s, &e, 1, &toggle) && toggle == -1 && s.muted[0] == 4);
    e.operation = 1;
    assert(xz_stem_pad_event(&s, &e, 0, &toggle));
    e.operation = 2; e.hotcue_mode = 0;
    assert(xz_stem_pad_event(&s, &e, 0, &toggle) && s.owned[0] == 0 && s.muted[0] == 4);
    e.operation = 0; e.hotcue_mode = 1;
    assert(xz_stem_pad_event(&s, &e, 1, &toggle) && s.muted[0] == 0);
    e.operation = 3;
    assert(xz_stem_pad_event(&s, &e, 1, &toggle));
    /* A stock-owned press must keep its repeat and release after opening STEMS. */
    e.operation = 0;
    assert(!xz_stem_pad_event(&s, &e, 0, &toggle));
    assert(!xz_stem_pad_event(&s, &e, 1, &toggle) && toggle == -1);
    e.operation = 2;
    assert(!xz_stem_pad_event(&s, &e, 1, &toggle));
    for (int deck = 0; deck < 2; deck++) for (int pad = 0; pad < 4; pad++) {
        e = (struct xz_cue_event){deck, pad, 0, 1, 0, 0, 0, -1, 0};
        unsigned other = s.muted[1-deck];
        assert(xz_stem_pad_event(&s, &e, 1, &toggle) && toggle == (pad<3?2-pad:XZ_STEM_PAD_BYPASS));
        assert(s.muted[1-deck] == other);
        e.operation = 3;
        assert(xz_stem_pad_event(&s, &e, 0, &toggle));
    }
    for (int pad = 4; pad < 8; pad++) {
        e = (struct xz_cue_event){0, pad, 0, 1, 0, 0, 0, -1, 0};
        assert(!xz_stem_pad_event(&s, &e, 1, &toggle));
    }
    e = (struct xz_cue_event){0, 1, 0, 0, 0, -1, 0, -1, 0};
    assert(!xz_stem_pad_event(&s, &e, 1, &toggle));
    e = (struct xz_cue_event){0, -1, 0, 1, 1, 0, 0, -1, 0};
    assert(!xz_stem_pad_event(&s, &e, 1, &toggle));
    for (int page=0;page<4;page++) {
        s=(struct xz_stem_pads){0};
        e=(struct xz_cue_event){0,0,0,page==0,0,page,0,-1,0};
        assert(xz_stem_control_event(&s,&e,1,page,0,&toggle) && toggle==2);
        e.operation=2; e.pad_page=-1;
        assert(xz_stem_control_event(&s,&e,0,page,0,&toggle));
        e=(struct xz_cue_event){0,-1,0,0,0,-1,1,page,0};
        assert(xz_stem_control_event(&s,&e,1,0,1,&toggle) && toggle==(page<3?2-page:XZ_STEM_PAD_BYPASS));
        e.operation=2;e.shift=0;
        assert(xz_stem_control_event(&s,&e,0,0,0,&toggle));
        e.operation=0;
        assert(!xz_stem_control_event(&s,&e,1,0,1,&toggle));
        e.shift=1;
        assert(!xz_stem_control_event(&s,&e,1,0,1,&toggle));
    }
    for(int bank=0;bank<2;bank++)for(int deck=0;deck<2;deck++)for(int pad=0;pad<8;pad++){
        s=(struct xz_stem_pads){0};s.bank=bank;
        e=(struct xz_cue_event){deck,pad,0,1,0,0,0,-1,0};
        int slot=pad-bank*4,expected=slot>=0&&slot<4;
        assert(xz_stem_pad_event(&s,&e,1,&toggle)==expected);
        assert(toggle==(expected?(slot<3?2-slot:XZ_STEM_PAD_BYPASS):-1));
        s.bank=1-bank;e.operation=2;
        assert(xz_stem_pad_event(&s,&e,1,&toggle)==expected);
    }
    s=(struct xz_stem_pads){0};s.bank=1;
    e=(struct xz_cue_event){0,0,0,1,0,0,0,-1,0};
    assert(!xz_stem_pad_event(&s,&e,1,&toggle));s.bank=0;
    assert(!xz_stem_pad_event(&s,&e,1,&toggle));e.operation=2;
    assert(!xz_stem_pad_event(&s,&e,1,&toggle));
    puts("PASS A-D/E-H banks, inactive-bank hot cues, deck independence and release ownership across bank changes");
    /* Slots 0-2 keep the saved order on both deck types; slot 3 alone differs. */
    for(int order=0;order<XZ_STEM_ORDER_COUNT;order++)for(int slot=0;slot<3;slot++){
        assert(xz_stem_pad_role(order,3,slot)==xz_stem_at(order,slot));
        assert(xz_stem_pad_role(order,4,slot)==xz_stem_at(order,slot));
    }
    /* Unset or unexpected counts are three-stem, so a zeroed struct is unchanged. */
    for(int count=-1;count<8;count++)
        assert(xz_stem_pad_role(0,count,3)==(count==4?XZ_STEM_BASS:XZ_STEM_PAD_BYPASS));
    assert(xz_stem_pad_role(0,4,4)==-1&&xz_stem_pad_role(0,4,-1)==-1);
    /* Four-stem slot 3 mutes bass (bit 3) on either bank, never bypass, and leaves the other bank stock. */
    for(int bank=0;bank<2;bank++)for(int pad=0;pad<8;pad++){
        s=(struct xz_stem_pads){0};s.bank=bank;s.count[0]=4;
        e=(struct xz_cue_event){0,pad,0,1,0,0,0,-1,0};
        int slot=pad-bank*4,owned=slot>=0&&slot<4;
        assert(xz_stem_pad_event(&s,&e,1,&toggle)==owned);
        if(!owned){assert(toggle==-1&&s.muted[0]==0);continue;}
        assert(toggle==(slot<3?2-slot:XZ_STEM_BASS));
        assert(s.muted[0]==1u<<toggle);
        e.operation=2;assert(xz_stem_pad_event(&s,&e,1,&toggle)&&toggle==-1&&s.muted[0]==1u<<(slot<3?2-slot:XZ_STEM_BASS));
    }
    /* Shift + mode button 3 is BASS on a four-stem deck too. */
    s=(struct xz_stem_pads){0};s.count[1]=4;
    e=(struct xz_cue_event){1,-1,0,0,0,-1,1,3,0};
    assert(xz_stem_control_event(&s,&e,1,0,1,&toggle)&&toggle==XZ_STEM_BASS&&s.muted[1]==8u&&s.muted[0]==0);
    /* Mixed decks: deck 1 four-stem, deck 2 three-stem; pad D means BASS and BYPASS respectively. */
    s=(struct xz_stem_pads){0};s.count[0]=4;s.count[1]=3;
    e=(struct xz_cue_event){0,3,0,1,0,0,0,-1,0};
    assert(xz_stem_pad_event(&s,&e,1,&toggle)&&toggle==XZ_STEM_BASS&&s.muted[0]==8u);
    e.deck=1;
    assert(xz_stem_pad_event(&s,&e,1,&toggle)&&toggle==XZ_STEM_PAD_BYPASS&&s.muted[1]==0&&s.muted[0]==8u);
    /* A track load changes the count while the pad is held: the release is still
       consumed and toggles nothing, in both directions. */
    for(int from=3;from<=4;from++){
        s=(struct xz_stem_pads){0};s.count[0]=from;
        e=(struct xz_cue_event){0,3,0,1,0,0,0,-1,0};
        assert(xz_stem_pad_event(&s,&e,1,&toggle)&&toggle==(from==4?XZ_STEM_BASS:XZ_STEM_PAD_BYPASS));
        unsigned muted=s.muted[0];
        s.count[0]=from==4?3:4;
        e.operation=2;
        assert(xz_stem_pad_event(&s,&e,1,&toggle)&&toggle==-1&&s.muted[0]==muted&&s.owned[0]==0);
        e.operation=0;
        assert(xz_stem_pad_event(&s,&e,1,&toggle)&&toggle==(from==4?XZ_STEM_PAD_BYPASS:XZ_STEM_BASS));
    }
    puts("PASS four-stem BASS on pad D/H, mixed decks, shift page 3, and count change while held");
}
