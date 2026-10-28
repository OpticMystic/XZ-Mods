/* SPDX-License-Identifier: MIT */
/* Hardware navigation and the info box. Nothing here draws or touches the
   runtime: focus is UI state, and every activation goes through the same
   touch path a finger would take, so encoder and screen can never disagree. */
#include "ui_skin.h"
#include "ui_network.h"
#include "ui_support.h"
#include "stem_pads.h"
#include "ui_sections.h"
#include <stdio.h>
#include <string.h>

int xz_ui_focusable(const struct xz_ui_widget *w){
 return w&&w->kind!=XZ_UI_BACK&&w->kind!=XZ_UI_CLOSE&&w->kind!=XZ_UI_NONE&&w->kind!=XZ_UI_SUPPORT_ALERT;
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
 /* A SET UP button and the switch it turns into share one slot: focus left on one finds the other. */
 if(u->focus_kind==XZ_UI_FEATURE_SETUP_OPEN||u->focus_kind==XZ_UI_LOCAL_STEM_SETUP_OPEN||u->focus_kind==XZ_UI_ENABLE)
  for(size_t i=0;i<n;i++)if(w[i].index==u->focus_index&&(w[i].kind==XZ_UI_ENABLE||w[i].kind==XZ_UI_FEATURE_SETUP_OPEN||w[i].kind==XZ_UI_LOCAL_STEM_SETUP_OPEN))return (int)i;
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
 /* A pair STOCK greys out cannot be edited: its push only says why. */
 if(a.kind==XZ_UI_JUMP_PAIR&&u->page==XZ_UI_BEAT_JUMP&&xz_ui_widget_ready(u,m,&a))u->adjusting=1;
 /* Section tabs inside a page survive the page change, so focus stays on the one pushed.
    Rail tabs and links open a new page and start at its first control. */
 if(a.kind==XZ_UI_CONTROL_SECTION){u->focus_kind=a.kind;u->focus_index=a.index;u->focus_deck=a.deck;}
 return total;
}

