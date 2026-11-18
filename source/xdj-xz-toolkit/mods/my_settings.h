#ifndef MY_SETTINGS_H
#define MY_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MS_MAX_FILE_SIZE 160u
#define MS_HEADER_SIZE 104u

enum ms_kind { MS_PLAYER, MS_PLAYER2, MS_MIXER, MS_DEVICE, MS_KIND_COUNT };
enum ms_error {
    MS_OK = 0, MS_ERR_ARGUMENT = -1, MS_ERR_SIZE = -2,
    MS_ERR_HEADER = -3, MS_ERR_CHECKSUM = -4, MS_ERR_VALUE = -5,
    MS_ERR_IO = -6, MS_ERR_READ_ONLY = -7, MS_ERR_PATH = -8,
    MS_ERR_DURABILITY = -9
};

enum ms_toggle {
    MS_TOGGLE_OFF = 0x80,
    MS_TOGGLE_ON = 0x81,
};
enum ms_brightness {
    MS_BRIGHTNESS_1 = 0x81,
    MS_BRIGHTNESS_2 = 0x82,
    MS_BRIGHTNESS_3 = 0x83,
    MS_BRIGHTNESS_4 = 0x84,
    MS_BRIGHTNESS_5 = 0x85,
};
enum ms_auto_cue_level {
    MS_AUTO_CUE_LEVEL_MINUS_36_DB = 0x80,
    MS_AUTO_CUE_LEVEL_MINUS_42_DB = 0x81,
    MS_AUTO_CUE_LEVEL_MINUS_48_DB = 0x82,
    MS_AUTO_CUE_LEVEL_MINUS_54_DB = 0x83,
    MS_AUTO_CUE_LEVEL_MINUS_60_DB = 0x84,
    MS_AUTO_CUE_LEVEL_MINUS_66_DB = 0x85,
    MS_AUTO_CUE_LEVEL_MINUS_72_DB = 0x86,
    MS_AUTO_CUE_LEVEL_MINUS_78_DB = 0x87,
    MS_AUTO_CUE_LEVEL_MEMORY = 0x88,
};
enum ms_language {
    MS_LANGUAGE_ENGLISH = 0x81,
    MS_LANGUAGE_FRENCH = 0x82,
    MS_LANGUAGE_GERMAN = 0x83,
    MS_LANGUAGE_ITALIAN = 0x84,
    MS_LANGUAGE_DUTCH = 0x85,
    MS_LANGUAGE_SPANISH = 0x86,
    MS_LANGUAGE_RUSSIAN = 0x87,
    MS_LANGUAGE_KOREAN = 0x88,
    MS_LANGUAGE_CHINESE_SIMPLIFIED = 0x89,
    MS_LANGUAGE_CHINESE_TRADITIONAL = 0x8a,
    MS_LANGUAGE_JAPANESE = 0x8b,
    MS_LANGUAGE_PORTUGUESE = 0x8c,
    MS_LANGUAGE_SWEDISH = 0x8d,
    MS_LANGUAGE_CZECH = 0x8e,
    MS_LANGUAGE_HUNGARIAN = 0x8f,
    MS_LANGUAGE_DANISH = 0x90,
    MS_LANGUAGE_GREEK = 0x91,
    MS_LANGUAGE_TURKISH = 0x92,
};
enum ms_illumination {
    MS_ILLUMINATION_OFF = 0x80,
    MS_ILLUMINATION_DARK = 0x81,
    MS_ILLUMINATION_BRIGHT = 0x82,
};
enum ms_lock {
    MS_LOCK_UNLOCK = 0x80,
    MS_LOCK_LOCK = 0x81,
};
enum ms_play_mode {
    MS_PLAY_MODE_CONTINUE_ON = 0x80,
    MS_PLAY_MODE_SINGLE_OFF = 0x81,
};
enum ms_quantize_beats {
    MS_QUANTIZE_BEATS_1_BEAT = 0x80,
    MS_QUANTIZE_BEATS_1_2_BEAT = 0x81,
    MS_QUANTIZE_BEATS_1_4_BEAT = 0x82,
    MS_QUANTIZE_BEATS_1_8_BEAT = 0x83,
};
enum ms_hotcue_autoload {
    MS_HOTCUE_AUTOLOAD_OFF = 0x80,
    MS_HOTCUE_AUTOLOAD_ON = 0x81,
    MS_HOTCUE_AUTOLOAD_LIBRARY_SETTING = 0x82,
};
enum ms_time_mode {
    MS_TIME_MODE_ELAPSED = 0x80,
    MS_TIME_MODE_REMAIN = 0x81,
};
enum ms_jog_mode {
    MS_JOG_MODE_DIGITAL = 0x80,
    MS_JOG_MODE_VINYL = 0x81,
};
enum ms_tempo_range {
    MS_TEMPO_RANGE_6_PERCENT = 0x80,
    MS_TEMPO_RANGE_10_PERCENT = 0x81,
    MS_TEMPO_RANGE_16_PERCENT = 0x82,
    MS_TEMPO_RANGE_WIDE = 0x83,
};
enum ms_phase_meter {
    MS_PHASE_METER_TYPE_1 = 0x80,
    MS_PHASE_METER_TYPE_2 = 0x81,
};
enum ms_vinyl_speed {
    MS_VINYL_SPEED_TOUCH_AND_RELEASE = 0x80,
    MS_VINYL_SPEED_TOUCH = 0x81,
    MS_VINYL_SPEED_RELEASE = 0x82,
};
enum ms_jog_display {
    MS_JOG_DISPLAY_AUTO = 0x80,
    MS_JOG_DISPLAY_INFO = 0x81,
    MS_JOG_DISPLAY_SIMPLE = 0x82,
    MS_JOG_DISPLAY_ARTWORK = 0x83,
};
enum ms_pad_brightness {
    MS_PAD_BRIGHTNESS_1 = 0x81,
    MS_PAD_BRIGHTNESS_2 = 0x82,
    MS_PAD_BRIGHTNESS_3 = 0x83,
    MS_PAD_BRIGHTNESS_4 = 0x84,
};
enum ms_waveform_divisions {
    MS_WAVEFORM_DIVISIONS_TIME_SCALE = 0x80,
    MS_WAVEFORM_DIVISIONS_PHRASE = 0x81,
};
enum ms_waveform {
    MS_WAVEFORM_WAVEFORM = 0x80,
    MS_WAVEFORM_PHASE_METER = 0x81,
};
enum ms_beat_jump {
    MS_BEAT_JUMP_1_2_BEAT = 0x80,
    MS_BEAT_JUMP_1_BEAT = 0x81,
    MS_BEAT_JUMP_2_BEATS = 0x82,
    MS_BEAT_JUMP_4_BEATS = 0x83,
    MS_BEAT_JUMP_8_BEATS = 0x84,
    MS_BEAT_JUMP_16_BEATS = 0x85,
    MS_BEAT_JUMP_32_BEATS = 0x86,
    MS_BEAT_JUMP_64_BEATS = 0x87,
};
enum ms_channel_curve {
    MS_CHANNEL_CURVE_STEEP_TOP = 0x80,
    MS_CHANNEL_CURVE_LINEAR = 0x81,
    MS_CHANNEL_CURVE_STEEP_BOTTOM = 0x82,
};
enum ms_cross_curve {
    MS_CROSS_CURVE_CONSTANT_POWER = 0x80,
    MS_CROSS_CURVE_SLOW_CUT = 0x81,
    MS_CROSS_CURVE_FAST_CUT = 0x82,
};
enum ms_headphones_eq {
    MS_HEADPHONES_EQ_POST_EQ = 0x80,
    MS_HEADPHONES_EQ_PRE_EQ = 0x81,
};
enum ms_headphones_split {
    MS_HEADPHONES_SPLIT_STEREO = 0x80,
    MS_HEADPHONES_SPLIT_MONO_SPLIT = 0x81,
};
enum ms_talk_mode {
    MS_TALK_MODE_ADVANCED = 0x80,
    MS_TALK_MODE_NORMAL = 0x81,
};
enum ms_talk_level {
    MS_TALK_LEVEL_MINUS_24_DB = 0x80,
    MS_TALK_LEVEL_MINUS_18_DB = 0x81,
    MS_TALK_LEVEL_MINUS_12_DB = 0x82,
    MS_TALK_LEVEL_MINUS_6_DB = 0x83,
};
enum ms_midi_channel {
    MS_MIDI_CHANNEL_1 = 0x80,
    MS_MIDI_CHANNEL_2 = 0x81,
    MS_MIDI_CHANNEL_3 = 0x82,
    MS_MIDI_CHANNEL_4 = 0x83,
    MS_MIDI_CHANNEL_5 = 0x84,
    MS_MIDI_CHANNEL_6 = 0x85,
    MS_MIDI_CHANNEL_7 = 0x86,
    MS_MIDI_CHANNEL_8 = 0x87,
    MS_MIDI_CHANNEL_9 = 0x88,
    MS_MIDI_CHANNEL_10 = 0x89,
    MS_MIDI_CHANNEL_11 = 0x8a,
    MS_MIDI_CHANNEL_12 = 0x8b,
    MS_MIDI_CHANNEL_13 = 0x8c,
    MS_MIDI_CHANNEL_14 = 0x8d,
    MS_MIDI_CHANNEL_15 = 0x8e,
    MS_MIDI_CHANNEL_16 = 0x8f,
};
enum ms_midi_button {
    MS_MIDI_BUTTON_TOGGLE = 0x80,
    MS_MIDI_BUTTON_TRIGGER = 0x81,
};
enum ms_mixer_display {
    MS_MIXER_DISPLAY_WHITE = 0x80,
    MS_MIXER_DISPLAY_1 = 0x81,
    MS_MIXER_DISPLAY_2 = 0x82,
    MS_MIXER_DISPLAY_3 = 0x83,
    MS_MIXER_DISPLAY_4 = 0x84,
    MS_MIXER_DISPLAY_5 = 0x85,
};
enum ms_indicator_brightness {
    MS_INDICATOR_BRIGHTNESS_1 = 0x80,
    MS_INDICATOR_BRIGHTNESS_2 = 0x81,
    MS_INDICATOR_BRIGHTNESS_3 = 0x82,
};
enum ms_long_curve {
    MS_LONG_CURVE_EXPONENTIAL = 0x80,
    MS_LONG_CURVE_SMOOTH = 0x81,
    MS_LONG_CURVE_LINEAR = 0x82,
};
enum ms_overview {
    MS_OVERVIEW_HALF_WAVEFORM = 0x1,
    MS_OVERVIEW_FULL_WAVEFORM = 0x2,
};
enum ms_waveform_color {
    MS_WAVEFORM_COLOR_BLUE = 0x1,
    MS_WAVEFORM_COLOR_RGB = 0x3,
    MS_WAVEFORM_COLOR_3_BAND = 0x4,
};
enum ms_key_display {
    MS_KEY_DISPLAY_CLASSIC = 0x1,
    MS_KEY_DISPLAY_ALPHANUMERIC = 0x2,
};
enum ms_waveform_position {
    MS_WAVEFORM_POSITION_CENTER = 0x1,
    MS_WAVEFORM_POSITION_LEFT = 0x2,
};
enum ms_field {
    MS_FIELD_PLAYER_ON_AIR_DISPLAY,
    MS_FIELD_PLAYER_LCD_BRIGHTNESS,
    MS_FIELD_PLAYER_QUANTIZE,
    MS_FIELD_PLAYER_AUTO_CUE_LEVEL,
    MS_FIELD_PLAYER_LANGUAGE,
    MS_FIELD_PLAYER_JOG_RING_BRIGHTNESS,
    MS_FIELD_PLAYER_JOG_RING_INDICATOR,
    MS_FIELD_PLAYER_SLIP_FLASHING,
    MS_FIELD_PLAYER_DISC_SLOT_ILLUMINATION,
    MS_FIELD_PLAYER_EJECT_LOCK,
    MS_FIELD_PLAYER_SYNC,
    MS_FIELD_PLAYER_PLAY_MODE,
    MS_FIELD_PLAYER_QUANTIZE_BEAT_VALUE,
    MS_FIELD_PLAYER_HOTCUE_AUTOLOAD,
    MS_FIELD_PLAYER_HOTCUE_COLOR,
    MS_FIELD_PLAYER_NEEDLE_LOCK,
    MS_FIELD_PLAYER_TIME_MODE,
    MS_FIELD_PLAYER_JOG_MODE,
    MS_FIELD_PLAYER_AUTO_CUE,
    MS_FIELD_PLAYER_MASTER_TEMPO,
    MS_FIELD_PLAYER_TEMPO_RANGE,
    MS_FIELD_PLAYER_PHASE_METER,
    MS_FIELD_PLAYER2_VINYL_SPEED_ADJUST,
    MS_FIELD_PLAYER2_JOG_DISPLAY_MODE,
    MS_FIELD_PLAYER2_PAD_BUTTON_BRIGHTNESS,
    MS_FIELD_PLAYER2_JOG_LCD_BRIGHTNESS,
    MS_FIELD_PLAYER2_WAVEFORM_DIVISIONS,
    MS_FIELD_PLAYER2_WAVEFORM,
    MS_FIELD_PLAYER2_BEAT_JUMP_BEAT_VALUE,
    MS_FIELD_MIXER_CHANNEL_FADER_CURVE,
    MS_FIELD_MIXER_CROSSFADER_CURVE,
    MS_FIELD_MIXER_HEADPHONES_PRE_EQ,
    MS_FIELD_MIXER_HEADPHONES_MONO_SPLIT,
    MS_FIELD_MIXER_BEAT_FX_QUANTIZE,
    MS_FIELD_MIXER_MIC_LOW_CUT,
    MS_FIELD_MIXER_TALK_OVER_MODE,
    MS_FIELD_MIXER_TALK_OVER_LEVEL,
    MS_FIELD_MIXER_MIDI_CHANNEL,
    MS_FIELD_MIXER_MIDI_BUTTON_TYPE,
    MS_FIELD_MIXER_DISPLAY_BRIGHTNESS,
    MS_FIELD_MIXER_INDICATOR_BRIGHTNESS,
    MS_FIELD_MIXER_CHANNEL_FADER_CURVE_LONG,
    MS_FIELD_DEVICE_OVERVIEW_WAVEFORM_TYPE,
    MS_FIELD_DEVICE_WAVEFORM_COLOR,
    MS_FIELD_DEVICE_KEY_DISPLAY_FORMAT,
    MS_FIELD_DEVICE_WAVEFORM_CURRENT_POSITION,
    MS_FIELD_COUNT
};

