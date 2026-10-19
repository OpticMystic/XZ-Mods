#ifndef XZ_SHELL_HELP_H
#define XZ_SHELL_HELP_H
/* What each UTILITY and shell display setting does, for the description panel under the values
   (shell_settings.c). Written for a DJ from the player's manual (DRI1625-B pages in the comments), not
   copied from it. Keyed by item id, with the label as the fallback for runtime ids this table does not
   know (the stick's stored MY SETTINGS use stock labels). Choice lines are keyed by the choice's label:
   an exact match, or "*text" for any label containing text. Included by shell_settings.c only.
   Manual pages behind the longer entries: NEEDLE LOCK 63, HOT CUE COLOR 54, HOT CUE AUTO LOAD 55,
   AUTO CUE LEVEL and AUTO CUE 117, JOG DISPLAY MODE 36, VINYL SPEED ADJUST 45, AUTO PLAY MODE and
   AUTO STANDBY 120, EQUALIZER CURVE and MASTER EQUALIZER 97, fader curves and headphones 98, TALK OVER
   121, LANGUAGE and touch calibration 122; every item's range is on pages 123-127. */
#include "shell_settings.h"

struct sh_choice_help { const char *label, *text; };
struct sh_help {
 int id;
 const char *label;
 const char *text;
 struct sh_choice_help choices[6];
};

