/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "settings.h"
#include "ui/themes.h"
#include "ui/wave_viewport.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <mntent.h>
_Static_assert(XZ_CLASSIC_THEME_COUNT==XZ_THEME_FLAGSHIP&&XZ_THEME_FLAGSHIP==XZ_THEME_COUNT-1,"classic themes are every theme before FLAGSHIP");
int xz_settings_look(int ui_interface, int classic_theme) {
    return ui_interface==XZ_INTERFACE_MODERN?XZ_THEME_FLAGSHIP:classic_theme;
}
void xz_settings_default(struct xz_settings *s) {
    *s = (struct xz_settings){.stems=1,.ui_interface=XZ_INTERFACE_MODERN,.theme=XZ_THEME_FLAGSHIP,.shift_pages=1,.shift_keysync=1,.pad_feedback=1,.jump=xz_jump_defaults(),.stems_overlay=1,.wave_mode=1,.browse_columns={XZ_BROWSE_TITLE,XZ_BROWSE_KEY,XZ_BROWSE_BPM},.browse_key_prefix=1,.browse_traffic=1,.key_feedback=1,.rekordbox_menu=0,.ram_debug=1,.metrics_enabled=1,.rekordbox_press=XZ_REKORDBOX_INTERFACE,.rekordbox_shift=XZ_REKORDBOX_STOCK};
}
static int valid(const struct xz_settings *s) {
    for (int i=0;i<8;i++) if ((unsigned)s->jump.sizes[i]>=XZ_JUMP_SIZES) return 0;
    for (int i=0;i<3;i++) {
        if ((unsigned)s->stem_colors[i]>=XZ_STEM_COLOR_CHOICES) return 0;
        if ((unsigned)s->browse_columns[i]>=XZ_BROWSE_COLUMN_COUNT) return 0;
        for (int j=0;j<i;j++) if(s->browse_columns[i]==s->browse_columns[j]) return 0;
    }
    return (unsigned)s->ui_interface<=XZ_INTERFACE_MODERN && (unsigned)s->classic_theme<XZ_CLASSIC_THEME_COUNT && (unsigned)s->modern_theme<XZ_MODERN_THEME_COUNT &&
        (unsigned)s->stem_order<XZ_STEM_ORDER_COUNT && (unsigned)s->metrics_enabled<=1 && (unsigned)s->rekordbox_menu<=1 && (unsigned)s->rekordbox_press<XZ_REKORDBOX_ACTIONS && (unsigned)s->rekordbox_shift<XZ_REKORDBOX_ACTIONS && (unsigned)s->ram_debug<=1 && (unsigned)s->key_feedback<=1 && (unsigned)s->browse_key_prefix<=1 && (unsigned)s->browse_traffic<=1 && (unsigned)s->key_format<=XZ_KEY_FORMAT_CAMELOT && (unsigned)s->stems_overlay<XZ_STEM_LAYOUT_COUNT && (unsigned)s->wave_mode<=2 && (unsigned)s->jump.enabled<=1 && (unsigned)s->jump.shift_page<=1 && (unsigned)s->stems <= 1 && (unsigned)s->gate <= 1 &&
        (unsigned)s->smart <= 1 && (unsigned)s->theme < XZ_THEME_COUNT &&
        (unsigned)s->stem_page <= 3 && (unsigned)s->shift_pages <= 1 &&
        (unsigned)s->pad_feedback <= 1 && (unsigned)s->shift_keysync <= 1 &&
        (unsigned)s->fb_takeover <= 1 && (unsigned)s->takeover_assign <= 2 && (unsigned)s->spare_eq <= 1 && (unsigned)s->stem_bank <= 1;
}
/* Field order is the file order; a record may stop at a version boundary.
   The appearance block (interface, classic_theme, modern_theme) is all or
   nothing. A record without it is older: its theme slot decides the rest.
   The REKORDBOX block (rekordbox_press, rekordbox_shift) is all or nothing too;
   a record without it migrates from rekordbox_menu (rekordbox_migrate). */
#define FIELDS 43
#define APPEARANCE_BLOCK 38
#define REKORDBOX_MENU 35
#define REKORDBOX_BLOCK 41
/* Older records named one REKORDBOX choice: rekordbox_menu 1 (its default, also when the record
   predates it) or 0 (the stock action, or the VJ.Tools screen switch when takeover_assign chose the
   key). MODS opens from the MODERN interface's MENU key, so 1 becomes INTERFACE. */