struct ms_choice { unsigned value; const char *label; };
struct ms_field_info {
    enum ms_field id; enum ms_kind kind; const char *name;
    unsigned body_offset; int display;
    const struct ms_choice *choices; size_t choice_count;
};

struct ms_player_values {
    enum ms_toggle on_air_display;
    enum ms_brightness lcd_brightness;
    enum ms_toggle quantize;
    enum ms_auto_cue_level auto_cue_level;
    enum ms_language language;
    enum ms_illumination jog_ring_brightness;
    enum ms_toggle jog_ring_indicator;
    enum ms_toggle slip_flashing;
    enum ms_illumination disc_slot_illumination;
    enum ms_lock eject_lock;
    enum ms_toggle sync;
    enum ms_play_mode play_mode;
    enum ms_quantize_beats quantize_beat_value;
    enum ms_hotcue_autoload hotcue_autoload;
    enum ms_toggle hotcue_color;
    enum ms_lock needle_lock;
    enum ms_time_mode time_mode;
    enum ms_jog_mode jog_mode;
    enum ms_toggle auto_cue;
    enum ms_toggle master_tempo;
    enum ms_tempo_range tempo_range;
    enum ms_phase_meter phase_meter;
};
struct ms_player2_values {
    enum ms_vinyl_speed vinyl_speed_adjust;
    enum ms_jog_display jog_display_mode;
    enum ms_pad_brightness pad_button_brightness;
    enum ms_brightness jog_lcd_brightness;
    enum ms_waveform_divisions waveform_divisions;
    enum ms_waveform waveform;
    enum ms_beat_jump beat_jump_beat_value;
};
struct ms_mixer_values {
    enum ms_channel_curve channel_fader_curve;
    enum ms_cross_curve crossfader_curve;
    enum ms_headphones_eq headphones_pre_eq;
    enum ms_headphones_split headphones_mono_split;
    enum ms_toggle beat_fx_quantize;
    enum ms_toggle mic_low_cut;
    enum ms_talk_mode talk_over_mode;
    enum ms_talk_level talk_over_level;
    enum ms_midi_channel midi_channel;
    enum ms_midi_button midi_button_type;
    enum ms_mixer_display display_brightness;
    enum ms_indicator_brightness indicator_brightness;
    enum ms_long_curve channel_fader_curve_long;
};
struct ms_device_values {
    enum ms_overview overview_waveform_type;
    enum ms_waveform_color waveform_color;
    enum ms_key_display key_display_format;
    enum ms_waveform_position waveform_current_position;
};

