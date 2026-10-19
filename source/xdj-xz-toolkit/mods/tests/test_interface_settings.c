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
int main(void) {
 struct xz_settings s,loaded,current;char text[1024],bad[1024];unsigned present;
 /* Defaults: the modern interface, classic theme ORIGINAL, modern theme STANDARD. */
 xz_settings_default(&s);
 assert(s.ui_interface==XZ_INTERFACE_MODERN&&s.classic_theme==0&&s.modern_theme==0&&s.theme==XZ_THEME_FLAGSHIP);
 assert(xz_settings_look(XZ_INTERFACE_MODERN,7)==XZ_THEME_FLAGSHIP&&xz_settings_look(XZ_INTERFACE_CLASSIC,7)==7);
 /* A new record writes the block and the legacy slot derived from it. */
 s.ui_interface=XZ_INTERFACE_CLASSIC;s.classic_theme=11;s.theme=0;
 assert(xz_settings_format(&s,text,sizeof text)>0);
 assert(strstr(text,"\ntheme=11\n")&&strstr(text,"\ninterface=0\nclassic_theme=11\nmodern_theme=0\n"));
 assert(!xz_settings_parse_present(text,&loaded,&present)&&present==41);
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
  assert(!xz_settings_parse_present(text,&loaded,&present)&&present==41);
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
