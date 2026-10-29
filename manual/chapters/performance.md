---
interface: both
settings: jump_enabled, jump_1, jump_2, jump_3, jump_4, jump_5, jump_6, jump_7, jump_8, gate, smart, key_feedback
---

XZ Mods adds performance features to the pads and deck keys: two pages of beat jump sizes you choose, gate cue, smart cue, key shift and KEY SYNC, and RAM tracks. Their settings live on one MODS page, PLAY. Every change saves to your USB as you make it.

## The PLAY page

Open MODS and choose PLAY in the section rail.

```figure classic:theme-{look}-play MODS · PLAY
The PLAY page with the default settings: gate cue and smart cue off, the default jump sizes, and the button above [[LINK]] on INTERFACE.
1 @ 300,84 BEAT JUMP PADS | Opens the beat jump editor. The numbers are your page 1 and page 2 sizes in beats.
2 @ 250,132 GATE CUE | Hot cues play only while you hold the pad. Off by default.
3 @ 600,132 SMART CUE | A hot cue press also moves the CUE point there. Off by default.
4 @ 230,196 KEYS | Opens the key page: key shift and KEY SYNC for all four decks.
5 @ 605,196 RAM TRACKS | Opens RAM TRACKS: copy a deck's track into memory.
6 @ 350,260 BUTTON ABOVE LINK | What the button above LINK does, alone and with SHIFT. See Controls on the player.
7 @ 200,425 Dock | Explains the highlighted row in plain words.
8 @ 718,425 Works on | The decks the highlighted feature works on, in mixer order 3 1 2 4. A dim number is a deck it does not work on, with the reason below.
```

