/* SPDX-License-Identifier: MIT */
/* Hardware navigation and the info box. Nothing here draws or touches the
   runtime: focus is UI state, and every activation goes through the same
   touch path a finger would take, so encoder and screen can never disagree. */
#include "ui_skin.h"
#include "stem_pads.h"
#include <stdio.h>
#include <string.h>

int xz_ui_focusable(const struct xz_ui_widget *w){
 return w&&w->kind!=XZ_UI_BACK&&w->kind!=XZ_UI_CLOSE&&w->kind!=XZ_UI_NONE;
}
/* Content in reading order, then header deck pickers, then the section rail. */
static int zone(const struct xz_ui_widget *w){
 if(w->x<XZ_UI_RAIL_W&&w->y>=XZ_UI_HEAD)return 2;
 if(w->y<XZ_UI_HEAD)return 1;
 return 0;
}
static int before(const struct xz_ui_widget *a,const struct xz_ui_widget *b){
 int za=zone(a),zb=zone(b);
 if(za!=zb)return za<zb;
 /* Controls whose tops sit within half a row read as one row. */
 if(a->y+20<=b->y||b->y+20<=a->y)return a->y<b->y;
 return a->x<b->x;
}
static size_t order(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget w[XZ_UI_WIDGETS]){
 struct xz_ui_widget all[XZ_UI_WIDGETS];size_t n=xz_ui_layout(u,m,all),count=0;
 for(size_t i=0;i<n;i++)if(xz_ui_focusable(&all[i])){
  size_t k=count++;
  while(k>0&&before(&all[i],&w[k-1])){w[k]=w[k-1];k--;}
  w[k]=all[i];
 }
 return count;
}
static int same(const struct xz_ui *u,const struct xz_ui_widget *w){
 return w->kind==u->focus_kind&&w->index==u->focus_index&&w->deck==u->focus_deck;
}
static int resolve(const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *w,size_t n){
 if(!n)return -1;
 if(u->focus_kind!=XZ_UI_NONE)for(size_t i=0;i<n;i++)if(same(u,&w[i]))return (int)i;
 /* No remembered focus: start on the open section tab if the page has one, else the first control. */
 for(size_t i=0;i<n;i++)if(w[i].kind==XZ_UI_CONTROL_SECTION&&xz_ui_widget_look(u,m,&w[i]).selected)return (int)i;
 return 0;
}
static void focus_on(struct xz_ui *u,const struct xz_ui_widget *w){
 u->focus_kind=w->kind;u->focus_index=w->index;u->focus_deck=w->deck;
}
int xz_ui_focused(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_widget *out){
 struct xz_ui_widget w[XZ_UI_WIDGETS];
 if(!u||!m||u->deck<0||u->deck>3)return 0;
 size_t n=order(u,m,w);int i=resolve(u,m,w,n);
 if(i<0)return 0;
 if(out)*out=w[i];
 return 1;
}
/* Value editing kinds: push toggles adjusting, turning edits the value. */
static int adjustable(enum xz_ui_action_kind kind){return kind==XZ_UI_JUMP_PAIR||kind==XZ_UI_VOLUME;}
size_t xz_ui_encoder_turn(struct xz_ui *u,const struct xz_ui_model *m,int delta,struct xz_ui_action out[XZ_UI_ACTIONS]){
 struct xz_ui_widget w[XZ_UI_WIDGETS];
 if(!u||!m||!out||!delta||u->deck<0||u->deck>3||u->down)return 0;
 size_t n=order(u,m,w);int i=resolve(u,m,w,n);
 if(i<0)return 0;
 if(u->adjusting&&adjustable(w[i].kind)){
  int deck=w[i].deck>=0?w[i].deck:u->deck;
  if(w[i].kind==XZ_UI_JUMP_PAIR){
   /* The runtime clamps the step, so a fast spin runs to the smallest or largest size. */
   out[0]=(struct xz_ui_action){.kind=XZ_UI_JUMP_STEP,.phase=XZ_UI_PRESS,.deck=deck,.index=delta,.value=0,.pc_target=-1};
   return 1;
  }
  float value=m->deck[deck].sample_volume+(delta>0?.05f:-.05f);
  value=value<0?0:value>1?1:value;
  out[0]=(struct xz_ui_action){.kind=XZ_UI_VOLUME,.phase=XZ_UI_MOVE,.deck=deck,.index=w[i].index,.value=value,.pc_target=-1};
  return 1;
 }
 u->adjusting=0;
 /* One detent moves one control; a fast spin is clamped so it cannot skip a page. */
 int step=delta>0?1:-1,count=delta>0?delta:-delta;if(count>4)count=4;
 int next=i;
 for(int k=0;k<count;k++)next=(next+step+(int)n)%(int)n;
 focus_on(u,&w[next]);u->notice[0]=0;
 return 0;
}
size_t xz_ui_encoder_push(struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_action out[XZ_UI_ACTIONS]){
 struct xz_ui_widget w[XZ_UI_WIDGETS];
 if(!u||!m||!out||u->deck<0||u->deck>3||u->down)return 0;
 size_t n=order(u,m,w);int i=resolve(u,m,w,n);
 if(i<0)return 0;
 struct xz_ui_widget a=w[i];
 if(u->adjusting&&adjustable(a.kind)){u->adjusting=0;return 0;}
 focus_on(u,&a);
 /* VOLUME is a slider: a tap would jump it to wherever the finger lands. */
 if(a.kind==XZ_UI_VOLUME){if(xz_ui_widget_ready(u,m,&a))u->adjusting=1;return 0;}
 struct xz_ui_action press[XZ_UI_ACTIONS],release[XZ_UI_ACTIONS];
 size_t pressed=xz_ui_touch(u,m,a.x+a.w/2,a.y+a.h/2,1,press);
 size_t released=xz_ui_touch(u,m,a.x+a.w/2,a.y+a.h/2,0,release),total=0;
 for(size_t k=0;k<pressed&&total<XZ_UI_ACTIONS;k++)out[total++]=press[k];
 for(size_t k=0;k<released&&total<XZ_UI_ACTIONS;k++)out[total++]=release[k];
 if(a.kind==XZ_UI_JUMP_PAIR&&u->page==XZ_UI_BEAT_JUMP)u->adjusting=1;
 /* Section tabs inside a page survive the page change, so focus stays on the one pushed.
    Rail tabs and links open a new page and start at its first control. */
 if(a.kind==XZ_UI_CONTROL_SECTION){u->focus_kind=a.kind;u->focus_index=a.index;u->focus_deck=a.deck;}
 return total;
}