/* Info box text. Titles name the control; bodies say what it changes (DESIGN 1.2). */
const char *xz_ui_page_title(int page){
 if(xz_ui_network_page(page))return xz_ui_network_title(page);
 if(xz_ui_support_page(page))return xz_ui_support_title(page);
 const char *section=xz_ui_section_label(page);
 if(section)return section;
 switch(page){
 case XZ_UI_GENERAL:return "GENERAL";case XZ_UI_FEATURE_SETUP:return "FEATURE SETUP";
 case XZ_UI_CONTROLS:return "CONTROLS";case XZ_UI_THEMES:return "APPEARANCE";case XZ_UI_SETTINGS:return "ADVANCED";
 case XZ_UI_STEMS:return "GROOVE PADS";case XZ_UI_XPAD:return "X-PAD SAMPLER";
 case XZ_UI_CONNECTION:return "VJ.TOOLS";case XZ_UI_BEAT_JUMP:return "BEAT JUMP PADS";case XZ_UI_GAMES:return "GAMES";
 case XZ_UI_DOOM:return "DOOM";case XZ_UI_DOOM_SETUP:return "GAME SETUP";case XZ_UI_DOOM_FILES:return "USB WAD LOADER";
 case XZ_UI_UPDATES:return "UPDATES";case XZ_UI_UPDATE_SETUP:return "UPDATE SETUP";case XZ_UI_MYHOUSE_SETUP:return "MY HOUSE";
 case XZ_UI_KEY_SETUP:return "KEYS";case XZ_UI_BROWSE_SETUP:return "KEY NOTATION";case XZ_UI_PC_STEMS:return "STEMS IN DJ SOFTWARE";
 case XZ_UI_STEM_COLORS:return "STEM COLOURS";case XZ_UI_THEME_PICK:return "CLASSIC THEMES";case XZ_UI_APPS:return "USB APPS";case XZ_UI_APPS_SETUP:return "SET UP APPS";
 case XZ_UI_RAM_SETUP:return "RAM TRACKS";case XZ_UI_LOCAL_STEM_SETUP:return "USB STEMS";case XZ_UI_METRICS_SETUP:return "SYSTEM READOUT";
 case XZ_UI_PROFILE_SETUP:case XZ_UI_PROFILE_EDIT:return "DJ PROFILE";case XZ_UI_ABOUT:return "ABOUT XZ MODS";case XZ_UI_BUTTONS:return "REKORDBOX BUTTON";
 case XZ_UI_STEM_PADS:return "STEM PADS";case XZ_UI_SCREEN_LOOK:return "LAMPS AND TITLES";case XZ_UI_BAND_COLORS:return "3-BAND COLOURS";
 default:return "MODS";}
}
const char *xz_ui_page_body(int page){
 switch(page){
 case XZ_UI_HOME:return "What MODS can do, and anything that needs you.";
 case XZ_UI_PLAY:return "Changes what the pads and buttons do while you perform: custom beat jump distances, gate cue, smart cue, key sync, RAM tracks and the button next to LINK.";
 case XZ_UI_STEM_SECTION:return "Mute vocals, harmonics and drums from the pads or the screen: USB stems, stems in DJ software, the stem pads, the stem buttons on screen and their colours.";
 case XZ_UI_SCREEN:return "How the play screen looks: interface, deck view, theme, waveform colours, titles, keys and the top bar.";
 case XZ_UI_FOUR_DECKS:return "Play decks 3 and 4 over the loopback cable: the cable's state, the deck view and what works on each deck.";
 case XZ_UI_CONNECT:return "Link to people and computers: your DJ profile, VJ.Tools, the network and updates.";
 case XZ_UI_EXTRAS:return "Games, USB apps, report an issue and about.";
 case XZ_UI_FEATURE_SETUP:return "Detect the feature, choose a deck, enable it and test it on that deck.";
 case XZ_UI_THEME_PICK:return "Restyles the classic player screen and this menu.";
 case XZ_UI_STEMS:return "Plays a groove in place of a stem. A pad with a groove swaps it in; pads without one keep their hot cues.";
 case XZ_UI_XPAD:return "A sampler on the pads: hold the loop strip to loop a slice (left to right sets length, up and down sets pitch), play the eight sample pads, HOLD keeps the loop, OVERDUB layers hits.";
 case XZ_UI_CONNECTION:return "Lets VJ.Tools on a computer connect, announces this player on the network, and shows the computer's picture instead of the play screen.";
 case XZ_UI_GAMES:return "Doom and Chex Quest, Beat Arcade, Wave Rider, Rhythm Lab and Static Surfer. Audio keeps playing.";
 case XZ_UI_APPS:return "Runs apps found on a USB; AUTOLOAD starts them when their USB goes in.";
 case XZ_UI_UPDATES:return "Check for, download and go back from signed XZ Mods updates. An update starts at the next power cycle.";
 case XZ_UI_UPDATE_SETUP:return "Connect to the update server started in the XZ Mods desktop app.";
 case XZ_UI_NETWORK:return "Adapter, DHCP or static IP and priority. Apply for this boot, or revert.";
 case XZ_UI_KEY_SETUP:return "Shift a deck's key a semitone at a time, reset it, or match it to the master deck. SHIFT + SYNC on the deck does the match too.";
 case XZ_UI_BROWSE_SETUP:return "How keys are written: as on the USB, musical (Am) or wheel (8A).";
 case XZ_UI_RAM_SETUP:return "Retains original audio, up to 64 MiB per file and 128 MiB shared. New deck 3/4 copies need loopback. Loaded waves and cues stay with the track.";
 case XZ_UI_STEM_COLORS:return "The colour of each stem on pads, stem buttons and the STEMS waveform. Bass on 4-stem tracks follows the theme.";
 case XZ_UI_BAND_COLORS:return "The colours of the 3-BAND waveform's lows, mids and highs, in both interfaces and on decks 3 and 4. Saved to the USB.";
 case XZ_UI_DOOM:return "Start the selected game. It runs entirely on the XZ.";
 case XZ_UI_DOOM_FILES:return "Choose a base game and optional add-on from USB.";
 case XZ_UI_DOOM_SETUP:return "Check the engine and USB game files step by step.";
 case XZ_UI_MYHOUSE_SETUP:return "Check the native GZDoom engine and My House files.";
 case XZ_UI_APPS_SETUP:return "Walk through detecting, installing and testing an XZ SDK app.";
 case XZ_UI_BEAT_JUMP:return "How far each pad pair jumps in BEAT JUMP mode, on all four decks. The first pad jumps back, the second forward. SHIFT + BEAT JUMP gives page 2: let go of SHIFT, then press a pad. SHIFT + pad is stock on decks 1 and 2. UTILITY's BEAT JUMP BEAT VALUE is separate.";
 case XZ_UI_STEM_PADS:return "Which pad mode button gives stem pads, which four pads carry them, their order, and the pad lights.";
 case XZ_UI_PC_STEMS:return "When a deck plays from a computer over USB, the stem pads switch the DJ software's stems instead. Setup checks the link and tests one switch.";
 case XZ_UI_LOCAL_STEM_SETUP:return "Plays prepared stem files from the USB so pads and stem buttons can mute vocals, harmonics and drums.";
 case XZ_UI_METRICS_SETUP:return "CPU, memory, temperature and audio render times. Shows the active key shifters on all four decks.";
 case XZ_UI_PROFILE_SETUP:return "Your DJ name and logo for VJ screens, from the USB. The player's own saved settings are MY SETTINGS in UTILITY.";
 case XZ_UI_ABOUT:return "Created by VJ OpticMystic. Credits, project links, licences and voluntary support.";
 case XZ_UI_SUPPORT:return "Tell the maintainer about a problem or an idea. The player adds its own details; you can add words on your computer.";
 case XZ_UI_BUTTONS:return "What the rekordbox button above LINK does, alone and while SHIFT is held on either deck.";
 case XZ_UI_SCREEN_LOOK:return "The MODERN play screen's top bar lamps, waveform titles and track colours: show or hide each one.";
 default:return "Opens this page.";}
}
/* The deck feature a control belongs to (enum xz_deck_feature), 0 for the whole player. */
static unsigned feature_of(const struct xz_ui_widget *a){
 int k=a->index;
 switch(a->kind){
 case XZ_UI_JUMP_PAIR:case XZ_UI_JUMP_SIZE:case XZ_UI_JUMP_ENABLE:case XZ_UI_JUMP_EDIT_PAGE:case XZ_UI_JUMP_STEP:return XZ_DECK_CAN_BEAT_JUMP;
 case XZ_UI_ENABLE:case XZ_UI_FEATURE_SETUP_OPEN:
  return k==XZ_UI_GATE?XZ_DECK_CAN_GATE_CUE:k==XZ_UI_SMART?XZ_DECK_CAN_SMART_CUE:k==XZ_UI_STEM?XZ_DECK_CAN_STEMS:0;
 case XZ_UI_INFO:
  if(k>=XZ_UI_INFO_GRID&&k<XZ_UI_INFO_GRID+XZ_UI_GRID_ROWS)return xz_ui_grid_features[k-XZ_UI_INFO_GRID];
  return k==XZ_UI_INFO_COLUMNS?XZ_DECK_CAN_DISPLAY:0;
 case XZ_UI_FEATURE_LINK:case XZ_UI_PANEL:
  switch(k){
  case XZ_UI_BEAT_JUMP:return XZ_DECK_CAN_BEAT_JUMP;case XZ_UI_KEY_SETUP:return XZ_DECK_CAN_KEY_SYNC;
  case XZ_UI_RAM_SETUP:return XZ_DECK_CAN_RAM;case XZ_UI_PC_STEMS:case XZ_UI_STEM_PADS:case XZ_UI_LOCAL_STEM_SETUP:return XZ_DECK_CAN_STEMS;
  case XZ_UI_STEM_COLORS:case XZ_UI_THEME_PICK:case XZ_UI_BAND_COLORS:return XZ_DECK_CAN_DISPLAY;
  default:return 0;
  }
 case XZ_UI_LOCAL_STEM_SETUP_OPEN:case XZ_UI_STEM_PAGE:case XZ_UI_STEM_BANK:case XZ_UI_STEM_ORDER:case XZ_UI_PAD_FEEDBACK:case XZ_UI_STEM_PAD_CONFIG:
 case XZ_UI_STEMS_OVERLAY:case XZ_UI_MUTE:case XZ_UI_LEVEL:case XZ_UI_BYPASS:return XZ_DECK_CAN_STEMS;
 case XZ_UI_KEY_SHIFT:case XZ_UI_KEY_SYNC:return XZ_DECK_CAN_KEY_SYNC;
 case XZ_UI_KEY_FEEDBACK:return XZ_DECK_CAN_PLAYING_KEY;
 case XZ_UI_GROOVE_PAD:return XZ_DECK_CAN_GROOVE;
 case XZ_UI_STRIP:case XZ_UI_HOLD:case XZ_UI_OVERDUB:case XZ_UI_VOLUME:case XZ_UI_SAMPLE_PAD:return XZ_DECK_CAN_XPAD;
 case XZ_UI_RAM_DECK:case XZ_UI_RAM_REQUEST:case XZ_UI_RAM_CANCEL:case XZ_UI_RAM_RELEASE:
 case XZ_UI_RAM_SETUP_OPEN:case XZ_UI_RAM_SETUP_STEP:case XZ_UI_RAM_CHOOSE:case XZ_UI_RAM_CONNECT:case XZ_UI_RAM_MANAGE:return XZ_DECK_CAN_RAM;
 case XZ_UI_SET_INTERFACE:case XZ_UI_SET_THEME:case XZ_UI_SET_MODERN_THEME:case XZ_UI_DECK_VIEW:case XZ_UI_WAVE_MODE:
 case XZ_UI_KEY_FORMAT:case XZ_UI_BROWSE_TRAFFIC:case XZ_UI_BROWSE_KEY_PREFIX:case XZ_UI_STEM_COLOR:case XZ_UI_STEM_COLORS_RESET:
 case XZ_UI_BAND_COLOR:case XZ_UI_BAND_COLORS_RESET:
  return XZ_DECK_CAN_DISPLAY;
 /* the BYPASS button belongs to the stem buttons; the other MODERN parts are display */
 case XZ_UI_SHELL_LOOK:return k/8==XZ_UI_LOOK_BYPASS?XZ_DECK_CAN_STEMS:XZ_DECK_CAN_DISPLAY;
 default:return 0;
 }
}
/* Which decks a control works on now, and why a deck is left out (decks in mixer order 3 1 2 4). */
static void coverage(const struct xz_ui_model *m, const struct xz_ui_widget *a, struct xz_ui_info *out){
 unsigned feature=feature_of(a);out->feature=feature;out->works_on=0;out->excluded=NULL;
 if(!feature)return;
 for(int d=0;d<4;d++)if(xz_deck_caps(m,d)&feature)out->works_on|=1u<<d;
 static const int order[4]={2,0,1,3};
 for(int k=0;k<4&&!out->excluded;k++){
  int d=order[k];
  if(out->works_on&(1u<<d))continue;
  /* Decks 3 and 4 that this loader cannot run at all leave display settings alone. */
  if(feature==XZ_DECK_CAN_DISPLAY&&d>=2)continue;
  out->excluded=xz_deck_cap_reason(m,d,feature);
 }
}
static const char *jump_pair_body(const struct xz_ui *u,const struct xz_ui_model *m,int pair){
 static char text[200];
 struct xz_jump_settings d=xz_jump_defaults();int page=u->jump_edit_page?1:0;
 int beats=xz_jump_beats(!page&&!m->jump.enabled?d.sizes[pair]:m->jump.sizes[page*4+pair]);
 char back='A'+2*pair,fwd='B'+2*pair;
 if(u->adjusting)snprintf(text,sizeof text,"Turn to change. %s pad %c jumps back %d beat%s and %c jumps forward %d. Push or BACK when done.",
  page?"With SHIFT + BEAT JUMP,":"In BEAT JUMP mode",back,beats,beats==1?"":"s",fwd,beats);
 else if(!page&&!m->jump.enabled)snprintf(text,sizeof text,"Page 1 uses the player's own jumps: pad %c jumps back %d and %c forward %d. Choose MY SIZES to change them.",back,beats,fwd,beats);
 else snprintf(text,sizeof text,"%s pad %c jumps back %d beat%s and pad %c jumps forward %d. With a loop on, the loop moves instead.",
  page?"With SHIFT + BEAT JUMP,":"In BEAT JUMP mode",back,beats,beats==1?"":"s",fwd,beats);
 return text;
}
/* The 4 DECKS grid: what one feature does on each deck, from the capabilities only. */
static const char *grid_body(const struct xz_ui_model *m,unsigned f){
 unsigned on=0;for(int d=0;d<4;d++)if(xz_deck_caps(m,d)&f)on|=1u<<d;
 if((on&3u)!=3u){
  if(f==XZ_DECK_CAN_STEMS)return "This USB loader has no stem engine. USB STEMS setup in STEMS checks it.";
  return xz_deck_cap_reason(m,(on&1u)?1:0,f);
 }
 if((on&12u)==12u)return "Works on all four decks.";
 switch(f){
 case XZ_DECK_CAN_PLAYING_KEY:return "Decks 3 and 4 show the track's own key: it does not follow their tempo in this version.";
 case XZ_DECK_CAN_KEY_SYNC:return "Decks 3 and 4 cannot shift key in this version, so key sync stays on decks 1 and 2.";
 case XZ_DECK_CAN_STEMS:return "Decks 3 and 4 are software players. They cannot split a track into stems in this version, so the stem pads and stem buttons stay on decks 1 and 2.";
 case XZ_DECK_CAN_RAM:return "RAM tracks need the RAM engine; new deck 3/4 copies also need the loopback route.";
 default:return "Works on decks 1 and 2 in this version.";
 }
}
static const char *section_body(const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *a){
 int k=a->index;
 switch(a->kind){
 case XZ_UI_INFO:
  if(k>=XZ_UI_INFO_GRID)return grid_body(m,xz_ui_grid_features[(k-XZ_UI_INFO_GRID)%XZ_UI_GRID_ROWS]);
  return k==XZ_UI_INFO_COLUMNS?"Tap a column title in the track list, or press MENU there, to choose its columns and sort.":
   "Press SHIFT twice on a side to switch it between decks 1 and 3 (left) or 2 and 4 (right). LOAD loads into the deck that side controls.";
 case XZ_UI_HOME_SETUP:return k==XZ_UI_STEM_SECTION?"Opens USB STEMS setup: it checks the stem engine, turns stems on and checks a loaded track.":
  "Opens 4 DECKS: the cable's state and RESCAN, which checks the loopback cable and audio route now.";
 case XZ_UI_ROUTE_RESCAN:return "Checks the loopback cable and audio route again now. The XZ also keeps checking on its own.";
 case XZ_UI_DECK_VIEW:return "How many decks the play screen shows and in what order. Mixer order puts them top to bottom as the mixer channels run left to right: 3 1 2 4. Sound and controls do not change.";
 case XZ_UI_JUMP_PAIR:if(u->page==XZ_UI_BEAT_JUMP)return jump_pair_body(u,m,(k/2)&3);break;
 case XZ_UI_JUMP_SIZE:{static char text[96];snprintf(text,sizeof text,"Makes pads %c and %c jump %d beat%s.",'A'+2*u->jump_edit_pair,'B'+2*u->jump_edit_pair,xz_jump_beats(k),xz_jump_beats(k)==1?"":"s");return text;}
 case XZ_UI_JUMP_EDIT_PAGE:return k?"Edit page 2: the sizes SHIFT + BEAT JUMP gives. Page 2 always uses your sizes.":"Edit page 1: the sizes BEAT JUMP gives.";
 case XZ_UI_STEM_ORDER:if(u->page==XZ_UI_STEM_PADS)return "Pads A, B and C mute stems in this order, and the stem buttons on the screen follow it. D (or H) is BYPASS: the original track. A 4-stem track puts bass on D or H.";break;
 case XZ_UI_STEM_BANK:return "Which four pads carry the stems in that mode. The other four keep their normal job.";
 case XZ_UI_STEM_PAGE:
  if(u->page!=XZ_UI_STEM_PADS)break;
  if(k==3)return "BEAT JUMP gives stem pads, but page 2 jumps always win its pads and page 1 jumps win while page 1 is MY SIZES. With page 1 on STOCK, its pads use this mode's stem bank.";
  return "The pad mode button that gives stem pads. SHIFT + HOT CUE, BEAT LOOP or SLIP LOOP always gives stem pads too.";
 case XZ_UI_PAD_FEEDBACK:if(k)return "On decks 1 and 2, STEM COLOURS lights stem pads in their stem colours and dims muted stems. OFF leaves the pad lights to the player.";break;
 case XZ_UI_STEMS_OVERLAY:if(k)return "Where the stem buttons sit on the 2 DECKS play screen. Tap a button to mute that stem; drag along it to set its level. The STEMS button steps through these; in 4 DECKS it shows or hides them.";break;
 case XZ_UI_BROWSE_TRAFFIC:return "Colours keys in the track list green or amber by how well they mix with the master deck.";
 case XZ_UI_OPEN_UTILITY:return "The player's own settings: quantize, auto cue, play mode, the display and more. MODS closes and they open.";
 case XZ_UI_SHELL_LOOK:
  switch(k/8){
  case XZ_UI_LOOK_WAVE_COLOR:return "How the scrolling waveforms are coloured: STOCK blue, RGB, 3-BAND lows, mids and highs, or STEMS in the stem colours. UTILITY's WAVEFORM COLOR is the same setting.";
  case XZ_UI_LOOK_USB_COLORS:return k%8==0?"Each track's colour from your library shows on its deck tag and its waveform.":
   k%8==1?"Each track's colour from your library shows on its deck tag only.":"Track colours from your library are not shown.";
  case XZ_UI_LOOK_MODE_LAMPS:return "The top bar's SINGLE / CONTINUE, A. HOT CUE and A. CUE lamps.";
  case XZ_UI_LOOK_QUANTIZE_LAMP:return "Each deck's QUANTIZE lamp and beat value above its time.";
  case XZ_UI_LOOK_BYPASS:return "The BYPASS button after each deck's stem buttons: lit, the original track plays instead of the stems.";
  case XZ_UI_LOOK_WAVE_TITLES:return "Track titles inside the waveform lanes.";
  default:break;
  }
  break;
 case XZ_UI_BROWSE_KEY_PREFIX:return "Shows the key in front of each title in the track list.";
 default:break;
 }
 return NULL;
}
static const char *body(const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *a){
 int k=a->index;
 const char *own=section_body(u,m,a);if(own)return own;
 switch(a->kind){
 case XZ_UI_PANEL:case XZ_UI_FEATURE_LINK:return xz_ui_page_body(k);
 case XZ_UI_FEATURE_SETUP_OPEN:return "Detect this feature and its USB track, choose a deck, then enable and test it.";
 case XZ_UI_FEATURE_SETUP_STEP:return u->feature_step==3?"Confirm only after testing the feature on the loaded deck.":"Run the next setup step. Missing support keeps its specific reason on screen.";
 case XZ_UI_LOCAL_STEM_SETUP_OPEN:return xz_ui_usb_next_step(xz_ui_usb_blocked(m,a->deck>=0?a->deck:u->deck<2?u->deck:0));
 case XZ_UI_METRICS_TOGGLE:return "Open setup and verify live CPU, RAM and temperature readings beside STEMS and MODS.";
 case XZ_UI_DECK:return u->page==XZ_UI_PC_STEMS?"Choose the deck this setup checks.":"Choose which deck these controls act on.";
 case XZ_UI_CONTROL_SECTION:return k==0?"Assign stem mutes to four pads in a pad mode.":k==1?"Edit custom beat jump sizes for the pads.":
  k==2?"Switch the DJ software's stems on a computer linked by USB cable.":"Choose what the rekordbox button above LINK does, alone and with SHIFT.";
 case XZ_UI_ENABLE:
  if(k==XZ_UI_STEM)return "Plays prepared stem files from the USB so pads and stem buttons can mute vocals, harmonics and drums. Off gives every pad its normal job back.";
  if(k==XZ_UI_GATE)return "Hot cue pads play only while you hold them and jump back to the hot cue when you let go. Use it to stab hot cues like samples.";
  if(k==XZ_UI_SMART)return "Pressing a hot cue also moves the CUE point there, so CUE takes you back to the last hot cue you used.";
  return "Turns this feature on or off.";
 case XZ_UI_FOUR_WAVE_ORDER:return "Four-deck waveforms: numeric 1 2 3 4 or physical mixer 3 1 2 4. Shared across interfaces until reboot.";
 case XZ_UI_STEMS_OVERLAY:return "Push to cycle where the stem mute row sits on the play screen.";
 case XZ_UI_STEM_ORDER:return "Push to cycle the left-to-right stem order of the play screen rows and pads A, B and C.";
 case XZ_UI_STEM_PAGE:return "Pad mode that holds the stem shortcuts. Shortcuts replace that mode's pads.";
 case XZ_UI_STEM_PAD_CONFIG:return "Push to move stem shortcuts between pads A-D and E-H.";
 case XZ_UI_STEM_BANK:return "Which four pads carry the stem shortcuts.";
 case XZ_UI_PAD_FEEDBACK:return "Stem pad lights on decks 1 and 2 show colors and dim muted stems.";
 case XZ_UI_SHIFT_PAGES:return "SHIFT + a pad mode button gives the four stem actions in that order.";
 case XZ_UI_SHIFT_KEYSYNC:return "SHIFT + SYNC matches this deck's key to the master deck.";
 case XZ_UI_KEY_SHIFT:return k<0?"Lowers the deck key one semitone.":k>0?"Raises the deck key one semitone.":"Removes explicit key shift. With Master Tempo off, tempo can still change the key.";
 case XZ_UI_KEY_SYNC:return "Shifts this deck to a key compatible with the master deck.";
 case XZ_UI_KEY_CHECK:return "Detects this deck, opens connection or track loading, and verifies processed key output.";
 case XZ_UI_KEY_FEEDBACK:return m->ui_interface==XZ_UI_INTERFACE_MODERN?
  "Shows the key the track plays in now, after tempo and key shift, as the big key. Off shows the track's original key.":
  "Shows the key the track plays in now, after tempo and key shift, where the beat countdown sits. Off brings the countdown back.";
 case XZ_UI_KEY_FORMAT:return "How keys are written everywhere MODS draws them: AS ON THE USB, MUSICAL (Am) or WHEEL (8A).";
 case XZ_UI_RAM_DEBUG:return "Shows free memory beside the STEMS button.";
 case XZ_UI_REKORDBOX_KEY:return xz_ui_rekordbox_choice_help(m->rekordbox_assign[0]);
 case XZ_UI_REKORDBOX_ASSIGN:return xz_ui_rekordbox_choice_help(k%XZ_UI_RB_CHOICES);
 case XZ_UI_WAVE_MODE:return "How the scrolling waveform is coloured: STOCK blue, 3-BAND low, mid and high, STEMS drums, harmonics and vocals (3-band on tracks without stems).";
 case XZ_UI_OPEN_UTILITY:return "The player's own settings.";
 case XZ_UI_MUTE:return k==XZ_STEM_BASS?"Mutes the bass stem of this 4-stem track. Pad D (or H) does the same.":"Push to mute this stem. Drag sideways to set its level.";
 case XZ_UI_SET_THEME:return "Restyles the classic player screen and this menu.";
 case XZ_UI_SET_INTERFACE:return "CLASSIC keeps the player's own play screen with MODS added. MODERN is a full-screen layout with bigger waveforms and a browse sidebar.";
 case XZ_UI_SET_MODERN_THEME:return "The modern interface's look. More modern themes are coming.";
 case XZ_UI_STEM_COLOR:return "The colour of this stem on the pads, the stem buttons and the STEMS waveform.";
 case XZ_UI_STEM_COLORS_RESET:return "Puts every stem back to the theme's colors.";
 case XZ_UI_BAND_COLOR:return a->index%XZ_BAND_COLOR_CHOICES?"The colour of this band in the 3-BAND waveform.":"This band in the interface's own colour.";
 case XZ_UI_BAND_COLORS_RESET:return "Puts every band back to STANDARD.";
 case XZ_UI_JUMP_ENABLE:return "STOCK keeps the player's own 1, 2, 4, 8 beats on page 1. MY SIZES uses yours. Page 2 always uses yours.";
 case XZ_UI_JUMP_SHIFT:return "SHIFT + BEAT JUMP selects page 2 sizes.";
 case XZ_UI_JUMP_EDIT_PAGE:return "Choose which page of sizes to edit.";
 case XZ_UI_JUMP_PAIR:return u->adjusting?"Turn to change the size. Push or BACK when done.":"Push, then turn to change this pair's jump size. The first pad jumps back.";
 case XZ_UI_JUMP_STEP:return k>0?"Next larger jump size for the selected pair.":"Next smaller jump size for the selected pair.";
 case XZ_UI_TAKEOVER_TOGGLE:return "Shows the computer's VJ.Tools screen instead of the player screen.";
 case XZ_UI_TAKEOVER_ASSIGN:return k==0?"The LINK key switches the VJ.Tools screen.":k==1?"The rekordbox button switches the VJ.Tools screen when it is set to VJ.TOOLS SCREEN.":"An onscreen button switches the VJ.Tools screen.";
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
 case XZ_UI_RAM_SETUP_OPEN:case XZ_UI_RAM_SETUP_STEP:case XZ_UI_RAM_CHOOSE:case XZ_UI_RAM_CONNECT:case XZ_UI_RAM_MANAGE:return "Detect the RAM engine, choose a track, connect its route and verify attachment.";
 case XZ_UI_RAM_DECK:return "Choose the deck whose track goes into RAM.";
 case XZ_UI_RAM_REQUEST:return "Copies the loaded track's audio into RAM.";
 case XZ_UI_RAM_CANCEL:return "Stops the copy. The track keeps playing from USB.";
 case XZ_UI_RAM_RELEASE:return "Frees the RAM copy. Needs the USB still connected.";
 case XZ_UI_LOCAL_STEM_SETUP_STEP:return xz_ui_usb_next_step(xz_ui_usb_blocked(m,u->deck<2?u->deck:0));
 case XZ_UI_PC_CHECK:return "Checks this deck is playing from the DJ software on the computer.";
 case XZ_UI_PC_TEST:return "Sends one drums switch to the DJ software.";
 case XZ_UI_PC_CONFIRM:return k?"Drums switched in the DJ software. Finishes setup.":"Drums did not switch. Goes back to the test.";
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
 if(!xz_ui_focused(u,m,&a)){out->title=xz_ui_page_title(u?u->page:0);out->body="";out->state="";return 0;}
 return xz_ui_describe_widget(u,m,&a,out);
}
int xz_ui_describe_widget(const struct xz_ui *u,const struct xz_ui_model *m,const struct xz_ui_widget *w,struct xz_ui_info *out){
 struct xz_ui_widget a=*w;
 memset(out,0,sizeof(*out));
 struct xz_ui_look l=xz_ui_widget_look(u,m,&a);
 out->available=l.available;out->selected=l.selected;
 out->title=a.kind==XZ_UI_PANEL||a.kind==XZ_UI_FEATURE_LINK?xz_ui_page_title(a.index):a.label&&a.label[0]?a.label:xz_ui_page_title(u->page);
 if(a.kind==XZ_UI_DECK)out->title=a.index?"DECK 2":"DECK 1";
 else if(a.kind==XZ_UI_PROFILE&&m->dj&&m->dj->current>=0&&m->dj->entry[m->dj->current].name)out->title=m->dj->entry[m->dj->current].name;
 else if(a.kind==XZ_UI_STEM_PAD_CONFIG)out->title="STEM PADS";
 else if(a.kind==XZ_UI_JUMP_PAIR)out->title="JUMP PAIR";
 else if(a.kind==XZ_UI_VOLUME)out->title="SAMPLE VOLUME";
 else if(a.kind==XZ_UI_STRIP)out->title="LOOP STRIP";
 else if(a.kind==XZ_UI_REKORDBOX_ASSIGN){
  static char title[48];
  snprintf(title,sizeof(title),"%s: %s",a.index<XZ_UI_RB_CHOICES?"PRESS":"SHIFT + PRESS",a.label?a.label:"");
  out->title=title;
 }
 if(a.kind==XZ_UI_DECK_VIEW){static const char *const names[3]={"DECK VIEW: 2 DECKS","DECK VIEW: 4 DECKS, 1 2 3 4","DECK VIEW: 4 DECKS, 3 1 2 4"};out->title=names[a.index>=0&&a.index<3?a.index:0];}
 else if(a.kind==XZ_UI_JUMP_PAIR&&u->page==XZ_UI_BEAT_JUMP){static char title[24];snprintf(title,sizeof title,"PADS %c / %c",'A'+2*((a.index/2)&3),'B'+2*((a.index/2)&3));out->title=title;}
 else if(a.kind==XZ_UI_STEMS_OVERLAY&&a.index){static char title[48];snprintf(title,sizeof title,"STEM BUTTONS: %s",a.label);out->title=title;}
 else if(a.kind==XZ_UI_PAD_FEEDBACK&&a.index)out->title=a.index==1?"STEM PAD LIGHTS: STEM COLOURS":"STEM PAD LIGHTS: OFF";
 else if(a.kind==XZ_UI_SHELL_LOOK&&(a.index/8==XZ_UI_LOOK_WAVE_COLOR||a.index/8==XZ_UI_LOOK_USB_COLORS)){
  static char title[48];snprintf(title,sizeof title,"%s: %s",a.index/8==XZ_UI_LOOK_WAVE_COLOR?"WAVEFORM COLOURS":"TRACK COLOURS",a.label);out->title=title;}
 else if(a.kind==XZ_UI_STEM_ORDER&&u->page==XZ_UI_STEM_PADS){static char title[48];snprintf(title,sizeof title,"ORDER  |  %s",xz_stem_order_name(m->stem_order));out->title=title;}
 out->body=body(u,m,&a);
 coverage(m,&a,out);
 if(a.kind==XZ_UI_SHELL_LOOK&&a.index/8==XZ_UI_LOOK_WAVE_COLOR&&((out->works_on>>2)&3u))out->excluded="Decks 3 and 4: 3-band for STOCK and STEMS.";
 if(a.kind==XZ_UI_ABOUT_LINK)out->body="Push or tap to show this link as a QR. Scan with your phone.";
 if(xz_ui_network_kind(a.kind))out->body=xz_ui_network_help(a.kind);
 if(xz_ui_support_kind(a.kind)){const char *title=NULL;out->body=xz_ui_support_describe(u,m,&a,&title);if(title)out->title=title;}
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
 case XZ_UI_FOUR_WAVE_ORDER:out->state=m->four_wave_mixer?"3 1 2 4 / MIXER":"1 2 3 4 / NUMERIC";break;
 case XZ_UI_STEMS_OVERLAY:case XZ_UI_PAD_FEEDBACK:out->state=a.index?(l.selected?"SELECTED":""):l.selected?"ON":"OFF";break;
 case XZ_UI_ENABLE:case XZ_UI_SHIFT_PAGES:case XZ_UI_SHIFT_KEYSYNC:
 case XZ_UI_KEY_FEEDBACK:case XZ_UI_RAM_DEBUG:case XZ_UI_REKORDBOX_KEY:case XZ_UI_JUMP_SHIFT:
 case XZ_UI_TAKEOVER_TOGGLE:case XZ_UI_CONNECTION_ENABLE:case XZ_UI_DISCOVERY:case XZ_UI_HOLD:case XZ_UI_OVERDUB:case XZ_UI_METRICS_TOGGLE:
  out->state=l.selected?"ON":"OFF";break;
 case XZ_UI_SET_THEME:case XZ_UI_SET_INTERFACE:case XZ_UI_SET_MODERN_THEME:case XZ_UI_STEM_PAGE:case XZ_UI_WAVE_MODE:case XZ_UI_KEY_FORMAT:case XZ_UI_TAKEOVER_ASSIGN:case XZ_UI_STEM_COLOR:case XZ_UI_REKORDBOX_ASSIGN:
 case XZ_UI_BAND_COLOR:
  out->state=l.selected?"SELECTED":"";break;
 case XZ_UI_PANEL:case XZ_UI_CONTROL_SECTION:out->state=l.selected?"OPEN":"";break;
 case XZ_UI_FEATURE_LINK:out->state=u->page==XZ_UI_HOME?"OPEN":"";break;
 case XZ_UI_DECK_VIEW:case XZ_UI_JUMP_ENABLE:case XZ_UI_JUMP_SIZE:case XZ_UI_STEM_BANK:out->state=l.selected?"SELECTED":"";break;
 case XZ_UI_SHELL_LOOK:out->state=a.index/8==XZ_UI_LOOK_WAVE_COLOR||a.index/8==XZ_UI_LOOK_USB_COLORS?(l.selected?"SELECTED":""):l.selected?"SHOW":"HIDE";break;
 case XZ_UI_HOME_SETUP:out->state="NEEDS YOU";break;
 case XZ_UI_PROFILE_PICK:out->state=l.selected?"ACTIVE":"";break;
 default:out->state="";}
 return 1;
}
