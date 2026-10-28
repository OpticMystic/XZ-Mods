---
interface: both
settings: deck_view
---

Four decks adds two software players, DECK 3 and DECK 4, to the XDJ-XZ's own DECK 1 and DECK 2. You hear them on mixer channels CH3 and CH4. A short USB cable from the back of the player to a top USB port carries their sound into the mixer. The physical controls switch between decks with the same presses DJ software uses on the XDJ-XZ.

> **Experimental.** Four decks is experimental in this version. Test it with your own tracks before a set, keep the music stick connected, and keep a two-deck plan ready.

## What you need {experimental}

- The **loopback cable**: a USB cable with a USB-B plug on one end and a USB-A plug on the other, the kind a printer uses.
- For tracks, a **music stick** in the other top USB port, or music sticks on one **USB 2.0 hub** in that port. Decks 3 and 4 can start before you insert any music.
- Decks 1 and 2 **stopped or paused** while decks 3 and 4 start. Once they run, you can play all four decks.
- **CH3** and **CH4** on the mixer set to **USB** so you hear decks 3 and 4.

The cable goes directly from the rear USB socket to either **USB 1** or **USB 2** on top. Put music in the other top port. The picture uses USB 1 as an example; both arrangements work.

## Set up four decks {experimental}

1. Stop or pause decks 1 and 2.
2. Choose either top USB port for the loopback cable. Leave the other port for music.
3. Plug the USB-B end of the loopback cable into the **rear USB socket**.
4. Plug the USB-A end directly into your chosen top port, **USB 1** or **USB 2**.
5. Set the **CH3** and **CH4** input switches to **USB**.
6. Tap **DECKS** at the top of the play screen and choose one of the **4 DECKS** pictures, or open MODS and choose [4 DECKS](mods-menu#4-decks).
7. Wait for **DECKS 3 AND 4 ARE READY**. The card in the lanes of decks 3 and 4 tells you each step it is waiting for.
8. Press [[SHIFT]] twice on the left side to play deck 3, or on the right side to play deck 4.

Decks 3 and 4 start as soon as the cable is in, even when decks 1 and 2 have no tracks loaded. You can leave the cable connected before starting the USB loader. No **RESCAN** is needed. Decks 1 and 2 must be stopped or paused while the route starts. **RESCAN** checks at once if you want to check again. After a disconnection, reconnect the cable to the same top port. To move it to the other port, stop playback and restart the USB loader first.
> **Note.** The [XZ Mods app](desktop-app#four-decks-in-the-app) shows steps 2 to 5 with an animation of the cable while it writes your USB and when it is done. You set four decks up at the player, not in the app.

> **Tip.** You can choose a four-deck view before you plug in the cable. Decks 3 and 4 then show a card with the cable steps and their current state, and decks 1 and 2 keep playing as usual.

## Add music and a USB hub {experimental}

Once the USB loader is running, you can bring up decks 3 and 4 with only the loopback cable connected. Insert music later in the other top port. Starting the audio route does not load a track.

To connect several music sticks, put one high-speed USB 2.0 hub in the top port opposite the loopback cable. Stop or pause decks 1 and 2 while a newly connected hub starts. Connect the sticks to that hub and choose the sources the player makes available. The hub does not add more decks or source buttons.

Connect the loopback cable directly to the player, never through the music hub. Do not connect a second hub behind the first. Check that your hub and drives have enough power; use the hub's own power supply if it has one. Keep any drive that holds a playing track connected.

## The 4 DECKS page in MODS

MODS > **4 DECKS** shows the cable's state, lets you choose the deck view, and lists what works on each deck.

```figure classic:theme-{look}-four-decks MODS · 4 DECKS
The 4 DECKS page before the cable is plugged in. In this picture the key and RAM engines have not reported yet, so KEY SYNC has no tick on decks 1 and 2 and RAM TRACKS none on any deck.
1 @ 138,58 Cable picture | Shows USB 1 as an example; USB 2 works too. The picture explains the connections, not which port is currently occupied.
2 @ 340,67 State | Where four decks stand now, with one sentence on what happens next. The states are listed under [The card in every state](#the-card-in-every-state).
3 @ 620,110 RESCAN | Checks the cable and the audio route now. Reads TRY AGAIN after a failed start and SCANNING while a check runs.
4 @ 128,178 DECK VIEW | 2 DECKS, 4 DECKS in number order (1 2 3 4) or 4 DECKS in mixer order (3 1 2 4). The highlighted box is the saved view.
5 @ 442,208 Deck columns | One column per deck, in mixer order 3 1 2 4, the same on both halves of the grid.
6 @ 128,238 Feature row | A tick means the feature works on that deck. A dash means it does not. Turn the encoder onto a row and the dock says why.
7 @ 120,425 Dock | Explains the highlighted control or row.
8 @ 712,425 Save state | Confirms that your deck view is saved to the USB.
```

The HOME page shows the same state on the 4 DECKS card. When four decks need you (no cable found, a failed start, a lost connection, or no four-deck engine on the USB loader) HOME also shows **SET UP FOUR DECKS**, which opens this page.

If the USB loader has no four-deck engine, the page says so and offers **CHECK INSTALLED ENGINE**. Rebuild the USB loader in the [XZ Mods app](desktop-app), restart the player with it, then press **CHECK INSTALLED ENGINE**.

| HOME status line | What it means |
|---|---|
| Not connected | No loopback cable found yet. |
| Looking for the cable | A check is running now. |
| Starting decks 3 and 4 | The cable is found and the software players are starting, or they wait for decks 1 and 2 to stop. |
| Ready, 2 decks shown | Decks 3 and 4 run. The play screen shows two decks. |
| Ready, 4 decks shown | Decks 3 and 4 run. The play screen shows four decks. |
| Could not start | The start failed. The 4 DECKS page says why. |
| Disconnected | The cable was unplugged or lost power. |
| Not available on this player | The software players are not running in this build. |

## The card in every state

While decks 3 and 4 are not running, a four-deck view puts a card in their lanes. The card shows the cable steps, the state, and the buttons you need. Decks 1 and 2 stay live above it. In number order the card fills the lanes of decks 3 and 4 under decks 1 and 2. In mixer order it splits into a strip above deck 1 and a strip below deck 2.

```figure modern:card-01-not-detected Play screen · NO CABLE FOUND
Four decks in number order before the cable is plugged in. CLASSIC draws the same card in its own colours, with shorter sentences.
1 @ 240,266 Cable picture | USB 1 is the illustrated example; USB 2 works too. The picture moves for a minute after the card appears or your last input, then rests.
2 @ 665,203 Steps | The two things to do: connect the cable, and set the CH3 and CH4 input switches to USB.
3 @ 282,283 State | The state's title and what happens next.
4 @ 475,299 RESCAN | Checks for the cable now.
5 @ 720,266 TWO DECKS | Goes back to the two-deck view. Decks 3 and 4 keep trying in the background.
6 @ 82,240 Deck 3 tag | NOT CONNECTED while deck 3 is not running.
7 @ 150,410 Deck 3 channel | The bottom channel of deck 3 waits for the loopback cable.
8 @ 637,12 DECKS button | Shows the current view. The small amber dot means decks 3 and 4 are not connected.
```

You can drive the card with the encoder. While the card shows, push the encoder to highlight **RESCAN**, turn it to move between the card's buttons and the **DECKS** button, and push again to press. [[BACK]], or six seconds without input, gives the encoder back to the player.

| Card title | What it means | What to do |
|---|---|---|
| NO CABLE FOUND | The XZ does not see the loopback cable. | Connect the cable directly to either top port. Music goes in the other port and is optional for startup. Press **RESCAN** to check at once. |
| LOOKING FOR THE CABLE... | A check runs now. The button reads **SCANNING**. | Wait a moment. |
| STARTING DECKS 3 AND 4 | The cable is found and the audio input is starting. | Wait a few seconds. The card's sentence says which step is running. |
| STOP DECKS 1 AND 2 | Decks 3 and 4 or a newly connected music hub wait for decks 1 and 2 to stop. | Stop or pause decks 1 and 2. The card changes by itself. |
| COULD NOT START DECKS 3 AND 4 | The start failed. The picture marks the type of connection that needs attention: top USB, cable, rear socket or CH3/CH4 switches. The drawn port is an example. | Read the sentence, fix that part, then press **TRY AGAIN**. |
| DECKS 3 AND 4 DISCONNECTED | The cable was unplugged or lost power while decks 3 and 4 ran. | Plug the cable back into the same top port and press **RESCAN**. |
| FOUR DECKS ARE NOT AVAILABLE | The software players are not running in this build. Decks 1 and 2 play as usual. | Press **TWO DECKS**. |
| DECKS 3 AND 4 ARE READY | The route came up. The card turns green, then wipes away. | Press [[SHIFT]] twice on a side to play deck 3 or 4. |

```gallery
modern:card-02-scanning LOOKING FOR THE CABLE. A check is running, so the button reads SCANNING
modern:card-03-starting STARTING DECKS 3 AND 4. The cable is found and the audio input is starting
modern:card-04-waiting-decks-1-2-stop-mixer STOP DECKS 1 AND 2, in mixer order. The card splits into two strips
modern:card-05-failed-port COULD NOT START. The failed part is marked in red and the button reads TRY AGAIN
modern:card-06-lost DECKS 3 AND 4 DISCONNECTED. The cable went away while decks 3 and 4 ran
modern:card-07-no-engine FOUR DECKS ARE NOT AVAILABLE. Only TWO DECKS is offered
modern:card-08-not-detected-mixer NO CABLE FOUND in mixer order
modern:card-09-encoder-focus-rescan The encoder on the card. RESCAN is highlighted
modern:card-10-ready-hold DECKS 3 AND 4 ARE READY. The card turns green
modern:card-11-ready-wipe The card wipes away and decks 3 and 4 appear
```

When the route comes up, a banner says **DECKS 3 AND 4 ARE READY** for a few seconds. If the play screen shows two decks at that moment, the banner says **DECKS 3 AND 4 ARE READY. TAP DECKS AT THE TOP FOR FOUR DECKS.** The view does not change by itself.

### The E-8307 hub warning {experimental}

The player sees the loopback cable as a USB hub. When you plug it in, the player raises its own warning **E-8307: HUBS ARE NOT SUPPORTED** and keeps it up while the cable stays in. That is not an error, so XZ Mods never shows it, whether four decks are on or not.

- MODERN and MODS show nothing and keep working as usual.
- In CLASSIC the player's own warning window turns into a calm card: **LOOPBACK CABLE CONNECTED**, **USED BY DECKS 3 AND 4 · NOT AN ERROR**.

```gallery {classic}
classic:hub-card The player's window with the loopback cable in
classic:hub-card-large The same card in a larger window
```

The warning can also appear when you connect a music hub. With the loopback cable connected, stop or pause decks 1 and 2 and let the music hub finish connecting. A hub in the loopback port, a second hub behind the first, or a hub that does not support high-speed USB 2.0 is not supported. Remove that hub or connect the music stick directly.

## DECK VIEW

DECK VIEW chooses what the play screen shows. It changes neither the sound, the mixer channels, nor which deck a side controls.

| DECK VIEW | Waveform lanes, top to bottom | Bottom channels |
|---|---|---|
| 2 DECKS (the default) | The two decks the sides control | The same two decks |
| 4 DECKS, number order 1 2 3 4 | 1, 2, 3, 4 | 3, 1, 2, 4 |
| 4 DECKS, mixer order 3 1 2 4 | 3, 1, 2, 4 | 3, 1, 2, 4 |

The bottom channels always follow the mixer, left to right: CH3, CH1, CH2, CH4. Mixer order puts the lanes in the same order, top to bottom. Each lane and each channel carries its deck's number in a badge, so you can always tell which deck is which. Decks 1 and 2 have a white number on grey. Decks 3 and 4 have a black number on white, as on the jog displays.

DECK VIEW is saved to the USB with your other settings. You can change it in five places, and they all change the same setting:

- the **DECKS** button at the top of the play screen;
- **TWO DECKS** on the card;
- MODS > **SCREEN** > **DECK VIEW**;
- MODS > **4 DECKS** > **DECK VIEW**;
- the rekordbox button above LINK, when you set it to **DECK VIEW** in MODS > PLAY > **REKORDBOX BUTTON**. Each press then steps 2 DECKS, 4 DECKS in number order, 4 DECKS in mixer order.

### The DECKS button and its picker

The **DECKS** button is always at the top of the play screen in both interfaces. It names the decks on screen: **DECKS 1 2**, **DECKS 1234**, **DECKS 3124**, or the pair the sides control in 2 DECKS, such as **DECKS 3 2**. In MODERN it sits left of the INFO button. In CLASSIC it sits in the black space between VJ.TOOLS and the METRICS readout.

```figure modern:deckview-02-picker DECK VIEW picker
Tap DECKS to open the picker. Tap a picture to choose it.
1 @ 637,12 DECKS button | Opens and closes the picker. It shows the view now on screen.
2 @ 420,185 2 DECKS | The two decks the sides control, like the player itself.
3 @ 575,165 4 DECKS, number order | All four decks, lanes 1 2 3 4.
4 @ 740,165 4 DECKS, mixer order | All four decks, lanes 3 1 2 4, matching the mixer channels.
5 @ 618,188 Status | LIKE THE PLAYER ITSELF for two decks. DECKS 3 AND 4 READY in green, or 3 AND 4 NOT CONNECTED in amber.
```

A tap outside the picker, [[BACK]], or six seconds without input closes it. The encoder also turns and pushes inside the open picker.

```gallery
modern:deckview-01-two-decks 2 DECKS. The player's own two-deck screen with the DECKS button at the top
modern:deckview-03-four-1234-ready 4 DECKS in number order with decks 3 and 4 ready
modern:deckview-04-four-3124-ready 4 DECKS in mixer order. Lanes 3 1 2 4 over channels 3 1 2 4
modern:deckview-05-two-decks-pair-3-2 2 DECKS with the left side on deck 3. The screen shows decks 3 and 2
modern:deckview-06-two-decks-ready-notice 2 DECKS when decks 3 and 4 come up. The banner says how to show four
```

## Switch a side between decks {experimental}

The left controls play DECK 1 or DECK 3. The right controls play DECK 2 or DECK 4. Decks 3 and 4 must be ready.

- Press [[SHIFT]] twice quickly on a side to switch it. The left [[SHIFT]] switches between decks 1 and 3, the right [[SHIFT]] between decks 2 and 4. These are the presses the XDJ-XZ uses for DJ software.
- Or press [[SHORTCUT]] and tap the deck under **DECK SELECT** {modern}.

After a switch the tempo fader does not jump. The deck you switched to keeps its tempo until you move the fader across that deck's value. The same holds when you switch back.

In 2 DECKS the screen follows the sides. A side on deck 3 or 4 shows that deck's waveform, tag and channel, and the screen says where the side went for two seconds, for example **LEFT: DECK 3**. In a four-deck view the lanes stay where they are, and a pale blue tag marks the decks the sides control.

If decks 3 and 4 are not running, [[SHIFT]] pressed twice does not switch. The screen says **DECK 3 AND 4 NEED THE CH3/CH4 ROUTE**. If the route is lost while a side plays deck 3 or 4, both sides go back to decks 1 and 2 and the screen says **DECK 3/4 ROUTE LOST**.

```figure modern:shortcut-4deck-deck-select-left-3 SHORTCUT · DECK SELECT {modern}
SHORTCUT with the left side on deck 3.
1 @ 708,66 DECK SELECT | Each deck row has its switch. Tap 1 or 3 for the left side, 2 or 4 for the right side.
2 @ 100,470 CONTROL | In the bottom strip, the decks the sides control carry CONTROL with an arrow.
```

```gallery {modern}
modern:shortcut-4deck SHORTCUT with four decks before the software players report. No DECK SELECT yet
modern:shortcut-4deck-deck-select-native DECK SELECT with both sides on decks 1 and 2
modern:shortcut-4deck-deck-select-both DECK SELECT with both sides on decks 3 and 4
modern:play-4deck-players-left-3 The play screen with the left side on deck 3. Its tag and channel read CONTROL
```

## The MODERN four-deck screen {modern}

```figure modern:play-4deck-full MODERN · four decks, number order
All four decks loaded and playing.
1 @ 637,12 DECKS button | Shows DECKS 1234 or DECKS 3124. Tap it to change the view.
2 @ 240,192 Lane tag | The deck's badge, source stick and the key it plays in now, in the key match colours.
3 @ 68,246 VINYL | Whether VINYL is on for deck 3 or 4.
4 @ 198,436 Bottom channels | The four decks in mixer order 3 1 2 4: time, tempo, range, BPM, hot cues and the whole-track overview.
5 @ 362,350 MASTER | The tempo master. A software master reads 3/4 MASTER.
6 @ 782,426 SYNC | What SYNC is doing. SYNC 3 means synced to deck 3, BPM 3 deck 3's tempo only, LIMIT a tempo out of reach. See [SYNC on decks 3 and 4](#sync-on-decks-3-and-4).
```

Tap a lane of deck 3 or 4 to open its detail panel. It shows the full pad bank, the deck's modes, what SYNC is doing and a whole-track overview you can touch to search. Tap the lane again, or **CLOSE**, to close it.

```gallery {modern}
modern:play-4deck-active-3 Deck 3 is the deck the encoder and screen act on
modern:play-4deck-decks-3-4-unloaded Decks 3 and 4 with no track loaded
modern:play-4deck-master-4-loop Deck 4 is the tempo master and plays a loop
modern:play-4deck-software-sync-status A software deck as master. Deck 3 reads 3/4 MASTER and deck 4 reads SYNC 3
modern:play-4deck-sync-pending-status SYNC states. Deck 3 reads BPM 1 and follows deck 1's tempo only. Deck 4 reads SYNC 3 and follows deck 3
modern:play-4deck-mixer-waves Mixer order 3 1 2 4
modern:play-4deck-on-air-needle-lock-left ON AIR marks and NEEDLE LOCK, with the playhead on the left
modern:play-4deck-long-titles Long titles are cut short to fit
modern:play-4deck-alert-deck3 A read error on deck 3. CANNOT READ THE TRACK, with CHOOSE TRACK
modern:play-4deck-grid-deck4 GRID ADJUST on deck 4
modern:play-4deck-grid-deck4-stems GRID ADJUST on deck 4 with stem rows on decks 1 and 2
```

Stem buttons show in a half row under a deck's waveform, only for decks 1 and 2 and only while the deck's track has prepared stems. Decks 3 and 4 have no stems. A deck without stems shows no stem buttons and its waveform keeps the whole lane.

**STEMS** in the top bar shows or hides the stem buttons on all four lanes: one tap hides them, the next shows them. The screen says **STEM BUTTONS HIDDEN** or **STEM BUTTONS SHOWN**, and **STEMS** is lit while they show. Your 2 DECKS stem layout does not apply here and does not change. See [Stems](stems).

```gallery {modern}
modern:play-4deck-stems-shown Stem buttons on decks 1 and 2. STEMS is lit
modern:play-4deck-stems-hidden One tap of STEMS: the stem buttons are hidden and every waveform has its whole lane
modern:play-4deck-stems-deck2-none Deck 2's track has no prepared stems: no stem buttons, the whole lane
modern:play-4deck-stems-bypass BYPASS on deck 2
```

## The CLASSIC four-deck screen {classic}

CLASSIC keeps the player's own look and adds what four decks need.

- **Waveforms.** The waveform area holds four narrower lanes, one per deck, in the order DECK VIEW chooses. Decks 3 and 4 draw the same 3-band waveform with beat ticks, hot cue flags A to H, the cue point, loops and the playhead.
- **Side column.** The left column has one tag per lane. The tag of a deck a side controls is pale blue with LEFT or RIGHT. Decks 1 and 2 show the analysed key, the playing key and its cents. Decks 3 and 4 show their playing key and its cents the same way, TRACK KEY until they report it, or NOT CONNECTED and SEE THE CARD while they are not running.
- **Bottom panel.** Four channels in mixer order 3 1 2 4. Each shows the deck's title, time, QUANTIZE, tempo and range, AUTO CUE and MT, what SYNC follows, what the pads do, the BPM (orange with MASTER for the master deck) and the whole-track overview. A deck that is not connected shows a dim band with NOT CONNECTED and DECK n WAITS FOR THE LOOPBACK CABLE.
- **Stem rows** are not drawn while four decks show, because there is no room, and **STEMS** stays dark. Your stem layout comes back when you return to two decks. Pressing STEMS meanwhile changes nothing and says **STEM BUTTONS SHOW IN 2 DECKS VIEW**.
- **2 DECKS** shows the decks the sides control. A side on deck 3 or 4 gets a whole half in the player's own style: its waveform, its side tag and its bottom panel.

Tap a lane, side tag or channel title of deck 3 or 4 to open its detail panel. Touch the overview in the bottom channel to search the track. NEEDLE LOCK refuses a search while the deck plays.

## Load tracks onto decks 3 and 4 {experimental}

[[LOAD]] on a side loads the highlighted track into the deck that side controls, deck 3 or deck 4 included. In CLASSIC this goes through the player's own browser. In MODERN, tap a track to see a **LOAD** chip with one cell per deck, then tap the deck number. See [Browsing and loading](browse) for the rest.

```figure modern:browse-4deck-load-cells MODERN · load to any deck {modern}
A tapped track with its LOAD cells.
1 @ 455,231 LOAD | Tap a deck number to load the track there. LOAD returns to the play screen.
2 @ 608,186 Target deck | The blue cell is the deck the screen acts on now.
3 @ 100,470 Bottom strip | The four decks in mixer order while you browse.
```

```gallery {modern}
modern:browse-4deck Browsing with four decks. The bottom strip shows all four decks
modern:browse-4deck-players Browsing while the sides control decks 3 and 2
modern:browse-4deck-deck4-unloaded Deck 4 with no track. Not Loaded in the strip
modern:browse-4deck-load-all The play screen after a load
modern:browse-4deck-load-pair-1-3 After LOAD on the left side. The track went to the left side's deck
modern:browse-4deck-load-pair-2-4 After LOAD on the right side. The track went to the right side's deck
```

Decks 3 and 4 cannot load a track at a memory point from the CUE/LOOP LIST. The screen says **DECK 3 AND 4 CANNOT LOAD AT A MEMORY POINT**.

## INFO, SOURCE and UTILITY with four decks {modern}

INFO has a tab for each deck. SOURCE shows which decks use each stick. UTILITY and the other full screens keep a compact strip of all four decks at the bottom.

```gallery {modern}
modern:info-4deck INFO with tabs DECK 1 to DECK 4
modern:source-4deck SOURCE. Decks 1 and 2 on USB 1, decks 3 and 4 on USB 2
modern:utility-4deck UTILITY with the four-deck strip at the bottom
modern:parity-info-player3 INFO tabs named PLAYER 3 and PLAYER 4 are other players on the LINK network, not decks 3 and 4
modern:parity-info-player4 A LINK player with no track loaded
```

## What decks 3 and 4 can do {experimental}

Decks 3 and 4 are software players. They follow the player's own controls on the side that controls them. MODS > 4 DECKS shows the current answer for each feature.

| Feature | Decks 1 and 2 | Decks 3 and 4 |
|---|---|---|
| Beat jump pads with your own sizes | Yes | Yes, the same sizes and both pages |
| Gate cue and smart cue | Yes | Yes |
| Waveform colours, key notation, titles | Yes | Yes |
| Playing key | Yes | Yes |
| Key sync and key shift | Yes | Yes. Both can shift together; new shifts check measured audio headroom. |
| Stems | Yes, with USB stems set up | No. Decks 3 and 4 have no stems. |
| RAM tracks | Yes | Yes, while the loopback cable is connected. See [RAM tracks](performance#ram-tracks). |

On a side that controls deck 3 or 4, these controls work on that deck {experimental}:

| Control | What it does on deck 3 or 4 |
|---|---|
| [[PLAY/PAUSE]], [[CUE]] | Play and pause, back cue and cue audition. With VINYL on, pause brakes to a stop at the VINYL SPEED ADJUST rate. |
| [[HOT CUE]], [[BEAT LOOP]], [[SLIP LOOP]], [[BEAT JUMP]] | Set the pad mode of the software deck. The native deck keeps its own mode. |
| Pads | Hot cues, beat loops, slip loops and beat jumps, as on decks 1 and 2. |
| [[LOOP IN]], [[LOOP OUT]], [[RELOOP/EXIT]] | Manual loops, loop adjustment with the jog, reloop and exit. |
| [[SLIP]], [[DIRECTION REV]], [[VINYL]] | Slip, reverse and vinyl mode. |
| [[TEMPO RANGE]], [[TEMPO RESET]], [[MASTER TEMPO]] | Tempo range up to WIDE, original speed, and key lock while the tempo moves. |
| [[SYNC]], [[MASTER]] | Follow the master deck's tempo, and with QUANTIZE its beat. Make this deck the master. See [SYNC on decks 3 and 4](#sync-on-decks-3-and-4). |
| [[SHIFT]] + [[SYNC]], [[MASTER TEMPO]] | KEY SYNC to the master deck, and back to the deck's own key. See [Keys and KEY SYNC](performance#keys-and-key-sync). |
| [[DECK QUANTIZE]] | Cue, loop and hot cue points land on the beat. [[DECK QUANTIZE]] switches every deck. [[SHIFT]] + [[DECK QUANTIZE]] switches only this side's deck. |
| [[MEMORY]], [[DELETE]], [[CUE/LOOP CALL]] | Store, delete and call memory points. |
| [[TRACK SEARCH]], [[SEARCH]] | Previous and next track, and fine search. With AUTO CUE on, the new track waits at its cue. Previous and next follow the list the track was loaded from in MODERN. After a CLASSIC load the deck shows NO SAVED TRACK ORDER, and [[TRACK SEARCH]] back returns to the cue. |

On decks 3 and 4, TRACK SEARCH respects AUTO CUE. With AUTO CUE on, a newly loaded track waits at its cue point. With AUTO CUE off, a deck that was playing continues with the new track; a paused deck stays paused.

Hot cues and memory points you set on decks 3 and 4 are saved on the music stick for those decks only. Decks 1 and 2 and rekordbox do not see them, and the rekordbox export stays unchanged.

### SYNC on decks 3 and 4 {experimental}

Press [[SYNC]] and the deck takes the master deck's BPM at once. The SYNC button stays lit for as long as SYNC is on. With QUANTIZE on and a beat grid on both tracks, the deck also locks to the master's beat. Press [[SYNC]] again to turn it off.

If the beats drift apart, press [[SYNC]] off and on again. With QUANTIZE on, every engage snaps to the nearest position at the selected beat value, even for a small drift. The beat value can be 1, 1/2, 1/4 or 1/8 beat. With QUANTIZE off, only the tempo follows.

- The tempo follows even when the master or the synced deck is paused. If the beat position is available, the paused deck aligns once. If it is unavailable, alignment happens as soon as it can be read. Continuous beat lock resumes when both decks play.
- A pause, a jump, a loop or a new master never turns SYNC off. The deck follows again on its own, with no second press.
- Bending or scratching takes control of the deck. When you let go, QUANTIZE snaps it back into alignment as soon as beat timing is available.
- The tempo fader does not move the deck while SYNC is on.

The bottom channel, the lane and the detail panel say what SYNC is doing:

| Shown | What it means |
|---|---|
| SYNC 1 to SYNC 4 | SYNC is on and follows that deck. |
| BPM 1 to BPM 4 | Only the tempo can follow that deck: QUANTIZE is off, a track has no beat grid, a loop is running, or the master's beat position is not available. |
| MASTER | This deck is the tempo master. |
| 3/4 MASTER | This deck is the master for decks 3 and 4 only. |
| LIMIT | The master's tempo is out of this deck's reach. The deck keeps its last tempo. |
| EXT MASTER | The master is another player on the LINK network, which decks 3 and 4 cannot follow. |
| NO BPM | The track has no BPM to sync with. Analyse it in rekordbox. |

The detail panel adds a line about the reason, for example:

| Detail panel | What it means |
|---|---|
| SYNC ON / FOLLOWING DECK 1 | The deck follows deck 1. |
| SYNC ON / TEMPO MATCHED, PAUSED / TEMPO MATCHED | The tempo matches. The beat locks once both decks play. |
| BPM SYNC / MASTER PAUSED, BPM SYNC / DECK PAUSED | One of the two decks is paused. The tempo follows, and the beat locks once both play. |
| BPM SYNC / NO BEAT GRID | A track has no beat grid, so only the tempo follows. |
| BPM SYNC / LOOP ACTIVE | A loop is running, so only the tempo follows until it ends. |
| BPM SYNC / BEAT POSITION UNAVAILABLE | The master's beat position cannot be read, so only the tempo follows. |
| BPM SYNC / QUANTIZE OFF | QUANTIZE is off, so only the tempo follows. |
| SYNC ON / JOG CONTROL | You are bending or scratching. SYNC picks up again when you let go. |
| NO MASTER / TEMPO UNCHANGED | No deck is master. The deck keeps its tempo and follows as soon as there is one. |
| MASTER CLOCK UNAVAILABLE / LAST TEMPO RETAINED | The master's tempo cannot be read right now. The deck keeps the last tempo. |
| SYNC TEMPO LIMIT / LAST TEMPO RETAINED | The master's tempo is out of reach. |
| EXTERNAL MASTER / SYNC SOURCE UNAVAILABLE | A LINK player is master. |
| TRACK BPM UNAVAILABLE / SYNC ON | The track has no BPM. |

## Limits

- Decks 1 and 2 must be stopped or paused while decks 3 and 4 start.
- Keep the music stick connected while you play. Decks 3 and 4 read from it all the time.
- Decks 3 and 4 play WAV, AIFF, FLAC, MP3 and Ogg Vorbis files. AAC and M4A files do not play on decks 3 and 4.
- A deck 3 or 4 master reads MASTER when decks 1 and 2 can follow it too, and 3/4 MASTER when it drives decks 3 and 4 only.
- When the master stops, MASTER passes to the next playing deck in deck order. After deck 3 comes deck 4, then decks 1 and 2. After deck 4 come decks 1 and 2, then deck 3. A master set by another player over LINK is never taken.
- Some lamps on the player do not yet light as they do for decks 1 and 2. The beat loop and slip loop pad colours are provisional.
- Four decks, the handover of MASTER between native and software decks, and scratching on decks 3 and 4 are experimental in this version.

If something goes wrong, the card or the 4 DECKS page names the state. [Messages and troubleshooting](troubleshooting) lists every notice.
