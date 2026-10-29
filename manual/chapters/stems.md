---
interface: both
settings: stems, stems_overlay, modern_stems, stem_order, stem_drums_color, stem_harmonics_color, stem_vocals_color
---

Stems split a track into its parts so you can mute them live. XZ Mods plays three stems per track: **VOCALS**, **HARMONICS** (everything melodic that is not a voice) and **DRUMS**. You mute them from the pads or from buttons on the screen, change their levels by sliding, and switch back to the whole track with **BYPASS**. Stems work on decks 1 and 2. Decks 3 and 4 have no stems.

> **Note.** MODERN labels the harmonics stem **INST** and the vocals stem **VOCAL** on its stem buttons. They are the same stems.

## Prepare stems

The player cannot split a track itself. You prepare stems on your computer, once per track, and they travel on the USB beside your music.

1. Prepare the stems in the XZ Mods app on your computer, or in OverCue Desktop. See [The XZ Mods app](desktop-app).
2. Keep the stems on the same USB as the music. Your original files and the library your DJ software exported stay unchanged.
3. On the player, load the original track through the normal browser and wait a moment for its stems to load.

A track without prepared stems plays as normal. Its stem buttons say why, and the button at their end tells you what to do (see [Stem readiness](#stem-readiness)).

## Turn stems on

Open MODS, choose **STEMS** on the section rail, and turn **USB STEMS** on. It is on by default. With it off, every pad keeps its normal job and the stem buttons say **STEMS ARE OFF** or **STEMS OFF**.

```figure classic:theme-{look}-stems-section MODS · STEMS
The STEMS page with USB stems on.
1 @ 445,116 USB STEMS | Plays prepared stems from the USB. Off gives every pad its normal job back. Without the stem engine on the USB it reads SET UP USB STEMS and starts the setup.
2 @ 775,116 STEMS IN DJ SOFTWARE | Sets up stem control for DJ software on a computer. See [Stems in DJ software](#stems-in-dj-software).
3 @ 400,180 STEM PADS | Which pad mode gives stem pads, the stem order and the pad lights. See [Stem pads](#stem-pads).
4 @ 400,287 Stem buttons on screen | Where the stem buttons sit on the play screen. The outlined tile is the current layout.
5 @ 400,350 STEM COLOURS | The colour of each stem. See [Stem colours](#stem-colours).
6 @ 725,425 WORKS ON | The decks this page applies to. Decks 3 and 4 have no stems.
```

## Stem buttons on the play screen

Each deck gets three stem buttons and **BYPASS**. Tap a stem button to mute that stem, and tap it again to bring it back. A muted stem goes dark and keeps its level in its label.

### MODERN stem buttons {modern}

```figure modern:stems-bypass-below-full PLAY · STEMS {modern}
BELOW / FULL rows. Deck 1 plays its stems with INST muted. Deck 2 is bypassed and plays the whole track.
1 @ 708,40 STEMS | Lit blue while stems are on. Tap to step to the next layout. The marks under the label show which one is current. In 4 DECKS it shows or hides the stem buttons.
2 @ 240,90 DRUMS | Tap to mute. Slide sideways to set the level. The fill and the percentage show it.
3 @ 395,90 VOCAL | The vocals stem.
4 @ 595,90 INST | Muted. It goes dark and keeps its level in the label.
5 @ 750,90 BYPASS | Grey while the stems play.
6 @ 690,200 BYPASS lit | Lit white, the deck plays the original track and every stem button goes dark. Tap again to hear the stems as you left them.
```

On full-height rows each stem button is also a fader. Put a finger on it and slide right to raise the level or left to lower it. One button's width covers the whole range. A tap without a slide toggles the mute. Half-height rows and the small buttons beside the hot cues mute only.

In 2 DECKS, **STEMS** in the top bar steps through six layouts in this order: beside the hot cues, below each deck at full height, below at half height, at the outer edges at full height, at the outer edges at half height, and hidden. You can also pick one in MODS under **STEMS**. MODERN keeps its own layout (`modern_stems`), so switching to CLASSIC and back does not change it. The default is the small buttons beside the hot cues.

```gallery {modern}
modern:stems-layout-pad-row BY THE CUES: small buttons beside the hot-cue lamps (the default)
modern:stems-layout-below-full BELOW / FULL: a full row under each deck's waveform
modern:stems-layout-below-half BELOW / HALF: half-height rows, more room for the waveforms
modern:stems-layout-outside-full OUTER / FULL: above deck 1 and below deck 2
modern:stems-layout-outside-half OUTER / HALF
modern:stems-layout-hidden HIDDEN: full-height waveforms, stems still play
```

The marks under **STEMS** follow the same order, so the lit mark tells you the layout without looking at the decks.

```gallery {modern}
modern:stems-header-0-pads First mark: BY THE CUES
modern:stems-header-1 Second mark: BELOW / FULL
modern:stems-header-2 Third mark: BELOW / HALF
modern:stems-header-3 Fourth mark: OUTER / FULL
modern:stems-header-4 Fifth mark: OUTER / HALF
modern:stems-header-5-hidden Sixth mark: HIDDEN
```

**BYPASS BUTTON** in MODS under **STEMS** shows or hides the BYPASS button. Hidden, the stem buttons take its room and the pads still bypass.

```gallery {modern}
modern:stems-bypass-pads BYPASS beside the hot cues, deck 2 bypassed
modern:stems-bypass-pad-row An older loader that does not report the layout: small buttons with BYPASS
modern:stems-bypass-below-half BYPASS on half rows
modern:stems-bypass-outside-full BYPASS at the outer edges
modern:stems-bypass-outside-half BYPASS at the outer edges, half height
modern:stems-bypass-hidden-setting BYPASS BUTTON hidden: three wider stem buttons
```

```gallery {modern}
modern:stems-fader-mid-drag Sliding deck 1's DRUMS down to 35 %
modern:stems-muted-vs-playing Deck 2 with DRUMS and VOCAL muted beside deck 1 playing all three
modern:play-stems-on Stems on with the stem buttons beside the hot cues
modern:play-stems-muted STEMS waveform with stems muted on both decks
modern:r5-play-stems-slip-traffic VOCAL muted on deck 2
modern:r5-play-stems-muted-green-key Two stems muted on deck 2, with stem colours of the DJ's own choice
modern:r5-play-no-stems-hot-cue-row A track without stems: only the hot-cue lamps
```

If stems are off when you tap **STEMS**, the layout still changes, and a notice says **STEM ROWS SHOW ONCE STEMS ARE ON IN MODS**.

### Stem buttons in 4 DECKS {modern}

In a 4 DECKS view the lanes are too short for the layouts, so **STEMS** is a switch there. One tap hides the stem buttons on every deck, and the screen says **STEM BUTTONS HIDDEN**. The next tap brings them back and says **STEM BUTTONS SHOWN**. **STEMS** is lit while they show and has no layout marks. Your 2 DECKS layout does not change, and XZ Mods saves the 4 DECKS choice on the USB separately.

A deck whose track has no prepared stems shows no stem buttons in 4 DECKS, and its waveform keeps the whole lane. The buttons appear when you load a track with stems. Decks 3 and 4 have no stems, so they never show stem buttons. [Four decks](four-decks) has pictures.

### CLASSIC stem rows {classic}

In CLASSIC the stem rows sit inside the player's own waveform area, one row per deck.

```figure classic:theme-0-inline PLAY · STEM ROWS {classic}
Two full rows. Deck 1 plays HARMONICS at 70 % with DRUMS muted.
1 @ 66,48 VOCALS | Tap to mute. Slide sideways to set the level.
2 @ 200,48 Level | The fill and the percentage show the stem's level.
3 @ 335,48 Muted | A muted stem goes dark and reads MUTE with the level it returns to.
4 @ 470,48 BYPASS | D1 OFF means deck 1 plays its stems. Tap to play the original track.
```

In 2 DECKS, the **STEMS** button in the top right corner of the play screen steps the rows through five layouts: BELOW / FULL, BELOW / HALF, OUTER / FULL, OUTER / HALF and HIDDEN, then back to BELOW / FULL. Its five marks show the current one. Half rows give each waveform more height. HIDDEN gives the player's waveforms their full size back while the stems keep playing. CLASSIC keeps its own layout (`stems_overlay`).

While four decks show in CLASSIC, no stem rows are drawn, and **STEMS** stays dark. A tap changes nothing and says **STEM BUTTONS SHOW IN 2 DECKS VIEW**. The layout comes back when you return to two decks.

## Stem pads

The pads mute stems too, without looking at the screen. Open **STEM PADS** on the STEMS page.

```figure classic:theme-{look}-stem-pads MODS · STEM PADS
HOT CUE gives stem pads on A to D, in the order VOCALS, HARMONICS, DRUMS.
1 @ 405,54 Pad mode button | Pick the pad mode that gives stem pads: HOT CUE, BEAT LOOP, SLIP LOOP or BEAT JUMP. The other modes keep their normal pads.
2 @ 314,190 Stem pads | Pads A, B and C mute the three stems in the order below.
3 @ 499,190 BYPASS pad | Pad D plays the original track while lit.
4 @ 314,265 Other pads | Pads E to H keep their normal job.
5 @ 650,143 STEMS ON | Put the stems on pads A to D or on pads E to H.
6 @ 760,206 Order | The stem order on the pads and on the screen. Push, then turn to change it.
7 @ 760,290 Pad lights | STEM COLOURS lights the stem pads in their stem colours and dims a muted one. OFF leaves the lights to the player.
8 @ 480,330 SHIFT shortcut | SHIFT + HOT CUE, BEAT LOOP or SLIP LOOP always gives stem pads, whatever mode you picked.
```

Mute and bypass are performance state. They are not saved, and every track starts with all stems playing.

### Stem order

The order puts the stems where your fingers expect them. It changes the pads and the stem buttons on the screen together. There are six orders.

| Order | Pads A, B, C |
|---|---|
| VOCALS / HARMONICS / DRUMS | The default |
| VOCALS / DRUMS / HARMONICS | |
| HARMONICS / VOCALS / DRUMS | |
| HARMONICS / DRUMS / VOCALS | |
| DRUMS / VOCALS / HARMONICS | |
| DRUMS / HARMONICS / VOCALS | |

The layered STEMS waveform and the stem row for DJ software keep their own order.

## Stem colours

Each stem has a colour on the pads, the stem buttons and the STEMS waveform. In the ORIGINAL theme drums are blue, harmonics red and vocals green. Every theme has its own shades.

```figure classic:theme-{look}-stem-colors MODS · STEM COLOURS
All three stems follow the theme.
1 @ 232,62 Stem | DRUMS, HARMONICS and VOCALS each have their own column.
2 @ 205,151 THEME | Follows the theme you chose. The outlined tile is the current choice.
3 @ 360,300 Bass | A four-stem track's bass always follows the theme.
4 @ 470,341 Preview | The three colours as the STEMS waveform draws them.
5 @ 470,399 RESET STEM COLORS | Puts all three back to THEME.
```

The choices are THEME, BLUE, RED, GREEN, AMBER, PURPLE, CYAN, PINK and WHITE. A fixed colour stays when you change theme.

```gallery {modern}
modern:stems-custom-colours Amber drums, cyan harmonics and pink vocals on half rows
modern:stems-waveform-two-tone-wide The STEMS waveform zoomed out
modern:stems-waveform-two-tone-close The STEMS waveform zoomed in
modern:play-stems-zoom-in The STEMS waveform at full zoom on the play screen
modern:play-stems-deck2-not-ready Deck 2's stems not loaded yet: it draws 3-BAND and says NO STEM WAVE YET
```

## Stem readiness

Each deck's stem buttons tell you whether its stems can play. When they cannot, the stem buttons go dark and the last button names the reason. Where you can fix it from the player, that button does it.

| On the stem row | What it means | What to do |
|---|---|---|
| STEMS READY | The stems play | Nothing |
| NO STEM ENGINE, with SET UP | The USB loader has no stem engine | Update the USB loader with XZ Mods on your computer, then restart the player |
| STEMS OFF, with TURN ON | USB STEMS is off | Tap TURN ON, or turn USB STEMS on in MODS |
| NO TRACK LOADED | The deck is empty | Load a track from USB on this deck |
| NOT PREPARED, with HOW TO FIX | This track has no prepared stems | Prepare it in XZ Mods on your computer, then load it again |
| LOADING STEMS | The stems are loading from USB | Wait a moment |
| PLAY TO ALIGN | The stems are loaded and wait for the track | Press PLAY. The stems lock to the track a few seconds in |
| STEM SET ERROR, with HOW TO FIX | A stem file is missing or damaged | Verify the track in XZ Mods on your computer |

```gallery {classic}
classic:theme-0-inline-usb-ready STEMS READY on deck 1
classic:theme-0-inline-usb-engine NO STEM ENGINE on both decks
classic:theme-0-inline-usb-off STEMS OFF on both decks
classic:theme-0-inline-usb-no-track NO TRACK LOADED on deck 1
classic:theme-0-inline-usb-not-prepared NOT PREPARED on deck 1, with HOW TO FIX
classic:theme-0-inline-usb-loading LOADING STEMS on deck 1
classic:theme-0-inline-usb-aligning PLAY TO ALIGN on deck 1
classic:theme-0-inline-usb-error STEM SET ERROR on deck 1
classic:theme-0-inline-unavailable USB STEMS off and no deck ready: TURN ON for all decks
```

In MODERN the stem buttons on a deck that cannot play stems read **STEMS ARE OFF** or **NO STEMS ON THIS TRACK**, and only the hot-cue lamps stay in the pad row. In 4 DECKS such a deck shows no stem buttons, and its waveform keeps the whole lane.

### USB STEMS setup

Tap **SET UP** or **HOW TO FIX** on a stem row, or **SET UP USB STEMS** in MODS, to open USB STEMS setup. It checks one deck at a time and walks you through the three steps: check the engine, turn stems on, load a prepared track.

```figure classic:theme-0-usb-setup-aligning MODS · USB STEMS
Deck 1 has found its OverCue stems and waits for PLAY.
1 @ 290,20 DECK 1 / DECK 2 | The deck this setup checks.
2 @ 485,82 Steps | Check the engine, turn stems on, load a prepared track.
3 @ 330,140 Deck status | Where this deck stands now.
4 @ 275,162 FOUND | The stem format found for this track.
5 @ 430,184 Next step | What to do next, in one line.
6 @ 470,328 Action | Does the next step, or checks the deck again.
```

```gallery
classic:theme-0-usb-setup-ready STEMS READY: STEMS READY / BACK TO PLAY
classic:theme-0-usb-setup-engine NO STEM ENGINE
classic:theme-0-usb-setup-off STEMS OFF
classic:theme-0-usb-setup-no-track NO TRACK LOADED
classic:theme-0-usb-setup-not-prepared NOT PREPARED
classic:theme-0-usb-setup-loading LOADING STEMS
classic:theme-0-usb-setup-error STEM SET ERROR, with the missing file named
classic:theme-0-stem-setup The setup page reached from MODS
```

## Stems in DJ software {experimental}

When the XZ plays from DJ software on a computer over USB, the stem buttons can switch the software's stems instead. Open **STEMS IN DJ SOFTWARE** on the STEMS page and follow its four steps for each deck: **DETECT**, **MAP STEMS**, **LIVE TEST** and **CONFIRM**.

> **Experimental.** Stem switching works after the live test. Levels and BYPASS for DJ software need more checking, and the software has no stem bypass over USB.

```figure classic:theme-{look}-pc-stems MODS · STEMS IN DJ SOFTWARE
Deck 1 before setup, with no computer deck found yet.
1 @ 240,84 Deck | The deck this setup checks.
2 @ 560,84 Status | The deck's source, the link to the computer and whether stem switching has been tested.
3 @ 466,142 Steps | DETECT, MAP STEMS, LIVE TEST and CONFIRM.
4 @ 590,356 DETECT PC | Checks that this deck plays from the computer.
```

Connect the XZ to the computer by USB and open your DJ software in Performance mode before you start. A deck playing from the computer then shows a stem row for the software, with three or four stems.

```gallery {classic}
classic:theme-0-inline-pc4-ready Four software stems ready on deck 1, the XZ's own stems on deck 2
classic:theme-0-inline-pc4-feedback Four stems with the software's on and off state shown
classic:theme-0-inline-pc4-pending A switch sent, waiting for the software to confirm
classic:theme-0-inline-pc4-pressed A stem button held down
classic:theme-0-inline-pc4-setup Not tested yet: SET UP
classic:theme-0-inline-pc4-link-lost PC LINK LOST: nothing is sent
classic:theme-0-inline-pc4-unknown Source not confirmed yet
classic:theme-0-inline-pc3-ready Three software stems ready
classic:theme-0-inline-pc3-feedback Three stems with feedback
classic:theme-0-inline-pc3-pending Three stems, a switch pending
classic:theme-0-inline-pc3-pressed Three stems, a button held
classic:theme-0-inline-pc3-setup Three stems, not tested yet
classic:theme-0-inline-pc3-link-lost Three stems, PC link lost
classic:theme-0-inline-pc3-unknown Three stems, source not confirmed
```

```gallery {classic}
classic:theme-0-pc4-setup-setup Setup, four stems, before the live test
classic:theme-0-pc4-setup-ready Setup done, four stems
classic:theme-0-pc4-setup-feedback Four stems with feedback
classic:theme-0-pc4-setup-pending Four stems, a switch pending
classic:theme-0-pc4-setup-link-lost Four stems, link lost
classic:theme-0-pc4-setup-unknown Four stems, source not confirmed
classic:theme-0-pc3-setup-setup Setup, three stems, before the live test
classic:theme-0-pc3-setup-ready Setup done, three stems
classic:theme-0-pc3-setup-feedback Three stems with feedback
classic:theme-0-pc3-setup-pending Three stems, a switch pending
classic:theme-0-pc3-setup-link-lost Three stems, link lost
classic:theme-0-pc3-setup-unknown Three stems, source not confirmed
```

## If stems do not play

| You see | What to do |
|---|---|
| No stem buttons at all in MODERN | Tap **STEMS** in the top bar until the layout you want shows. HIDDEN shows none. In 4 DECKS, tap **STEMS** once. A deck whose track has no stems shows none |
| STEMS ARE OFF | Turn USB STEMS on in MODS under **STEMS** |
| NO STEMS ON THIS TRACK or NOT PREPARED | Prepare the track in the XZ Mods app, copy it to the USB, then load it again |
| PLAY TO ALIGN does not go away | Let the track play for a few seconds. If it stays, check the track in the XZ Mods app |
| Stems on decks 3 and 4 | Not available. Stems work on decks 1 and 2 only |
