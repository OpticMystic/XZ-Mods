#define _POSIX_C_SOURCE 200809L
#include "../settings.h"
#include "../ui/themes.h"
#include "../ui/wave_viewport.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
/* Sets a one-digit field of a formatted record, or cuts the record just before it. */
static void field(char *text, const char *key, char digit) { char *at=strstr(text,key);assert(at);at[strlen(key)]=digit; }
static void cut(char *text, const char *key) { char *at=strstr(text,key);assert(at);*at=0; }
static void rekordbox_buttons(void) {
    struct xz_settings a,b;char text[1024];unsigned present=0;
    xz_settings_default(&a);
    assert(a.rekordbox_press==XZ_REKORDBOX_INTERFACE&&a.rekordbox_shift==XZ_REKORDBOX_STOCK&&!a.rekordbox_menu);
    /* Every pair round-trips; rekordbox_menu is written as press == MODS for older runtimes. */
    for(int press=0;press<XZ_REKORDBOX_ACTIONS;press++)for(int shift=0;shift<XZ_REKORDBOX_ACTIONS;shift++){
        struct xz_settings w=a;w.rekordbox_press=press;w.rekordbox_shift=shift;w.rekordbox_menu=press==XZ_REKORDBOX_MODS;
        assert(xz_settings_format(&w,text,sizeof(text))>0&&strstr(text,press==XZ_REKORDBOX_MODS?"rekordbox_menu=1\n":"rekordbox_menu=0\n"));
        assert(!xz_settings_parse_present(text,&b,&present)&&present==57&&!memcmp(&w,&b,sizeof(w)));
    }
    { struct xz_settings w=a;w.rekordbox_press=XZ_REKORDBOX_ACTIONS;assert(xz_settings_format(&w,text,sizeof(text))<0);
      w=a;w.rekordbox_shift=-1;assert(xz_settings_format(&w,text,sizeof(text))<0); }
    assert(xz_settings_format(&a,text,sizeof(text))>0);
    { char bad[1024];strcpy(bad,text);field(bad,"rekordbox_shift=",'7');assert(xz_settings_parse(bad,&b)<0);
      /* The pair is all or nothing. */
      strcpy(bad,text);cut(bad,"rekordbox_shift=");assert(xz_settings_parse(bad,&b)<0); }
    /* modern_stems: MODERN's own stem layout, PADS by default; a record without it keeps PADS; 0..5 only. */
    { char bad[1024];assert(a.modern_stems==XZ_MODERN_STEMS_PADS&&a.stems_overlay==XZ_STEM_LAYOUT_BELOW);
      for(int mode=0;mode<XZ_MODERN_STEMS_COUNT;mode++){struct xz_settings w=a;w.modern_stems=mode;
        assert(xz_settings_format(&w,bad,sizeof(bad))>0&&!xz_settings_parse_present(bad,&b,&present)&&present==57&&b.modern_stems==mode);}
      strcpy(bad,text);field(bad,"modern_stems=",'6');assert(xz_settings_parse(bad,&b)<0);
      strcpy(bad,text);field(bad,"modern_stems=",'0');cut(bad,"modern_stems=");
      assert(!xz_settings_parse_present(bad,&b,&present)&&present==43&&b.modern_stems==XZ_MODERN_STEMS_PADS);
      /* The STEMS cycle: PADS, the four rows, HIDDEN, PADS. */
      int mode=XZ_MODERN_STEMS_PADS;static const int order[7]={XZ_MODERN_STEMS_PADS,XZ_STEM_LAYOUT_BELOW,XZ_STEM_LAYOUT_BELOW_HALF,
        XZ_STEM_LAYOUT_OUTER,XZ_STEM_LAYOUT_OUTER_HALF,XZ_STEM_LAYOUT_HIDDEN,XZ_MODERN_STEMS_PADS};
      for(int i=1;i<7;i++){mode=xz_modern_stems_next(mode);assert(mode==order[i]);}
      assert(xz_modern_stems_next(99)==XZ_MODERN_STEMS_PADS&&!strcmp(xz_modern_stems_name(XZ_MODERN_STEMS_PADS),"PAD ROW / SMALL")); }
    /* Records older than the pair migrate from rekordbox_menu and takeover_assign. */
    static const struct { char menu, takeover; int press, assign; } cases[]={
        {'1','0',XZ_REKORDBOX_INTERFACE,XZ_TAKEOVER_LINK},{'0','0',XZ_REKORDBOX_STOCK,XZ_TAKEOVER_LINK},
        {'0','1',XZ_REKORDBOX_VJ_SCREEN,XZ_TAKEOVER_LINK},{'1','1',XZ_REKORDBOX_INTERFACE,XZ_TAKEOVER_LINK},
        {'0','2',XZ_REKORDBOX_STOCK,XZ_TAKEOVER_ONSCREEN}};
    for(size_t k=0;k<sizeof(cases)/sizeof(cases[0]);k++){
        char old[1024];strcpy(old,text);field(old,"rekordbox_menu=",cases[k].menu);field(old,"takeover_assign=",cases[k].takeover);
        cut(old,"rekordbox_press=");
        assert(!xz_settings_parse_present(old,&b,&present)&&present==41);
        assert(b.rekordbox_press==cases[k].press&&b.rekordbox_shift==XZ_REKORDBOX_STOCK&&b.takeover_assign==cases[k].assign);
        assert(b.rekordbox_menu==(cases[k].press==XZ_REKORDBOX_MODS));
        /* Saved again, the record carries the pair and reads back the same. */
        struct xz_settings again;assert(xz_settings_format(&b,old,sizeof(old))>0&&!xz_settings_parse(old,&again)&&!memcmp(&b,&again,sizeof(b)));
    }
    /* Before rekordbox_menu existed its default was 1: INTERFACE. */
    { char old[1024];strcpy(old,text);cut(old,"rekordbox_menu=");
      assert(!xz_settings_parse_present(old,&b,&present)&&present==35&&b.rekordbox_press==XZ_REKORDBOX_INTERFACE&&b.rekordbox_shift==XZ_REKORDBOX_STOCK); }
    /* A current record naming the retired REKORDBOX takeover key loads as LINK and keeps its pair. */
    { char now[1024];struct xz_settings w=a;w.rekordbox_press=XZ_REKORDBOX_NOTHING;assert(xz_settings_format(&w,now,sizeof(now))>0);
      field(now,"takeover_assign=",'1');assert(!xz_settings_parse(now,&b)&&b.takeover_assign==XZ_TAKEOVER_LINK&&b.rekordbox_press==XZ_REKORDBOX_NOTHING); }
    /* Overlay: an older record brings its migrated press; the SHIFT slot it never named is kept. */
    { char old[1024];strcpy(old,text);field(old,"rekordbox_menu=",'0');field(old,"takeover_assign=",'1');cut(old,"rekordbox_press=");
      struct xz_settings current=a,record;current.rekordbox_press=XZ_REKORDBOX_NOTHING;current.rekordbox_shift=XZ_REKORDBOX_MODS;current.rekordbox_menu=0;
      assert(!xz_settings_parse_present(old,&record,&present)&&!xz_settings_overlay(&current,&record,present));
      assert(current.rekordbox_press==XZ_REKORDBOX_VJ_SCREEN&&current.rekordbox_shift==XZ_REKORDBOX_MODS&&current.takeover_assign==XZ_TAKEOVER_LINK&&!current.rekordbox_menu);
      /* A record from before rekordbox_menu leaves both slots as they are. */
      strcpy(old,text);cut(old,"rekordbox_menu=");current.rekordbox_press=XZ_REKORDBOX_MODS;
      assert(!xz_settings_parse_present(old,&record,&present)&&!xz_settings_overlay(&current,&record,present));
      assert(current.rekordbox_press==XZ_REKORDBOX_MODS&&current.rekordbox_shift==XZ_REKORDBOX_MODS&&current.rekordbox_menu);
      /* A current record sets both. */
      struct xz_settings w=a;w.rekordbox_press=XZ_REKORDBOX_STEMS;w.rekordbox_shift=XZ_REKORDBOX_NOTHING;assert(xz_settings_format(&w,old,sizeof(old))>0);
      assert(!xz_settings_parse_present(old,&record,&present)&&!xz_settings_overlay(&current,&record,present));
      assert(current.rekordbox_press==XZ_REKORDBOX_STEMS&&current.rekordbox_shift==XZ_REKORDBOX_NOTHING&&!current.rekordbox_menu); }
    puts("PASS REKORDBOX buttons: 49 pairs round-trip, derived rekordbox_menu, range and pair refusal, migration from rekordbox_menu/takeover_assign, overlay");
}
/* DECK VIEW: deck_view and deck_view_button are one block after modern_stems. A record without it (44 fields)
   still loads with the defaults; overlaying it keeps the view the DJ has now; half a block, a value out of range or
   a DECK VIEW choice on an older field is refused. deck_view_button is retired (the DECKS button always shows): a
   stored 0 reads as 1. The longest record stays inside the internal store's 1023 bytes. */
