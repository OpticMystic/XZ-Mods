# MODS menu: device acceptance list

One line per MODS row (DESIGN 7.6). Run each in both interfaces (CLASSIC panel and MODERN MODS
screen) with a track loaded on decks 1 and 2, then with the loopback cable for decks 3 and 4.
The host tests prove the same rows reach the runtime (`ui/test_section_rows_runtime.h`); this
list checks what only the unit can show: sound, pads, lights and the play screen.

| Section | Row | Press | You should hear or see | Decks |
|---|---|---|---|---|
| HOME | Tiles | Tap PLAY, then BACK, then each tile | Each tile opens its section; status lines match the section pages | - |
| HOME | SET UP (4 DECKS) | Choose a four-deck view with no cable, open MODS | 4 DECKS tile says Not connected with one SET UP button; the rail's 4 DECKS has an amber dot; SET UP opens 4 DECKS | - |
| HOME | SET UP (STEMS) | Use a USB loader without the stem engine | STEMS tile shows SET UP; it opens the USB stems setup | - |
| PLAY | BEAT JUMP PADS | Open it, choose PAGE 1: MY SIZES, set pads C / D to 16, press BEAT JUMP, play pad C then D | The deck jumps back 16 beats, then forward 16 | 1 2 (3 4 after the parity merge) |
| PLAY | PAGE 1: STOCK | Choose STOCK, play pads A to H in BEAT JUMP | The player's own 1 2 4 8 jumps; page 1 pads greyed in the editor | 1 2 3 4 |
| PLAY | PAGE 2 | SHIFT + BEAT JUMP on a deck, play a pad | The page 2 size from the editor; PAD PAGE EACH DECK IS ON NOW says PAGE 2 for that deck | 1 2 |
| PLAY | Size chips / encoder | Push a pair, turn, push | The size steps 1..256 as you turn; the ruler and pads follow | - |
| PLAY | GATE CUE | Turn on, hold and release a hot cue | Plays while held, back to the hot cue on release | 1 2 |
| PLAY | SMART CUE | Turn on, press a hot cue, press CUE | CUE returns to that hot cue | 1 2 |
| PLAY | HOT CUE PREVIEW | Loader with the engine: SET UP, then pause and hold a hot cue | Preview plays from the hot cue and returns | 1 2 |
| PLAY | KEYS | KEY UP, KEY DOWN, RESET, KEY SYNC | Key moves a semitone each press; KEY SYNC matches the master deck | 1 2 |
| PLAY | X-PAD SAMPLER | Loader with the engine: hold the loop strip, play sample pads | Loop slice and samples play | 1 2 |
| PLAY | RAM TRACKS | LOAD INTO RAM, pull the USB | Playback carries on | 1 2 |
| PLAY | BUTTON NEXT TO LINK | Set DECK VIEW, press the button three times | 2 decks, 4 decks 1 2 3 4, 4 decks 3 1 2 4, each with a notice | - |
| PLAY | SHIFT + BUTTON NEXT TO LINK | Set STEMS, hold SHIFT and press | The stem layout steps | - |
| STEMS | USB STEMS | Turn off, then on, with a prepared track | Off: pads back to normal; on: stem pads mute stems | 1 2 |
| STEMS | STEMS IN DJ SOFTWARE | Run the setup with a computer deck | The test switches the drums stem in the DJ software | 1 2 |
| STEMS | STEM PADS: mode | Choose BEAT LOOP, press BEAT LOOP on the deck | Pads A-D become stems in the shown colours | 1 2 |
| STEMS | STEM PADS: PADS E-H | Choose E-H | Stems move to pads E to H | 1 2 |
| STEMS | STEM PADS: ORDER | Push, turn | Pad and screen order change together | 1 2 |
| STEMS | STEM PADS: PAD LIGHTS | Choose OFF | Stem pads stop lighting in stem colours | 1 2 |
| STEMS | GROOVE PADS | Loader with the engine: play a groove pad | The groove replaces its stem | 1 2 |
| STEMS | STEM BUTTONS ON SCREEN | Pick each layout | The play screen's stem buttons move as pictured | 1 2 |
| STEMS | BYPASS BUTTON (MODERN) | HIDE | The BYPASS button leaves the stem rows | 1 2 |
| STEMS | STEM COLOURS | Pick a colour per stem | Pads, stem buttons and STEMS waveform use it | all |
| SCREEN | INTERFACE | Pick the other interface | The play screen switches | all |
| SCREEN | DECK VIEW | Pick each picture | Play screen lanes change; sound and controls do not | all |
| SCREEN | DECK VIEW BUTTON | HIDE | The DECKS button leaves the top bar | all |
| SCREEN | THEME | CLASSIC: pick a theme; MODERN: STANDARD | The theme applies | all |
| SCREEN | WAVEFORM COLOURS | Pick each | Scrolling waveforms recolour; UTILITY WAVEFORM COLOR shows the same value | all (3 4 draw 3-band for STOCK and STEMS) |
| SCREEN | TITLES IN WAVEFORMS (MODERN) | HIDE | Titles leave the lanes | all |
| SCREEN | TRACK COLOURS (MODERN) | Each choice | Deck tag and waveform use, then drop, the library colour | all |
| SCREEN | KEY NOTATION | Each choice | Keys read Am, 8A or as on the USB everywhere MODS draws them | all |
| SCREEN | PLAYING KEY | Turn on, shift tempo | The playing key replaces the beat countdown | 1 2 3 4 |
| SCREEN | KEY MATCH COLOURS | Turn on, browse | Track keys green / amber by match with the master | - |
| SCREEN | KEY BEFORE TITLE | Turn on, browse | Keys show before titles | - |
| SCREEN | MODE LAMPS / QUANTIZE LAMP (MODERN) | HIDE each | The lamps leave the top bar | - |
| SCREEN | SYSTEM READOUT | Run the setup | CPU, memory and temperature beside STEMS and MODS | - |
| 4 DECKS | RESCAN | No cable: press RESCAN | LOOKING FOR THE CABLE..., then the result | 3 4 |
| 4 DECKS | DECK VIEW segments | Tap each | Same as SCREEN > DECK VIEW | all |
| 4 DECKS | WHAT WORKS ON EACH DECK | Turn through the rows | Each row's checks match the decks; a dash says why below | all |
| CONNECT | ACTIVE DJ PROFILE | Pick another profile | Header chip changes; only that USB is written | - |
| CONNECT | VJ.TOOLS / NETWORK / UPDATES | Open each | Their pages as before, BACK returns to CONNECT | - |
| EXTRAS | GAMES / USB APPS / REPORT AN ISSUE / ABOUT | Open each | Their pages as before, BACK returns to EXTRAS | - |
