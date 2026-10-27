---
interface: both
settings: wave_mode
---

The play screen is where you spend the set. XZ Mods gives you two ways to draw it. MODERN replaces the whole screen with its own layout. CLASSIC keeps the player's own screen and adds a row of buttons, the playing key and the stem rows to it. You choose between them in MODS under **SCREEN** (see [Screen, interfaces and themes](screen-and-themes)).

## The MODERN play screen {modern}

MODERN draws every deck the same way: a top bar across the screen, one waveform lane per deck with a narrow info column on its left, a row of hot-cue lamps, and a deck strip at the bottom. Two decks share the screen in halves. For four decks, see [Four decks](four-decks).

### Top bar {modern}

```figure modern:play-full PLAY · TOP BAR {modern}
Deck 1 is the master, deck 2 plays in a 4-beat loop with KEY SYNC holding its key.
1 @ 112,40 SINGLE | CONTINUE | PLAY MODE. The lit word is what a deck does at the end of a track: SINGLE stops, CONTINUE loads the next track in the list and plays it. Tap to switch.
2 @ 160,40 A. HOT CUE | Lit while HOT CUE AUTO LOAD is on. Red means every track brings its hot cues, white means only the tracks marked for it in the library. Tap to step through ON, the library setting and OFF.
3 @ 222,40 A. CUE | Lit while AUTO CUE is on, so the cue point lands where the music starts. Tap to switch it.
4 @ 380,40 BEAT FX | The effect, its beat and time, the FX BPM and the channel it is on. It fills red while the effect is on.
5 @ 563,40 DECKS | Shows which decks the sides drive and opens DECK VIEW. See [Four decks](four-decks).
6 @ 650,40 INFO | Opens INFO for the active deck's track.
7 @ 708,40 STEMS | Steps the stem buttons through their layouts. Lit blue while stems are on. See [Stems](stems).
8 @ 772,40 MODS | Opens MODS.
```

A lamp label in grey shows a mode the player does not let XZ Mods change from the screen. Change it in the player's own UTILITY instead (hold [[MENU]]). You can hide the three lamps in MODS under **SCREEN**, **LAMPS AND TITLES**. The BEAT FX readout then takes their room.

```gallery {modern}
modern:play-fx-on ECHO on: the readout fills red, with the beat, the time in milliseconds and the FX BPM
modern:play-fx-off PING PONG chosen but off: a quiet outline
modern:play-fx-reverb An effect without beats shows its name alone
modern:play-fx-long VINYL BRAKE with a long time and the FX BPM set by TAP
modern:play-fx-unknown The first moments after start, before the effect has been read
modern:key-indicators-hidden MODE LAMPS and QUANTIZE LAMP hidden: the readout starts at the left edge
modern:key-indicators-hidden-fx-on The same with ECHO on
```

### Deck column and waveform lanes {modern}

```figure modern:play-full PLAY · DECKS AND LANES {modern}
Each deck gets an info column and a scrolling waveform. The playhead is the red line in the middle.
1 @ 108,38 DECK tag | Tap to make this the active deck. The blue tile marks the active deck. The dashes beside the name count the beats of the bar.
2 @ 108,62 Source | Where the track came from: USB1 or USB2.
3 @ 108,105 Key | The key the deck plays now. Green or amber means it mixes with the master deck's key. See [Key](#key-and-key-sync).
4 @ 75,199 KEY SYNC | Replaces the KEY caption while KEY SYNC holds this deck's key.
5 @ 108,222 Tuning | How many cents the playing key sits off the nearest semitone.
6 @ 88,248 ORIGINAL | The track's analysed key, shown once the playing key has moved away from it.
7 @ 482,44 Beat countdown | Bars and beats to the next cue ahead of the playhead, as bars.beats.
8 @ 282,134 Bar number | Counts bars every four bars, so you can count phrases.
9 @ 178,162 Loop | A running loop shades the waveform and names its length.
10 @ 436,100 Playhead | The current position. The waveform scrolls past it.
11 @ 700,253 ZOOM | How far the waveforms are zoomed. Turn the rotary encoder to zoom in or out.
12 @ 245,277 Hot-cue lamps | Pads A to H in their cue colours. A dark lamp is an empty pad.
```

