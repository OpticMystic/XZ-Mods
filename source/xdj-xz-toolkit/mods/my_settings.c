/* Fixed-size little-endian settings codec. See MY_SETTINGS.md for evidence. */
#define _POSIX_C_SOURCE 200809L
#include "my_settings.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

static const struct ms_choice choices_toggle[] = {
    {0x80, "Off"},
    {0x81, "On"},
};
static const struct ms_choice choices_brightness[] = {
    {0x81, "1"},
    {0x82, "2"},
    {0x83, "3"},
    {0x84, "4"},
    {0x85, "5"},
};
static const struct ms_choice choices_auto_cue_level[] = {
    {0x80, "-36 dB"},
    {0x81, "-42 dB"},
    {0x82, "-48 dB"},
    {0x83, "-54 dB"},
    {0x84, "-60 dB"},
    {0x85, "-66 dB"},
    {0x86, "-72 dB"},
    {0x87, "-78 dB"},
    {0x88, "Memory"},
};
static const struct ms_choice choices_language[] = {
    {0x81, "English"},
    {0x82, "French"},
    {0x83, "German"},
    {0x84, "Italian"},
    {0x85, "Dutch"},
    {0x86, "Spanish"},
    {0x87, "Russian"},
    {0x88, "Korean"},
    {0x89, "Chinese simplified"},
    {0x8a, "Chinese traditional"},
    {0x8b, "Japanese"},
    {0x8c, "Portuguese"},
    {0x8d, "Swedish"},
    {0x8e, "Czech"},
    {0x8f, "Hungarian"},
    {0x90, "Danish"},
    {0x91, "Greek"},
    {0x92, "Turkish"},
};
static const struct ms_choice choices_illumination[] = {
    {0x80, "Off"},
    {0x81, "Dark"},
    {0x82, "Bright"},
};
static const struct ms_choice choices_lock[] = {
    {0x80, "Unlock"},
    {0x81, "Lock"},
};
static const struct ms_choice choices_play_mode[] = {
    {0x80, "Continue / On"},
    {0x81, "Single / Off"},
};
static const struct ms_choice choices_quantize_beats[] = {
    {0x80, "1 beat"},
    {0x81, "1/2 beat"},
    {0x82, "1/4 beat"},
    {0x83, "1/8 beat"},
};
static const struct ms_choice choices_hotcue_autoload[] = {
    {0x80, "Off"},
    {0x81, "On"},
    {0x82, "Library setting"},
};
static const struct ms_choice choices_time_mode[] = {
    {0x80, "Elapsed"},
    {0x81, "Remain"},
};
static const struct ms_choice choices_jog_mode[] = {
    {0x80, "Digital"},
    {0x81, "Vinyl"},
};
static const struct ms_choice choices_tempo_range[] = {
    {0x80, "6 percent"},
    {0x81, "10 percent"},
    {0x82, "16 percent"},
    {0x83, "Wide"},
};
static const struct ms_choice choices_phase_meter[] = {
    {0x80, "Type 1"},
    {0x81, "Type 2"},
};
static const struct ms_choice choices_vinyl_speed[] = {
    {0x80, "Touch and release"},
    {0x81, "Touch"},
    {0x82, "Release"},
};
static const struct ms_choice choices_jog_display[] = {
    {0x80, "Auto"},
    {0x81, "Info"},
    {0x82, "Simple"},
    {0x83, "Artwork"},
};
static const struct ms_choice choices_pad_brightness[] = {
    {0x81, "1"},
    {0x82, "2"},
    {0x83, "3"},
    {0x84, "4"},
};
static const struct ms_choice choices_waveform_divisions[] = {
    {0x80, "Time scale"},
    {0x81, "Phrase"},
};
static const struct ms_choice choices_waveform[] = {
    {0x80, "Waveform"},
    {0x81, "Phase meter"},
};
static const struct ms_choice choices_beat_jump[] = {
    {0x80, "1/2 beat"},
    {0x81, "1 beat"},
    {0x82, "2 beats"},
    {0x83, "4 beats"},
    {0x84, "8 beats"},
    {0x85, "16 beats"},
    {0x86, "32 beats"},
    {0x87, "64 beats"},
};
static const struct ms_choice choices_channel_curve[] = {
    {0x80, "Steep top"},
    {0x81, "Linear"},
    {0x82, "Steep bottom"},
};
static const struct ms_choice choices_cross_curve[] = {
    {0x80, "Constant power"},
    {0x81, "Slow cut"},
    {0x82, "Fast cut"},
};
static const struct ms_choice choices_headphones_eq[] = {
    {0x80, "Post EQ"},
    {0x81, "Pre EQ"},
};
static const struct ms_choice choices_headphones_split[] = {
    {0x80, "Stereo"},
    {0x81, "Mono split"},
};
static const struct ms_choice choices_talk_mode[] = {
    {0x80, "Advanced"},
    {0x81, "Normal"},
};
static const struct ms_choice choices_talk_level[] = {
    {0x80, "-24 dB"},
    {0x81, "-18 dB"},
    {0x82, "-12 dB"},
    {0x83, "-6 dB"},
};
static const struct ms_choice choices_midi_channel[] = {
    {0x80, "1"},
    {0x81, "2"},
    {0x82, "3"},
    {0x83, "4"},
    {0x84, "5"},
    {0x85, "6"},
    {0x86, "7"},
    {0x87, "8"},
    {0x88, "9"},
    {0x89, "10"},
    {0x8a, "11"},
    {0x8b, "12"},
    {0x8c, "13"},
    {0x8d, "14"},
    {0x8e, "15"},
    {0x8f, "16"},
};
static const struct ms_choice choices_midi_button[] = {
    {0x80, "Toggle"},
    {0x81, "Trigger"},
};
static const struct ms_choice choices_mixer_display[] = {
    {0x80, "White"},
    {0x81, "1"},
    {0x82, "2"},
    {0x83, "3"},
    {0x84, "4"},
    {0x85, "5"},
};
static const struct ms_choice choices_indicator_brightness[] = {
    {0x80, "1"},
    {0x81, "2"},
    {0x82, "3"},
};
static const struct ms_choice choices_long_curve[] = {
    {0x80, "Exponential"},
    {0x81, "Smooth"},
    {0x82, "Linear"},
};
static const struct ms_choice choices_overview[] = {
    {0x1, "Half waveform"},
    {0x2, "Full waveform"},
};
static const struct ms_choice choices_waveform_color[] = {
    {0x1, "Blue"},
    {0x3, "RGB"},
    {0x4, "3 band"},
};
static const struct ms_choice choices_key_display[] = {
    {0x1, "Classic"},
    {0x2, "Alphanumeric"},
};
static const struct ms_choice choices_waveform_position[] = {
    {0x1, "Center"},
    {0x2, "Left"},
};
static const struct ms_field_info fields[] = {
    {MS_FIELD_PLAYER_ON_AIR_DISPLAY, MS_PLAYER, "on_air_display", 8, 1, choices_toggle, sizeof(choices_toggle) / sizeof(choices_toggle[0])},
    {MS_FIELD_PLAYER_LCD_BRIGHTNESS, MS_PLAYER, "lcd_brightness", 9, 1, choices_brightness, sizeof(choices_brightness) / sizeof(choices_brightness[0])},
    {MS_FIELD_PLAYER_QUANTIZE, MS_PLAYER, "quantize", 10, 0, choices_toggle, sizeof(choices_toggle) / sizeof(choices_toggle[0])},
    {MS_FIELD_PLAYER_AUTO_CUE_LEVEL, MS_PLAYER, "auto_cue_level", 11, 0, choices_auto_cue_level, sizeof(choices_auto_cue_level) / sizeof(choices_auto_cue_level[0])},
    {MS_FIELD_PLAYER_LANGUAGE, MS_PLAYER, "language", 12, 1, choices_language, sizeof(choices_language) / sizeof(choices_language[0])},
    {MS_FIELD_PLAYER_JOG_RING_BRIGHTNESS, MS_PLAYER, "jog_ring_brightness", 14, 1, choices_illumination, sizeof(choices_illumination) / sizeof(choices_illumination[0])},
    {MS_FIELD_PLAYER_JOG_RING_INDICATOR, MS_PLAYER, "jog_ring_indicator", 15, 1, choices_toggle, sizeof(choices_toggle) / sizeof(choices_toggle[0])},
    {MS_FIELD_PLAYER_SLIP_FLASHING, MS_PLAYER, "slip_flashing", 16, 1, choices_toggle, sizeof(choices_toggle) / sizeof(choices_toggle[0])},
    {MS_FIELD_PLAYER_DISC_SLOT_ILLUMINATION, MS_PLAYER, "disc_slot_illumination", 20, 1, choices_illumination, sizeof(choices_illumination) / sizeof(choices_illumination[0])},
    {MS_FIELD_PLAYER_EJECT_LOCK, MS_PLAYER, "eject_lock", 21, 0, choices_lock, sizeof(choices_lock) / sizeof(choices_lock[0])},
    {MS_FIELD_PLAYER_SYNC, MS_PLAYER, "sync", 22, 0, choices_toggle, sizeof(choices_toggle) / sizeof(choices_toggle[0])},
    {MS_FIELD_PLAYER_PLAY_MODE, MS_PLAYER, "play_mode", 23, 0, choices_play_mode, sizeof(choices_play_mode) / sizeof(choices_play_mode[0])},
    {MS_FIELD_PLAYER_QUANTIZE_BEAT_VALUE, MS_PLAYER, "quantize_beat_value", 24, 0, choices_quantize_beats, sizeof(choices_quantize_beats) / sizeof(choices_quantize_beats[0])},
    {MS_FIELD_PLAYER_HOTCUE_AUTOLOAD, MS_PLAYER, "hotcue_autoload", 25, 0, choices_hotcue_autoload, sizeof(choices_hotcue_autoload) / sizeof(choices_hotcue_autoload[0])},
    {MS_FIELD_PLAYER_HOTCUE_COLOR, MS_PLAYER, "hotcue_color", 26, 1, choices_toggle, sizeof(choices_toggle) / sizeof(choices_toggle[0])},
    {MS_FIELD_PLAYER_NEEDLE_LOCK, MS_PLAYER, "needle_lock", 29, 0, choices_lock, sizeof(choices_lock) / sizeof(choices_lock[0])},
    {MS_FIELD_PLAYER_TIME_MODE, MS_PLAYER, "time_mode", 32, 1, choices_time_mode, sizeof(choices_time_mode) / sizeof(choices_time_mode[0])},
    {MS_FIELD_PLAYER_JOG_MODE, MS_PLAYER, "jog_mode", 33, 0, choices_jog_mode, sizeof(choices_jog_mode) / sizeof(choices_jog_mode[0])},
    {MS_FIELD_PLAYER_AUTO_CUE, MS_PLAYER, "auto_cue", 34, 0, choices_toggle, sizeof(choices_toggle) / sizeof(choices_toggle[0])},
    {MS_FIELD_PLAYER_MASTER_TEMPO, MS_PLAYER, "master_tempo", 35, 0, choices_toggle, sizeof(choices_toggle) / sizeof(choices_toggle[0])},
    {MS_FIELD_PLAYER_TEMPO_RANGE, MS_PLAYER, "tempo_range", 36, 0, choices_tempo_range, sizeof(choices_tempo_range) / sizeof(choices_tempo_range[0])},
    {MS_FIELD_PLAYER_PHASE_METER, MS_PLAYER, "phase_meter", 37, 1, choices_phase_meter, sizeof(choices_phase_meter) / sizeof(choices_phase_meter[0])},
    {MS_FIELD_PLAYER2_VINYL_SPEED_ADJUST, MS_PLAYER2, "vinyl_speed_adjust", 0, 0, choices_vinyl_speed, sizeof(choices_vinyl_speed) / sizeof(choices_vinyl_speed[0])},
    {MS_FIELD_PLAYER2_JOG_DISPLAY_MODE, MS_PLAYER2, "jog_display_mode", 1, 1, choices_jog_display, sizeof(choices_jog_display) / sizeof(choices_jog_display[0])},
    {MS_FIELD_PLAYER2_PAD_BUTTON_BRIGHTNESS, MS_PLAYER2, "pad_button_brightness", 2, 1, choices_pad_brightness, sizeof(choices_pad_brightness) / sizeof(choices_pad_brightness[0])},
    {MS_FIELD_PLAYER2_JOG_LCD_BRIGHTNESS, MS_PLAYER2, "jog_lcd_brightness", 3, 1, choices_brightness, sizeof(choices_brightness) / sizeof(choices_brightness[0])},
    {MS_FIELD_PLAYER2_WAVEFORM_DIVISIONS, MS_PLAYER2, "waveform_divisions", 4, 1, choices_waveform_divisions, sizeof(choices_waveform_divisions) / sizeof(choices_waveform_divisions[0])},
    {MS_FIELD_PLAYER2_WAVEFORM, MS_PLAYER2, "waveform", 10, 1, choices_waveform, sizeof(choices_waveform) / sizeof(choices_waveform[0])},
    {MS_FIELD_PLAYER2_BEAT_JUMP_BEAT_VALUE, MS_PLAYER2, "beat_jump_beat_value", 12, 0, choices_beat_jump, sizeof(choices_beat_jump) / sizeof(choices_beat_jump[0])},
    {MS_FIELD_MIXER_CHANNEL_FADER_CURVE, MS_MIXER, "channel_fader_curve", 12, 0, choices_channel_curve, sizeof(choices_channel_curve) / sizeof(choices_channel_curve[0])},
    {MS_FIELD_MIXER_CROSSFADER_CURVE, MS_MIXER, "crossfader_curve", 13, 0, choices_cross_curve, sizeof(choices_cross_curve) / sizeof(choices_cross_curve[0])},
    {MS_FIELD_MIXER_HEADPHONES_PRE_EQ, MS_MIXER, "headphones_pre_eq", 14, 0, choices_headphones_eq, sizeof(choices_headphones_eq) / sizeof(choices_headphones_eq[0])},
    {MS_FIELD_MIXER_HEADPHONES_MONO_SPLIT, MS_MIXER, "headphones_mono_split", 15, 0, choices_headphones_split, sizeof(choices_headphones_split) / sizeof(choices_headphones_split[0])},
    {MS_FIELD_MIXER_BEAT_FX_QUANTIZE, MS_MIXER, "beat_fx_quantize", 16, 0, choices_toggle, sizeof(choices_toggle) / sizeof(choices_toggle[0])},
    {MS_FIELD_MIXER_MIC_LOW_CUT, MS_MIXER, "mic_low_cut", 17, 0, choices_toggle, sizeof(choices_toggle) / sizeof(choices_toggle[0])},
    {MS_FIELD_MIXER_TALK_OVER_MODE, MS_MIXER, "talk_over_mode", 18, 0, choices_talk_mode, sizeof(choices_talk_mode) / sizeof(choices_talk_mode[0])},
    {MS_FIELD_MIXER_TALK_OVER_LEVEL, MS_MIXER, "talk_over_level", 19, 0, choices_talk_level, sizeof(choices_talk_level) / sizeof(choices_talk_level[0])},
    {MS_FIELD_MIXER_MIDI_CHANNEL, MS_MIXER, "midi_channel", 20, 0, choices_midi_channel, sizeof(choices_midi_channel) / sizeof(choices_midi_channel[0])},
    {MS_FIELD_MIXER_MIDI_BUTTON_TYPE, MS_MIXER, "midi_button_type", 21, 0, choices_midi_button, sizeof(choices_midi_button) / sizeof(choices_midi_button[0])},
    {MS_FIELD_MIXER_DISPLAY_BRIGHTNESS, MS_MIXER, "display_brightness", 22, 1, choices_mixer_display, sizeof(choices_mixer_display) / sizeof(choices_mixer_display[0])},
    {MS_FIELD_MIXER_INDICATOR_BRIGHTNESS, MS_MIXER, "indicator_brightness", 23, 1, choices_indicator_brightness, sizeof(choices_indicator_brightness) / sizeof(choices_indicator_brightness[0])},
    {MS_FIELD_MIXER_CHANNEL_FADER_CURVE_LONG, MS_MIXER, "channel_fader_curve_long", 24, 0, choices_long_curve, sizeof(choices_long_curve) / sizeof(choices_long_curve[0])},
    {MS_FIELD_DEVICE_OVERVIEW_WAVEFORM_TYPE, MS_DEVICE, "overview_waveform_type", 9, 1, choices_overview, sizeof(choices_overview) / sizeof(choices_overview[0])},
    {MS_FIELD_DEVICE_WAVEFORM_COLOR, MS_DEVICE, "waveform_color", 10, 1, choices_waveform_color, sizeof(choices_waveform_color) / sizeof(choices_waveform_color[0])},
    {MS_FIELD_DEVICE_KEY_DISPLAY_FORMAT, MS_DEVICE, "key_display_format", 12, 1, choices_key_display, sizeof(choices_key_display) / sizeof(choices_key_display[0])},
    {MS_FIELD_DEVICE_WAVEFORM_CURRENT_POSITION, MS_DEVICE, "waveform_current_position", 13, 1, choices_waveform_position, sizeof(choices_waveform_position) / sizeof(choices_waveform_position[0])},
};

