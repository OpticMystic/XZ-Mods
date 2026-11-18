/* SPDX-License-Identifier: MIT */
#include "shell_mysettings.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* One entry per applied field: file value v = 0x80 + k, and arg[k] the stock setter argument (-1: not a
   value this field takes). Fields not listed here are not applied. */
#define N 19
#define NONE -1
struct entry { enum ms_field field; const char *key; int count; int32_t arg[N]; };
static const struct entry map[]={
 /* CmnFunc_CmnInfo_SetUtilityOnAirFlg -> UiSetUtilOnAirDisplay(v != 0) */
 {MS_FIELD_PLAYER_ON_AIR_DISPLAY,"OnAirDisplay",2,{0,1}},
 /* CmnFunc_CmnInfo_SetUtilityLcdBrightness -> UiSetUtilLcdBrightness(table[v-1] = v) */
 {MS_FIELD_PLAYER_LCD_BRIGHTNESS,"LcdBrightness",6,{NONE,1,2,3,4,5}},
 /* CmnFunc_CmnInfo_SetUtilityAutoCueLevelVal -> UiSetUtilACueLevel(v <= 7 ? 7 - v : 8) */
 {MS_FIELD_PLAYER_AUTO_CUE_LEVEL,"AutoCueLevel",9,{7,6,5,4,3,2,1,0,8}},
 /* CmnFunc_CmnInfo_SetUtilityLanguageNo -> UiSetUtilLanguage(v), 1 English .. 18 Turkish */
 {MS_FIELD_PLAYER_LANGUAGE,"Language",19,{NONE,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18}},
 /* UiSetUtilJogRingBrightness(v): OFF, 1 (dark), 2 (bright) */
 {MS_FIELD_PLAYER_JOG_RING_BRIGHTNESS,"JogRingBrightness",3,{0,1,2}},
 /* UiSetUtilJogRingIndicator(v) */
 {MS_FIELD_PLAYER_JOG_RING_INDICATOR,"JogRingIndicator",2,{0,1}},
 /* CmnFunc_CmnInfo_SetUtilitySlipBlink -> UiSetUtilSlipFlashing(v != 0) */
 {MS_FIELD_PLAYER_SLIP_FLASHING,"SlipFlashing",2,{0,1}},
 /* CmnFunc_CmnInfo_SetUtilityEjectLoadLock -> UiSetUtilLoadLock(v != 0) */
 {MS_FIELD_PLAYER_EJECT_LOCK,"EjectLoadLock",2,{0,1}},
 /* UiSetUtilAutoPlayMode(1 - v): CONTINUE is AUTO PLAY on, SINGLE off */
 {MS_FIELD_PLAYER_PLAY_MODE,"AutoPlayMode",2,{1,0}},
 /* CmnFunc_CmnInfo_SetUtilityQuantizeValue -> UiSetUtilQuantizeValue(v): 0 = 1 beat .. 3 = 1/8 */
 {MS_FIELD_PLAYER_QUANTIZE_BEAT_VALUE,"QuantizeValue",4,{0,1,2,3}},
 /* CmnFunc_CmnInfo_SetUtilityHotCueAutoLoad -> UiSetUtilHotCueAutoLoad(v <= 1 ? v : 2): off, on, library */
 {MS_FIELD_PLAYER_HOTCUE_AUTOLOAD,"HotCueAutoLoad",3,{0,1,2}},
 /* CmnFunc_CmnInfo_SetUtilityHotCueRecAutoColor -> UiSetUtilHotCueColor(v != 0) */
 {MS_FIELD_PLAYER_HOTCUE_COLOR,"HotCueColor",2,{0,1}},
 /* UiSetUtilNeedleLock(v) */
 {MS_FIELD_PLAYER_NEEDLE_LOCK,"NeedleLock",2,{0,1}},
 /* UiSetUtilVinylAdjust(v): TOUCH & RELEASE, TOUCH, RELEASE */
 {MS_FIELD_PLAYER2_VINYL_SPEED_ADJUST,"VinylSpeedAdjust",3,{0,1,2}},
 /* UiSetUtilJogDisplayMode(v): AUTO, INFO, SIMPLE, ARTWORK */
 {MS_FIELD_PLAYER2_JOG_DISPLAY_MODE,"JogDisplayMode",4,{0,1,2,3}},
 /* UiSetUtilPadButtonBrightness(v), 1..4 */
 {MS_FIELD_PLAYER2_PAD_BUTTON_BRIGHTNESS,"PadButtonBrightness",5,{NONE,1,2,3,4}},
 /* UiSetUtilJogLcdBrightness(v), 1..5 */
 {MS_FIELD_PLAYER2_JOG_LCD_BRIGHTNESS,"JogLcdBrightness",6,{NONE,1,2,3,4,5}},
 /* UiSetUtilMonoSplitStereo: the file says STEREO, MONO SPLIT; UTILITY lists MONO SPLIT (0), STEREO (1) */
 {MS_FIELD_MIXER_HEADPHONES_MONO_SPLIT,"MonoSplitStereo",2,{1,0}},
 /* UiSetUtilTalkOverMode: ADVANCED, NORMAL in both */
 {MS_FIELD_MIXER_TALK_OVER_MODE,"TalkOverMode",2,{0,1}},
 /* UiSetUtilTalkOverLevel: -24, -18, -12, -6 dB in both */
 {MS_FIELD_MIXER_TALK_OVER_LEVEL,"TalkOverLevel",4,{0,1,2,3}},
};
#undef N
enum { MAP = (int)(sizeof map / sizeof *map) };

