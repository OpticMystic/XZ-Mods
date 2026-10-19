#define _POSIX_C_SOURCE 200809L
#include "../settings.h"
#include "../ui/themes.h"
#include "../ui/wave_viewport.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void) {
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
        assert(xz_settings_format(&off,text,sizeof(text))>0&&!xz_settings_parse_present(text,&record,&present)&&present==38);
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
        a.stems=1;a.stem_page=page;a.shift_pages=1;a.shift_keysync=1;a.theme=6;
        a.fb_takeover=page%2;a.takeover_assign=page%3;
        a.spare_eq=page%2;
        a.stem_bank=page%2;
        assert(xz_settings_format(&a,text,sizeof(text))>0);
        assert(!xz_settings_parse(text,&b) && !memcmp(&a,&b,sizeof(a)));
    }
    for(int theme=0;theme<XZ_THEME_COUNT;theme++){
        a.theme=theme;assert(xz_settings_format(&a,text,sizeof(text))>0);
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