const struct ms_field_info *ms_field_info(enum ms_field field) {
    return (unsigned)field < MS_FIELD_COUNT ? &fields[field] : NULL;
}
const char *ms_choice_label(enum ms_field field, unsigned value) {
    const struct ms_field_info *f = ms_field_info(field); size_t i;
    if (f) for (i = 0; i < f->choice_count; ++i)
        if (f->choices[i].value == value) return f->choices[i].label;
    return NULL;
}

int ms_get(const struct ms_settings *s, enum ms_field field, unsigned *value) {
    const struct ms_field_info *f = ms_field_info(field);
    if (!s || !value || !f || s->kind != f->kind) return MS_ERR_ARGUMENT;
    switch (field) {
    case MS_FIELD_PLAYER_ON_AIR_DISPLAY: *value = (unsigned)s->values.player.on_air_display; break;
    case MS_FIELD_PLAYER_LCD_BRIGHTNESS: *value = (unsigned)s->values.player.lcd_brightness; break;
    case MS_FIELD_PLAYER_QUANTIZE: *value = (unsigned)s->values.player.quantize; break;
    case MS_FIELD_PLAYER_AUTO_CUE_LEVEL: *value = (unsigned)s->values.player.auto_cue_level; break;
    case MS_FIELD_PLAYER_LANGUAGE: *value = (unsigned)s->values.player.language; break;
    case MS_FIELD_PLAYER_JOG_RING_BRIGHTNESS: *value = (unsigned)s->values.player.jog_ring_brightness; break;
    case MS_FIELD_PLAYER_JOG_RING_INDICATOR: *value = (unsigned)s->values.player.jog_ring_indicator; break;
    case MS_FIELD_PLAYER_SLIP_FLASHING: *value = (unsigned)s->values.player.slip_flashing; break;
    case MS_FIELD_PLAYER_DISC_SLOT_ILLUMINATION: *value = (unsigned)s->values.player.disc_slot_illumination; break;
    case MS_FIELD_PLAYER_EJECT_LOCK: *value = (unsigned)s->values.player.eject_lock; break;
    case MS_FIELD_PLAYER_SYNC: *value = (unsigned)s->values.player.sync; break;
    case MS_FIELD_PLAYER_PLAY_MODE: *value = (unsigned)s->values.player.play_mode; break;
    case MS_FIELD_PLAYER_QUANTIZE_BEAT_VALUE: *value = (unsigned)s->values.player.quantize_beat_value; break;
    case MS_FIELD_PLAYER_HOTCUE_AUTOLOAD: *value = (unsigned)s->values.player.hotcue_autoload; break;
    case MS_FIELD_PLAYER_HOTCUE_COLOR: *value = (unsigned)s->values.player.hotcue_color; break;
    case MS_FIELD_PLAYER_NEEDLE_LOCK: *value = (unsigned)s->values.player.needle_lock; break;
    case MS_FIELD_PLAYER_TIME_MODE: *value = (unsigned)s->values.player.time_mode; break;
    case MS_FIELD_PLAYER_JOG_MODE: *value = (unsigned)s->values.player.jog_mode; break;
    case MS_FIELD_PLAYER_AUTO_CUE: *value = (unsigned)s->values.player.auto_cue; break;
    case MS_FIELD_PLAYER_MASTER_TEMPO: *value = (unsigned)s->values.player.master_tempo; break;
    case MS_FIELD_PLAYER_TEMPO_RANGE: *value = (unsigned)s->values.player.tempo_range; break;
    case MS_FIELD_PLAYER_PHASE_METER: *value = (unsigned)s->values.player.phase_meter; break;
    case MS_FIELD_PLAYER2_VINYL_SPEED_ADJUST: *value = (unsigned)s->values.player2.vinyl_speed_adjust; break;
    case MS_FIELD_PLAYER2_JOG_DISPLAY_MODE: *value = (unsigned)s->values.player2.jog_display_mode; break;
    case MS_FIELD_PLAYER2_PAD_BUTTON_BRIGHTNESS: *value = (unsigned)s->values.player2.pad_button_brightness; break;
    case MS_FIELD_PLAYER2_JOG_LCD_BRIGHTNESS: *value = (unsigned)s->values.player2.jog_lcd_brightness; break;
    case MS_FIELD_PLAYER2_WAVEFORM_DIVISIONS: *value = (unsigned)s->values.player2.waveform_divisions; break;
    case MS_FIELD_PLAYER2_WAVEFORM: *value = (unsigned)s->values.player2.waveform; break;
    case MS_FIELD_PLAYER2_BEAT_JUMP_BEAT_VALUE: *value = (unsigned)s->values.player2.beat_jump_beat_value; break;
    case MS_FIELD_MIXER_CHANNEL_FADER_CURVE: *value = (unsigned)s->values.mixer.channel_fader_curve; break;
    case MS_FIELD_MIXER_CROSSFADER_CURVE: *value = (unsigned)s->values.mixer.crossfader_curve; break;
    case MS_FIELD_MIXER_HEADPHONES_PRE_EQ: *value = (unsigned)s->values.mixer.headphones_pre_eq; break;
    case MS_FIELD_MIXER_HEADPHONES_MONO_SPLIT: *value = (unsigned)s->values.mixer.headphones_mono_split; break;
    case MS_FIELD_MIXER_BEAT_FX_QUANTIZE: *value = (unsigned)s->values.mixer.beat_fx_quantize; break;
    case MS_FIELD_MIXER_MIC_LOW_CUT: *value = (unsigned)s->values.mixer.mic_low_cut; break;
    case MS_FIELD_MIXER_TALK_OVER_MODE: *value = (unsigned)s->values.mixer.talk_over_mode; break;
    case MS_FIELD_MIXER_TALK_OVER_LEVEL: *value = (unsigned)s->values.mixer.talk_over_level; break;
    case MS_FIELD_MIXER_MIDI_CHANNEL: *value = (unsigned)s->values.mixer.midi_channel; break;
    case MS_FIELD_MIXER_MIDI_BUTTON_TYPE: *value = (unsigned)s->values.mixer.midi_button_type; break;
    case MS_FIELD_MIXER_DISPLAY_BRIGHTNESS: *value = (unsigned)s->values.mixer.display_brightness; break;
    case MS_FIELD_MIXER_INDICATOR_BRIGHTNESS: *value = (unsigned)s->values.mixer.indicator_brightness; break;
    case MS_FIELD_MIXER_CHANNEL_FADER_CURVE_LONG: *value = (unsigned)s->values.mixer.channel_fader_curve_long; break;
    case MS_FIELD_DEVICE_OVERVIEW_WAVEFORM_TYPE: *value = (unsigned)s->values.device.overview_waveform_type; break;
    case MS_FIELD_DEVICE_WAVEFORM_COLOR: *value = (unsigned)s->values.device.waveform_color; break;
    case MS_FIELD_DEVICE_KEY_DISPLAY_FORMAT: *value = (unsigned)s->values.device.key_display_format; break;
    case MS_FIELD_DEVICE_WAVEFORM_CURRENT_POSITION: *value = (unsigned)s->values.device.waveform_current_position; break;
    default: return MS_ERR_ARGUMENT;
    }
    return MS_OK;
}