If the cue engine has not reported on this USB loader, GATE CUE and SMART CUE show as SET UP GATE CUE and SET UP SMART CUE instead of switches. See [Feature setup](#feature-setup).

## Beat jump

In BEAT JUMP mode the eight pads form four pairs: A/B, C/D, E/F and G/H. The first pad of a pair jumps back, the second jumps forward, by the same number of beats. With a loop on, the loop moves by that amount instead.

You get two pages of sizes:

| Page | How you select it | Default sizes A/B, C/D, E/F, G/H |
|---|---|---|
| Page 1 | Press [[BEAT JUMP]] | 1, 2, 4, 8 beats |
| Page 2 | Hold [[SHIFT]], press [[BEAT JUMP]], let go of [[SHIFT]] | 16, 32, 64, 128 beats (4, 8, 16, 32 bars) |

```explainer beat-jump Beat jump, page 1 and page 2
Page 1 | Press [[BEAT JUMP]]. With the default sizes the pairs A/B, C/D, E/F and G/H jump 1, 2, 4 and 8 beats. The first pad of a pair jumps back, the second forward.
Page 2 | Hold [[SHIFT]], press [[BEAT JUMP]], let go of [[SHIFT]]. The pairs jump 16, 32, 64 and 128 beats, and the pads light warm red and amber.
With a loop on | With a loop on, the loop moves by the jump instead.
```

1. Press [[BEAT JUMP]] for page 1, or [[SHIFT]] + [[BEAT JUMP]] for page 2.
2. Let go of [[SHIFT]].
3. Press a pad. The first pad of a pair jumps back, the second forward.

Each deck remembers its own page. Choosing another pad mode and coming back with [[BEAT JUMP]] gives page 1 again. On page 2 the pads light warm red and amber, so you can see which page you are on. [[SHIFT]] + pad keeps its stock job on decks 1 and 2.

### Change the jump sizes

Open MODS > PLAY > BEAT JUMP PADS. The editor shows the pads as they sit on the deck: A to D on top, E to H below.

```figure classic:theme-{look}-beat-jump MODS · BEAT JUMP PADS
Editing page 2. Pads A and B are selected and jump 16 beats.
1 @ 292,70 PAGE 1 | Edit the sizes BEAT JUMP gives.
2 @ 408,47 PAGE 2 | Edit the sizes SHIFT + BEAT JUMP gives. The filled tab is the page you are editing.
3 @ 640,47 PAGE 1: STOCK or MY SIZES | STOCK keeps the player's own 1, 2, 4 and 8 beats on page 1. MY SIZES uses yours. Page 2 always uses yours.
4 @ 251,104 Pad pair | Two pads with one size. The left pad jumps back, the right pad forward. The selected pair has a light outline.
5 @ 228,154 Beats | The size in beats.
6 @ 228,193 Bars | The same size in bars. 4 beats make 1 bar.
7 @ 735,196 Readout | The selected pair and its size.
8 @ 172,342 Sizes | Tap a size to give it to the selected pair. The filled size is the current one.
9 @ 100,425 Dock | What the highlighted control does.
10 @ 718,425 Works on | Beat jump works on all four decks.
```

1. Choose PAGE 1 or PAGE 2.
2. For page 1, choose PAGE 1: MY SIZES. With STOCK, page 1 keeps the player's own sizes.
3. Tap a pad in the pair you want to change.
4. Tap a size in the row along the bottom.

Without the touch screen, turn the rotary selector to a pair, push it, then turn to step the size. It stops at the smallest and largest size. Push or press [[BACK]] when you are done.

| Sizes you can choose | |
|---|---|
| Beats | 1, 2, 4, 8, 16, 32, 64, 128, 256 |
| Bars | 1/4, 1/2, 1, 2, 4, 8, 16, 32, 64 |

The sizes apply to every deck. UTILITY's own beat jump setting is separate and does not change them.

| Setting | Choices | Default |
|---|---|---|
| PAGE 1 | STOCK, MY SIZES | MY SIZES |
| Page 1 A/B, C/D, E/F, G/H | 1 to 256 beats | 1, 2, 4, 8 beats |
| Page 2 A/B, C/D, E/F, G/H | 1 to 256 beats | 16, 32, 64, 128 beats |

### Decks 3 and 4

Decks 3 and 4 use the same sizes and both pages. They jump along the track's beat grid, or by its BPM when there is no grid. A track with neither jumps at 120 BPM, as on decks 1 and 2. With a loop on, the loop moves.

### When a jump is refused

A jump the deck cannot make does nothing else. XZ Mods never silently shortens the selected distance. The screen shows why:

| Notice | Deck | What it means |
|---|---|---|
| JUMP UNAVAILABLE: TRACK EDGE, BUFFER OR DECK STATE | 1 or 2 | The player refused the jump. It would pass the start or end of the track, or the deck cannot jump right now. |
| JUMP UNAVAILABLE: BEFORE TRACK START | 3 or 4 | The jump would land before the start of the track. |
| JUMP UNAVAILABLE: AFTER TRACK END | 3 or 4 | The jump would land after the end of the track. |
| JUMP UNAVAILABLE: LOOP MOVE OUT OF TRACK | 3 or 4 | Moving the loop would push it past the start or end. |
| JUMP UNAVAILABLE: NO BEAT GRID OR BPM | 3 or 4 | The track's beat grid is damaged, so beats can't be counted. Analyse it again in your DJ software. |

## Gate cue and smart cue

Both change what a hot cue pad does in HOT CUE mode. Turn them on in MODS > PLAY. Both are off by default, and each works on its own.

| Feature | What a hot cue pad does |
|---|---|
| GATE CUE | The track plays from the hot cue only while you hold the pad. Let go and it jumps back to the hot cue and pauses. Use it to stab hot cues like samples. Press [[PLAY/PAUSE]] while you hold the pad to keep playing after you let go. |
| SMART CUE | Calling a hot cue also moves the CUE point there, so [[CUE]] takes you back to the last hot cue you used. Recording a new hot cue does not move it. |

Gate cue and smart cue work on all four decks. On decks 1 and 2 they need the cue engine; see below.

### Feature setup

When the cue engine has not reported on a deck, MODS shows a short setup instead of the switch. It checks the feature on a loaded USB track in four steps: detect, choose deck, enable and test.

```figure classic:theme-{look}-feature-setup MODS · FEATURE SETUP
The setup page at its first step, checking gate cue. For smart cue it reads SMART CUE.
1 @ 270,60 Feature | The feature this setup checks.
2 @ 390,82 Steps | The four steps. The button at the bottom always runs the next one.
3 @ 410,20 Deck | Choose deck 1 or deck 2.
4 @ 375,145 What to do | The instruction for the current step.
5 @ 395,180 Track | The track loaded on the chosen deck.
6 @ 510,209 Status | FEATURE DETECTED, or what to do when it is missing.
7 @ 310,356 Next step | Runs the step. Missing support keeps its reason on the screen.
```

1. Load a track from USB on deck 1 or deck 2.
2. Press DETECT FEATURE.
3. Choose the deck, then enable the feature.
4. Close MODS and test it: hold and release a hot cue for gate cue, or call a hot cue and check that the cue point follows for smart cue.
5. Reopen the setup and confirm.

If the status still asks you to load a USB track while one is loaded, prepare the USB again with the current [XZ Mods app](desktop-app).

## Keys and KEY SYNC

XZ Mods can move the key of any deck a semitone at a time, and match a deck's key to the master deck with KEY SYNC. The master can be any deck, 1 to 4. The key display then shows the key the track plays in now.

### From the deck

1. Load tracks with analysed keys, and make the deck you want to match to the tempo master.
2. Hold [[SHIFT]] and press [[SYNC]] on the deck you want to move. On a side that drives deck 3 or 4, this moves that deck.
3. The deck shifts to a key that fits the master deck and MASTER TEMPO turns on. A notice shows the new key and the master deck, with RELATIVE KEY when it matches the relative major or minor.

To end KEY SYNC, press [[SHIFT]] + [[SYNC]] again, or press [[MASTER TEMPO]]. Both return the deck to its own key and put MASTER TEMPO back the way it was. After that, [[MASTER TEMPO]] works as on a stock player.

KEY SYNC changes the key only. The tempo, SYNC and the master deck stay as they were.

All four decks can hold key shifts and KEY SYNC together. Before starting a new shift on deck 3 or 4, the player checks its measured audio workload. If there is too little headroom, it shows **KEY SHIFT REFUSED: AUDIO LOAD TOO HIGH** and leaves the current keys unchanged. Shifts already in use keep running. SYSTEM READOUT shows render times and the number of active key shifters.

If KEY SYNC cannot work, a notice or the deck's key display shows why for a few seconds:

| Message | What to do |
|---|---|
| KEY ENGINE OFF: SET UP KEYS | This USB loader has no key engine. Open MODS > PLAY > KEYS and press SET UP KEYS, or prepare the USB again with the current XZ Mods app. |
| DECK 1 TRACK HAS NO KEY (or another deck) | The track on this deck has no analysed key. Analyse it in your DJ software. |
| LOAD AN ANALYZED TRACK / SET UP KEYS | The deck has no track with an analysed key. Load one. |
| LOAD A KEYED TRACK ON DECK 2 (or another deck) | The master deck has no track with an analysed key. |
| THIS DECK IS MASTER, MAKE DECK 2 MASTER FIRST | The deck you pressed is the master, so there is nothing to match. Make another deck the master. On the KEYS page the master deck has no KEY SYNC button and says it is the master. |
| SELECT ONE LOCAL MASTER / SET UP KEYS | No deck on this player is the tempo master. Press [[MASTER]] on the deck to match to. |
| VERIFY MASTER PLAYING KEY / SET UP KEYS | The master deck's playing key is not known yet. Let it play a moment, then try again. |
| MASTER CHANGED / RETRY KEY SYNC, TRACK CHANGED / RETRY KEY CONTROL | The master or the track changed before the key moved. Press again. |
| KEY SHIFT REFUSED: AUDIO LOAD TOO HIGH | Measured audio work leaves too little headroom for a new shift. Existing shifts keep running. Check SYSTEM READOUT and retry when the load falls. |
| SHIFT OUT OF RANGE, KEY RANGE IS -12 TO +12 SEMITONES | The key that fits is further away than the deck can shift. |
| EMERGENCY LOOP / LOAD NEXT TRACK | The deck lost its track and loops. Load another track first. |

### The KEYS page

MODS > PLAY > KEYS does the same from the screen, and adds key shift by semitones. On the play screen, tapping a deck's key opens the same controls for that deck.

```figure classic:theme-{look}-active-key-setup MODS · KEYS
The KEYS page for deck 1 with the key engine ready. The deck is in its own key.
1 @ 242,20 Deck | Choose deck 1, 2, 3 or 4. Where there is room, a tab adds USB DECK for decks 1 and 2 and SOFTWARE for decks 3 and 4.
2 @ 197,186 KEY DOWN | Lowers the deck's key one semitone.
3 @ 333,186 RESET | Shows the current shift. Press it to return the deck to its own key.
4 @ 471,186 KEY UP | Raises the deck's key one semitone.
5 @ 660,186 KEY SYNC | Shifts the deck to a key that fits the master deck, as SHIFT + SYNC does.
6 @ 300,276 Deck keys | Each deck's own key, the key it plays in now, the cents and SYNC or MASTER. The rows are empty in this picture.
7 @ 718,425 Works on | Key shift and KEY SYNC work on all four decks.
```

With MASTER TEMPO on, the key holds while the tempo moves. Without it, the tempo changes the key too, and the key display shows that.

When the key engine has not reported, the buttons show an amber dot, the dock says NOT READY and **SET UP KEYS** appears. It opens a short setup that checks the key engine and the chosen deck's track, and ends with a live test:

```gallery {modern}
classic:theme-24-key-setup The KEYS page before the key engine reports, with SET UP KEYS
```

```gallery {classic}
classic:theme-0-key-setup The KEYS page before the key engine reports, with SET UP KEYS
```

### The key on the MODERN play screen {modern}

In MODERN each deck's panel beside its waveform shows its key. With PLAYING KEY on, the big key is the key the track plays in now, after tempo and key shift.

```figure modern:key-sync-lit MODERN · KEY SYNC
Deck 2 holds KEY SYNC: it plays Bb minor, moved up from its own A minor, 12 cents sharp.
1 @ 112,200 Key panel | KEY SYNC, the key the deck plays in now, the cents left over and the track's own key.
2 @ 112,95 Deck in its own key | Deck 1 plays in its own key, so the panel shows only KEY and the key.
3 @ 612,378 MT | MASTER TEMPO. It blinks while KEY SYNC holds on that deck.
```

| Part of the key panel | Meaning |
|---|---|
| KEY SYNC | KEY SYNC holds on this deck. |
| The big key | The key the deck plays in now. Green or amber compares it with the master deck's key; see [Key and KEY SYNC](play-screen#key-and-key-sync). |
| +12¢ | Cents between the playing key and the nearest semitone. |
| ORIGINAL | The track's own key, shown when the playing key differs. |
| ▲1 | The key shift in semitones, on older USB loaders that do not report the analysed key. |

```gallery {modern}
modern:key-sync-dark KEY SYNC with the MT lamp in the dark half of its blink
modern:key-tempo-cents With MASTER TEMPO off, +2.20 % tempo lifts A minor by 38 cents
modern:key-tempo-semitone With MASTER TEMPO off and +6.10 % tempo, the track plays in Bb minor
modern:key-alphanumeric Keys in WHEEL notation, deck 1 shifted down two semitones
modern:key-old-runtime An older USB loader shows the key and its shift step only
modern:key-unknown-and-odd No key on deck 1, and a key name too long for the panel
modern:key-indicators-hidden The top bar lamps and QUANTIZE lamps hidden
modern:key-indicators-hidden-fx-on The same, with BEAT FX on
```

### The key on the CLASSIC play screen {classic}

In CLASSIC, XZ Mods draws the playing key where the player shows its beat countdown, under the key:

| What you see | Meaning |
|---|---|
| KEY and a key | The track plays in its own key. |
| TRACK KEY | Deck 3 or 4 shows the track's own key until it reports the key it plays in. |
| Own key ▸ playing key | The tempo or a key shift moved the key. |
| SYNC | KEY SYNC holds on this deck. The tile turns green while the key matches the master. |
| MASTER | This deck is the master that KEY SYNC matches to. |
| +12c in amber | Cents between the playing key and the nearest semitone. |
| Green or amber key | The playing key compared with the master deck's key, as KEY MATCH COLOURS sets it. |
| A message in red | KEY SYNC could not work; see the table above. |

### PLAYING KEY

PLAYING KEY is on by default. Turn it off in MODS > SCREEN to show the track's own key instead. In CLASSIC, off brings the beat countdown back. Key notation (AS ON THE USB, MUSICAL or WHEEL) is set on the same page; see [Screen, interfaces and themes](screen-and-themes).

Decks 3 and 4 show their playing key too, after tempo and key shift. Until a software deck reports it, the deck shows the track's own key.

## RAM tracks {experimental}

RAM TRACKS copies the track on a deck into the player's memory. The deck then plays from memory, so it keeps playing if the music USB goes out. It works on all four decks. Decks 3 and 4 need the loopback cable connected to start a copy.

```figure classic:theme-{look}-ram-setup MODS · RAM TRACKS
Deck 1 on a USB loader without the RAM engine.
1 @ 240,116 Deck | Choose deck 1, 2, 3 or 4.
2 @ 450,155 State | The deck and the state of its RAM copy.
3 @ 358,180 Source | The track loaded on that deck, or what to do next.
4 @ 590,218 Progress | How much of the track is copied, in MiB.
5 @ 375,242 Memory | How much of the 128 MiB the copies use, prepared stems apart, then each deck's copy and state.
6 @ 330,308 Action | The one next step for this state: set up, choose a track, load, cancel, retry or release.
7 @ 325,355 Scope | What RAM holds for this deck. COPIES AUDIO / STEMS STAY ON USB means only the original audio goes into RAM.
8 @ 548,452 Reason | Why the feature is not available. Here the RAM engine is missing.
```

1. Load a track from USB on the deck.
2. Open MODS > PLAY > RAM TRACKS and choose that deck.
3. Press LOAD TRACK INTO RAM. Keep the USB connected while it copies.
4. Wait for ORIGINAL AUDIO IN RAM.

A track that plays is also copied on its own when there is room. CANCEL COPY or RELEASE RAM COPY stops that for the loaded track.

| State | Button | What it means |
|---|---|---|
| RAM ENGINE NOT DETECTED | SET UP RAM TRACKS | This USB loader has no RAM engine, or it has not reported yet. The setup checks the engine. If it stays, prepare the USB again with the current XZ Mods app. |
| LOOPBACK CABLE NOT CONNECTED | SET UP FOUR DECKS | Deck 3 or 4 is chosen and decks 3 and 4 are not running. The button opens 4 DECKS. |
| NOT IN RAM | LOAD TRACK INTO RAM | The deck plays from USB. |
| COPYING TO RAM | CANCEL COPY | The copy is running. Cancel leaves the track playing from USB. |
| ORIGINAL AUDIO IN RAM | RELEASE RAM COPY | The deck plays from memory. Release frees the memory and needs the USB still connected. |
| COPY FAILED | RETRY LOAD INTO RAM | The track keeps playing from USB. Retry; if it fails again, load the track again from USB. |
| USB OUT / RAM COPY ONLY | BACK TO PLAY | The USB is out and the deck plays from memory. The play screen says so in the deck's lane; see [below](#when-the-music-usb-goes-out). Load another track to release the copy. |

When a copy does not fit, the page says why and offers the next step:

| Message | What to do |
|---|---|
| This file exceeds the 64 MiB RAM limit. | Press CHOOSE ANOTHER TRACK. |
| Other RAM tracks leave too little space for this file. | Press MANAGE RAM TRACKS, choose a deck with a copy and release it. |
| Free RAM cannot keep the 64 MiB safety reserve. | Release another copy the same way. |

- One track per deck. Each file can be up to 64 MiB, and all four decks together up to 128 MiB. The player always keeps 64 MiB of its memory free.
- RAM TRACKS copies the original audio. On decks 1 and 2, the line under the action button says what else is held; see the table below.
- On decks 3 and 4 the waveform, beat grid and cues already loaded stay with the copy when the USB goes out. Cues you change then are kept for the session and not written to a USB.
- If the loopback cable goes out, decks 3 and 4 keep their copies and pause, as they do without RAM. They do not start again on their own when the cable returns.
- Loading another track on the deck drops its RAM copy.

### When the music USB goes out {experimental}

```explainer ram-usb A deck playing from RAM when the USB goes out
NOT IN RAM | The deck plays from USB.
COPYING TO RAM | Press LOAD TRACK INTO RAM. Keep the USB connected while it copies.
ORIGINAL AUDIO IN RAM | The deck plays from memory.
USB OUT / RAM COPY ONLY | The music USB goes out and the deck plays on from memory. A teal card, WHOLE TRACK IN RAM · PLAYABLE, takes the waveform's place and says how to get it back.
```

A deck with its whole track in RAM keeps playing when the music USB goes out. The waveform was read from the USB, so the deck's lane cannot draw it any more. A teal card takes its place: **WHOLE TRACK IN RAM · PLAYABLE**, and under it how to get the waveform back.

Everything else about the deck stays on screen: title and artist, time and remain, BPM, tempo, key, the lamps, the hot cues and the playhead.

```gallery {modern}
modern:ram-01-two-decks-deck-1 2 DECKS: deck 1 plays from RAM. Deck 2 is unchanged
modern:ram-02-two-decks-both Both decks play from RAM
modern:ram-03-four-decks-2-4 4 DECKS: decks 2 and 4 play from RAM
modern:ram-04-mixer-2-4 MIXER order: the card stays with its deck
```

```figure classic:classic-ram-two-decks PLAY · CLASSIC {classic}
Deck 1 plays from RAM after the USB went out. The stem rows, the deck column and the deck strip stay as the player shows them.
1 @ 640,72 RAM card | The deck plays its whole track from RAM. The second line says how to get the waveform back.
```

To get the waveform back, reinsert the same USB or insert another, then load a track from it. The card goes as soon as the deck has a waveform to draw again. Loading a track also ends the deck's RAM copy, as it always does.

On decks 3 and 4 the waveform loaded before the USB went out stays, so the card shows there only if the waveform had not loaded yet.

| Line under the button, decks 1 and 2 | What it means |
|---|---|
| COPIES AUDIO / STEMS STAY ON USB | No copy yet. RAM TRACKS copies the audio, not the stems. |
| AUDIO IN RAM / STEMS NEED USB | The audio is in RAM. The stems still read the USB. |
| AUDIO + STEMS IN RAM / WAVES KEPT | The deck's prepared stems are held in RAM as well, and the loaded waveforms stay. |

> **Experimental.** Playing on from RAM after the USB goes out has not been confirmed on a player yet. Keep the USB in during a set.

## Quantize

On decks 1 and 2, quantize is the player's own: [[DECK QUANTIZE]] and UTILITY work as on a stock player. In MODERN each deck shows its QUANTIZE lamp and beat value above its time. You can hide them in MODS > SCREEN > LAMPS AND TITLES.

While a side drives deck 3 or 4:

| Key | Action |
|---|---|
| [[DECK QUANTIZE]] | Switches quantize for every deck. Decks 1 and 2 switch through the player as before. |
| [[SHIFT]] + [[DECK QUANTIZE]] | Switches quantize only for the deck of that side. |

With quantize on, decks 3 and 4 put cue points, loop in and out, hot cues and beat and slip loops on the beat grid, at a one-beat value. [[SYNC]] then also locks the deck to the master's beat. [Four decks](four-decks) has the rest of what decks 3 and 4 do.