static void deck_view_block(void) {
    struct xz_settings a,b;char text[1024];unsigned present=0;
    xz_settings_default(&a);
    assert(a.deck_view==XZ_DECK_VIEW_TWO&&a.deck_view_button==1);
    for(int view=0;view<XZ_DECK_VIEW_COUNT;view++)for(int button=0;button<2;button++){
        struct xz_settings w=a;w.deck_view=view;w.deck_view_button=button;
        assert(xz_settings_format(&w,text,sizeof(text))>0);
        assert(!xz_settings_parse_present(text,&b,&present)&&present==57&&b.deck_view_button==1);
        w.deck_view_button=1;assert(!memcmp(&w,&b,sizeof(w)));
    }
    { struct xz_settings w=a;w.deck_view=XZ_DECK_VIEW_COUNT;assert(xz_settings_format(&w,text,sizeof(text))<0);
      w=a;w.deck_view=-1;assert(xz_settings_format(&w,text,sizeof(text))<0);
      w=a;w.deck_view_button=2;assert(xz_settings_format(&w,text,sizeof(text))<0); }
    struct xz_settings mixer=a;mixer.deck_view=XZ_DECK_VIEW_MIXER;mixer.deck_view_button=0;
    assert(xz_settings_format(&mixer,text,sizeof(text))>0);
    char bad[1024];
    strcpy(bad,text);field(bad,"deck_view=",'3');assert(xz_settings_parse(bad,&b)<0);
    strcpy(bad,text);field(bad,"deck_view_button=",'2');assert(xz_settings_parse(bad,&b)<0);
    /* Half the block is refused. */
    strcpy(bad,text);cut(bad,"deck_view_button=");assert(xz_settings_parse(bad,&b)<0);
    /* An older 44-field record loads with the defaults... */
    char old[1024];strcpy(old,text);cut(old,"deck_view=");
    assert(!xz_settings_parse_present(old,&b,&present)&&present==44&&b.deck_view==XZ_DECK_VIEW_TWO&&b.deck_view_button==1);
    /* ...and overlaying it keeps the DECK VIEW the DJ has now; the retired button reads as shown. */
    { struct xz_settings current=a,record;current.deck_view=XZ_DECK_VIEW_MIXER;current.deck_view_button=0;
      assert(!xz_settings_parse_present(old,&record,&present)&&!xz_settings_overlay(&current,&record,present));
      assert(current.deck_view==XZ_DECK_VIEW_MIXER&&current.deck_view_button==1);
      /* A current record sets both. */
      struct xz_settings w=a;w.deck_view=XZ_DECK_VIEW_FOUR;w.deck_view_button=1;assert(xz_settings_format(&w,text,sizeof(text))>0);
      assert(!xz_settings_parse_present(text,&record,&present)&&!xz_settings_overlay(&current,&record,present));
      assert(current.deck_view==XZ_DECK_VIEW_FOUR&&current.deck_view_button==1); }
    /* DECK VIEW on the button next to LINK, alone and with SHIFT. */
    { struct xz_settings w=a;w.rekordbox_press=XZ_REKORDBOX_DECK_VIEW;w.rekordbox_shift=XZ_REKORDBOX_DECK_VIEW;
      assert(xz_settings_format(&w,text,sizeof(text))>0&&strstr(text,"rekordbox_press=6\nrekordbox_shift=6\n"));
      assert(!xz_settings_parse(text,&b)&&b.rekordbox_press==XZ_REKORDBOX_DECK_VIEW&&b.rekordbox_shift==XZ_REKORDBOX_DECK_VIEW&&!b.rekordbox_menu);
      strcpy(bad,text);field(bad,"rekordbox_press=",'7');assert(xz_settings_parse(bad,&b)<0); }
    /* The retired 16 BARS jump ID (7, also 64 beats) still loads. */
    { struct xz_settings w=a;w.jump.sizes[3]=7;assert(xz_settings_format(&w,text,sizeof(text))>0);
      assert(!xz_settings_parse(text,&b)&&b.jump.sizes[3]==7&&xz_jump_beats(7)==64); }
    /* The longest record: every two-digit field at its largest value. */
    { struct xz_settings w=a;for(int i=0;i<8;i++)w.jump.sizes[i]=9;w.ui_interface=XZ_INTERFACE_CLASSIC;w.classic_theme=XZ_CLASSIC_THEME_COUNT-1;
      w.theme=w.classic_theme;w.modern_stems=XZ_MODERN_STEMS_COUNT-1;w.stems_overlay=XZ_STEM_LAYOUT_COUNT-1;w.stem_order=XZ_STEM_ORDER_COUNT-1;
      w.deck_view=XZ_DECK_VIEW_MIXER;for(int i=0;i<3;i++)w.stem_colors[i]=XZ_STEM_COLOR_CHOICES-1;
      int n=xz_settings_format(&w,text,sizeof(text));assert(n>0&&n<=1023);
      printf("longest settings record: %d bytes (limit 1023)\n",n); }
    puts("PASS DECK VIEW: 6 values round-trip, range refusal, block refusal, 44-field records load, overlay keeps the view, DECK VIEW button choice, old jump ID 7, record size");
}
static void browse_preview_preference(void) {
    struct xz_settings a,b;char text[1024],old[1024];unsigned present;
    xz_settings_default(&a);assert(!a.browse_preview);
    for(int shown=0;shown<=1;shown++){
        a.browse_preview=shown;
        assert(xz_settings_format(&a,text,sizeof text)>0);
        assert(!xz_settings_parse_present(text,&b,&present)&&present==57&&b.browse_preview==shown);
        assert(!xz_settings_overlay(&b,&a,present)&&b.browse_preview==shown);
    }
    a.browse_preview=2;assert(xz_settings_format(&a,text,sizeof text)<0);
    a.browse_preview=-1;assert(xz_settings_format(&a,text,sizeof text)<0);
    a.browse_preview=1;assert(xz_settings_format(&a,text,sizeof text)>0);
    strcpy(old,text);cut(old,"browse_preview=");
    assert(!xz_settings_parse_present(old,&b,&present)&&present==46&&!b.browse_preview);
    assert(!xz_settings_overlay(&a,&b,present)&&a.browse_preview==1);
    strcpy(old,text);field(old,"browse_preview=",'2');assert(xz_settings_parse(old,&b)<0);
    strcpy(old,text);cut(old,"fb_takeover=");
    assert(!xz_settings_parse_present(old,&b,&present)&&present==8&&!b.browse_preview);
    assert(!xz_settings_overlay(&a,&b,present)&&a.browse_preview==1);
    puts("PASS browse preview: default hidden, both values round-trip, old records default hidden, old overlay preserves choice, invalid values refused");
}
int main(void) {
    browse_preview_preference();
    deck_view_block();
    rekordbox_buttons();
    struct xz_settings a,b;
    char text[1024], root[]="/dev/shm/xz-settings-XXXXXX",path[1024];
    xz_settings_default(&a);
    assert(a.shift_keysync && a.shift_pages && a.pad_feedback && a.stems && !a.spare_eq && !a.stem_bank);
    assert(a.fb_takeover == 0 && a.takeover_assign == XZ_TAKEOVER_LINK);
    const char *legacy = "XZ_MODS_SETTINGS 1\nstems=0\ngate=0\nsmart=0\ntheme=0\nstem_page=0\nshift_pages=0\npad_feedback=1\nshift_keysync=0\n";
    assert(xz_settings_parse(legacy, &b) == 0 && b.fb_takeover == 0 && b.takeover_assign == XZ_TAKEOVER_LINK && !b.spare_eq);
    assert(a.browse_columns[0]==XZ_BROWSE_TITLE && a.browse_columns[1]==XZ_BROWSE_KEY && a.browse_columns[2]==XZ_BROWSE_BPM && a.browse_key_prefix && a.browse_traffic);
    assert(b.browse_key_prefix && b.browse_traffic && b.key_format==XZ_KEY_FORMAT_NATIVE);
    assert(a.wave_mode == 1 && b.wave_mode == 1); /* older USB records get 3-band */
    {
        unsigned present=99;struct xz_settings current,record;
        assert(!xz_settings_parse_present(legacy,&record,&present)&&present==8);
        xz_settings_default(&current);current.key_feedback=0;current.wave_mode=0;current.theme=9;current.stems=1;
        assert(!xz_settings_overlay(&current,&record,present));
        assert(!current.key_feedback&&!current.wave_mode&&current.theme==0&&current.stems==0);
        current.key_feedback=1;current.wave_mode=2;
        assert(!xz_settings_overlay(&current,&record,present)&&current.key_feedback==1&&current.wave_mode==2);
        struct xz_settings off=current;off.key_feedback=0;off.wave_mode=0;
        assert(xz_settings_format(&off,text,sizeof(text))>0&&!xz_settings_parse_present(text,&record,&present)&&present==57);
        assert(!xz_settings_overlay(&current,&record,present)&&!current.key_feedback&&!current.wave_mode);
        record=current;record.theme=XZ_THEME_COUNT;struct xz_settings kept=current;
        assert(xz_settings_overlay(&current,&record,38)<0&&!memcmp(&kept,&current,sizeof(kept)));
    }
    assert(!b.stem_colors[0]&&!b.stem_colors[1]&&!b.stem_colors[2]);
    for(int stem=0;stem<3;stem++)for(int choice=0;choice<XZ_STEM_COLOR_CHOICES;choice++){
        struct xz_settings w=a;w.stem_colors[stem]=choice;
        assert(xz_settings_format(&w,text,sizeof(text))>0);
        assert(!xz_settings_parse(text,&b)&&!memcmp(&w,&b,sizeof(w)));
    }
    {
        struct xz_settings w=a;w.stem_colors[0]=8;w.stem_colors[1]=2;w.stem_colors[2]=4;
        assert(xz_settings_format(&w,text,sizeof(text))>0);
        char *extension=strstr(text,"stem_drums_color=");assert(extension);
        *extension=0;assert(!xz_settings_parse(text,&b)&&!b.stem_colors[0]&&!b.stem_colors[1]&&!b.stem_colors[2]);
        strcat(text,"stem_drums_color=8\n");assert(xz_settings_parse(text,&b)<0);
        w.stem_colors[1]=XZ_STEM_COLOR_CHOICES;assert(xz_settings_format(&w,text,sizeof(text))<0);
    }
    for(int order=0;order<XZ_STEM_ORDER_COUNT;order++){
        struct xz_settings w=a;w.stem_order=order;
        assert(xz_settings_format(&w,text,sizeof(text))>0);
        assert(!xz_settings_parse(text,&b)&&b.stem_order==order&&!memcmp(&w,&b,sizeof(w)));
    }
    {
        struct xz_settings w=a;w.stem_order=XZ_STEM_ORDER_COUNT;assert(xz_settings_format(&w,text,sizeof(text))<0);
        w.stem_order=3;assert(xz_settings_format(&w,text,sizeof(text))>0);
        char *extension=strstr(text,"stem_order=");assert(extension);
        unsigned present=0;*extension=0;assert(!xz_settings_parse_present(text,&b,&present)&&present==37&&!b.stem_order);
        strcat(text,"stem_order=6\n");assert(xz_settings_parse(text,&b)<0);
    }
    for(int layout=0;layout<XZ_STEM_LAYOUT_COUNT;layout++){
        struct xz_settings w=a;w.stems_overlay=layout;
        assert(xz_settings_format(&w,text,sizeof(text))>0);
        assert(!xz_settings_parse(text,&b)&&b.stems_overlay==layout&&!memcmp(&w,&b,sizeof(w)));
    }
    {struct xz_settings w=a;w.stems_overlay=5;assert(xz_settings_format(&w,text,sizeof(text))<0);}
    for (int mode=0;mode<3;mode++) {
        struct xz_settings w=a;w.wave_mode=mode;
        assert(xz_settings_format(&w,text,sizeof(text))>0);
        assert(!xz_settings_parse(text,&b)&&b.wave_mode==mode&&!memcmp(&w,&b,sizeof(w)));
    }
    { struct xz_settings w=a;w.wave_mode=3;assert(xz_settings_format(&w,text,sizeof(text))<0); }
    for (int page=0;page<4;page++) {
        a.stems=1;a.stem_page=page;a.shift_pages=1;a.shift_keysync=1;a.ui_interface=XZ_INTERFACE_CLASSIC;a.classic_theme=6;a.theme=6;
        a.fb_takeover=page%2;a.takeover_assign=page%2?XZ_TAKEOVER_ONSCREEN:XZ_TAKEOVER_LINK;
        a.spare_eq=page%2;
        a.stem_bank=page%2;
        assert(xz_settings_format(&a,text,sizeof(text))>0);
        assert(!xz_settings_parse(text,&b) && !memcmp(&a,&b,sizeof(a)));
    }
    for(int theme=0;theme<XZ_THEME_COUNT;theme++){
        a.ui_interface=theme==XZ_THEME_FLAGSHIP?XZ_INTERFACE_MODERN:XZ_INTERFACE_CLASSIC;a.classic_theme=theme==XZ_THEME_FLAGSHIP?0:theme;a.theme=theme;assert(xz_settings_format(&a,text,sizeof(text))>0);
        assert(!xz_settings_parse(text,&b)&&b.theme==theme);
    }
    assert(b.jump.enabled&&b.jump.shift_page&&b.jump.sizes[7]==8);
    a.stems_overlay=0;
    a.jump.enabled=0;a.jump.shift_page=0;
    assert(xz_settings_format(&a,text,sizeof(text))>0);
    assert(!xz_settings_parse(text,&b)&&!b.jump.enabled&&b.jump.shift_page&&!b.stems_overlay);
    a.jump.enabled=1;a.jump.shift_page=1;
    for(int size=0;size<XZ_JUMP_SIZES;size++){
        for(int pair=0;pair<8;pair++)a.jump.sizes[pair]=(pair+size)%XZ_JUMP_SIZES;
        assert(xz_settings_format(&a,text,sizeof(text))>0);
        assert(!xz_settings_parse(text,&b)&&!memcmp(&a,&b,sizeof(a)));
    }
    a.jump.sizes[7]=XZ_JUMP_SIZES;assert(xz_settings_format(&a,text,sizeof(text))<0);a.jump.sizes[7]=9;
    a.theme=XZ_THEME_COUNT;assert(xz_settings_format(&a,text,sizeof(text))<0);a.theme=XZ_THEME_COUNT-1;
    b=a;
    assert(xz_settings_parse("XZ_MODS_SETTINGS 2\n",&b)<0 && !memcmp(&a,&b,sizeof(a)));
    strcat(text,"unexpected=1\n");assert(xz_settings_parse(text,&b)<0);
    a.stem_page=4;assert(xz_settings_format(&a,text,sizeof(text))<0);a.stem_page=3;
    a.spare_eq=2;assert(xz_settings_format(&a,text,sizeof(text))<0);a.spare_eq=1;
    a.stem_bank=2;assert(xz_settings_format(&a,text,sizeof(text))<0);a.stem_bank=1;
    { struct xz_settings w=a;w.browse_columns[1]=w.browse_columns[0];assert(xz_settings_format(&w,text,sizeof(text))<0);w=a;w.key_format=3;assert(xz_settings_format(&w,text,sizeof(text))<0); }
    a.browse_columns[0]=XZ_BROWSE_ARTIST;a.browse_columns[1]=XZ_BROWSE_TITLE;a.browse_columns[2]=XZ_BROWSE_KEY;a.browse_key_prefix=0;a.browse_traffic=0;a.key_format=XZ_KEY_FORMAT_CAMELOT;
    assert(mkdtemp(root));assert(xz_settings_load(root,&b)==1);
    assert(xz_settings_save_mount(root,~0ul,&a)<0);
    assert(!xz_settings_save(root,&a));assert(!xz_settings_load(root,&b));assert(!memcmp(&a,&b,sizeof(a)));
    a.shift_keysync=0;assert(!xz_settings_save(root,&a));assert(!xz_settings_load(root,&b) && b.shift_keysync);
    snprintf(path,sizeof(path),"%s/VJ.Tools/XZ-Mods.cfg",root);assert(!unlink(path));
    assert(!symlink("/dev/null",path));assert(xz_settings_load(root,&b)<0);assert(!unlink(path));
    snprintf(path,sizeof(path),"%s/VJ.Tools",root);assert(!rmdir(path));
    assert(!symlink("/tmp",path));assert(xz_settings_save(root,&a)<0);assert(!unlink(path));assert(!rmdir(root));
    puts("PASS USB settings defaults, round-trip, atomic replacement, version/range validation and symlink refusal");
}