static void assign(struct ms_settings *s, enum ms_field field, unsigned value) {
    switch (field) {
    case MS_FIELD_PLAYER_ON_AIR_DISPLAY: s->values.player.on_air_display = (enum ms_toggle)value; break;
    case MS_FIELD_PLAYER_LCD_BRIGHTNESS: s->values.player.lcd_brightness = (enum ms_brightness)value; break;
    case MS_FIELD_PLAYER_QUANTIZE: s->values.player.quantize = (enum ms_toggle)value; break;
    case MS_FIELD_PLAYER_AUTO_CUE_LEVEL: s->values.player.auto_cue_level = (enum ms_auto_cue_level)value; break;
    case MS_FIELD_PLAYER_LANGUAGE: s->values.player.language = (enum ms_language)value; break;
    case MS_FIELD_PLAYER_JOG_RING_BRIGHTNESS: s->values.player.jog_ring_brightness = (enum ms_illumination)value; break;
    case MS_FIELD_PLAYER_JOG_RING_INDICATOR: s->values.player.jog_ring_indicator = (enum ms_toggle)value; break;
    case MS_FIELD_PLAYER_SLIP_FLASHING: s->values.player.slip_flashing = (enum ms_toggle)value; break;
    case MS_FIELD_PLAYER_DISC_SLOT_ILLUMINATION: s->values.player.disc_slot_illumination = (enum ms_illumination)value; break;
    case MS_FIELD_PLAYER_EJECT_LOCK: s->values.player.eject_lock = (enum ms_lock)value; break;
    case MS_FIELD_PLAYER_SYNC: s->values.player.sync = (enum ms_toggle)value; break;
    case MS_FIELD_PLAYER_PLAY_MODE: s->values.player.play_mode = (enum ms_play_mode)value; break;
    case MS_FIELD_PLAYER_QUANTIZE_BEAT_VALUE: s->values.player.quantize_beat_value = (enum ms_quantize_beats)value; break;
    case MS_FIELD_PLAYER_HOTCUE_AUTOLOAD: s->values.player.hotcue_autoload = (enum ms_hotcue_autoload)value; break;
    case MS_FIELD_PLAYER_HOTCUE_COLOR: s->values.player.hotcue_color = (enum ms_toggle)value; break;
    case MS_FIELD_PLAYER_NEEDLE_LOCK: s->values.player.needle_lock = (enum ms_lock)value; break;
    case MS_FIELD_PLAYER_TIME_MODE: s->values.player.time_mode = (enum ms_time_mode)value; break;
    case MS_FIELD_PLAYER_JOG_MODE: s->values.player.jog_mode = (enum ms_jog_mode)value; break;
    case MS_FIELD_PLAYER_AUTO_CUE: s->values.player.auto_cue = (enum ms_toggle)value; break;
    case MS_FIELD_PLAYER_MASTER_TEMPO: s->values.player.master_tempo = (enum ms_toggle)value; break;
    case MS_FIELD_PLAYER_TEMPO_RANGE: s->values.player.tempo_range = (enum ms_tempo_range)value; break;
    case MS_FIELD_PLAYER_PHASE_METER: s->values.player.phase_meter = (enum ms_phase_meter)value; break;
    case MS_FIELD_PLAYER2_VINYL_SPEED_ADJUST: s->values.player2.vinyl_speed_adjust = (enum ms_vinyl_speed)value; break;
    case MS_FIELD_PLAYER2_JOG_DISPLAY_MODE: s->values.player2.jog_display_mode = (enum ms_jog_display)value; break;
    case MS_FIELD_PLAYER2_PAD_BUTTON_BRIGHTNESS: s->values.player2.pad_button_brightness = (enum ms_pad_brightness)value; break;
    case MS_FIELD_PLAYER2_JOG_LCD_BRIGHTNESS: s->values.player2.jog_lcd_brightness = (enum ms_brightness)value; break;
    case MS_FIELD_PLAYER2_WAVEFORM_DIVISIONS: s->values.player2.waveform_divisions = (enum ms_waveform_divisions)value; break;
    case MS_FIELD_PLAYER2_WAVEFORM: s->values.player2.waveform = (enum ms_waveform)value; break;
    case MS_FIELD_PLAYER2_BEAT_JUMP_BEAT_VALUE: s->values.player2.beat_jump_beat_value = (enum ms_beat_jump)value; break;
    case MS_FIELD_MIXER_CHANNEL_FADER_CURVE: s->values.mixer.channel_fader_curve = (enum ms_channel_curve)value; break;
    case MS_FIELD_MIXER_CROSSFADER_CURVE: s->values.mixer.crossfader_curve = (enum ms_cross_curve)value; break;
    case MS_FIELD_MIXER_HEADPHONES_PRE_EQ: s->values.mixer.headphones_pre_eq = (enum ms_headphones_eq)value; break;
    case MS_FIELD_MIXER_HEADPHONES_MONO_SPLIT: s->values.mixer.headphones_mono_split = (enum ms_headphones_split)value; break;
    case MS_FIELD_MIXER_BEAT_FX_QUANTIZE: s->values.mixer.beat_fx_quantize = (enum ms_toggle)value; break;
    case MS_FIELD_MIXER_MIC_LOW_CUT: s->values.mixer.mic_low_cut = (enum ms_toggle)value; break;
    case MS_FIELD_MIXER_TALK_OVER_MODE: s->values.mixer.talk_over_mode = (enum ms_talk_mode)value; break;
    case MS_FIELD_MIXER_TALK_OVER_LEVEL: s->values.mixer.talk_over_level = (enum ms_talk_level)value; break;
    case MS_FIELD_MIXER_MIDI_CHANNEL: s->values.mixer.midi_channel = (enum ms_midi_channel)value; break;
    case MS_FIELD_MIXER_MIDI_BUTTON_TYPE: s->values.mixer.midi_button_type = (enum ms_midi_button)value; break;
    case MS_FIELD_MIXER_DISPLAY_BRIGHTNESS: s->values.mixer.display_brightness = (enum ms_mixer_display)value; break;
    case MS_FIELD_MIXER_INDICATOR_BRIGHTNESS: s->values.mixer.indicator_brightness = (enum ms_indicator_brightness)value; break;
    case MS_FIELD_MIXER_CHANNEL_FADER_CURVE_LONG: s->values.mixer.channel_fader_curve_long = (enum ms_long_curve)value; break;
    case MS_FIELD_DEVICE_OVERVIEW_WAVEFORM_TYPE: s->values.device.overview_waveform_type = (enum ms_overview)value; break;
    case MS_FIELD_DEVICE_WAVEFORM_COLOR: s->values.device.waveform_color = (enum ms_waveform_color)value; break;
    case MS_FIELD_DEVICE_KEY_DISPLAY_FORMAT: s->values.device.key_display_format = (enum ms_key_display)value; break;
    case MS_FIELD_DEVICE_WAVEFORM_CURRENT_POSITION: s->values.device.waveform_current_position = (enum ms_waveform_position)value; break;
    default: break;
    }
}

