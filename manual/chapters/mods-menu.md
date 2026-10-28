---
interface: both
settings:
---

MODS is the menu XZ Mods adds to the player. Everything the mod can change lives here, sorted by what you want to do rather than by how the firmware is built. Both interfaces open the same menu, so what you learn here works in MODERN and CLASSIC. MODERN draws it in the FLAGSHIP look. CLASSIC draws it in the theme you chose.

## Open and close MODS

Press [[MENU]] on the play screen. A short press opens MODS wherever the player's own firmware has no job for [[MENU]], and leaves the stock job alone everywhere else. Hold [[MENU]] to open the player's own UTILITY, exactly as on a stock XDJ-XZ.

```generated menu-key
```

> **Note.** Rows marked experimental follow the XDJ-XZ operating instructions but have not been checked on every firmware version yet. If a press does something different on your player, [report it](https://github.com/OpticMystic/XZ-Mods/issues).

To leave MODS, press [[MENU]] again, tap **CLOSE** in the top right corner, or press [[BACK]] until you are back on the play screen.

You can also open MODS from the **MODS** button at the top of the MODERN play screen, or from the rekordbox button above LINK when you set it to **MODS MENU** in [PLAY](#play).

## Move around

You can drive the whole menu with the rotary encoder, with touch, or with both.

| Control | What it does in MODS |
|---|---|
| Turn the encoder | Moves the highlight to the next card, row or button |
| Push the encoder | Opens or changes the highlighted item |
| [[BACK]] | Goes up one level: from an open item to its page, then from a page to the section rail |
| Tap | Opens or changes whatever you tap |

The hints in the bottom right corner (**TURN MOVE · PUSH SELECT · BACK UP**) always show what the encoder and [[BACK]] will do next.

Every page has the same frame. The section rail on the left lists HOME, PLAY, STEMS, SCREEN, 4 DECKS, CONNECT and EXTRAS. The dock along the bottom explains whatever is highlighted. On the right of the dock, **WORKS ON** shows which decks the highlighted feature works on, in mixer order 3 1 2 4. A dimmed number means the feature does not work on that deck, and a short amber line says why.

## HOME

HOME is the first page. It shows one card for each section, with a one-line summary of what the section does and the current state of its most important setting, so you can see at a glance whether stems are ready or four decks are connected.

```figure classic:theme-{look}-home MODS · HOME
The HOME page with stems ready and four decks not connected yet.
1 @ 95,20 BACK | Goes up one level. On HOME it closes MODS.
2 @ 105,300 Section rail | Every section of MODS, top to bottom. Tap one, or turn the encoder while the rail is highlighted.
3 @ 225,67 Section card | Opens that section. The highlighted card has a light outline.
4 @ 550,197 Status line | The state of the section's main setting. Green means ready, amber means it needs you.
5 @ 250,335 Setup button | Appears when a feature needs setting up, and starts a short guide for it.
6 @ 125,425 Dock | Explains the highlighted item in plain words before you open it.
7 @ 712,425 Save state | Confirms your changes are saved to the USB, or says why they cannot be.
8 @ 760,445 Encoder hints | What turning, pushing and BACK will do right now.
9 @ 776,46 CLOSE | Closes MODS and returns to where you were.
```

| Card | Its status line shows |
|---|---|
| PLAY | Your beat jump sizes, page 1 then page 2 |
| STEMS | Not set up, Off, or On with the state of deck 1 and deck 2 |
| SCREEN | The interface, the waveform colours and the key notation |
| 4 DECKS | Not connected, Looking for the cable, Starting decks 3 and 4, Ready with the deck view, Could not start, Disconnected, or Not available on this player |
| CONNECT | Your DJ profile and its USB, and whether VJ.Tools is connected |
| EXTRAS | The games and how many USB apps are on the stick |

Two setup buttons can appear on HOME. **SET UP USB STEMS** opens the USB stems guide when the stem engine is not ready. **SET UP FOUR DECKS** opens 4 DECKS when no cable is found, a start failed, the cable was lost, or the USB loader has no four-deck engine.

## PLAY

PLAY changes what the pads and buttons do while you perform.

```figure classic:theme-{look}-play MODS · PLAY
1 @ 128,84 BEAT JUMP PADS | Your own beat jump distances for both pad pages. The row shows the sizes now in use.
2 @ 128,132 GATE CUE and SMART CUE | Switch each cue trick on or off. A feature the player has not reported yet shows a SET UP button instead.
3 @ 462,196 KEYS and RAM TRACKS | Open their own pages.
4 @ 128,260 REKORDBOX BUTTON | What the rekordbox button does on a press, and with SHIFT.
5 @ 722,425 WORKS ON | The decks the highlighted feature works on.
```

[Beat jump and performance features](performance) explains every item on this page.

## STEMS

STEMS mutes vocals, harmonics and drums from the pads or the screen.

```figure classic:theme-{look}-stems-section MODS · STEMS
1 @ 128,84 USB STEMS | Switches stems from the USB on or off. Reads SET UP USB STEMS until the stem engine is ready.
2 @ 128,148 STEM PADS | Which pad mode and pad bank the stems use.
3 @ 128,235 Stem buttons on screen | Where the stem buttons sit on the play screen. Each picture is one layout.
4 @ 128,316 STEM COLOURS | The colours of drums, harmonics and vocals.
5 @ 722,425 WORKS ON | Only decks 1 and 2 are lit. The amber line under it says decks 3 and 4 have no stems.
```

STEMS IN DJ SOFTWARE sets up stems for a computer playing through the player. MODERN adds a **BYPASS BUTTON** switch beside STEM COLOURS. [Stems](stems) explains every item on this page.

## SCREEN

SCREEN sets how the play screen looks.

```figure classic:theme-{look}-screen MODS · SCREEN
1 @ 128,100 INTERFACE | CLASSIC or MODERN. The picture shows each one.
2 @ 530,54 DECK VIEW | 2 DECKS, 4 DECKS in number order, or 4 DECKS in mixer order.
3 @ 128,164 THEME | The CLASSIC theme. In MODERN this place holds LAMPS AND TITLES instead.
4 @ 680,164 3-BAND COLOURS | A colour for the lows, mids and highs of the 3-BAND waveform. The squares show the colours in use.
5 @ 128,228 WAVEFORM COLOURS | STOCK, 3-BAND or STEMS. MODERN adds RGB.
6 @ 790,198 KEY NOTATION | AS ON THE USB, MUSICAL or WHEEL, each with an example key.
7 @ 128,292 Keys and the track list | PLAYING KEY, KEY MATCH COLOURS, KEY BEFORE TITLE and TRACK LIST COLUMNS.
8 @ 128,384 SYSTEM READOUT | CPU, memory and temperature on the top bar, on its own row at the bottom of the page.
```

[Screen, interfaces and themes](screen-and-themes) explains every item on this page. DECK VIEW is explained in [Four decks](four-decks#deck-view).

## 4 DECKS

4 DECKS plays decks 3 and 4 over the loopback cable {experimental}.

```figure classic:theme-{look}-active-four-decks MODS · 4 DECKS
1 @ 340,67 Cable state | Where four decks stand, and what happens next.
2 @ 620,110 RESCAN | Checks the cable now. Reads TRY AGAIN after a failed start.
3 @ 128,178 DECK VIEW | How many decks the play screen shows, and in what order.
4 @ 128,238 What works on each deck | A tick for every deck a feature works on.
```

[Four decks](four-decks) explains the cable, every state and what decks 3 and 4 can do.

## CONNECT

CONNECT links the player to you and to computers.

```figure classic:theme-{look}-connect MODS · CONNECT
1 @ 225,110 DJ PROFILE | Your DJ name and logo from the USB, for VJ screens.
2 @ 557,110 VJ.TOOLS | Lets VJ.Tools on a computer connect, and shows its picture instead of the play screen.
3 @ 225,288 NETWORK | The network adapter and address, applied for this start of the player.
4 @ 557,288 UPDATES | Signed updates over the network. An update becomes active at the next start.
```

[Connect and DJ profiles](connect) explains every page here.

## EXTRAS

```figure classic:theme-{look}-extras MODS · EXTRAS
1 @ 225,110 GAMES | Doom, Chex Quest, Beat Arcade, Wave Rider, Rhythm Lab and Static Surfer. The music keeps playing.
2 @ 557,110 USB APPS | Apps for the XZ from the USB.
3 @ 225,288 REPORT AN ISSUE | Sends a problem, a question or an idea, in three presses.
4 @ 557,288 ABOUT XZ MODS | Credits, links and support.
```

[Extras](extras) explains every page here.

## UTILITY, the player's own settings

[[MENU]] held opens the player's own UTILITY: quantize, auto cue, play mode, the display and the other stock settings.

- In CLASSIC you see the player's own UTILITY screen, unchanged.
- In MODERN, XZ Mods draws UTILITY in the MODERN look. It is the same list of stock settings, in the stock order.
- In MODERN, MODS also has a **UTILITY** cell at the bottom of the section rail. It closes MODS and opens UTILITY. Until MODERN has read the player's settings, it reads NOT READY.

### MODERN UTILITY {modern}

```figure modern:settings-utility-rail MODERN · UTILITY
UTILITY in MODERN, open on its first setting.
1 @ 44,104 UTILITY STOCK | The player's own settings. On the player, the rail lists the MODS sections below it, and each one opens MODS on that section.
2 @ 180,66 Group | DJ SETTING, DISPLAY (LCD), MIXER, SYSTEM and INFO, as on the player.
3 @ 380,99 Lock | XZ Mods shows this setting but cannot change it here. The help says why.
4 @ 630,61 Choices | The setting's choices. The filled circle is the current one.
5 @ 520,178 Help | What the setting does, and what the highlighted choice means.
6 @ 690,18 Version | The player's firmware version.
7 @ 400,428 Deck strip | Decks 1 and 2 stay in view while you change settings.
```

Turn the encoder to move through the settings and push to open the choices. Turn again to pick a choice and push to set it. [[BACK]] goes back to the list. [[MENU]] or **X** closes UTILITY.

A lock next to a setting comes with one of these notes:

| Note | What it means |
|---|---|
| PLAYER CONTROLLED | The player sets this itself. XZ Mods only shows it. |
| FIRMWARE LIMIT | XZ Mods cannot read or set it on this firmware version. |
| READ-ONLY BUILD | A RAM trial shows settings but changes none. Start from the USB loader to change them. |
| READ ONLY HERE | XZ Mods shows this setting but cannot change it here. |

Pushing a locked setting does nothing.

```gallery {modern}
modern:settings-utility-info A setting highlighted in the list
modern:settings-utility-values The choices open. Turn and push to set one
modern:settings-utility-values-writable A setting XZ Mods can change, its choices open
modern:settings-utility-values-scrolled A locked setting further down. Its help says it is read only here
modern:settings-utility-items-deep The end of the list. MIXER, SYSTEM and VERSION No.
modern:settings-utility-empty Before the version is read. TIME MODE first, then the display settings
modern:screen-utility UTILITY on its DJ SETTING and DISPLAY (LCD) groups
modern:settings-utility-decks-unknown Before the decks report. The deck strip shows dashes
modern:settings-utility-slot-gone SETTINGS NOT AVAILABLE HERE when the settings cannot be read
modern:utility-4deck UTILITY with four decks. A compact strip of all four decks
```

## MY SETTINGS {modern}

MY SETTINGS shows the player settings saved on a USB stick, and the DJ profiles on it. In MODERN, open the stick in browse, highlight the category rail on the left, and press [[MENU]].

```figure modern:my-settings-top MODERN · MY SETTINGS
MY SETTINGS for USB 1 while deck 1 plays.
1 @ 700,62 MY SETTINGS | The panel for the stick you browse. X closes it.
2 @ 600,100 Stick | The stick whose settings these are: USB 1, USB 2, or a LINK player's USB.
3 @ 765,130 LOAD TO DECK | Applies the settings saved on the stick to the active deck. Reads PAUSE DECK 1 TO LOAD while that deck plays.
4 @ 690,164 SAVE TO USB 1 | Saves the player's current settings to the stick, replacing the ones there. Push once to arm it, then again to save.
5 @ 650,232 DJ PROFILE | The DJ profiles on the stick. Push one to make it active and apply its settings and look. ACTIVE marks the one in use.
6 @ 650,300 DAMAGED | A profile whose files cannot be read.
7 @ 770,334 SAVE SETTINGS TO | Saves the current settings into the active DJ's profile. Push twice.
8 @ 780,375 Help | What the highlighted row does.
```

Further down, **WAVEFORM** sets COLOR, POSITION, ZOOM, USB COLORS and the stem row layout. **SAVED ON USB 1** lists the settings stored on the stick, which you can change there.

```gallery {modern}
modern:my-settings-save-armed PUSH AGAIN TO SAVE. The save is armed
modern:my-settings-profiles The DJ PROFILE rows
modern:my-settings-waveform The WAVEFORM rows
modern:my-settings-stored SAVED ON USB 1. The settings on the stick, with help for the highlighted one
modern:my-settings-locked-empty A locked stick with nothing saved. Only the WAVEFORM rows remain
modern:parity-my-settings-pause A LINK player's USB while deck 1 plays
modern:parity-my-settings-done After a load. How many settings applied and how many do not exist on this deck
```

## Saving to the USB

There is no save button in MODS. Every change saves on its own to the settings USB, the stick you browse. The settings live in the VJ.Tools folder on that stick, so they travel with you to another XZ running XZ Mods. When you browse a stick, the player takes that stick's settings. A stick with no settings, or with damaged ones, leaves the current settings as they are.

The XZ also remembers the last settings it applied. If you start it with no stick, it uses them.

The save state at the right of the dock says what happened to your last change:

| Save state | What it means |
|---|---|
| SAVING... | The change is being written. |
| SAVED TO USB 1 (or USB 2) | The change is on the stick. |
| SAVED ON THIS XZ / NO USB | No stick is in. The change is kept on this XZ. |
| SAVED ON THIS XZ / USB CHANGED | The settings stick changed. The change is kept on this XZ. |
| SAVED ON THIS XZ / USB PROFILE KEPT | The stick is locked. The change is kept on this XZ and the stick is left as it was. |
| USB REMOVED: SETTINGS KEPT | You pulled the settings stick. Nothing resets. |
| NOT SAVED TO USB 1: CHECK USB | Writing failed. Check that the stick is in and not locked. |
| NOT SAVED: USB PROFILE KEPT | The stick is locked and the XZ could not keep the change either. |
| NO SETTINGS USB: CHANGES LAST UNTIL POWER OFF. | Nothing can be saved. Your change works until you switch the player off. |
| NOT SAVED: CHANGES LAST UNTIL POWER OFF. | This build does not save settings. Changes work until you switch the player off. |

Lines that say NOT SAVED or NO SETTINGS USB show in amber. DJ profiles are not settings. They stay on the DJ's stick and never on the XZ. See [Connect and DJ profiles](connect).

## Older pages

Earlier versions had GENERAL, CONTROLS, THEMES and SETTINGS pages in MODS. They are gone. Anything that pointed at them now opens the page that replaced them:

| Old page | Opens |
|---|---|
| GENERAL | HOME |
| CONTROLS | STEM PADS, in STEMS |
| THEMES | SCREEN |
| SETTINGS | PLAY |
