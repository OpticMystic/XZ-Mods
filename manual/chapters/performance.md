---
interface: both
settings: jump_enabled, jump_1, jump_2, jump_3, jump_4, jump_5, jump_6, jump_7, jump_8, gate, smart, key_feedback
---

XZ Mods adds performance features to the pads and deck keys: two pages of beat jump sizes you choose, gate cue, smart cue, key shift and KEY SYNC, and RAM tracks. Their settings live on one MODS page, PLAY. Every change saves to your USB as you make it.

## The PLAY page

Open MODS and choose PLAY in the section rail.

```figure classic:theme-{look}-play MODS · PLAY
The PLAY page with the default settings: gate cue and smart cue off, the default jump sizes, and the button next to LINK on INTERFACE.
1 @ 300,84 BEAT JUMP PADS | Opens the beat jump editor. The numbers are your page 1 and page 2 sizes in beats.
2 @ 250,132 GATE CUE | Hot cues play only while you hold the pad. Off by default.
3 @ 600,132 SMART CUE | A hot cue press also moves the CUE point there. Off by default.
4 @ 318,180 HOT CUE PREVIEW | Not in this version of XZ Mods.
5 @ 545,180 KEYS | Opens the key page: key shift and KEY SYNC for decks 1 and 2.
6 @ 310,244 X-PAD SAMPLER | Not in this version of XZ Mods.
7 @ 605,244 RAM TRACKS | Opens RAM TRACKS: copy a deck's track into memory.
8 @ 350,308 BUTTON NEXT TO LINK | What the media button above LINK does, alone and with SHIFT. See Controls on the player.
9 @ 200,425 Dock | Explains the highlighted row in plain words.
10 @ 718,425 Works on | The decks the highlighted feature works on, in mixer order 3 1 2 4. A dim number is a deck it does not work on, with the reason below.
```

