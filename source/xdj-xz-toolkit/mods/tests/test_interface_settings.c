/* Interface and theme settings: one interface switch, a theme list per
   interface, and older records (a single theme slot) migrating exactly. */
#include "settings.h"
#include "ui/themes.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* An older runtime's record: the first `fields` lines, theme slot = theme. */
static void record(char *text, size_t size, int theme, unsigned fields) {
 struct xz_settings s;xz_settings_default(&s);
 s.ui_interface=XZ_INTERFACE_CLASSIC;s.classic_theme=0;
 assert(xz_settings_format(&s,text,size)>0);
 char *line=text;
 for(unsigned i=0;i<=fields;i++){line=strchr(line,'\n');assert(line);line++;}
 *line=0;
 char *slot=strstr(text,"\ntheme=0\n");assert(slot);
 char rest[1024];snprintf(rest,sizeof rest,"%s",slot+9);
 snprintf(slot,size-(size_t)(slot-text),"\ntheme=%d\n%s",theme,rest);
}
static void replace(char *text, const char *from, const char *to) {
 char *at=strstr(text,from);assert(at&&strlen(from)==strlen(to));memcpy(at,to,strlen(to));
}

static void display_and_bands(void) {
 struct xz_settings s,got,current;char wire[1024],cut[1024],again[1024];unsigned present;
 xz_settings_default(&s);
 assert(!s.band_colors[0]&&!s.band_colors[1]&&!s.band_colors[2]);
 assert(s.modern_wave_color==2&&!s.wave_position&&!s.key_display&&s.wave_zoom==3&&!s.usb_colors&&!s.hidden_parts);
 for(int band=0;band<3;band++)for(int choice=0;choice<XZ_BAND_COLOR_CHOICES;choice++){
  s.band_colors[band]=choice;assert(xz_settings_format(&s,wire,sizeof wire)>0);
  assert(!xz_settings_parse_present(wire,&got,&present)&&present==57&&got.band_colors[band]==choice);
 }
 s.modern_wave_color=3;s.wave_position=1;s.key_display=2;s.wave_zoom=6;s.usb_colors=2;s.hidden_parts=15;
 assert(xz_settings_format(&s,wire,sizeof wire)>0);
 for(unsigned fields=47;fields<=57;fields++){
  strcpy(cut,wire);char *line=cut;
  for(unsigned i=0;i<=fields;i++){line=strchr(line,'\n');assert(line);line++;}*line=0;
  if(fields!=47&&fields!=50&&fields!=56&&fields!=57){assert(xz_settings_parse(cut,&got)<0);continue;}
  assert(!xz_settings_parse_present(cut,&got,&present)&&present==fields);
  if(fields==47)assert(!got.band_colors[0]);
  if(fields<56)assert(got.modern_wave_color==2&&got.wave_zoom==3&&!got.hidden_parts);
  current=s;assert(!xz_settings_overlay(&current,&got,present));
  assert(!memcmp(&current,&s,sizeof s));
 }
 int *slots[]={&s.band_colors[0],&s.band_colors[1],&s.band_colors[2],&s.modern_wave_color,&s.wave_position,&s.key_display,&s.wave_zoom,&s.usb_colors,&s.hidden_parts};
 const int maximum[]={8,8,8,3,1,2,6,2,15};
 const char *keys[]={"band_low_color=","band_mid_color=","band_high_color=","modern_wave_color=","wave_position=","key_display=","wave_zoom=","usb_colors=","hidden_parts="};
 for(unsigned i=0;i<9;i++){
  int saved=*slots[i];*slots[i]=-1;assert(xz_settings_format(&s,cut,sizeof cut)<0);
  *slots[i]=maximum[i]+1;assert(xz_settings_format(&s,cut,sizeof cut)<0);*slots[i]=saved;
  char *at=strstr(wire,keys[i]);assert(at);size_t prefix=(size_t)(at-wire)+strlen(keys[i]);
  snprintf(cut,sizeof cut,"%.*s%d%s",(int)prefix,wire,maximum[i]+1,strchr(at,'\n'));
  assert(xz_settings_parse(cut,&got)<0);
 }
 /* All variable-width numbers at their maximum, booleans on, valid distinct browse columns. */
 s.stems=s.gate=s.smart=s.shift_pages=s.pad_feedback=s.shift_keysync=s.fb_takeover=s.spare_eq=s.stem_bank=1;
 s.takeover_assign=2;s.stem_page=3;s.jump.enabled=s.jump.shift_page=1;
 for(int i=0;i<8;i++)s.jump.sizes[i]=9;
 s.ui_interface=XZ_INTERFACE_MODERN;s.classic_theme=23;s.modern_theme=0;s.theme=XZ_THEME_FLAGSHIP;
 s.stems_overlay=4;s.modern_stems=5;s.wave_mode=2;s.stem_order=5;s.deck_view=2;s.deck_view_button=1;s.browse_preview=1;
 s.browse_columns[0]=3;s.browse_columns[1]=2;s.browse_columns[2]=1;
 s.browse_key_prefix=s.browse_traffic=s.key_feedback=s.metrics_enabled=s.ram_debug=1;s.key_format=2;
 s.rekordbox_press=s.rekordbox_shift=6;s.rekordbox_menu=0;s.modern_browse_columns=2047;
 for(int i=0;i<3;i++)s.stem_colors[i]=s.band_colors[i]=8;
 int n=xz_settings_format(&s,wire,sizeof wire);assert(n>0&&n<=1023&&(size_t)n==strlen(wire));
 assert(!xz_settings_parse_present(wire,&got,&present)&&present==57&&!memcmp(&s,&got,sizeof s));
 assert(xz_settings_format(&got,again,sizeof again)==n&&!strcmp(wire,again));
 printf("PASS band/display blocks: defaults, choices, partial-block rejection, overlay, ranges, max record %d/1023 bytes round-trip\n",n);
}