Turn the rotary encoder on the play screen to zoom both lanes together. There are seven zoom steps. The zoom is kept when you leave the screen.

```gallery {modern}
modern:play-zoom-in Zoomed fully in on deck 2
modern:play-zoom-out Zoomed fully out
modern:play-left WAVEFORM CURRENT POSITION set to LEFT: more of what is coming up
```

### Deck strip {modern}

```figure modern:play-full PLAY · DECK STRIP {modern}
The two deck strips. Deck 1 is the tempo master, deck 2 is synced to it.
1 @ 30,288 DECK badge | The deck this strip belongs to, lit when it is the active deck.
2 @ 250,303 Title and artist | The loaded track.
3 @ 368,302 Play state | A triangle while the deck plays, two bars while it is paused.
4 @ 52,345 TRACK | The track's number in the list it was loaded from.
5 @ 150,380 REMAIN / TIME | The lit word says how the time counts: REMAIN counts down to the end, TIME counts up from the start. Tap the time to switch.
6 @ 215,313 QUANTIZE | Red while QUANTIZE is on, with the beat value loops and cues snap to.
7 @ 297,313 Tempo range | The tempo slider's range, for example ±10.
8 @ 262,375 MT | MASTER TEMPO. It blinks while KEY SYNC holds the key.
9 @ 700,375 SYNC | Lit while the deck is synced.
10 @ 399,350 BPM | The deck's playing BPM. MASTER marks the tempo master.
11 @ 190,395 Cue marks | Hot cues as lettered chips and memory cues as red marks above the overview.
12 @ 200,425 Overview | The whole track. The lighter part is what has played.
13 @ 112,470 Minute scale | Minutes to the end in REMAIN, from the start in TIME.
```

In the last 30 seconds of a track the overview's playing address flashes slowly, and in the last 15 seconds it flashes fast, in either time mode.

```gallery {modern}
modern:play-ending-flash Deck 1 in its last 12 seconds, the end warning flashing
modern:play-not-loaded Deck 2 with no track: Not Loaded. in its lane and strip
modern:play-track-123 A three-digit track number
modern:play-time-mode-writable TIME MODE that you can switch by touch: the lit word is white
modern:play-unknown The first frame after start, before anything has been read: dashes everywhere
```

### Key and KEY SYNC {modern}

The large key in each deck's column is the key the deck plays right now. Tempo with MASTER TEMPO off, a key shift and KEY SYNC all move it. Its colour compares it with the master deck's key. Green means the same key or its relative major or minor. Amber means one step away on the key wheel. White means neither, and the master deck itself is always white. The cents beside it show how far it sits off the semitone, so you can match by ear. Once the playing key leaves the track's analysed key, ORIGINAL shows the analysed key below it.

While KEY SYNC holds a deck's key, the KEY caption becomes a blue **KEY SYNC** chip and the deck's **MT** lamp blinks, half a second lit and half a second dark.

```gallery {modern}
modern:key-sync-lit KEY SYNC holds Bbm, MT lit
modern:key-sync-dark Half a second later, MT dark
modern:key-tempo-cents MASTER TEMPO off: +2.20 % lifts A minor by 38 cents, still Am
modern:key-tempo-semitone At +6.10 % the playing key reaches Bbm
modern:key-alphanumeric Keys written as wheel numbers, with a key shift on deck 1
modern:play-key-alphanumeric The whole play screen with wheel-number keys
modern:key-old-runtime An older loader without the analysed key: the key and its key-shift step
modern:key-unknown-and-odd No key read on deck 1, and a key name too long for the large face on deck 2
```

