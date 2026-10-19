/* SPDX-License-Identifier: MIT */
/* Host test: MY SETTINGS onto the stock UTILITY items (shell_mysettings.c), on the codec's public export
   fixtures. POSIX (the codec writes with fsync/rename). */
#ifdef NDEBUG
#error "Shell MY SETTINGS acceptance requires active assertions"
#endif
#include "shell_mysettings.h"
#include "shell_utility_table.h"
#include "my_settings_fixtures.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void put(const char *dir, enum ms_kind kind, const struct fixture *f) {
 char path[512];snprintf(path,sizeof path,"%s/PIONEER/%s",dir,ms_filename(kind));
 FILE *o=fopen(path,"wb");assert(o);assert(fwrite(f->bytes,1,f->size,o)==f->size);fclose(o);
}
static int def(const char *key) {
 for(int i=0;i<XZ_SHUT_DEFS;i++)if(!strcmp(xz_shut_defs[i].key,key))return i;
 assert(0);return -1;
}

int main(void) {
 char root[]="/tmp/xz-shms-XXXXXX";assert(mkdtemp(root));
 char pioneer[600];snprintf(pioneer,sizeof pioneer,"%s/PIONEER",root);assert(!mkdir(pioneer,0700));
 struct xz_shms_files f;
 /* No files: nothing on the stick. */
 assert(xz_shms_read(root,&f)==0&&!xz_shms_any(&f)&&xz_shms_wave_color(&f)==-1);
 /* The public demo export: all four files. */
 for(size_t i=0;i<sizeof fixtures/sizeof *fixtures&&i<4;i++)put(root,fixtures[i].kind,&fixtures[i]);
 assert(xz_shms_read(root,&f)==4&&xz_shms_any(&f));
 for(int k=0;k<MS_KIND_COUNT;k++)assert(f.state[k]==XZ_SHMS_OK);

 /* Every map key names a UTILITY item in the generated table. */
 for(int field=0;field<MS_FIELD_COUNT;field++){const char *k=xz_shms_utility_key((enum ms_field)field);if(k)(void)def(k);}

 /* LOAD plan: every applied field becomes a UTILITY setter argument that item accepts; the rest is counted. */
 struct xz_shms_step steps[MS_FIELD_COUNT];int not=0;
 int n=xz_shms_plan(&f,xz_shut_defs,XZ_SHUT_DEFS,steps,MS_FIELD_COUNT,&not);
 int fields=0;for(int field=0;field<MS_FIELD_COUNT;field++){const struct ms_field_info *i=ms_field_info((enum ms_field)field);fields+=i&&f.state[i->kind]==XZ_SHMS_OK;}
 assert(n>=18&&n+not==fields);
 int time_steps=0;
 for(int i=0;i<n;i++){
  if(steps[i].def==XZ_SHMS_STEP_TIME_MODE){time_steps++;continue;}
  assert(steps[i].def>=0&&steps[i].def<XZ_SHUT_DEFS&&xz_shut_index(&xz_shut_defs[steps[i].def],steps[i].arg)>=0);
 }
 assert(time_steps==1);
 /* The stock translations, field by field (TotalMySet_UtilityPac_Unpac and its CmnFunc tables). */
 struct { enum ms_field field; unsigned value; const char *key; int index; } cases[]={
  {MS_FIELD_PLAYER_AUTO_CUE_LEVEL,MS_AUTO_CUE_LEVEL_MINUS_36_DB,"AutoCueLevel",7},
  {MS_FIELD_PLAYER_AUTO_CUE_LEVEL,MS_AUTO_CUE_LEVEL_MINUS_78_DB,"AutoCueLevel",0},
  {MS_FIELD_PLAYER_AUTO_CUE_LEVEL,MS_AUTO_CUE_LEVEL_MEMORY,"AutoCueLevel",8},
  {MS_FIELD_PLAYER_QUANTIZE_BEAT_VALUE,MS_QUANTIZE_BEATS_1_BEAT,"QuantizeValue",3},
  {MS_FIELD_PLAYER_QUANTIZE_BEAT_VALUE,MS_QUANTIZE_BEATS_1_8_BEAT,"QuantizeValue",0},
  {MS_FIELD_PLAYER_HOTCUE_AUTOLOAD,MS_HOTCUE_AUTOLOAD_LIBRARY_SETTING,"HotCueAutoLoad",1},
  {MS_FIELD_PLAYER_HOTCUE_AUTOLOAD,MS_HOTCUE_AUTOLOAD_ON,"HotCueAutoLoad",2},
  {MS_FIELD_PLAYER_LCD_BRIGHTNESS,MS_BRIGHTNESS_5,"LcdBrightness",4},
  {MS_FIELD_PLAYER_LANGUAGE,MS_LANGUAGE_ENGLISH,"Language",0},
  {MS_FIELD_PLAYER_LANGUAGE,MS_LANGUAGE_TURKISH,"Language",17},
  {MS_FIELD_PLAYER_PLAY_MODE,MS_PLAY_MODE_CONTINUE_ON,"AutoPlayMode",1},
  {MS_FIELD_PLAYER2_PAD_BUTTON_BRIGHTNESS,MS_PAD_BRIGHTNESS_1,"PadButtonBrightness",0},
 };
 for(size_t c=0;c<sizeof cases/sizeof *cases;c++){
  struct xz_shms_files g=f;const struct ms_field_info *info=ms_field_info(cases[c].field);
  assert(ms_set(&g.s[info->kind],cases[c].field,cases[c].value)==MS_OK);
  n=xz_shms_plan(&g,xz_shut_defs,XZ_SHUT_DEFS,steps,MS_FIELD_COUNT,&not);
  int d=def(cases[c].key),seen=0;
  for(int i=0;i<n;i++)if(steps[i].def==d){assert(xz_shut_index(&xz_shut_defs[d],steps[i].arg)==cases[c].index);seen=1;}
  assert(seen);
 }

 /* SAVE: the deck's readings go back into the same fields; a second capture changes nothing. */
 int32_t raw[XZ_SHUT_DEFS];uint8_t known[XZ_SHUT_DEFS];memset(known,0,sizeof known);
 int acl=def("AutoCueLevel"),qv=def("QuantizeValue"),lang=def("Language");
 raw[acl]=xz_shut_defs[acl].args[8];known[acl]=1;          /* MEMORY */
 raw[qv]=xz_shut_defs[qv].args[0];known[qv]=1;             /* 1/8 */
 raw[lang]=xz_shut_defs[lang].args[2];known[lang]=1;       /* DEUTSCH */
 struct xz_shms_files g=f;
 assert(xz_shms_capture(&g,xz_shut_defs,XZ_SHUT_DEFS,raw,known,1,1)>=1);
 unsigned v;
 assert(ms_get(&g.s[MS_PLAYER],MS_FIELD_PLAYER_AUTO_CUE_LEVEL,&v)==MS_OK&&v==MS_AUTO_CUE_LEVEL_MEMORY);
 assert(ms_get(&g.s[MS_PLAYER],MS_FIELD_PLAYER_QUANTIZE_BEAT_VALUE,&v)==MS_OK&&v==MS_QUANTIZE_BEATS_1_8_BEAT);
 assert(ms_get(&g.s[MS_PLAYER],MS_FIELD_PLAYER_LANGUAGE,&v)==MS_OK&&v==MS_LANGUAGE_GERMAN);
 assert(ms_get(&g.s[MS_PLAYER],MS_FIELD_PLAYER_TIME_MODE,&v)==MS_OK&&v==MS_TIME_MODE_REMAIN);
 assert(xz_shms_capture(&g,xz_shut_defs,XZ_SHUT_DEFS,raw,known,1,1)==0);
 /* Written atomically and read back the same. */
 assert(xz_shms_write(root,&g,MS_PLAYER)==MS_OK);
 struct xz_shms_files back;assert(xz_shms_read(root,&back)==4);
 assert(ms_get(&back.s[MS_PLAYER],MS_FIELD_PLAYER_AUTO_CUE_LEVEL,&v)==MS_OK&&v==MS_AUTO_CUE_LEVEL_MEMORY);

 /* Rows: one per field, caps labels, the codec's choices, the stored value; SET changes one. */
 static struct xz_shms_rows rows;xz_shms_rows(&back,1,&rows);
 assert(rows.count==fields);
 int at=-1;for(int i=0;i<rows.count;i++)if(rows.item[i].id==XZ_SHMS_ID(MS_FIELD_PLAYER_AUTO_CUE_LEVEL))at=i;
 assert(at>=0&&!strcmp(rows.item[at].label,"AUTO CUE LEVEL")&&rows.item[at].known&&rows.item[at].writable);
 assert(!strcmp(rows.item[at].choices[rows.item[at].current],ms_choice_label(MS_FIELD_PLAYER_AUTO_CUE_LEVEL,MS_AUTO_CUE_LEVEL_MEMORY)));
 xz_shms_rows(&back,0,&rows);assert(!rows.item[at].writable);
 enum ms_kind kind;
 assert(!xz_shms_set(&back,XZ_SHMS_ID(MS_FIELD_PLAYER_AUTO_CUE_LEVEL),0,&kind)&&kind==MS_PLAYER);
 assert(xz_shms_set(&back,XZ_SHMS_ID(MS_FIELD_PLAYER_AUTO_CUE_LEVEL),99,&kind));
 assert(xz_shms_set(&back,0x1ff,0,&kind));

 /* DEVSETTING waveform colour: demo export, then the RGB and 3-band samples. */
 int wc=xz_shms_wave_color(&f);assert(wc>=XZ_SHELL_WAVE_BLUE&&wc<=XZ_SHELL_WAVE_3BAND);
 put(root,MS_DEVICE,&fixtures[11]);assert(xz_shms_read(root,&f)==4&&xz_shms_wave_color(&f)==XZ_SHELL_WAVE_RGB);
 put(root,MS_DEVICE,&fixtures[10]);assert(xz_shms_read(root,&f)==4&&xz_shms_wave_color(&f)==XZ_SHELL_WAVE_3BAND);
 /* A damaged file is unreadable, the others stay. */
 char path[700];snprintf(path,sizeof path,"%s/PIONEER/%s",root,ms_filename(MS_MIXER));
 FILE *o=fopen(path,"wb");fputs("not a settings file",o);fclose(o);
 assert(xz_shms_read(root,&f)==3&&f.state[MS_MIXER]==XZ_SHMS_BAD);
 for(int k=0;k<MS_KIND_COUNT;k++){snprintf(path,sizeof path,"%s/PIONEER/%s",root,ms_filename((enum ms_kind)k));unlink(path);}
 rmdir(pioneer);rmdir(root);
 printf("PASS MY SETTINGS: %d fields read, LOAD plan via UTILITY setters with stock's translations, SAVE capture, atomic write, rows, SET, waveform colour\n",fields);
 return 0;
}