static int rekordbox_migrate(int menu, int takeover_assign) {
    return menu ? XZ_REKORDBOX_INTERFACE : takeover_assign == XZ_TAKEOVER_REKORDBOX ? XZ_REKORDBOX_VJ_SCREEN : XZ_REKORDBOX_STOCK;
}
static void fields(struct xz_settings *s, int *values[FIELDS]) {
    int *all[FIELDS] = {
        &s->stems,&s->gate,&s->smart,&s->theme,&s->stem_page,
        &s->shift_pages,&s->pad_feedback,&s->shift_keysync,
        &s->fb_takeover,&s->takeover_assign,&s->spare_eq,&s->stem_bank,
        &s->jump.enabled,&s->jump.shift_page,&s->jump.sizes[0],&s->jump.sizes[1],&s->jump.sizes[2],&s->jump.sizes[3],&s->jump.sizes[4],&s->jump.sizes[5],&s->jump.sizes[6],&s->jump.sizes[7],&s->stems_overlay,&s->wave_mode,&s->browse_columns[0],&s->browse_columns[1],&s->browse_columns[2],&s->browse_key_prefix,&s->browse_traffic,&s->key_format,&s->stem_colors[0],&s->stem_colors[1],&s->stem_colors[2],&s->key_feedback,&s->ram_debug,&s->rekordbox_menu,&s->metrics_enabled,&s->stem_order,&s->ui_interface,&s->classic_theme,&s->modern_theme,
        &s->rekordbox_press,&s->rekordbox_shift
    };
    memcpy(values,all,sizeof(all));
}
int xz_settings_parse(const char *text, struct xz_settings *out) {
    return xz_settings_parse_present(text,out,NULL);
}
int xz_settings_parse_present(const char *text, struct xz_settings *out, unsigned *present) {
    struct xz_settings s;
    const char *header = "XZ_MODS_SETTINGS 1\n";
    const char *keys[] = {
        "stems=","gate=","smart=","theme=","stem_page=",
        "shift_pages=","pad_feedback=","shift_keysync=",
        "fb_takeover=","takeover_assign=","spare_eq=","stem_bank=",
        "jump_enabled=","jump_shift_page=","jump_1=","jump_2=","jump_3=","jump_4=","jump_5=","jump_6=","jump_7=","jump_8=","stems_overlay=","wave_mode=","browse_1=","browse_2=","browse_3=","browse_key_prefix=","browse_traffic=","key_format=","stem_drums_color=","stem_harmonics_color=","stem_vocals_color=","key_feedback=","ram_debug=","rekordbox_menu=","metrics_enabled=","stem_order=","interface=","classic_theme=","modern_theme=","rekordbox_press=","rekordbox_shift="
    };
    int *values[FIELDS];
    fields(&s,values);
    if (strncmp(text,header,strlen(header))) return -1;
    text += strlen(header);
    xz_settings_default(&s);
    unsigned i;
    for (i=0;i<FIELDS;i++) {
        if (((i >= 8 && i <= 12)||i==22||i==23||i==24||i==30||i==33||i==34||i==35||i==36||i==37||i==APPEARANCE_BLOCK||i==REKORDBOX_BLOCK) && *text == '\0') break;
        size_t size = strlen(keys[i]);
        if (strncmp(text,keys[i],size)) return -1;
        text += size;
        if (*text < '0' || *text > '9') return -1;
        int value = *text++ - '0';
        if (*text >= '0' && *text <= '9') {
            if (!value) return -1;
            value = value*10 + *text++ - '0';
        }
        if (*text++ != '\n') return -1;
        *values[i] = value;
    }
    if (*text || !valid(&s)) return -1;
    if (i < APPEARANCE_BLOCK+1) {
        /* Older record: theme 24 meant the FLAGSHIP screen, anything else a classic theme. */
        s.ui_interface = s.theme == XZ_THEME_FLAGSHIP ? XZ_INTERFACE_MODERN : XZ_INTERFACE_CLASSIC;
        s.classic_theme = s.theme == XZ_THEME_FLAGSHIP ? 0 : s.theme;
        s.modern_theme = 0;
    }
    if (i < REKORDBOX_BLOCK+1) {
        s.rekordbox_press = rekordbox_migrate(i > REKORDBOX_MENU ? s.rekordbox_menu : 1, s.takeover_assign);
        s.rekordbox_shift = XZ_REKORDBOX_STOCK;
    }
    if (s.takeover_assign == XZ_TAKEOVER_REKORDBOX) s.takeover_assign = XZ_TAKEOVER_LINK;
    s.rekordbox_menu = s.rekordbox_press == XZ_REKORDBOX_MODS;
    s.theme = xz_settings_look(s.ui_interface,s.classic_theme);
    s.jump.shift_page=1;s.ram_debug=1;s.metrics_enabled=1;s.shift_pages=1;s.shift_keysync=1;
    *out = s; if (present) *present = i; return 0;
}
int xz_settings_overlay(struct xz_settings *current, const struct xz_settings *record, unsigned present) {
    struct xz_settings merged = *current, source = *record;
    int *to[FIELDS], *from[FIELDS];
    fields(&merged,to); fields(&source,from);
    for (unsigned i=0;i<present&&i<FIELDS;i++) *to[i] = *from[i];
    /* An older record carries only theme: it sets the interface, and a classic
       theme when it named one. theme 24 leaves the current classic theme. */
    if (present > 3 && present <= APPEARANCE_BLOCK) {
        merged.ui_interface = source.theme == XZ_THEME_FLAGSHIP ? XZ_INTERFACE_MODERN : XZ_INTERFACE_CLASSIC;
        merged.classic_theme = source.theme == XZ_THEME_FLAGSHIP ? current->classic_theme : source.theme;
    }
    /* A record that names rekordbox_menu but predates the REKORDBOX block brings its migrated press;
       its SHIFT slot was never chosen and stays as it is. */
    if (present > REKORDBOX_MENU && present <= REKORDBOX_BLOCK) merged.rekordbox_press = source.rekordbox_press;
    if (merged.takeover_assign == XZ_TAKEOVER_REKORDBOX) merged.takeover_assign = XZ_TAKEOVER_LINK;
    merged.rekordbox_menu = merged.rekordbox_press == XZ_REKORDBOX_MODS;
    merged.theme = xz_settings_look(merged.ui_interface,merged.classic_theme);
    merged.jump.shift_page=1;merged.ram_debug=1;merged.metrics_enabled=1;merged.shift_pages=1;merged.shift_keysync=1;
    if (!valid(&merged)) return -1;
    *current = merged; return 0;
}
int xz_settings_format(const struct xz_settings *s, char *out, size_t size) {
    if (!valid(s)) return -1;
    int look = xz_settings_look(s->ui_interface,s->classic_theme);
    int n = snprintf(out,size,"XZ_MODS_SETTINGS 1\nstems=%d\ngate=%d\nsmart=%d\ntheme=%d\nstem_page=%d\nshift_pages=%d\npad_feedback=%d\nshift_keysync=%d\nfb_takeover=%d\ntakeover_assign=%d\nspare_eq=%d\nstem_bank=%d\njump_enabled=%d\njump_shift_page=%d\njump_1=%d\njump_2=%d\njump_3=%d\njump_4=%d\njump_5=%d\njump_6=%d\njump_7=%d\njump_8=%d\nstems_overlay=%d\nwave_mode=%d\nbrowse_1=%d\nbrowse_2=%d\nbrowse_3=%d\nbrowse_key_prefix=%d\nbrowse_traffic=%d\nkey_format=%d\nstem_drums_color=%d\nstem_harmonics_color=%d\nstem_vocals_color=%d\nkey_feedback=%d\nram_debug=%d\nrekordbox_menu=%d\nmetrics_enabled=%d\nstem_order=%d\ninterface=%d\nclassic_theme=%d\nmodern_theme=%d\nrekordbox_press=%d\nrekordbox_shift=%d\n",
        s->stems,s->gate,s->smart,look,s->stem_page,s->shift_pages,s->pad_feedback,s->shift_keysync,s->fb_takeover,s->takeover_assign,s->spare_eq,s->stem_bank,s->jump.enabled,s->jump.shift_page,
        s->jump.sizes[0],s->jump.sizes[1],s->jump.sizes[2],s->jump.sizes[3],s->jump.sizes[4],s->jump.sizes[5],s->jump.sizes[6],s->jump.sizes[7],s->stems_overlay,s->wave_mode,s->browse_columns[0],s->browse_columns[1],s->browse_columns[2],s->browse_key_prefix,s->browse_traffic,s->key_format,s->stem_colors[0],s->stem_colors[1],s->stem_colors[2],s->key_feedback,s->ram_debug,s->rekordbox_press==XZ_REKORDBOX_MODS,s->metrics_enabled,s->stem_order,s->ui_interface,s->classic_theme,s->modern_theme,
        s->rekordbox_press,s->rekordbox_shift);
    return n < 0 || (size_t)n >= size ? -1 : n;
}
static int paths(const char *usb, char *dir, char *file) {
    struct stat st;
    if (!usb || usb[0] != '/' || lstat(usb,&st) || !S_ISDIR(st.st_mode)) return -1;
    if (!strncmp(usb,"/media/",7)) {
        struct stat root;
        if (stat("/",&root) || root.st_dev == st.st_dev) return -1;
    }
    if (snprintf(dir,1024,"%s/VJ.Tools",usb) >= 1024 ||
        snprintf(file,1024,"%s/XZ-Mods.cfg",dir) >= 1024) return -1;
    if (!lstat(dir,&st) && !S_ISDIR(st.st_mode)) return -1;
    return 0;
}
int xz_settings_load(const char *usb, struct xz_settings *s) {
    return xz_settings_load_present(usb,s,NULL);
}
int xz_settings_load_present(const char *usb, struct xz_settings *s, unsigned *present) {
    char dir[1024], path[1024], text[1024];
    if (paths(usb,dir,path)) return -1;
    int fd = open(path,O_RDONLY|O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? 1 : -1;
    struct stat st;
    if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size >= (off_t)sizeof(text)) { close(fd); return -1; }
    ssize_t n = read(fd,text,sizeof(text)-1); close(fd);
    if (n < 0 || n != st.st_size || memchr(text,0,(size_t)n)) return -1;
    text[n] = 0;
    return xz_settings_parse_present(text,s,present);
}
int xz_settings_save_mount(const char *usb, unsigned long mount_id, const struct xz_settings *s) {
    char dir[1024],path[1024],temp[80],text[1024];
    static unsigned nonce;
    int n=xz_settings_format(s,text,sizeof(text));
    if(n<0||paths(usb,dir,path)||(!mount_id&&!strncmp(usb,"/media/",7)))return -1;
    int root=open(usb,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(root<0)return -1;
    struct stat pinned,current;
    if(fstat(root,&pinned)||lstat(usb,&current)||pinned.st_dev!=current.st_dev||pinned.st_ino!=current.st_ino||
       (mount_id&&xz_settings_mount_id(usb)!=mount_id)){close(root);return -1;}
    if(mkdirat(root,"VJ.Tools",0755)&&errno!=EEXIST){close(root);return -1;}
    int folder=openat(root,"VJ.Tools",O_RDONLY|O_DIRECTORY|O_NOFOLLOW);close(root);
    if(folder<0)return -1;
    snprintf(temp,sizeof(temp),".XZ-Mods.%ld.%u.tmp",(long)getpid(),++nonce);
    int fd=openat(folder,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0){close(folder);return -1;}
    int ok=write(fd,text,(size_t)n)==n&&fsync(fd)==0;
    if(close(fd))ok=0;
    if(ok&&renameat(folder,temp,folder,"XZ-Mods.cfg")==0){int rc=fsync(folder);close(folder);return rc;}
    unlinkat(folder,temp,0);close(folder);return -1;
}
int xz_settings_save(const char *usb,const struct xz_settings *s) {
    unsigned long mount_id=usb&&!strncmp(usb,"/media/",7)?xz_settings_mount_id(usb):0;
    return xz_settings_save_mount(usb,mount_id,s);
}
int xz_settings_find_usb(unsigned slot, char out[1024]) {
    if(slot>1||!out)return -1;
    char prefix[32];snprintf(prefix,sizeof(prefix),"/media/usb%u/",slot+1);
    FILE *mounts=setmntent("/proc/mounts","r");if(!mounts)return -1;
    struct mntent *entry;int best=0;out[0]=0;
    while((entry=getmntent(mounts))) {
        if(strncmp(entry->mnt_dir,prefix,strlen(prefix))||strlen(entry->mnt_dir)>=1024)continue;
        struct stat st;char path[1060];int score=1;
        snprintf(path,sizeof(path),"%s/PIONEER/rekordbox/export.pdb",entry->mnt_dir);
        if(!lstat(path,&st)&&S_ISREG(st.st_mode))score=2;
        snprintf(path,sizeof(path),"%s/VJ.Tools/XZ-Mods.cfg",entry->mnt_dir);
        if(!lstat(path,&st)&&S_ISREG(st.st_mode))score=3;
        if(score>best){best=score;strcpy(out,entry->mnt_dir);}
    }
    endmntent(mounts);return best?0:1;
}
unsigned long xz_settings_mount_id(const char *usb) {
    if(!usb||!usb[0])return 0;
    FILE *f=fopen("/proc/self/mountinfo","r");if(!f)return 0;
    char line[4096];unsigned long id=0;
    while(fgets(line,sizeof(line),f)) {
        char *end;errno=0;unsigned long current=strtoul(line,&end,10);
        if(errno||end==line||*end!=' ')continue;
        char *mount=line;
        for(int field=0;field<4&&mount;field++) {
            mount=strchr(mount,' ');if(mount)mount++;
        }
        if(!mount)continue;
        end=strchr(mount,' ');if(!end)continue;*end=0;
        /* getmntent decodes mount names; USB roots use simple partition names. */
        if(!strcmp(mount,usb)){id=current;break;}
    }
    fclose(f);return id;
}