static const struct sh_help sh_help_table[]={
 /* p.123 DECK category */
 {XZ_SHELL_ITEM_LOAD_LOCK,"LOAD LOCK","Stops a new track loading onto a deck while that deck plays, so a stray LOAD cannot cut the music.",
  {{"LOCK","A playing deck refuses new tracks."},{"UNLOCK","A track loads at any time, even mid-play."}}},
 {XZ_SHELL_ITEM_NEEDLE_LOCK,"NEEDLE LOCK","Controls searching by touching the waveform overview.",
  {{"LOCK","Touch search works while the deck is paused, or while the jog top is held in VINYL mode."},{"UNLOCK","Touch search also works while the deck plays."}}},
 {XZ_SHELL_ITEM_RT_NEEDLE_LOCK,"NEEDLE LOCK","Controls searching by touching the waveform overview.",
  {{"LOCK","Touch search works while the deck is paused, or while the jog top is held in VINYL mode."},{"UNLOCK","Touch search also works while the deck plays."}}},
 {XZ_SHELL_ITEM_QUANTIZE_BEAT,"QUANTIZE BEAT VALUE","The grid that loops, hot cues and beat jumps snap to while QUANTIZE is on.",{{0}}},
 {XZ_SHELL_ITEM_BEAT_JUMP_BEAT,"BEAT JUMP BEAT VALUE","How far one beat jump moves, in beats.",{{0}}},
 {XZ_SHELL_ITEM_HOT_CUE_AUTO_LOAD,"HOT CUE AUTO LOAD","Whether a track's stored hot cues go onto the pads when it loads.",
  {{"ON","Every track brings its hot cues, also when you search within it. A. HOT CUE lights red."},
   {"*SETTING","Only tracks marked for it in the library bring their hot cues. A. HOT CUE lights white."},
   {"OFF","Hot cues stay off the pads when a track loads."}}},
 {XZ_SHELL_ITEM_HOT_CUE_COLOR,"HOT CUE COLOR","Lights each hot cue pad in the colour saved with its cue.",
  {{"ON","Pads take the saved cue colours."},{"OFF","Pads light in one standard colour."}}},
 {XZ_SHELL_ITEM_AUTO_CUE_LEVEL,"AUTO CUE LEVEL","Where Auto Cue sets the cue point when a track loads.",
  {{"MEMORY","The saved memory cue nearest the start becomes the cue point. A. CUE lights white."},
   {"*dB","The cue lands on the first sound louder than this; lower levels catch quieter intros."}}},
 {XZ_SHELL_ITEM_SLIP_FLASHING,"SLIP FLASHING","Flashes the buttons while SLIP is on, as a reminder that the track runs on underneath.",{{0}}},
 /* p.124 */
 {XZ_SHELL_ITEM_ON_AIR_DISPLAY,"ON AIR DISPLAY","Shows ON AIR on a deck while its channel is live on the master output.",{{0}}},
 {XZ_SHELL_ITEM_RT_JOG_DISPLAY_MODE,"JOG DISPLAY MODE","What the display in the jog wheel shows.",
  {{"AUTO","Switches between INFO and SIMPLE as you browse."},{"INFO","Track details."},
   {"SIMPLE","Playing position with SLIP and VINYL."},{"ARTWORK","The track's artwork."}}},
 {XZ_SHELL_ITEM_JOG_RING_BRIGHTNESS,"JOG RING BRIGHTNESS","Brightness of the light around the jog wheel.",{{"OFF","The ring stays dark."}}},
 {XZ_SHELL_ITEM_RT_JOG_RING_INDICATOR,"JOG RING INDICATOR","Flashes the jog ring as the track nears its end.",{{0}}},
 {XZ_SHELL_ITEM_VINYL_SPEED_ADJUST,"VINYL SPEED ADJUST","Which part of a VINYL stop the VINYL SPEED ADJUST knob shapes.",
  {{"*&","Both the slowdown when you press the jog top and the restart when you let go."},
   {"TOUCH","Only the slowdown when you press the jog top."},{"RELEASE","Only the restart when you let go."}}},
 {XZ_SHELL_ITEM_RT_AUTO_PLAY_MODE,"AUTO PLAY MODE","What a deck does at the end of a track.",
  {{"ON","The next track in the same list loads and plays by itself. A. CUE turns off."},{"OFF","The deck stops at the end of each track."}}},
 {XZ_SHELL_ITEM_PLAY_MODE,"PLAY MODE","What a deck does at the end of a track.",
  {{"CONTINUE","The next track in the same list loads and plays by itself."},{"SINGLE","The deck stops at the end of each track."}}},
 /* p.124-126 MIXER category */
 {XZ_SHELL_ITEM_EQ_MODE,"EQUALIZER CURVE","How the channel EQ knobs cut.",
  {{"EQUALIZER","Tone shaping: a band turned down is quieter, not gone."},{"ISOLATOR","A band turned fully down disappears."}}},
 {XZ_SHELL_ITEM_CH_FADER_CURVE,"CHANNEL FADER CURVE","How quickly the volume rises as a channel fader moves up.",{{0}}},
 {XZ_SHELL_ITEM_CROSSFADER_CURVE,"CROSSFADER CURVE","How the crossfader blends the two sides, from a long smooth mix to a sharp cut.",{{0}}},
 {XZ_SHELL_ITEM_RT_MASTER_EQUALIZER,"MASTER EQUALIZER","Which outputs the master EQ knobs change.",
  {{"MASTER OUT","Only the master output."},{"MASTER AND BOOTH OUT","The master and the booth outputs."}}},
 {XZ_SHELL_ITEM_RT_MASTER_ATTENUATOR,"MASTER ATTENUATOR","Lowers the whole master output to suit the sound system.",{{0}}},
 {XZ_SHELL_ITEM_RT_BOOTH_ATTENUATOR,"BOOTH MONITOR ATTENUATOR","Lowers the booth monitor output.",{{0}}},
 {XZ_SHELL_ITEM_RT_PEAK_LIMITER,"PEAK LIMITER","Softens harsh digital clipping when the master output peaks.",{{0}}},
 {XZ_SHELL_ITEM_RT_MIC_OUT_TO_BOOTH,"MIC OUT TO BOOTH","Whether the microphone is heard in the booth monitors.",{{0}}},
 {XZ_SHELL_ITEM_RT_TALK_OVER_MODE,"TALK OVER MODE","How the music drops while talkover is on.",
  {{"ADVANCED","Only the mid range drops, so the beat carries on under your voice."},{"NORMAL","All of the music drops."}}},
 {XZ_SHELL_ITEM_RT_TALK_OVER_LEVEL,"TALK OVER LEVEL","How far the music drops while talkover is on.",{{0}}},
 {XZ_SHELL_ITEM_HEADPHONE_CUE_MODE,"HEADPHONES MONO SPLIT/STEREO","How the headphones carry the cue and the master.",
  {{"MONO SPLIT","Cued channels in the left ear, the master in the right."},{"STEREO","The cue mix in stereo in both ears."}}},
 {XZ_SHELL_ITEM_RT_USB_OUTPUT_LEVEL,"USB OUTPUT LEVEL","The level sent to a computer recording the mix over USB.",{{0}}},
 {XZ_SHELL_ITEM_RT_CH3_CONTROL_TONE,"CH3 CONTROL TONE","Which channel 3 input carries the timecode for DJ software control.",
  {{"PHONO","A turntable on the PHONO input."},{"LINE","A player on the LINE input."}}},
 {XZ_SHELL_ITEM_RT_CH4_CONTROL_TONE,"CH4 CONTROL TONE","Which channel 4 input carries the timecode for DJ software control.",
  {{"PHONO","A turntable on the PHONO input."},{"LINE","A player on the LINE input."}}},
 {XZ_SHELL_ITEM_RT_MIXER_MIDI_MESSAGE,"MIXER MIDI MESSAGE","Whether the mixer and effect controls send MIDI to connected software.",
  {{"OFF","The mixer sends no MIDI."},{"SEND","Mixer and effect moves are sent."},{"SEND WITH TIME PARAM","Also sends the effect time."}}},
 {XZ_SHELL_ITEM_MIC_EQ,"MIC EQ","Tone setting for the microphone channel.",{{0}}},
 /* p.126-127 GENERAL category */
 {XZ_SHELL_ITEM_LANGUAGE,"LANGUAGE","The language for menus and messages. Pick the language of your track names if they show as boxes.",{{0}}},
 {XZ_SHELL_ITEM_RT_PAD_BRIGHTNESS,"PAD/BUTTON BRIGHTNESS","Brightness of the pads and buttons.",{{0}}},
 {XZ_SHELL_ITEM_LCD_BRIGHTNESS,"LCD BRIGHTNESS","Brightness of the main touch screen.",{{0}}},
 {XZ_SHELL_ITEM_JOG_LCD_BRIGHTNESS,"JOG LCD BRIGHTNESS","Brightness of the jog wheel displays.",{{0}}},
 {XZ_SHELL_ITEM_SCREEN_SAVER,"SCREEN SAVER","Dims the screen after 5 minutes with no track loaded, or 100 minutes idle while paused, cued or at a track's end.",{{0}}},
 {XZ_SHELL_ITEM_RT_TOUCH_CALIBRATION,"TOUCH PANEL CALIBRATION","Realigns the touch screen when touches land in the wrong place. Pushing opens the player's own calibration screen.",{{0}}},
 {XZ_SHELL_ITEM_AUTO_STANDBY,"AUTO STANDBY","Puts the unit to sleep after 4 hours with no controls touched, no audio in and nothing connected.",{{0}}},
 {XZ_SHELL_ITEM_VERSION,"VERSION No.","The player's firmware version.",{{0}}},
 /* settings this unit shares with the newer players */
 {XZ_SHELL_ITEM_PLAYER_NUMBER,"PLAYER No.","This unit's number on the LINK network. AUTO takes a free one.",{{0}}},
 {XZ_SHELL_ITEM_MIDI_CHANNEL,"MIDI CHANNEL","The MIDI channel the unit uses when it controls DJ software.",{{0}}},
 {XZ_SHELL_ITEM_WAVE_PHASE_METER,"WAVEFORM / PHASE METER","Whether the play screen shows the zoomed waveform or the phase meter.",{{0}}},
 {XZ_SHELL_ITEM_TIME_MODE,"TIME MODE","Whether a deck's time counts down to the end or up from the start.",
  {{"REMAIN","Counts down the time left in the track."},{"TIME","Counts up from the start of the track."}}},
 {XZ_SHELL_ITEM_AUTO_CUE,"AUTO CUE","Sets the cue point where the music starts each time a track loads.",{{0}}},
 /* this shell's own display settings */
 {XZ_SHELL_ITEM_WAVE_COLOR,"WAVEFORM COLOR","Colours of the scrolling and overview waveforms.",
  {{"BLUE","One blue waveform with white peaks."},{"RGB","Lows red, mids green, highs blue."},
   {"3BAND","Lows blue, mids amber, highs white."},{"STEMS","Drums, harmonics and vocals in their stem colours. Tracks without stems show 3BAND."}}},
 {XZ_SHELL_ITEM_WAVE_POSITION,"WAVEFORM CURRENT POSITION","Where the playhead sits in the scrolling waveforms.",
  {{"CENTER","As much of the past as of what is coming."},{"LEFT","More of what is coming up."}}},
 {XZ_SHELL_ITEM_KEY_DISPLAY,"KEY DISPLAY FORMAT","How keys are written on screen.",
  {{"AS DECK","As the player writes them."},{"CLASSIC","Note names, like Am."},{"*ALPHANUM","Wheel numbers, like 8A."}}},
 {XZ_SHELL_ITEM_WAVE_ZOOM,"WAVEFORM ZOOM","How much of the track the scrolling waveforms show. Higher numbers zoom in.",{{0}}},
 {XZ_SHELL_ITEM_USB_COLORS,"USB COLORS","Whether each deck takes the colours saved on the USB stick its track came from.",
  {{"DECK + WAVE","The deck label takes the stick's colour and the waveform its waveform colour."},
   {"DECK ONLY","Only the deck label takes the stick's colour."},{"OFF","Every deck uses this unit's colours."}}},
};
#endif