static const struct entry *entry_of(enum ms_field f) {
 for(int i=0;i<MAP;i++)if(map[i].field==f)return &map[i];
 return NULL;
}
const char *xz_shms_utility_key(enum ms_field f) { const struct entry *e=entry_of(f);return e?e->key:NULL; }
static int def_of(const struct xz_shut_def *defs, int n, const char *key) {
 for(int i=0;i<n;i++)if(!strcmp(defs[i].key,key))return i;
 return -1;
}

int xz_shms_read(const char *root, struct xz_shms_files *f) {
 memset(f,0,sizeof *f);
 int ok=0;
 for(int k=0;k<MS_KIND_COUNT;k++){
  char path[1200];struct stat st;
  if(!root||snprintf(path,sizeof path,"%s/PIONEER/%s",root,ms_filename((enum ms_kind)k))>=(int)sizeof path)continue;
  if(stat(path,&st)){f->state[k]=XZ_SHMS_MISSING;continue;}
  if(ms_read_file((enum ms_kind)k,path,&f->s[k])==MS_OK){f->state[k]=XZ_SHMS_OK;ok++;}
  else f->state[k]=XZ_SHMS_BAD;
 }
 return ok;
}
int xz_shms_any(const struct xz_shms_files *f) {
 for(int k=0;f&&k<MS_KIND_COUNT;k++)if(f->state[k]==XZ_SHMS_OK)return 1;
 return 0;
}
int xz_shms_write(const char *root, const struct xz_shms_files *f, enum ms_kind kind) {
 char path[1200];
 if(!root||!f||(unsigned)kind>=MS_KIND_COUNT||f->state[kind]!=XZ_SHMS_OK)return MS_ERR_ARGUMENT;
 if(snprintf(path,sizeof path,"%s/PIONEER/%s",root,ms_filename(kind))>=(int)sizeof path)return MS_ERR_PATH;
 return ms_write_atomic(path,&f->s[kind]);
}
static int field_value(const struct xz_shms_files *f, enum ms_field field, unsigned *v) {
 const struct ms_field_info *i=ms_field_info(field);
 return i&&f->state[i->kind]==XZ_SHMS_OK&&ms_get(&f->s[i->kind],field,v)==MS_OK;
}

int xz_shms_plan(const struct xz_shms_files *f, const struct xz_shut_def *defs, int ndefs,
                 struct xz_shms_step *out, int cap, int *not_applied) {
 int n=0,skipped=0;
 for(int field=0;field<MS_FIELD_COUNT;field++){
  unsigned v;
  if(!field_value(f,(enum ms_field)field,&v))continue;
  if(field==MS_FIELD_PLAYER_TIME_MODE){
   /* UiSetKeyTimeMode(deck, v == REMAIN), as stock's load does per deck. */
   if((v==MS_TIME_MODE_ELAPSED||v==MS_TIME_MODE_REMAIN)&&n<cap){out[n++]=(struct xz_shms_step){XZ_SHMS_STEP_TIME_MODE,v==MS_TIME_MODE_REMAIN};continue;}
   skipped++;continue;
  }
  const struct entry *e=entry_of((enum ms_field)field);
  int d=e?def_of(defs,ndefs,e->key):-1;
  unsigned k=v-0x80u;
  if(!e||d<0||v<0x80u||k>=(unsigned)e->count||e->arg[k]<0||xz_shut_index(&defs[d],e->arg[k])<0||n>=cap){skipped++;continue;}
  out[n++]=(struct xz_shms_step){d,e->arg[k]};
 }
 if(not_applied)*not_applied=skipped;
 return n;
}