You choose how keys are written (as on the USB, musical or wheel numbers) and whether the playing key shows at all in MODS under **SCREEN**. See [Screen, interfaces and themes](screen-and-themes).

### Waveform colours {modern}

MODS under **SCREEN** offers four waveform colours for MODERN. They change the lanes and the overviews together.

| Choice | What you see |
|---|---|
| STOCK | One blue waveform with white peaks |
| RGB | Lows red, mids green, highs blue |
| 3-BAND | Lows blue, mids amber, highs white |
| STEMS | Drums, harmonics and vocals in their stem colours. A track without stems shows 3-BAND |

```gallery {modern}
modern:play-blue STOCK
modern:play-rgb RGB
modern:play-full 3-BAND
modern:play-stems STEMS
```

**USB COLORS** (MODS, **SCREEN**, **LAMPS AND TITLES**) lets each deck take the colours saved on the USB stick its track came from.

```gallery {modern}
modern:usb-colors-play DECK + WAVE: the deck tile takes the stick's colour and the waveform the stick's waveform colour
modern:usb-colors-deck-only DECK ONLY: only the deck tile takes the stick's colour
modern:usb-colors-off OFF: every deck uses the player's own colours
```

### Notices {modern}

When something you asked for does not happen, a notice appears in the middle of the screen for two and a half seconds. It says what went wrong in one line, for example **DECK 1 DID NOT LOAD THE TRACK**. A long notice wraps. [Messages and troubleshooting](troubleshooting) explains each one.

```gallery {modern}
modern:play-notice A notice over the lanes
modern:play-notice-long A long notice
modern:play-notice-expired The same screen once the notice has gone
```

If a deck cannot go on, its lane turns red and names the problem, for example **EMERGENCY LOOP** (the deck loops until you load a track) or **CANNOT READ THE TRACK** with the player's error code. Tap **CHOOSE TRACK** on the red band to make that deck active and open BROWSE.

```gallery {modern}
modern:parity-alert-emergency EMERGENCY LOOP on deck 2
modern:parity-alert-read CANNOT READ THE TRACK with error E-8302
```

## SHORTCUT {modern}

Press [[SHORTCUT]] on the play screen to see the settings you change most during a set on one page. It has the same four groups as the player's own SHORTCUT screen: PLAYER SETTINGS, DECK STATUS, DEVICE SETTINGS and MY SETTINGS. Press [[BACK]] or tap the arrow in the top left corner to return to the play screen.

```figure modern:screen-shortcut SHORTCUT {modern}
The SHORTCUT screen with quantize, waveform and time settings for both decks.
1 @ 205,68 Highlighted row | Turn the encoder to move between rows. The line under the title explains the highlighted row.
2 @ 125,130 Locked value | A lock means XZ Mods shows this value but cannot change it here. Push or tap it and the player's own UTILITY opens at that item.
3 @ 240,174 Choice buttons | Tap a value to change it. The lit button is the current value.
4 @ 430,92 DECK 1 lamps | QUANTIZE (or Q1), SYNC, SLIP, MASTER and the time mode for deck 1. Tap QUANTIZE to switch it, tap REMAIN or TIME to change the time mode.
5 @ 745,49 LINK lamp | Shows whether the player is linked to other players.
6 @ 480,200 Jumps | TRACK, PLAYLIST and SEARCH open those lists. MATCHING opens matching tracks, or hands over to the player's own browser when XZ Mods cannot list them.
7 @ 200,280 DEVICE SETTINGS | Waveform colour, playhead position, key display format and waveform zoom. These change the MODERN screen at once.
8 @ 600,295 MY SETTINGS | Time mode, play mode and auto cue for the decks.
```

Which rows you can change depends on what the player lets XZ Mods write. Rows with a lock always open the player's own UTILITY, where you change them the stock way. A short [[MENU]] on SHORTCUT opens MODS.