int main(void) {
 display_and_bands();
 struct xz_settings s,loaded,current;char text[1024],bad[1024];unsigned present;
 /* Defaults: the modern interface, classic theme ORIGINAL, modern theme STANDARD. */
 xz_settings_default(&s);
 assert(s.ui_interface==XZ_INTERFACE_MODERN&&s.classic_theme==0&&s.modern_theme==0&&s.theme==XZ_THEME_FLAGSHIP);
 assert(xz_settings_look(XZ_INTERFACE_MODERN,7)==XZ_THEME_FLAGSHIP&&xz_settings_look(XZ_INTERFACE_CLASSIC,7)==7);
 /* A new record writes the block and the legacy slot derived from it. */
 s.ui_interface=XZ_INTERFACE_CLASSIC;s.classic_theme=11;s.theme=0;
 assert(xz_settings_format(&s,text,sizeof text)>0);
 assert(strstr(text,"\ntheme=11\n")&&strstr(text,"\ninterface=0\nclassic_theme=11\nmodern_theme=0\n"));
 assert(!xz_settings_parse_present(text,&loaded,&present)&&present==57);
 assert(loaded.ui_interface==XZ_INTERFACE_CLASSIC&&loaded.classic_theme==11&&loaded.theme==11);
 s.ui_interface=XZ_INTERFACE_MODERN;
 assert(xz_settings_format(&s,text,sizeof text)>0&&strstr(text,"\ntheme=24\n"));
 assert(!xz_settings_parse(text,&loaded)&&loaded.ui_interface==XZ_INTERFACE_MODERN&&loaded.classic_theme==11&&loaded.theme==XZ_THEME_FLAGSHIP);
 /* The block decides; a stale legacy slot is ignored. */
 snprintf(bad,sizeof bad,"%s",text);replace(bad,"\ntheme=24\n","\ntheme=11\n");
 assert(!xz_settings_parse(bad,&loaded)&&loaded.ui_interface==XZ_INTERFACE_MODERN&&loaded.theme==XZ_THEME_FLAGSHIP);
 /* Out-of-range values in the block reject the record as a whole. */
 snprintf(bad,sizeof bad,"%s",text);replace(bad,"\nclassic_theme=11\n","\nclassic_theme=24\n");assert(xz_settings_parse(bad,&loaded));
 snprintf(bad,sizeof bad,"%s",text);replace(bad,"\ninterface=1\n","\ninterface=2\n");assert(xz_settings_parse(bad,&loaded));
 snprintf(bad,sizeof bad,"%s",text);replace(bad,"\nmodern_theme=0\n","\nmodern_theme=1\n");assert(xz_settings_parse(bad,&loaded));
 /* Half a block is not a record. */
 snprintf(bad,sizeof bad,"%s",text);*strstr(bad,"classic_theme=")=0;assert(xz_settings_parse(bad,&loaded));
 snprintf(bad,sizeof bad,"%s",text);*strstr(bad,"modern_theme=")=0;assert(xz_settings_parse(bad,&loaded));
 /* Migration from every older record length and every old theme value. */
 static const unsigned lengths[]={8,9,10,11,12,22,23,24,30,33,34,35,36,37,38};
 for(size_t l=0;l<sizeof lengths/sizeof *lengths;l++)for(int theme=0;theme<XZ_THEME_COUNT;theme++){
  record(text,sizeof text,theme,lengths[l]);
  assert(!xz_settings_parse_present(text,&loaded,&present)&&present==lengths[l]);
  if(theme==XZ_THEME_FLAGSHIP)assert(loaded.ui_interface==XZ_INTERFACE_MODERN&&loaded.classic_theme==0);
  else assert(loaded.ui_interface==XZ_INTERFACE_CLASSIC&&loaded.classic_theme==theme);
  assert(loaded.modern_theme==0&&loaded.theme==theme);
  /* Overlay keeps the current classic theme when an old record only said FLAGSHIP. */
  xz_settings_default(&current);current.ui_interface=XZ_INTERFACE_CLASSIC;current.classic_theme=5;current.theme=5;
  assert(!xz_settings_overlay(&current,&loaded,present));
  assert(current.theme==theme);
  assert(current.classic_theme==(theme==XZ_THEME_FLAGSHIP?5:theme));
  assert(current.ui_interface==(theme==XZ_THEME_FLAGSHIP?XZ_INTERFACE_MODERN:XZ_INTERFACE_CLASSIC));
  /* Saving the migrated settings round-trips through the new format. */
  assert(xz_settings_format(&current,text,sizeof text)>0);
  assert(!xz_settings_parse_present(text,&loaded,&present)&&present==57);
  assert(loaded.ui_interface==current.ui_interface&&loaded.classic_theme==current.classic_theme&&loaded.theme==current.theme);
 }
 /* A record shorter than the theme slot leaves the appearance as it is. */
 xz_settings_default(&current);current.ui_interface=XZ_INTERFACE_CLASSIC;current.classic_theme=9;current.theme=9;
 xz_settings_default(&s);assert(!xz_settings_overlay(&current,&s,3));
 assert(current.ui_interface==XZ_INTERFACE_CLASSIC&&current.classic_theme==9&&current.theme==9);
 /* A full new record overlays the block exactly. */
 s.ui_interface=XZ_INTERFACE_MODERN;s.classic_theme=3;assert(!xz_settings_overlay(&current,&s,41));
 assert(current.ui_interface==XZ_INTERFACE_MODERN&&current.classic_theme==3&&current.theme==XZ_THEME_FLAGSHIP);
 puts("PASS interface + classic/modern themes: defaults, derived legacy slot, block validation, 15 old record lengths x 25 old themes migrate and round-trip");
 return 0;
}