/* Info box text. Titles name the control; bodies say what it changes. */
static const char *page_title(int page){
 switch(page){
 case XZ_UI_GENERAL:return "GENERAL";case XZ_UI_FEATURE_SETUP:return "FEATURE SETUP";
 case XZ_UI_CONTROLS:return "CONTROLS";case XZ_UI_THEMES:return "APPEARANCE";case XZ_UI_SETTINGS:return "ADVANCED";
 case XZ_UI_EXTRAS:return "EXTRAS";case XZ_UI_STEMS:return "GROOVE PADS";case XZ_UI_XPAD:return "X-PAD CONTROLS";
 case XZ_UI_CONNECTION:return "VJ.TOOLS";case XZ_UI_BEAT_JUMP:return "BEAT JUMP";case XZ_UI_GAMES:return "GAMES";
 case XZ_UI_DOOM:return "DOOM";case XZ_UI_DOOM_SETUP:return "GAME SETUP";case XZ_UI_DOOM_FILES:return "USB WAD LOADER";
 case XZ_UI_UPDATES:return "NETWORK UPDATES";case XZ_UI_UPDATE_SETUP:return "UPDATE SETUP";case XZ_UI_MYHOUSE_SETUP:return "MY HOUSE";
 case XZ_UI_KEY_SETUP:return "KEY SETUP";case XZ_UI_BROWSE_SETUP:return "KEY FORMAT";case XZ_UI_PC_STEMS:return "PC STEMS";
 case XZ_UI_STEM_COLORS:return "STEM COLORS";case XZ_UI_APPS:return "APPLICATIONS";case XZ_UI_APPS_SETUP:return "SET UP APPS";
 case XZ_UI_RAM_SETUP:return "RAM TRACKS";case XZ_UI_LOCAL_STEM_SETUP:return "USB STEMS";
 case XZ_UI_PROFILE_SETUP:case XZ_UI_PROFILE_EDIT:return "DJ PROFILE";default:return "MODS";}
}
static const char *panel_body(int page){
 switch(page){
 case XZ_UI_GENERAL:return "Enable mods here. Open the settings beside a feature to customize it.";
 case XZ_UI_FEATURE_SETUP:return "Detect the feature, choose a deck, enable it and test it on that deck.";
 case XZ_UI_CONTROLS:return "Stem shortcuts on the pads, custom beat jumps and rekordbox stems.";
 case XZ_UI_THEMES:return "Interface theme for the whole player screen, plus stem colors.";
 case XZ_UI_SETTINGS:return "Cue options, key shift and key sync, waveform style, RAM tracks and hardware keys.";
 case XZ_UI_EXTRAS:return "VJ.Tools link, games, USB apps and network updates.";
 case XZ_UI_STEMS:return "Play grooves that replace a stem. Unassigned pads keep their hot cues.";
 case XZ_UI_XPAD:return "Experimental X-PAD sampler: loop strip, hold, overdub and sample pads.";
 case XZ_UI_CONNECTION:return "Link a computer running VJ.Tools for video on this screen and the filmstrip.";
 case XZ_UI_GAMES:return "Doom, Chex Quest, Beat Arcade, Wave Rider and Rhythm Lab. Audio keeps playing.";
 case XZ_UI_APPS:return "Run XZ SDK apps and services found on a USB.";
 case XZ_UI_UPDATES:return "Download a signed mod update to USB. It activates at the next power cycle.";
 case XZ_UI_UPDATE_SETUP:return "Connect to the update server started in the XZ Mods desktop app.";
 case XZ_UI_KEY_SETUP:return "Detect analyzed keys and enable SHIFT + SYNC key matching.";
 case XZ_UI_BROWSE_SETUP:return "Choose Classic or Camelot keys for the deck key strips.";
 case XZ_UI_RAM_SETUP:return "Copy a playing track into RAM so the USB can come out mid-set.";
 case XZ_UI_STEM_COLORS:return "Pick the colors stems use on pads and waveforms.";
 case XZ_UI_DOOM:return "Start the selected game. It runs entirely on the XZ.";
 case XZ_UI_DOOM_FILES:return "Choose a base game and optional add-on from USB.";
 case XZ_UI_DOOM_SETUP:return "Check the engine and USB game files step by step.";
 case XZ_UI_MYHOUSE_SETUP:return "Check the native GZDoom engine and My House files.";
 case XZ_UI_APPS_SETUP:return "Walk through detecting, installing and testing an XZ SDK app.";
 case XZ_UI_BEAT_JUMP:return "Custom beat jump sizes for the pads.";
 case XZ_UI_PC_STEMS:return "Switch rekordbox stems on a computer linked by USB cable.";
 case XZ_UI_PROFILE_SETUP:return "Your profile's name, logo and tagline, and creating or editing a profile on the USB.";
 default:return "Opens this page.";}
}
static const char *body(const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *a){
 int k=a->index;
 switch(a->kind){
 case XZ_UI_PANEL:case XZ_UI_FEATURE_LINK:return panel_body(k);
 case XZ_UI_FEATURE_SETUP_OPEN:return "Detect this feature and its USB track, choose a deck, then enable and test it.";
 case XZ_UI_FEATURE_SETUP_STEP:return u->feature_step==3?"Confirm only after testing the feature on the loaded deck.":"Run the next setup step. Missing support keeps its specific reason on screen.";
 case XZ_UI_LOCAL_STEM_SETUP_OPEN:return xz_ui_usb_next_step(xz_ui_usb_blocked(m,a->deck>=0?a->deck:u->deck<2?u->deck:0));
 case XZ_UI_METRICS_TOGGLE:return "Open setup and verify live CPU, RAM and temperature readings beside STEMS and MODS.";
 case XZ_UI_DECK:return u->page==XZ_UI_PC_STEMS?"Choose the deck this setup checks.":"Choose which deck these controls act on.";
 case XZ_UI_CONTROL_SECTION:return k==0?"Assign stem mutes to four pads in a pad mode.":k==1?"Edit custom beat jump sizes for the pads.":"Switch rekordbox stems on a computer linked by USB cable.";
 case XZ_UI_ENABLE:
  if(k==XZ_UI_STEM)return "Plays prepared stems from the USB on every deck. Off plays the normal track audio.";
  if(k==XZ_UI_GATE)return "Hot cue pads play only while held and jump back when released.";
  if(k==XZ_UI_SMART)return "Recalling a hot cue also moves the CUE point to it.";
  if(k==XZ_UI_PREVIEW)return "Hold a hot cue pad to hear it while the deck is paused.";
  if(k==XZ_UI_SAMPLE)return "Turns on the experimental X-PAD sample engine.";
  return "Turns this feature on or off.";
 case XZ_UI_STEMS_OVERLAY:return "Push to cycle where the stem mute row sits on the play screen.";
 case XZ_UI_STEM_ORDER:return "Push to cycle the left-to-right stem order of the play screen rows and pads A, B and C.";
 case XZ_UI_STEM_PAGE:return "Pad mode that holds the stem shortcuts. Shortcuts replace that mode's pads.";
 case XZ_UI_STEM_PAD_CONFIG:return "Push to move stem shortcuts between pads A-D and E-H.";
 case XZ_UI_STEM_BANK:return "Which four pads carry the stem shortcuts.";
 case XZ_UI_PAD_FEEDBACK:return "Pad lights show stem colors and dim muted stems.";
 case XZ_UI_SHIFT_PAGES:return "SHIFT + a pad mode button gives the four stem actions in that order.";
 case XZ_UI_SHIFT_KEYSYNC:return "SHIFT + SYNC matches this deck's key to the master deck.";
 case XZ_UI_KEY_SHIFT:return k<0?"Lowers the deck key one semitone.":k>0?"Raises the deck key one semitone.":"Returns the deck to its original key.";
 case XZ_UI_KEY_SYNC:return "Shifts this deck to a key compatible with the master deck.";
 case XZ_UI_KEY_CHECK:return "Checks that both decks report analyzed keys and turns on key sync.";
 case XZ_UI_KEY_FEEDBACK:return "Shows the playing key where the countdown sits. Off restores the countdown.";
 case XZ_UI_KEY_FORMAT:return k==0?"Use the format set in the player's own settings.":k==1?"Musical keys such as Am and F#.":"Camelot wheel keys such as 8A and 2B.";
 case XZ_UI_RAM_DEBUG:return "Shows free memory beside the STEMS button.";
 case XZ_UI_REKORDBOX_KEY:return m->rekordbox_menu?"REKORDBOX opens and closes this menu. Turn off to use it for rekordbox PC mode.":
  "REKORDBOX keeps its stock action. Turn on to open this menu with it.";
 case XZ_UI_WAVE_MODE:return k==0?"The player's own waveform.":k==1?"Low, mid and high bands in three colors.":"Drums, harmonics and vocals layers. Falls back to 3-band without stems or on 4-stem tracks.";
 case XZ_UI_MUTE:return k==XZ_STEM_BASS?"Mutes the bass stem of this 4-stem track. Pad D (or H) does the same.":"Push to mute this stem. Drag sideways to set its level.";
 case XZ_UI_SET_THEME:return "Restyles the whole player screen and this menu.";
 case XZ_UI_STEM_COLOR:return "Color for this stem on pads and waveforms.";
 case XZ_UI_STEM_COLORS_RESET:return "Puts every stem back to the theme's colors.";
 case XZ_UI_JUMP_ENABLE:return "Page 1 uses custom sizes instead of stock. SHIFT + BEAT JUMP page 2 is always available.";
 case XZ_UI_JUMP_SHIFT:return "SHIFT + BEAT JUMP selects page 2 sizes.";
 case XZ_UI_JUMP_EDIT_PAGE:return "Choose which page of sizes to edit.";
 case XZ_UI_JUMP_PAIR:return u->adjusting?"Turn to change the size. Push or BACK when done.":"Push, then turn to change this pair's jump size. The first pad jumps back.";
 case XZ_UI_JUMP_STEP:return k>0?"Next larger jump size for the selected pair.":"Next smaller jump size for the selected pair.";
 case XZ_UI_TAKEOVER_TOGGLE:return "Shows the computer's VJ.Tools screen instead of the player screen.";
 case XZ_UI_TAKEOVER_ASSIGN:return k==0?"The LINK key switches the VJ.Tools screen.":k==1?"The REKORDBOX key switches the VJ.Tools screen while it is not opening this menu.":"An onscreen button switches the VJ.Tools screen.";
 case XZ_UI_CONNECTION_ENABLE:return "Lets VJ.Tools on a computer connect to this player.";
 case XZ_UI_DISCOVERY:return "Announces this player so VJ.Tools finds it on the network.";
 case XZ_UI_GROOVE_PAD:return "Plays the assigned groove in place of its stem.";
 case XZ_UI_HOTCUE_PAD:return "No groove here, so the pad keeps its hot cue.";
 case XZ_UI_STRIP:return "Hold to loop a slice; left to right sets length, up and down sets pitch.";
 case XZ_UI_HOLD:return "Keeps the loop playing after you let go.";
 case XZ_UI_OVERDUB:return "Layers new pad hits over the playing loop.";
 case XZ_UI_VOLUME:return u->adjusting?"Turn to set the volume. Push or BACK when done.":"Push, then turn to set the sample volume.";
 case XZ_UI_SAMPLE_PAD:return "Plays this sample.";
 case XZ_UI_DOOM_START:return "Starts the game full screen. DJ audio keeps playing.";
 case XZ_UI_DOOM_CHECK:return "Checks the engine and USB game files.";
 case XZ_UI_WAD_SELECT:return "Use this file as the base game or add-on.";
 case XZ_UI_WAD_PAGE:return k<0?"Previous page of files.":"Next page of files.";
 case XZ_UI_WAD_SCAN:return "Searches the USB ports again for game files.";
 case XZ_UI_WAD_CLEAR:return "Plays the base game with no add-on.";
 case XZ_UI_GAME_SELECT:return k?"The original Chex Quest with its patch.":"Classic Doom or Doom II. Choose the game files next.";
 case XZ_UI_MYHOUSE_CHECK:return "Checks the native engine and My House files on USB.";
 case XZ_UI_ARCADE_OPEN:return "A beat-synced arcade game driven by the jog wheel.";
 case XZ_UI_WAVE_RIDER_OPEN:return "Ride the playing track's waveform with the jog wheel.";
 case XZ_UI_RHYTHM_LAB_OPEN:return "Ten short rhythm studies played on the pads.";
 case XZ_UI_GLYPH_RUN_OPEN:return "A runner game set to the playing track.";
 case XZ_UI_UPDATE_CHECK:return "Asks the update server for a newer signed runtime.";
 case XZ_UI_UPDATE_INSTALL:return "Downloads the update to USB. It activates after a power cycle.";
 case XZ_UI_UPDATE_ROLLBACK:return "Goes back to the previous mod version at the next power cycle.";
 case XZ_UI_RAM_DECK:return "Choose the deck whose track goes into RAM.";
 case XZ_UI_RAM_REQUEST:return "Copies the loaded track's audio into RAM.";
 case XZ_UI_RAM_CANCEL:return "Stops the copy. The track keeps playing from USB.";
 case XZ_UI_RAM_RELEASE:return "Frees the RAM copy. Needs the USB still connected.";
 case XZ_UI_LOCAL_STEM_SETUP_STEP:return xz_ui_usb_next_step(xz_ui_usb_blocked(m,u->deck<2?u->deck:0));
 case XZ_UI_PC_CHECK:return "Checks this deck is playing from rekordbox on the computer.";
 case XZ_UI_PC_TEST:return "Sends one drums switch to rekordbox.";
 case XZ_UI_PC_CONFIRM:return k?"Drums switched in rekordbox. Finishes setup.":"Drums did not switch. Goes back to the test.";
 case XZ_UI_SDK_RUN:return "Starts this app on the screen.";
 case XZ_UI_SDK_STOP:return "Stops this app.";
 case XZ_UI_SDK_SAVE:return "Saves this profile to USB.";
 case XZ_UI_SDK_FIX:return "Shows how to fix this package.";
 case XZ_UI_SDK_PAGE:return k<0?"Previous page of apps.":"Next page of apps.";
 case XZ_UI_SDK_SCAN:return "Searches the USB again for apps.";
 case XZ_UI_SDK_AUTOLOAD:return "Starts autoload apps when their USB goes in.";
 case XZ_UI_SDK_SETUP:return "Walks through detecting, installing and testing an app.";
 case XZ_UI_SDK_STEP:return "Jumps to this setup step.";
 case XZ_UI_SDK_CHOICE:return k?"Apps in the autoload folder start when the USB goes in.":"Apps start only when you run them.";
 case XZ_UI_PROFILE:return "Your DJ name and logo from the USB. VJ software shows the profile of the USB playing on the master deck.";
 case XZ_UI_PROFILE_PICK:return m->dj&&k>=0&&k<m->dj->count&&m->dj->entry[k].stick_active?
  "The active profile on its USB.":"Makes this the active profile on its USB. Only the USB is written.";
 case XZ_UI_PROFILE_PAGE:return k<0?"Previous profiles.":"More profiles.";
 case XZ_UI_PROFILE_CHECK:return k?"Opens the profile list.":"Reads both USB ports again for DJ profiles.";
 default:return "Push to use this control.";}
}
int xz_ui_describe(const struct xz_ui *u,const struct xz_ui_model *m,struct xz_ui_info *out){
 struct xz_ui_widget a;
 if(!out)return 0;
 memset(out,0,sizeof(*out));
 if(!xz_ui_focused(u,m,&a)){out->title=page_title(u?u->page:0);out->body="";out->state="";return 0;}
 struct xz_ui_look l=xz_ui_widget_look(u,m,&a);
 out->available=l.available;out->selected=l.selected;
 out->title=a.kind==XZ_UI_PANEL||a.kind==XZ_UI_FEATURE_LINK?page_title(a.index):a.label&&a.label[0]?a.label:page_title(u->page);
 if(a.kind==XZ_UI_DECK)out->title=a.index?"DECK 2":"DECK 1";
 else if(a.kind==XZ_UI_PROFILE&&m->dj&&m->dj->current>=0&&m->dj->entry[m->dj->current].name)out->title=m->dj->entry[m->dj->current].name;
 else if(a.kind==XZ_UI_STEM_PAD_CONFIG)out->title="STEM PADS";
 else if(a.kind==XZ_UI_JUMP_PAIR)out->title="JUMP PAIR";
 else if(a.kind==XZ_UI_VOLUME)out->title="SAMPLE VOLUME";
 else if(a.kind==XZ_UI_STRIP)out->title="LOOP STRIP";
 out->body=body(u,m,&a);
 if(xz_ui_profile_owns(a.kind)){const char *title=NULL,*text=xz_ui_profile_describe(u,m,&a,&title);if(text)out->body=text;if(title)out->title=title;}
 /* Locked stems name their reason instead of a bare NOT READY. */
 if(!l.available&&(a.kind==XZ_UI_MUTE||a.kind==XZ_UI_BYPASS||a.kind==XZ_UI_LEVEL)){
  enum xz_ui_usb_reason why=xz_ui_usb_blocked(m,a.deck>=0?a.deck:u->deck<2?u->deck:0);
  out->state=xz_ui_usb_reason_text(why);out->body=xz_ui_usb_next_step(why);
 }
 else if(!l.available)out->state="NOT READY";
 else if(u->adjusting)out->state="ADJUSTING";
 else switch(a.kind){
 case XZ_UI_STEM_ORDER:{
  /* Bass never moves: it is always pad D/H on 4-stem decks. */
  static char order[32];
  snprintf(order,sizeof(order),"%s%s",xz_stem_order_name(m->stem_order),xz_ui_stem_count(m,0)==4||xz_ui_stem_count(m,1)==4?" +BASS":"");
  out->state=order;break;}
 case XZ_UI_ENABLE:case XZ_UI_STEMS_OVERLAY:case XZ_UI_PAD_FEEDBACK:case XZ_UI_SHIFT_PAGES:case XZ_UI_SHIFT_KEYSYNC:
 case XZ_UI_KEY_FEEDBACK:case XZ_UI_RAM_DEBUG:case XZ_UI_REKORDBOX_KEY:case XZ_UI_JUMP_ENABLE:case XZ_UI_JUMP_SHIFT:
 case XZ_UI_TAKEOVER_TOGGLE:case XZ_UI_CONNECTION_ENABLE:case XZ_UI_DISCOVERY:case XZ_UI_HOLD:case XZ_UI_OVERDUB:case XZ_UI_METRICS_TOGGLE:
  out->state=l.selected?"ON":"OFF";break;
 case XZ_UI_SET_THEME:case XZ_UI_STEM_PAGE:case XZ_UI_WAVE_MODE:case XZ_UI_KEY_FORMAT:case XZ_UI_TAKEOVER_ASSIGN:case XZ_UI_STEM_COLOR:
  out->state=l.selected?"SELECTED":"";break;
 case XZ_UI_PANEL:case XZ_UI_CONTROL_SECTION:out->state=l.selected?"OPEN":"";break;
 case XZ_UI_PROFILE_PICK:out->state=l.selected?"ACTIVE":"";break;
 default:out->state="";}
 return 1;
}