int xz_shms_capture(struct xz_shms_files *f, const struct xz_shut_def *defs, int ndefs,
                    const int32_t *raw, const uint8_t *known, int time_known, int remain) {
 int changed=0;
 for(int i=0;i<MAP;i++){
  const struct entry *e=&map[i];const struct ms_field_info *info=ms_field_info(e->field);
  int d=def_of(defs,ndefs,e->key);unsigned old;
  if(!info||d<0||!known[d]||f->state[info->kind]!=XZ_SHMS_OK||ms_get(&f->s[info->kind],e->field,&old)!=MS_OK)continue;
  for(int k=0;k<e->count;k++)if(e->arg[k]>=0&&e->arg[k]==raw[d]){
   unsigned v=0x80u+(unsigned)k;
   if(v!=old&&ms_set(&f->s[info->kind],e->field,v)==MS_OK)changed++;
   break;
  }
 }
 if(time_known&&f->state[MS_PLAYER]==XZ_SHMS_OK){
  unsigned old,v=remain?MS_TIME_MODE_REMAIN:MS_TIME_MODE_ELAPSED;
  if(ms_get(&f->s[MS_PLAYER],MS_FIELD_PLAYER_TIME_MODE,&old)==MS_OK&&old!=v&&ms_set(&f->s[MS_PLAYER],MS_FIELD_PLAYER_TIME_MODE,v)==MS_OK)changed++;
 }
 return changed;
}

void xz_shms_rows(const struct xz_shms_files *f, int writable, struct xz_shms_rows *r) {
 r->count=0;
 for(int field=0;field<MS_FIELD_COUNT;field++){
  const struct ms_field_info *info=ms_field_info((enum ms_field)field);unsigned v;
  if(!info||f->state[info->kind]!=XZ_SHMS_OK||ms_get(&f->s[info->kind],(enum ms_field)field,&v)!=MS_OK)continue;
  int n=r->count,count=info->choice_count<24?(int)info->choice_count:24,current=-1;
  /* "on_air_display" -> "ON AIR DISPLAY" */
  size_t j=0;for(;info->name[j]&&j<sizeof r->label[n]-1;j++){char c=info->name[j];r->label[n][j]=c=='_'?' ':c>='a'&&c<='z'?(char)(c-32):c;}
  r->label[n][j]=0;
  for(int c=0;c<count;c++){r->choice[n][c]=info->choices[c].label;if(info->choices[c].value==v)current=c;}
  r->item[n]=(struct xz_shell_setting_item){XZ_SHMS_ID(field),(enum xz_shell_setting_category)info->kind,r->label[n],
   r->choice[n],count,current<0?0:current,current>=0,writable&&current>=0};
  r->count++;
 }
}
int xz_shms_set(struct xz_shms_files *f, int id, int index, enum ms_kind *kind) {
 int field=id-XZ_SHMS_ID(0);
 const struct ms_field_info *info=field>=0&&field<MS_FIELD_COUNT?ms_field_info((enum ms_field)field):NULL;
 if(!info||f->state[info->kind]!=XZ_SHMS_OK||index<0||(size_t)index>=info->choice_count)return -1;
 if(ms_set(&f->s[info->kind],(enum ms_field)field,info->choices[index].value)!=MS_OK)return -1;
 if(kind)*kind=info->kind;
 return 0;
}
int xz_shms_wave_color(const struct xz_shms_files *f) {
 unsigned v;
 if(!f||!field_value(f,MS_FIELD_DEVICE_WAVEFORM_COLOR,&v))return -1;
 /* DEVSETTING: 0x01 Blue, 0x03 RGB, 0x04 3 band. */
 return v==0x01?XZ_SHELL_WAVE_BLUE:v==0x03?XZ_SHELL_WAVE_RGB:v==0x04?XZ_SHELL_WAVE_3BAND:-1;
}