int ms_set(struct ms_settings *s, enum ms_field field, unsigned value) {
    const struct ms_field_info *f = ms_field_info(field);
    if (!s || !f || s->kind != f->kind) return MS_ERR_ARGUMENT;
    if (!ms_choice_label(field, value)) return MS_ERR_VALUE;
    assign(s, field, value); return MS_OK;
}

static size_t body_size(enum ms_kind kind) {
    static const size_t sizes[] = {40, 40, 52, 32};
    return (unsigned)kind < MS_KIND_COUNT ? sizes[kind] : 0;
}

const char *ms_filename(enum ms_kind kind) {
    static const char *const names[] = {
        "MYSETTING.DAT", "MYSETTING2.DAT", "DJMMYSETTING.DAT", "DEVSETTING.DAT"
    };
    return (unsigned)kind < MS_KIND_COUNT ? names[kind] : NULL;
}

const char *ms_error_string(int error) {
    switch (error) {
    case MS_OK: return "Success";
    case MS_ERR_ARGUMENT: return "Invalid argument or field for this file kind";
    case MS_ERR_SIZE: return "Unexpected file length or insufficient output capacity";
    case MS_ERR_HEADER: return "Invalid header lengths or unterminated header text";
    case MS_ERR_CHECKSUM: return "CRC-16/XMODEM checksum mismatch";
    case MS_ERR_VALUE: return "Unsupported edited choice";
    case MS_ERR_IO: return "File operation failed; errno describes the cause";
    case MS_ERR_READ_ONLY: return "Filesystem is mounted read-only";
    case MS_ERR_PATH: return "Invalid path or nonregular destination";
    case MS_ERR_DURABILITY: return "File replaced but directory sync failed";
    default: return "Unknown error";
    }
}