If the cue engine has not reported on this USB loader, GATE CUE and SMART CUE show as SET UP GATE CUE and SET UP SMART CUE instead of switches. See [Feature setup](#feature-setup).

## Beat jump

In BEAT JUMP mode the eight pads form four pairs: A/B, C/D, E/F and G/H. The first pad of a pair jumps back, the second jumps forward, by the same number of beats. With a loop on, the loop moves by that amount instead.

You get two pages of sizes:

| Page | How you select it | Default sizes A/B, C/D, E/F, G/H |
|---|---|---|
| Page 1 | Press [[BEAT JUMP]] | 1, 2, 4, 8 beats |
| Page 2 | Hold [[SHIFT]], press [[BEAT JUMP]], let go of [[SHIFT]] | 16, 32, 64, 128 beats (4, 8, 16, 32 bars) |

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

Decks 3 and 4 use the same sizes and both pages. They jump along the track's beat grid, or by its BPM when there is no grid. With a loop on, the loop moves.

### When a jump is refused

A jump the deck cannot make does nothing else. XZ Mods never falls back to a shorter jump or a jump by time. The screen shows why:

| Notice | Deck | What it means |
|---|---|---|
| JUMP UNAVAILABLE: TRACK EDGE, BUFFER OR DECK STATE | 1 or 2 | The player refused the jump. It would pass the start or end of the track, or the deck cannot jump right now. |
| JUMP UNAVAILABLE: BEFORE TRACK START | 3 or 4 | The jump would land before the start of the track. |
| JUMP UNAVAILABLE: AFTER TRACK END | 3 or 4 | The jump would land after the end of the track. |
| JUMP UNAVAILABLE: LOOP MOVE OUT OF TRACK | 3 or 4 | Moving the loop would push it past the start or end. |
| JUMP UNAVAILABLE: NO BEAT GRID OR BPM | 3 or 4 | The track has no beat grid and no BPM to count beats with. Analyse it in rekordbox. |

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
The setup page at its first step. It is drawn here with its X-PAD title; for gate cue and smart cue it reads GATE CUE or SMART CUE.
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

XZ Mods can move the key of decks 1 and 2 a semitone at a time, and match one deck's key to the other with KEY SYNC. The key display then shows the key the track plays in now.

### From the deck

1. Load tracks with analysed keys on decks 1 and 2.
2. Hold [[SHIFT]] and press [[SYNC]] on the deck you want to move.
3. The deck shifts to a key that fits the other deck and MASTER TEMPO turns on. A notice shows the new key and the target deck, with RELATIVE KEY when it matches the relative major or minor.

To end KEY SYNC, press [[SHIFT]] + [[SYNC]] again, or press [[MASTER TEMPO]]. Both return the deck to its own key and put MASTER TEMPO back the way it was. After that, [[MASTER TEMPO]] works as on a stock player.

If KEY SYNC cannot work, the deck's key display shows why for a few seconds:

| Message | What to do |
|---|---|
| KEY ENGINE OFF: SET UP KEYS | This USB loader has no key engine. Open MODS > PLAY > KEYS, or prepare the USB again with the current XZ Mods app. |
| DECK 1 TRACK HAS NO KEY (or DECK 2) | The track on this deck has no analysed key. Analyse it in rekordbox. |
| LOAD A KEYED TRACK ON DECK 2 (or DECK 1) | The other deck has no track with an analysed key. |
| SHIFT OUT OF RANGE | The key that fits is further away than the deck can shift. |

### The KEYS page

MODS > PLAY > KEYS does the same from the screen, and adds key shift by semitones.

```figure classic:theme-{look}-active-key-setup MODS · KEYS
The KEYS page for deck 1 with the key engine ready. The deck is in its own key.
1 @ 410,20 Deck | Choose deck 1 or deck 2.
2 @ 197,186 KEY DOWN | Lowers the deck's key one semitone.
3 @ 333,186 RESET | Shows the current shift. Press it to return the deck to its own key.
4 @ 471,186 KEY UP | Raises the deck's key one semitone.
5 @ 660,186 KEY SYNC | Shifts the deck to a key that fits the other deck, as SHIFT + SYNC does.
6 @ 300,276 Deck keys | Each deck's own key, the key it plays in now, the cents and SYNC or MASTER.
7 @ 562,456 Reason | Why some decks are left out. Here decks 3 and 4 cannot shift key yet.
8 @ 718,425 Works on | Key shift and KEY SYNC work on decks 1 and 2.
```

With MASTER TEMPO on, the key holds while the tempo moves. Without it, the tempo changes the key too, and the key display shows that.

When the key engine has not reported, the buttons show an amber dot and the dock says NOT READY:

```gallery {modern}
classic:theme-24-key-setup The KEYS page before the key engine reports
```

```gallery {classic}
classic:theme-0-key-setup The KEYS page before the key engine reports
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
| The big key, amber | The key the deck plays in now. Amber means it differs from the track's own key. |
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
| Own key ▸ playing key | The tempo or a key shift moved the key. |
| SYNC | KEY SYNC holds on this deck. The key shows in green. |
| MASTER | This deck is the reference for the other deck's KEY SYNC. |
| +12c in amber | Cents between the playing key and the nearest semitone. |
| A message in red | KEY SYNC could not work; see the table above. |

### PLAYING KEY

PLAYING KEY is on by default. Turn it off in MODS > SCREEN to show the track's own key instead. In CLASSIC, off brings the beat countdown back. Key notation (AS ON THE USB, MUSICAL or WHEEL) is set on the same page; see [Screen, interfaces and themes](screen-and-themes).

Decks 3 and 4 always show the track's own key. It does not follow their tempo, and they cannot shift key in this version.

## RAM tracks {experimental}

RAM TRACKS copies the track on deck 1 or deck 2 into the player's memory. The deck then plays from memory, so it keeps playing if the USB goes out.

```figure classic:theme-{look}-ram-setup MODS · RAM TRACKS
Deck 1 on a USB loader without the RAM engine.
1 @ 240,116 Deck | Choose the deck whose track goes into RAM.
2 @ 450,155 State | The deck and the state of its RAM copy.
3 @ 325,175 Source | The track loaded on that deck.
4 @ 590,194 What to do | The next step, or why the last one failed.
5 @ 260,230 Progress | How much of the track is copied.
6 @ 330,308 Action | The one next step for this state: check, load, cancel, retry or release.
7 @ 445,358 Limits | One track per deck. Only the original audio goes into RAM.
8 @ 548,452 Reason | Decks 3 and 4 always read the USB.
```

1. Load a track from USB on deck 1 or deck 2.
2. Open MODS > PLAY > RAM TRACKS and choose that deck.
3. Press LOAD TRACK INTO RAM. Keep the USB connected while it copies.
4. Wait for ORIGINAL AUDIO IN RAM.

| State | Button | What it means |
|---|---|---|
| RAM ENGINE NOT DETECTED | CHECK RAM ENGINE | This USB loader has no RAM engine, or it has not reported yet. If it stays, prepare the USB again with the current XZ Mods app. |
| NOT IN RAM | LOAD TRACK INTO RAM | The deck plays from USB. |
| COPYING TO RAM | CANCEL COPY | The copy is running. Cancel leaves the track playing from USB. |
| ORIGINAL AUDIO IN RAM | RELEASE RAM COPY | The deck plays from memory. Release frees the memory and needs the USB still connected. |
| COPY FAILED | RETRY LOAD INTO RAM | The track keeps playing from USB. Retry; if it fails again, load the track again from USB. |
| USB OUT / RAM COPY ONLY | None | The USB is out and the deck plays from memory. Load another track to release the copy. |

- One track per deck. Each file can be up to 64 MB, and both decks together up to 128 MB.
- Only the original audio goes into RAM. Stems and the 3-band waveform still read the USB.
- Loading another track on the deck drops its RAM copy.
- Decks 3 and 4 always read the USB.

> **Experimental.** Playing on from RAM after the USB goes out has not been confirmed on a player yet. Keep the USB in during a set.

## Quantize

On decks 1 and 2, quantize is the player's own: [[DECK QUANTIZE]] and UTILITY work as on a stock player. In MODERN each deck shows its QUANTIZE lamp and beat value above its time. You can hide them in MODS > SCREEN > LAMPS AND TITLES.

While a side drives deck 3 or 4:

| Key | Action |
|---|---|
| [[DECK QUANTIZE]] | Switches quantize for every deck. Decks 1 and 2 switch through the player as before. |
| [[SHIFT]] + [[DECK QUANTIZE]] | Switches quantize only for the deck of that side. |

With quantize on, decks 3 and 4 put cue points, loop in and out, hot cues and beat and slip loops on the beat grid, at a one-beat value. [[SYNC]] then also locks the deck to the master's beat. [Four decks](four-decks) has the rest of what decks 3 and 4 do.

## Sampler, hot cue preview and groove pads

These are not in this version of XZ Mods, on any deck. MODS lists them and marks them NOT IN THIS VERSION:

| Feature | What MODS says it does |
|---|---|
| X-PAD SAMPLER | A sampler on the pads: a loop strip, eight sample pads, HOLD and OVERDUB. |
| HOT CUE PREVIEW | While a deck is paused, hold a hot cue pad to hear it from that point. Let go and the deck goes back. |
| GROOVE PADS | Plays a groove in place of a stem. Listed on the STEMS page. |

The XDJ-XZ's own X-PAD, the touch strip in the BEAT FX section of the mixer, is a different control. XZ Mods does not change it.