```gallery {modern}
modern:settings-shortcut-full Every row available, with HOT CUE AUTO LOAD and AUTO CUE LEVEL
modern:shortcut-runtime-table QUANTIZE BEAT VALUE as four choices, play mode and auto cue locked
modern:r5-shortcut-graphic-stems The waveform colour row with STEMS chosen
modern:settings-shortcut-read-only The player's own settings shown as values while it is not accepting changes. DEVICE SETTINGS still change the MODERN screen
```

With four decks, SHORTCUT also shows DECK SELECT. See [Four decks](four-decks).

## The CLASSIC play screen {classic}

CLASSIC keeps the player's own play screen, so every stock element stays where the XDJ-XZ operating instructions put it. XZ Mods restyles it in the classic theme you chose and adds a row of buttons across the top, the playing key under each deck's key, and stem rows under the waveforms.

```figure capture:classic-play PLAY · CLASSIC {classic}
The ORIGINAL theme with stems on in the BELOW / HALF layout. Deck 1's playing key has moved from 2A to 11A.
1 @ 120,10 VJ.TOOLS | Switches to the VJ.Tools screen when a VJ.Tools computer is connected. See [Connect and DJ profiles](connect).
2 @ 220,30 DECKS | Shows which decks the sides drive and opens DECK VIEW. See [Four decks](four-decks).
3 @ 440,30 Metrics | The player's CPU load, free memory and temperature. Tap METRICS to set it up. See [Extras](extras).
4 @ 678,30 STEMS | Steps the stem rows through their layouts. The marks under the label show the current one. See [Stems](stems).
5 @ 789,28 MODS | Opens MODS.
6 @ 110,140 Playing key | Shown under the player's own key. The analysed key, an arrow, then the key the deck plays now, with its tuning in cents.
7 @ 110,280 Same key | When the playing key has not moved, KEY and the key, with the cents.
8 @ 400,150 Stem rows | VOCALS, DRUMS, HARMONICS and BYPASS for each deck. See [Stems](stems).
```

This picture is the stock screen of a real player with the XZ Mods buttons and stem rows drawn on by the firmware's own code. Everything else on it is the player's own: the deck tabs, source, key, BEAT FX, ZOOM, the deck strips with A.HOTCUE, A.CUE, QUANTIZE, TEMPO and the BPM box. The XDJ-XZ operating instructions explain those.

The key line replaces the player's beat countdown under the key. You choose the key notation and whether the playing key shows in MODS under **SCREEN**.

```figure capture:classic-play-stems PLAY · CLASSIC, FULL ROWS {classic}
An earlier capture with stems in the BELOW / FULL layout, the rows taller with room for the stem level bars. Stem colours here follow an older default. Today drums are blue and harmonics red; see [Stem colours](stems#stem-colours).
```

### Scrolling waveform {classic}

In CLASSIC, **SCREEN** offers three scrolling waveforms. The setting is `wave_mode` on the USB.

| Choice | What you see |
|---|---|
| STOCK | The player's own scrolling waveform |
| 3-BAND | Layered lows, mids and highs (the default) |
| STEMS | Drums, harmonics and vocals layered in their stem colours. A track without prepared stems shows 3-BAND |

The overview in each deck strip stays the player's own in every choice.

## If something looks wrong

| You see | Why | What to do |
|---|---|---|
| Dashes instead of numbers | The value has not been read yet, or the deck has no track | Wait a moment, or load a track |
| A grey mode lamp that does not react | The player does not let XZ Mods change that mode | Change it in UTILITY (hold [[MENU]]) |
| NO STEM WAVE YET · 3BAND in a lane | STEMS waveform chosen, but this track's stems are not loaded yet | Wait, or prepare the track's stems. See [Stems](stems) |
| The key stays white on every deck | No master deck, or KEY MATCH COLOURS is off | Set a tempo master, or turn KEY MATCH COLOURS on in **SCREEN** |