uint16_t ms_crc16(const void *bytes, size_t size) {
    const uint8_t *p = bytes;
    uint16_t crc = 0;
    size_t i;
    unsigned bit;
    for (i = 0; i < size; ++i) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t checksum(enum ms_kind kind, const uint8_t *p) {
    size_t start = kind == MS_MIXER ? 0 : MS_HEADER_SIZE;
    return ms_crc16(p + start, MS_HEADER_SIZE + body_size(kind) - start);
}

static int framing(enum ms_kind kind, const uint8_t *p, size_t size) {
    size_t body = body_size(kind);
    if (!body) return MS_ERR_ARGUMENT;
    if (size != MS_HEADER_SIZE + body + 4) return MS_ERR_SIZE;
    if (le32(p) != 96 || le32(p + 100) != body ||
        !memchr(p + 4, 0, 32) || !memchr(p + 36, 0, 32) ||
        !memchr(p + 68, 0, 32)) return MS_ERR_HEADER;
    if (checksum(kind, p) != (unsigned)(p[size - 4] | (p[size - 3] << 8)))
        return MS_ERR_CHECKSUM;
    return MS_OK;
}

int ms_parse(enum ms_kind kind, const void *bytes, size_t size, struct ms_settings *out) {
    struct ms_settings result;
    size_t i;
    int rc;
    if (!bytes || !out) return MS_ERR_ARGUMENT;
    rc = framing(kind, bytes, size);
    if (rc != MS_OK) return rc;
    memset(&result, 0, sizeof(result));
    result.kind = kind; result.size = size;
    memcpy(result.raw, bytes, size);
    for (i = 0; i < MS_FIELD_COUNT; ++i)
        if (fields[i].kind == kind)
            assign(&result, (enum ms_field)i, result.raw[MS_HEADER_SIZE + fields[i].body_offset]);
    *out = result;
    return MS_OK;
}

int ms_validate(const struct ms_settings *s) {
    size_t i;
    int rc;
    unsigned value;
    if (!s) return MS_ERR_ARGUMENT;
    rc = framing(s->kind, s->raw, s->size);
    if (rc != MS_OK) return rc;
    for (i = 0; i < MS_FIELD_COUNT; ++i) {
        if (fields[i].kind != s->kind) continue;
        ms_get(s, (enum ms_field)i, &value);
        if (value > 255 || (!ms_choice_label((enum ms_field)i, value) &&
            value != s->raw[MS_HEADER_SIZE + fields[i].body_offset])) return MS_ERR_VALUE;
    }
    return MS_OK;
}

int ms_serialize(const struct ms_settings *s, void *bytes, size_t capacity, size_t *size) {
    uint8_t result[MS_MAX_FILE_SIZE];
    uint16_t crc;
    unsigned value;
    size_t i;
    int rc;
    if (!bytes || !size) return MS_ERR_ARGUMENT;
    rc = ms_validate(s);
    if (rc != MS_OK) return rc;
    if (capacity < s->size) return MS_ERR_SIZE;
    memcpy(result, s->raw, s->size);
    for (i = 0; i < MS_FIELD_COUNT; ++i) {
        if (fields[i].kind != s->kind) continue;
        ms_get(s, (enum ms_field)i, &value);
        result[MS_HEADER_SIZE + fields[i].body_offset] = (uint8_t)value;
    }
    crc = checksum(s->kind, result);
    result[s->size - 4] = (uint8_t)crc;
    result[s->size - 3] = (uint8_t)(crc >> 8);
    memmove(bytes, result, s->size);
    *size = s->size;
    return MS_OK;
}

static int io_error(void) {
    return errno == EROFS ? MS_ERR_READ_ONLY : MS_ERR_IO;
}

int ms_read_file(enum ms_kind kind, const char *path, struct ms_settings *out) {
    uint8_t bytes[MS_MAX_FILE_SIZE + 1];
    size_t used = 0;
    int fd, rc, saved;
    ssize_t count;
    if (!path || !out || !body_size(kind)) return MS_ERR_ARGUMENT;
    fd = open(path, O_RDONLY);
    if (fd < 0) return io_error();
    while (used < sizeof(bytes)) {
        count = read(fd, bytes + used, sizeof(bytes) - used);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            rc = io_error(); saved = errno; close(fd); errno = saved; return rc;
        }
        if (count == 0) break;
        used += (size_t)count;
    }
    if (close(fd) != 0) return io_error();
    return ms_parse(kind, bytes, used, out);
}