/* raw retains the original header, reserved bytes, footer and unknown choices.
 * Treat raw and size as immutable after parse. Edit values directly or use set.
 * Unknown choices are accepted only while unchanged from raw. */
struct ms_settings {
    enum ms_kind kind; size_t size; uint8_t raw[MS_MAX_FILE_SIZE];
    union {
        struct ms_player_values player;
        struct ms_player2_values player2;
        struct ms_mixer_values mixer;
        struct ms_device_values device;
    } values;
};

const char *ms_filename(enum ms_kind kind);
const char *ms_error_string(int error);
const struct ms_field_info *ms_field_info(enum ms_field field);
const char *ms_choice_label(enum ms_field field, unsigned value);
int ms_get(const struct ms_settings *settings, enum ms_field field, unsigned *value);
int ms_set(struct ms_settings *settings, enum ms_field field, unsigned value);
/* Both functions validate framing and CRC. Unknown choices remain lossless. */
int ms_parse(enum ms_kind kind, const void *bytes, size_t size, struct ms_settings *out);
int ms_validate(const struct ms_settings *settings);
int ms_serialize(const struct ms_settings *settings, void *bytes, size_t capacity, size_t *size);
uint16_t ms_crc16(const void *bytes, size_t size);
int ms_read_file(enum ms_kind kind, const char *path, struct ms_settings *out);
/* Caller supplies a trusted directory and serializes competing saves.
 * Never creates directories. Refuses symlink/nonregular destinations.
 * DURABILITY means rename succeeded but directory sync failed; do not blindly retry. */
int ms_write_atomic(const char *path, const struct ms_settings *settings);

#ifdef __cplusplus
}
#endif
#endif
