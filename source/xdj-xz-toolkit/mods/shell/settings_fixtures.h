#ifndef XZ_SHELL_SETTINGS_FIXTURES_H
#define XZ_SHELL_SETTINGS_FIXTURES_H
/* Example settings table for the settings host test and previews. Most items
   arrive read-only, as they will until the settings-write phase; a few are
   unknown. Never device data. */
#include "shell_settings.h"
#include <string.h>

static const char *const fxs_quantize[]={"1/8","1/4","1/2","1"};
static const char *const fxs_jump[]={"1/2","1","2","4","8","16","32","64"};
static const char *const fxs_auto_load[]={"OFF","SETTING","ON"};
static const char *const fxs_off_on[]={"OFF","ON"};
static const char *const fxs_cue_level[]={"MEMORY","-36 dB","-42 dB","-48 dB","-54 dB","-60 dB","-66 dB","-72 dB","-78 dB"};
static const char *const fxs_vinyl[]={"TOUCH & RELEASE","TOUCH","RELEASE"};
static const char *const fxs_play_mode[]={"SINGLE","CONTINUE"};
static const char *const fxs_time_mode[]={"REMAIN","TIME"};
static const char *const fxs_lock[]={"UNLOCK","LOCK"};
static const char *const fxs_meter[]={"WAVEFORM","PHASE METER"};
static const char *const fxs_wave_color[]={"BLUE","RGB","3BAND"};
static const char *const fxs_position[]={"CENTER","LEFT"};
static const char *const fxs_level5[]={"1","2","3","4","5"};
static const char *const fxs_language[]={"ENGLISH","FRAN\xc3\x87" "AIS","DEUTSCH","ITALIANO","NEDERLANDS","ESPA\xc3\x91OL","PORTUGU\xc3\x8aS",
 "SVENSKA","DANSK","NORSK","SUOMI","POLSKI","MAGYAR","T\xc3\x9cRK\xc3\x87" "E"};
static const char *const fxs_ring[]={"OFF","1","2"};
static const char *const fxs_player[]={"AUTO","1","2","3","4"};
static const char *const fxs_midi[]={"1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"};
static const char *const fxs_curve[]={"CURVE 1","CURVE 2","CURVE 3"};
static const char *const fxs_eq[]={"EQ","ISOLATOR"};
static const char *const fxs_mic_eq[]={"FLAT","LOW CUT"};
static const char *const fxs_phones[]={"STEREO","MONO SPLIT"};
static const char *const fxs_version[]={"1.26"};

#define FXS(id,cat,label,choices,current,known,writable) \
 {XZ_SHELL_ITEM_##id,XZ_SHELL_SETTING_CAT_##cat,label,choices,(int)(sizeof choices/sizeof *choices),current,known,writable}
static const struct xz_shell_setting_item fxs_items[]={
 FXS(LOAD_LOCK,DJ_SETTING,"LOAD LOCK",fxs_lock,0,1,0),
 FXS(QUANTIZE_BEAT,DJ_SETTING,"QUANTIZE BEAT VALUE",fxs_quantize,3,1,0),
 FXS(BEAT_JUMP_BEAT,DJ_SETTING,"BEAT JUMP BEAT VALUE",fxs_jump,5,1,0),
 FXS(HOT_CUE_AUTO_LOAD,DJ_SETTING,"HOT CUE AUTO LOAD",fxs_auto_load,2,1,1),
 FXS(HOT_CUE_COLOR,DJ_SETTING,"HOT CUE COLOR",fxs_off_on,1,1,0),
 FXS(AUTO_CUE,DJ_SETTING,"AUTO CUE",fxs_off_on,1,1,1),
 FXS(AUTO_CUE_LEVEL,DJ_SETTING,"AUTO CUE LEVEL",fxs_cue_level,0,1,0),
 FXS(VINYL_SPEED_ADJUST,DJ_SETTING,"VINYL SPEED ADJUST",fxs_vinyl,1,1,0),
 FXS(PLAY_MODE,DJ_SETTING,"PLAY MODE",fxs_play_mode,0,1,1),
 FXS(TIME_MODE,DJ_SETTING,"TIME MODE",fxs_time_mode,0,1,1),
 FXS(WAVE_PHASE_METER,DJ_SETTING,"WAVEFORM / PHASE METER",fxs_meter,0,1,0),
 FXS(WAVE_COLOR,DISPLAY_LCD,"WAVEFORM COLOR",fxs_wave_color,2,1,1),
 FXS(WAVE_POSITION,DISPLAY_LCD,"WAVEFORM CURRENT POSITION",fxs_position,0,1,1),
 FXS(LCD_BRIGHTNESS,DISPLAY_LCD,"LCD BRIGHTNESS",fxs_level5,2,1,0),
 FXS(JOG_LCD_BRIGHTNESS,DISPLAY_LCD,"JOG LCD BRIGHTNESS",fxs_level5,0,0,0),
 FXS(LANGUAGE,DISPLAY_LCD,"LANGUAGE",fxs_language,0,1,0),
 FXS(SCREEN_SAVER,DISPLAY_LCD,"SCREEN SAVER",fxs_off_on,1,1,0),
 FXS(SLIP_FLASHING,DISPLAY_INDICATOR,"SLIP FLASHING",fxs_off_on,1,1,0),
 FXS(ON_AIR_DISPLAY,DISPLAY_INDICATOR,"ON AIR DISPLAY",fxs_off_on,1,1,0),
 FXS(JOG_RING_BRIGHTNESS,DISPLAY_INDICATOR,"JOG RING BRIGHTNESS",fxs_ring,2,1,0),
 FXS(PLAYER_NUMBER,PRO_DJ_LINK,"PLAYER No.",fxs_player,0,1,0),
 FXS(MIDI_CHANNEL,PRO_DJ_LINK,"MIDI CHANNEL",fxs_midi,0,1,0),
 FXS(CH_FADER_CURVE,MIXER,"CHANNEL FADER CURVE",fxs_curve,1,1,0),
 FXS(CROSSFADER_CURVE,MIXER,"CROSSFADER CURVE",fxs_curve,0,1,0),
 FXS(EQ_MODE,MIXER,"EQ / ISOLATOR",fxs_eq,0,1,0),
 FXS(MIC_EQ,MIXER,"MIC EQ",fxs_mic_eq,0,0,0),
 FXS(HEADPHONE_CUE_MODE,MIXER,"HEADPHONES CUE",fxs_phones,0,0,0),
 FXS(AUTO_STANDBY,SYSTEM,"AUTO STANDBY",fxs_off_on,1,1,0),
 FXS(VERSION,INFO,"VERSION No.",fxs_version,0,1,0),
};
#undef FXS
enum { FXS_COUNT=(int)(sizeof fxs_items/sizeof *fxs_items) };

/* A writable copy, so tests and previews can change single items. */
static struct xz_shell_setting_item fxs_table[FXS_COUNT];
static struct xz_shell_settings_model fxs_model;
static const struct xz_shell_settings_model *fxs_reset(void) {
 memcpy(fxs_table,fxs_items,sizeof fxs_table);
 fxs_model.count=FXS_COUNT;fxs_model.items=fxs_table;
 return &fxs_model;
}
static struct xz_shell_setting_item *fxs_item(int id) {
 for(int i=0;i<FXS_COUNT;i++)if(fxs_table[i].id==id)return &fxs_table[i];
 return NULL;
}
#endif