int ms_write_atomic(const char *path, const struct ms_settings *s) {
    uint8_t bytes[MS_MAX_FILE_SIZE];
    char *parent, *temporary;
    const char *slash;
    size_t size, parent_size, used = 0;
    struct statvfs mount_info;
    struct stat info;
    mode_t mode = 0600;
    int fd = -1, directory = -1, rc, saved;
    ssize_t count;
    if (!path || !*path) return MS_ERR_ARGUMENT;
    rc = ms_serialize(s, bytes, sizeof(bytes), &size);
    if (rc != MS_OK) return rc;
    slash = strrchr(path, '/');
    if (slash && !slash[1]) return MS_ERR_PATH;
    if (!strcmp(slash ? slash + 1 : path, ".") ||
        !strcmp(slash ? slash + 1 : path, "..")) return MS_ERR_PATH;
    parent_size = slash ? (size_t)(slash - path) : 1;
    if (slash == path) parent_size = 1;
    parent = malloc(parent_size + 1);
    temporary = malloc(parent_size + 24);
    if (!parent || !temporary) {
        free(parent); free(temporary); errno = ENOMEM; return MS_ERR_IO;
    }
    memcpy(parent, slash ? path : ".", parent_size);
    parent[parent_size] = 0;
    snprintf(temporary, parent_size + 24, "%s/.my-settings-XXXXXX", parent);
    if (statvfs(parent, &mount_info) != 0) { rc = io_error(); goto done; }
    if (mount_info.f_flag & ST_RDONLY) { errno = EROFS; rc = MS_ERR_READ_ONLY; goto done; }
    if (lstat(path, &info) == 0) {
        if (!S_ISREG(info.st_mode)) { rc = MS_ERR_PATH; goto done; }
        mode = info.st_mode & 0777;
    } else if (errno != ENOENT) { rc = io_error(); goto done; }
    directory = open(parent, O_RDONLY | O_DIRECTORY);
    if (directory < 0) { rc = io_error(); goto done; }
    fd = mkstemp(temporary);
    if (fd < 0) { rc = io_error(); goto done; }
    if (fchmod(fd, mode) != 0) { rc = io_error(); goto cleanup; }
    while (used < size) {
        count = write(fd, bytes + used, size - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            if (count == 0) errno = EIO;
            rc = io_error(); goto cleanup;
        }
        used += (size_t)count;
    }
    if (fsync(fd) != 0) { rc = io_error(); goto cleanup; }
    if (close(fd) != 0) { fd = -1; rc = io_error(); goto cleanup; }
    fd = -1;
    if (rename(temporary, path) != 0) { rc = io_error(); goto cleanup; }
    rc = fsync(directory) == 0 ? MS_OK : MS_ERR_DURABILITY;
    goto done;
cleanup:
    saved = errno;
    if (fd >= 0) close(fd);
    unlink(temporary);
    errno = saved;
done:
    saved = errno;
    if (directory >= 0) close(directory);
    free(parent); free(temporary);
    errno = saved;
    return rc;
}
