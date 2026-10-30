# Changelog

What changed in each version of XZ Mods for the XDJ-XZ, newest first. XZ Mods is independent and experimental:
test each build with your own tracks before a set, and keep a known working USB.

## 0.5.0-preview.22.13.2 - 2026-10-29

Decks 3 and 4 handle SYNC and MASTER like decks 1 and 2.

- A new app layout: the sections run along the top, a side list sits on the left, and each page is on a raised card.
- Press MASTER on a playing deck 3 or 4 and it is master at once, with no waiting notice.
- Pause the master and MASTER passes at once to the next playing deck. A deck with SYNC on comes first.
- Pause a deck 1 or 2 master and a playing deck 3 or 4 takes MASTER at once. If the other of decks 1 and 2 is playing, MASTER goes there instead, as with two decks.
- In VINYL mode, hold the master's platter top and MASTER passes to the other synced, playing deck.
- Load a new track on the master and MASTER passes to the other playing deck. The loading deck keeps SYNC on at the tempo the decks shared.
- A MASTER handover never changes the tempo.
- With no master yet, the first deck to play becomes master, decks 3 and 4 included.
- Nudge a synced deck from the side and it keeps the new alignment: SYNC stays on, the tempo stays matched and the SYNC button flashes. Press SYNC off and on to snap back to the beat.
- Press SYNC off and the deck keeps its position and the synced tempo.
- A short hold on the master's platter top no longer leaves a synced deck slightly off tempo for up to 20 seconds.
- Experimental. XDJ-XZ firmware 1.26 only.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.13.1 - 2026-10-29

SYNC and MASTER hand over cleanly between all four decks.

- When a deck 3 or 4 gives up MASTER and no other deck takes it, the MASTER lamps go dark and SYNC is free again. Before, SYNC could stay stuck following a deck that no longer led.
- In VINYL mode MASTER passes to the next playing deck when you press pause, while the deck slows down.
- When the master stops and the next deck is a playing deck 1 or 2, MASTER passes at once instead of waiting for the stopping deck to finish braking.
- When a deck 1 or 2 master stops, a playing deck 3 or 4 takes MASTER after a moment, even if it was paused while the master played.
- Experimental. XDJ-XZ firmware 1.26 only.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.13 - 2026-10-28

A visual Manual, a menu map and button pictures.

- The Manual has step-by-step videos for four-deck setup, the sound path, RAM and USB, beat jump and SYNC.
- How-to videos beside the manual sections show the player's screen next to the control you press, turn or tap, in step, so you can follow without reading the words.
- A menu map in the Manual: every screen as a flow chart, with the control that opens each one, search, zoom and a link to the section that explains it.
- Each button named in the app and the Manual has a picture, lit and unlit, so you can find it on the player.
- The interface step of the USB guide leads with the button above LINK, with a video of the screen switching.
- DJ Profile, Updates and About have their own places in the app. XZ Screen moved into Tools.
- After the music USB comes out and goes back in, waveforms and loads come back even if the first read fails.
- Decks 3 and 4 start with decks 1 and 2 empty (from 22.12).
- The button above LINK is called that everywhere, and its MODS page shows the buttons in the order they sit on the player.
- The browsing chapter no longer mentions PLAYLIST and SEARCH keys the player does not have.
- Experimental. XDJ-XZ firmware 1.26 only.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.12 - 2026-10-28

Four decks start with empty decks.

- Decks 3 and 4 start as soon as the loopback cable is in, with decks 1 and 2 empty, one track loaded or both loaded and paused. Before, they waited until both decks had a track loaded and paused.
- Decks 3 and 4 also start when the cable is already in when XZ Mods starts. No RESCAN needed.
- Experimental. XDJ-XZ firmware 1.26 only.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.11 - 2026-10-28

Four decks from either port and a Manual in the app.

- Four decks start as soon as the loopback cable is in, with empty decks and with the cable already plugged in when XZ Mods starts. No RESCAN needed.
- The loopback cable works in either top port, USB 1 or USB 2. Music goes in the other top port, or through one USB 2.0 hub there for several sticks. Decks 3 and 4 can start with no music stick at all.
- When the music USB goes out, a deck playing its whole track from RAM keeps playing and shows WHOLE TRACK IN RAM · PLAYABLE in place of its waveform.
- In the four-deck view, STEMS hides the stem buttons with one press and shows them with the next. A deck whose track has no stems shows no stem buttons and keeps its whole waveform.
- The app has a Manual tab: every chapter with pictures, numbered points, search, and animated explainers for four-deck setup, SYNC, beat jump, 3-band waveforms, RAM tracks and the USB diary.
- Feedback has its own place in the app. Report a problem, request a feature or ask a question, and follow it in My reports. Reports sent from the XZ wait there for your words.
- A new About page: what XZ Mods is, who makes it, links to the source, issues, releases and websites, plain disclaimers and the open-source licenses.
- The USB guide no longer stops at four decks. Four decks are explained while the USB is written and on the final page, with the cable animation and a link to the Manual.
- The app opens at a size that fits your screen, has Back on every step, and writes the loader to any writable USB stick or into a folder.
- Settings, UTILITY and SHORTCUT changes, deck 3 and 4 cues and the USB diary save again when the XZ starts from the USB loader.
- Choosing a value in UTILITY or SHORTCUT changes it instead of going back unchanged. NEEDLE LOCK is in SHORTCUT.
- Hot cue marks on decks 1 and 2 in MODERN no longer vanish when a pad holds a cue without an OUT point or a pad is unused.
- On decks 3 and 4, TRACK SEARCH respects AUTO CUE, and BEAT JUMP works on tracks without a detected BPM (120 BPM).
- The KEYS page shows the master deck plainly instead of a SET UP KEYS button with nothing to set up.
- The button above LINK is named correctly everywhere. By default it switches between CLASSIC and MODERN.
- Hot cue preview, the X-PAD sampler and groove pads are no longer listed, because they are not built yet.
- Experimental. XDJ-XZ firmware 1.26 only.
- Loopback cable in USB 1 or USB 2, music in the other top port.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.10 - 2026-10-28

RAM card, loopback card, Feedback and any USB stick.

- When the music USB goes out, a deck playing its whole track from RAM shows a card in place of its waveform: WHOLE TRACK IN RAM · PLAYABLE, and how to get the waveform back. Title, time, BPM, tempo, key, lamps and hot cues stay on screen. This works on every deck, in MODERN and CLASSIC.
- With the loopback cable in, CLASSIC shows a LOOPBACK CABLE CONNECTED card.
- Feedback has its own place in the app on every page. Report a problem, request a feature or ask a question there, and follow everything you sent in My reports. A count shows reports waiting from your XZ.
- The app is laid out as a tool: a compact header, Tools grouped into Your USB, Your XZ and This app, and the version at the foot of the side list.
- The window opens at a size that fits your screen and can shrink to 760 x 520. Every step of the USB guide has a Back button, and Cancel or Back from the first step opens a list of the USB sticks plugged in, with Write to a folder instead at the end.
- The loader can go on any writable USB stick, FAT32, exFAT or NTFS.
- When the player shows a warning, MODERN shows it once in the player's words and everything keeps working. An open MODS stays open, with the warning in the strip at its top.
- Choose a folder instead now writes the loader into the folder, ready to copy to a USB. It used to stop at the review step with an error.
- Hot cue marks on decks 1 and 2 in MODERN no longer vanish when one pad holds a cue without an OUT point or a pad is unused.
- The KEYS page no longer offers KEY SYNC and SET UP KEYS on the master deck.
- The UTILITY and SHORTCUT pages in the manual match the lock notes and NEEDLE LOCK.
- Experimental. XDJ-XZ firmware 1.26 only.
- Loopback cable in USB 1, music in USB 2.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.9 - 2026-10-27

Fixes from DJ reports, and bug reports you can follow.

- Tools has a new Report a problem or share an idea page. You fill in one box, and the app adds cleaned-up details from your XZ Mods USB, which you can see and untick before anything is sent. Each report gets a reference, and My reports shows where it stands.
- A report sent from the XZ (MODS > EXTRAS > REPORT AN ISSUE) now waits in My reports under From your XZ, so you can add what happened. While the app is open, a Windows notification tells you when one arrives.
- Settings save again when the XZ starts from the USB loader. Since preview.19 a loader start kept XZ Mods settings, deck 3 and 4 cues, UTILITY and SHORTCUT changes and the USB diary only until the next restart.
- Choosing a value in UTILITY or SHORTCUT, such as NEEDLE LOCK, changes it again instead of going back to the list unchanged.
- Hot cue marks on decks 1 and 2 in MODERN no longer vanish when one pad holds a cue without an OUT point or a pad is unused.
- On decks 3 and 4, TRACK SEARCH previous and next track respect AUTO CUE: the new track waits at its cue instead of playing.
- BEAT JUMP on decks 3 and 4 works again on tracks without a detected BPM, counting beats at 120 BPM as decks 1 and 2 do.
- The KEYS page no longer offers KEY SYNC and SET UP KEYS on the master deck. It says the deck is the master.
- Hot cue preview, the X-PAD sampler and groove pads are no longer listed in MODS, because they are not built yet.
- Experimental. XDJ-XZ firmware 1.26 only.
- Loopback cable in USB 1, music in USB 2.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.8 - 2026-10-27

A diary on every USB stick for bug reports.

- While the XZ plays, XZ Mods keeps a small diary on every USB stick in VJ.Tools/XZ-Mods-diary, at most 4 MB per stick, oldest part replaced first. It holds settings, state changes, the notices shown and a reading every 10 seconds of CPU, RAM, missed audio, SYNC drift, key shift load and USB read speed, so a later bug report can show what happened.
- The diary never holds track titles, file paths or serial numbers, never writes to the XZ itself, pauses while a stick loads a track or copies to RAM, and skips read-only or full sticks.
- Experimental. XDJ-XZ firmware 1.26 only.
- Loopback cable in USB 1, music in USB 2.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.7 - 2026-10-27

Key shift on every deck, and SYNC that snaps back.

- Every deck can key shift and KEY SYNC at the same time. A new shift is refused only if the XZ has too little audio time to spare (KEY SHIFT REFUSED: AUDIO LOAD TOO HIGH). A shift already playing is never cut.
- MODS > SCREEN > SYSTEM READOUT shows the audio load of decks 3 and 4 and how many decks are key shifting.
- Turning SYNC off and on again on deck 3 or 4 snaps it to the master's nearest beat (or the QUANTIZE beat value), as decks 1 and 2 do.
- The SYNC button lights while deck 3 or 4 has SYNC on, and the MASTER button lights on the side showing the master deck, including deck 3 or 4.
- Key shifting uses less than half the processor time it did, with the same sound.
- Experimental. XDJ-XZ firmware 1.26 only.
- Loopback cable in USB 1, music in USB 2.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.6 - 2026-10-27

SYNC without waiting, key sync and RAM tracks on decks 3 and 4.

- KEY shift and KEY SYNC on decks 3 and 4: KEY UP, KEY DOWN and RESET from MODS > PLAY > KEYS, the play-screen key popup or SHIFT + SYNC. Any deck can match its key to the master deck.
- RAM TRACKS on decks 3 and 4 while the loopback cable is connected. The track keeps playing from memory if the music USB is pulled. All four decks share one budget: up to 64 MiB a track and 128 MiB in all.
- MODS > SCREEN > 3-BAND COLOURS: choose the colour of the lows, mids and highs. STANDARD keeps each interface's own colour. The choice applies in MODERN, CLASSIC and on decks 3 and 4.
- Every MODS setting is saved to the USB stick, including waveform colour and position, key display, zoom, USB colours, hidden lamps and titles, browse columns and 3-band colours. They no longer reset when the player restarts.
- SYNC never shows WAIT. Pressing SYNC lights the button and matches the master's BPM at once, even when either deck is paused. The beat locks as soon as both decks play. The screen shows SYNC with the master's deck number, BPM when only the tempo can follow, or LIMIT when the master's tempo is out of reach.
- Decks 3 and 4 keep following the master's tempo when its beat position is briefly unavailable: while it is scratched, after a load, and on tracks without a beat grid or inside a loop.
- When deck 4 is the tempo master and stops, a playing deck 1 or 2 takes over before deck 3. From deck 3, MASTER passes to deck 4 first.
- The RAM TRACKS page says LOOPBACK CABLE NOT CONNECTED for decks 3 and 4 without the cable, beside its SET UP FOUR DECKS button.
- SYSTEM READOUT moved to its own row at the bottom of MODS > SCREEN.
- Experimental. XDJ-XZ firmware 1.26 only.
- Loopback cable in USB 1, music in USB 2.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.5 - 2026-10-26

The track menu with two decks.

- In MODERN, pushing the rotary selector on a track opens the track menu with two decks too (LOAD TO DECK 1, LOAD TO DECK 2, CUE/LOOP LIST, CANCEL). The cursor starts on the deck you are on, so two pushes load there. LOAD 1 and LOAD 2 still load directly.
- MODERN deck alerts show a clean separator between the alert name and the code.
- Experimental. XDJ-XZ firmware 1.26 only.
- Loopback cable in USB 1, music in USB 2.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.4 - 2026-10-26

Deck errors stay visible in MODS.

- In MODERN, a deck error (E-7206, E-8302, E-8304, E-8305, E-8306) stays visible while MODS is open: a red line at the bottom of MODS names the deck and the code. Tap it to return to the play screen.
- Experimental. XDJ-XZ firmware 1.26 only.
- Loopback cable in USB 1, music in USB 2.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.3 - 2026-10-26

One MODS menu and new deck badges.

- One MODS menu for both interfaces: the CLASSIC layout, with MODERN's picture choices, UTILITY on the rail and a new LAMPS AND TITLES page.
- New deck number badges: decks 1 and 2 white on grey, decks 3 and 4 black on white.
- A short MENU press opens MODS on screens where MENU has no other job. Holding MENU still opens the player's own settings.
- The MODERN browse screen starts with the preview waveform hidden. PREVIEW shows it and the choice is remembered.
- The DECKS button is always on the play screen. A saved hide no longer removes it.
- Per-deck errors show their code on the MODERN play screen.
- The USB setup in the app explains how MODS opens and what the button next to LINK does.
- Experimental. XDJ-XZ firmware 1.26 only.
- Loopback cable in USB 1, music in USB 2.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.2 - 2026-10-26

Deck colours, two-deck focus and a cable animation.

- Each deck has its own colour on the four-deck screens (1 yellow, 2 aqua, 3 rose, 4 violet): on the waveform tags, the bottom channels, the side tags and the DECKS button, so the top and the mixer-order bottom match at a glance.
- 2 DECKS shows the two decks the sides control. Press SHIFT twice on a side to switch the deck and its waveform together. CLASSIC has its own view of decks 3 and 4.
- A loopback cable animation shows where the cable goes, on the deck 3 and 4 cards, the 4 DECKS pages and the desktop four-deck guide.
- The BEAT JUMP PADS page is simpler: the pads show each jump in beats and bars, and one tap on a size sets the selected pair.
- The CLASSIC top bar buttons fit the black strip and no longer cover the DECK 1 tab, the BEAT FX header or the waveforms.
- Decks 3 and 4 react to the loopback cable as soon as it appears instead of checking once a second.
- On beat jump page 1, pushing a greyed pair with the rotary selector no longer changes sizes you cannot see.
- Experimental. XDJ-XZ firmware 1.26 only.
- Loopback cable in USB 1, music in USB 2.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22.1 - 2026-10-26

MODS menu, deck views and beat jump.

- A redesigned MODS menu organised by what you want to do: HOME, PLAY, STEMS, SCREEN, 4 DECKS, CONNECT and EXTRAS, with explanations and picture controls.
- Saved DECK VIEW choices: 2 DECKS, 4 DECKS 1 2 3 4, and 4 DECKS MIXER ORDER 3 1 2 4, with a play-screen button and a picker in both interfaces.
- The four-deck view can be chosen without the loopback cable. A status card offers RESCAN and TWO DECKS.
- The CLASSIC four-deck layout has four bottom channels in mixer order and four deck tags.
- Gate Cue and Smart Cue work on decks 3 and 4.
- Beat jump uses your set distances on all four decks. Decks 3 and 4 used fixed 1/2/4/8 distances and ignored page 2.
- Jump refusals show on the play screen.
- MODERN follows stem order, the key display setting and colour-compatible keys.
- Experimental. XDJ-XZ firmware 1.26 only.
- Hardware testing of this build is still in progress.

## 0.5.0-preview.22 - 2026-10-25

Steadier waveforms and saved reports.

- Waveforms and beat grids stay correct when you change tracks, in both interfaces.
- A four-deck start that fails no longer leaves a half-started deck behind.
- Changing the category or summary of a report on the XZ needs a fresh review before it can be sent.
- A stalled report upload stops at its time limit and is not marked as sent until it is checked.
- Reopen, review or discard saved reports in the Loader, including when the 32-draft limit is reached.
- Experimental. XDJ-XZ firmware 1.26 only. Stop decks 1 and 2 before decks 3 and 4 start. Loopback cable in USB 1, music in USB 2.
- Hardware testing of this build is still in progress. Some lamps on decks 3 and 4, half and double BPM matching, stems and key shift on decks 3 and 4, and pulling the music USB during playback are not finished in this build.

## 0.5.0-preview.21 - 2026-10-25

Four decks wait for a late music USB, and private reports.

- Decks 3 and 4 wait for a music USB that mounts late instead of giving up at startup.
- Later partitions on a stick, sticks without serial numbers and HFS+ sticks work alongside FAT and exFAT.
- The Loader offers a four-deck setup check. A saved older build is never replaced without you choosing it.
- Preview and send cleaned-up diagnostics from the Loader or from the Support menu on the XZ. Logs stay private. A separate GitHub draft lets you post a public issue yourself.
- Report previews and receipt QR codes on the XZ, in both interfaces. Nothing uploads automatically.
- Includes the changes prepared for preview.20, which was not released on its own.
- Experimental. XDJ-XZ firmware 1.26 only. Loopback cable in USB 1, music in USB 2. Pulling the music USB during playback, RAM tracks, and stems and key shift on decks 3 and 4 are not available in this build. Hardware testing of this build is still in progress.

## 0.5.0-preview.19 - 2026-10-24

New version numbering.

- Experimental four decks for XDJ-XZ firmware 1.26, with an animated guide to the USB loopback cable.
- One layout control switches between two decks, four decks in order 1 2 3 4 and mixer order 3 1 2 4. Both interfaces work with the rotary selector and BACK.
- A NETWORK tab brings adapter and IPv4 settings together with network updates. About has credits, QR links, licenses and optional support links.
- Decks 3 and 4 have performance pad modes, reverse, slip, quantize, memory and hot cues that are kept, vinyl coast and tempo slider pickup.
- SYNC tempo matching no longer depends on the tempo range. Turning SYNC off or taking MASTER keeps the matched speed until you pick up the tempo slider, and changing the range does not make the speed jump.
- BPM readouts no longer wander because beat positions were rounded.
- Four-deck start and reconnect show a reason when something stops.
- Jog deck badges sit alongside BPM, key, time and the position indicator. Fixes for browse, closing menus and held buttons.
- The beta is now numbered 0.5. This is the same build as 0.3.0-preview.19, and you do not need to update your USB for it.
- Windows 10 or 11, 64-bit. XDJ-XZ firmware 1.26. Update the USB loader after updating the app. Keep the music stick in USB 2 and put the loopback cable in USB 1 after you remove the loader stick.
- Four decks and the MASTER handover between decks are experimental. Test the build with your tracks before a set.
- Some lamps on decks 3 and 4 are not finished. Half and double BPM matching is not available; a tempo out of reach shows LIMIT.
- RAM tracks, pulling or swapping the music USB during playback, and stems and key shift on decks 3 and 4 are not available in this beta.
- In this preview, settings changed on the XZ are not saved. Set your startup preferences in the USB setup. The beta channel is separate from stable 0.2.1.

## 0.3.0-preview.19 - 2026-10-24

Experimental four decks.

- Experimental four decks for XDJ-XZ firmware 1.26, with an animated guide to the USB loopback cable.
- One layout control switches between two decks, four decks in order 1 2 3 4 and mixer order 3 1 2 4. Both interfaces work with the rotary selector and BACK.
- A NETWORK tab brings adapter and IPv4 settings together with network updates. About has credits, QR links, licenses and optional support links.
- Decks 3 and 4 have performance pad modes, reverse, slip, quantize, memory and hot cues that are kept, vinyl coast and tempo slider pickup.
- SYNC tempo matching no longer depends on the tempo range. Turning SYNC off or taking MASTER keeps the matched speed until you pick up the tempo slider, and changing the range does not make the speed jump.
- BPM readouts no longer wander because beat positions were rounded.
- Four-deck start and reconnect show a reason when something stops.
- Jog deck badges sit alongside BPM, key, time and the position indicator. Fixes for browse, closing menus and held buttons.
- Windows 10 or 11, 64-bit. XDJ-XZ firmware 1.26. Update the USB loader after updating the app. Keep the music stick in USB 2 and put the loopback cable in USB 1 after you remove the loader stick.
- Four decks and the MASTER handover between decks are experimental. Test the build with your tracks before a set.
- Some lamps on decks 3 and 4 are not finished. Half and double BPM matching is not available; a tempo out of reach shows LIMIT.
- RAM tracks, pulling or swapping the music USB during playback, and stems and key shift on decks 3 and 4 are not available in this beta.
- In this preview, settings changed on the XZ are not saved. Set your startup preferences in the USB setup. The beta channel is separate from stable 0.2.1.

## 0.3.0-preview.18 - 2026-10-20

MODERN and CLASSIC with experimental decks 3 and 4.

- Brings together the MODERN and CLASSIC interfaces, DJ Profile 1.0 and the latest USB loader.
- MODERN has one MODS menu, small stem buttons beside the hot cues, separate stem layouts and a BYPASS button. Four-deck layouts keep these controls for decks 1 and 2.
- Experimental decks 3 and 4 over a USB loopback cable, with loops, tempo ranges and pickup, Master Tempo, vinyl playback in both directions, AIFF files and both interface layouts.
- In a test on the XZ, MP3 decoding ran about 14 times faster with the same output.
- Key Sync can turn off during a short gap in track data without undoing a newer track or pitch change, and Master Tempo goes back to how it was.
- The beta checks its own update channel at launch and refuses downgrades.
- Update the USB loader to use this build. Updating the app alone does not change your USB.
- Four decks need a USB loopback cable. Smooth audio and long sessions are not verified yet.
- Slow waveform loading is still being looked at. On decks 3 and 4, AAC/M4A files, jog displays, PLAY/CUE lamps, beat sync, reverse, slip, quantize and memory cues are not finished.

## 0.3.0-preview.17 - 2026-10-19

DJ Profile 1.0.

- DJ Profile 1.0: each DJ's profile has its own folder on the USB, following the open DJ Profile standard. The first save on an older USB moves its profiles to the new layout and keeps a copy of the old files on this computer. The XZ reads and writes the new layout and shows the USB's colour.
- Update the loader on your USB to use it. Updating the app alone does not change the USB.

## 0.3.0-preview.16 - 2026-10-19

Profile track check.

- The profile track wizard checks the USB and tells you whether the track reached the USB's music library. A draft of the open DJ Profile 1.0 standard is in spec/djprofile.
- Updating this app does not change the loader on your USB.

## 0.3.0-preview.15 - 2026-10-19

Choose your screen when you set up the USB.

- Installing or updating a USB asks which screen the XZ shows: the new interface (the default) or the standard XDJ-XZ interface. Each option shows the screen it gives, and a picture shows how to switch later in MODS > APPEARANCE > INTERFACE.
- MODS > APPEARANCE starts with INTERFACE (MODERN or CLASSIC), then that interface's themes, the waveform style, key display and stem colours.
- A fresh USB written with the screen choice gets its settings file, so the XZ shows the screen you chose.
- Update the USB loader to use it. Settings saved by older loaders keep their look.

## 0.3.0-preview.14 - 2026-10-18

Safer eject.

- Eject is only offered for USB drives and card readers. An internal disk chosen by mistake is never dismounted.
- Same USB loader as preview.12. Updating this app does not change the loader on your USB.

## 0.3.0-preview.13 - 2026-10-18

Eject the USB from the app.

- When the app finishes writing to your USB it offers Eject USB, so you can unplug it straight away.
- Tick "Eject the USB when writing finishes" and the app ejects it by itself after every write. The choice is remembered on this computer.
- Choosing a USB shows "Reading the USB" with a spinner the moment you click, instead of a pause with no sign anything happened.
- Same USB loader as preview.12. Updating this app does not change the loader on your USB.

## 0.3.0-preview.12 - 2026-10-18

A new performance theme.

- A new theme in MODS > Appearance, later the MODERN interface: black panels with outlined captions, blue selection, red tempo and quantize marks, and a three-band waveform with blue lows, amber mids and cream highs.
- Its MODS menu has a grey category rail, value rows with ON/OFF chips, radio choices, DECK tags, pad chips and a BEAT LOOP strip. Turn the rotary selector to move, push to select, and press BACK to go up.
- Update the USB loader to get it. The theme is saved on the USB like every other theme.

## 0.3.0-preview.11 - 2026-10-14

A simpler app.

- The app has four places: USB, Stems, XZ Screen and Tools. DJ Profile, Repair library, Network updates, XZ Mods version, App updates and About are in Tools.
- Plug in your USB and it appears. The USB page shows one status (not installed, update available or up to date) with one button, plus your DJ profile, games and stems at a glance.
- Installing or updating is a short guided flow: your DJ profile if you want one, a plain list of what will be written, then write. Music, playlists and prepared stems are never touched.
- On a Mac, only local USB drives are offered, not disk images or network shares. The XZ's network address is suggested first for network updates.
- Same USB loader as preview.10. Updating this app does not change the loader on your USB.

## 0.3.0-preview.10 - 2026-10-14

Smart Cue fix.

- Includes the smoother XZ Screen stream from preview.9.
- Smart Cue updates the main cue without cancelling what the deck is doing. This fixes hot cues stopping on release with Gate Cue off.
- Update the USB loader to get both. Hardware testing of this build is still in progress.

## 0.3.0-preview.9 - 2026-10-14

A smoother XZ Screen.

- Live Streamer sends a smoother, lossless picture of the XZ's screen, shown using your graphics card. The badge shows the frame rate it measures.
- The app preview and OBS share one connection to the XZ. A slow viewer skips old pictures instead of falling behind.
- The Windows installer backs up the previous app resources before replacing them.
- Update the USB loader for the smoother stream. An older loader keeps its working preview and offers an update button.
- The frame rate on the XZ while it plays is still being measured.

## 0.3.0-preview.8 - 2026-10-14

See the XZ screen on your computer.

- Live Streamer shows the XZ screen on this computer and gives you a local OBS Browser Source URL. The preview refreshes up to once per second.
- USB stems load by themselves when a prepared track is loaded. Newer 44.1 kHz FLAC stem sets play alongside the older formats.
- Stem controls show why a deck is unavailable, loading or aligning. Turning USB stems on is one action, and the menu keeps USB stems apart from stems played from a computer.
- The desktop stem list names the supported FLAC formats and no longer marks unknown formats as ready.
- Update the USB loader for screen viewing and the new stem format.

## 0.3.0-preview.7 - 2026-10-12

Shorter USB setup.

- Set up USB is five steps: USB, Loadout, DJ Profile, Write, Done.
- A new USB gets every free game that works on the XZ (Doom shareware, Chex Quest, Freedoom Phase 2). Untick any of them in Custom installation. A USB that already has the loader keeps its games.
- Your USB's colour is read correctly.
- Repair library can fix another kind of USB library damage, with a backup and undo while the files stay unchanged.
- Loader updates accept the current default settings profile instead of rejecting it as too large.
- My House is removed. It needs a game engine that does not run on the XZ.
- The settings editor is gone from the app. Change settings in the MODS menu on the XZ.

## 0.3.0-preview.6 - 2026-10-12

DJ Profile in USB setup.

- A DJ Profile step in Set up USB. Type your DJ name, drop your logo, and the Write step saves the profile with the loader. Skip it if you don't want one yet.
- Drag a PNG or JPEG anywhere onto the window while the profile editor is open, or use Choose file. Several files at once become alternates.
- A new profile starts in this USB's colour, marked among eight swatches. Any other colour is one click away.
- The editor shows the name, logo and colour. Tagline, links, other names and the note fold away under one optional heading.

## 0.3.0-preview.5 - 2026-10-12

DJ Profile page, games and an update preview.

- A DJ Profile page: your DJ name, a tagline, an accent colour, up to eight logos and links. A live preview shows the MODS header and the logo tile. Profiles are saved on the USB only.
- Profile track: for players that do not read extra files from the USB, the app makes a silent MP3 that carries your name, logos and links in its tags. A short guide checks it is silent and shows how to add it to a DJ PROFILE playlist.
- Update preview: when the USB holds an older loader, see what changes on your XZ, grouped as New, Improved and Fixed.
- A Games page and a NonEssentials step for choosing games.
- The USB loadout tree and settings colours lay out correctly in the installed app.
- XDJ-XZ firmware 1.26.
- Not hardware-verified: test the loader on your XZ before a set.

## 0.3.0-preview.4 - 2026-10-12

Your USB as a file tree.

- The Loadout step shows the USB as folders and files: the loader, settings, loader backups, each game, prepared stems and your music library, each marked Up to date, Update available, Not on USB or Never changed. Tick what to add or update.
- Set up USB is five steps: USB, Loadout, Settings, Write, Done.
- XDJ-XZ firmware 1.26.
- Not hardware-verified: test the loader on your XZ before a set.

## 0.3.0-preview.3 - 2026-10-11

One guided USB setup.

- Set up USB is one guided flow: find the USB, see what is on it, add or update the loader, pick games, edit settings, write, done.
- Games are a choice. Free downloads are listed with their sizes and only downloaded when ticked. Beat Arcade, Wave Rider, Rhythm Lab and Static Surfer stay built into the loader.
- The settings editor is laid out like the MODS menu on the XZ, with an info dock that explains the control under the pointer.
- One write at the end: the loader (backed up first), the chosen games and your settings go to the USB together, then the USB is read back.
- A USB without an exported music library is now found by the USB finder.
- Preparing or updating a loader no longer forces every free game onto the USB.
- XDJ-XZ firmware 1.26.
- Not hardware-verified: test the loader on your XZ before a set.

## 0.3.0-preview.2 - 2026-10-10

MP3 stems on the player's timeline.

- The stems page marks older MP3 stems with Prepare again and explains why. Select those tracks and prepare them again.
- Stems prepared from MP3s now line up with the player's own timing. Before, stems from most MP3s started 25 ms early. WAV, FLAC and untagged MP3s were never affected.
- Installing this preview does not change your USB.

## 0.3.0-preview.1 - 2026-10-10

One Prepare stems page and a new separation engine.

- Prepare stems is one flow: pick the USB, see every track as Ready or Missing with its size, select, and press Prepare.
- Prepare stems from MP3 exports and from 24-bit, float and any-rate WAV or FLAC. AIFF and AAC are listed as not yet supported.
- Every track shows the USB space its stems will take, and the selection shows the total and the space left after it.
- Import stems made elsewhere (Suno, Demucs, UVR or a DAW), as files or a ZIP. Roles are assigned by name, timing and level are checked against the track, and stems from another song are refused.
- A new separation engine uses the GPU when there is one: Balanced, Fast and Light presets, all MIT-licensed, with a live percentage and a time estimate.
- The STEMS scrolling waveform and stem overview now work for tracks XZ Mods prepared.
- Imported VBR MP3 stems decode correctly, and adding waveforms to a track can no longer lock it out if the USB is unplugged.
- Engine setup checks your computer, picks a preset, installs what it needs and runs a 30-second test before the first batch.
- The Vocal focus preset is retired. Balanced replaces it with a better vocal model under the same licence.
- Installing this preview does not change your USB.

## 0.2.1 - 2026-10-02

- XZ Mods 0.2.1 is the hotfix for 0.2.0. Use it for new loaders and loader updates.
- Includes the 0.2.0 feature set: FLAC stems, selected-track batches, layered waveforms, Beat Arcade, Wave Rider and game downloads.
- Corrects the old 0.1.9 labels in the bundled app so its version is correct before the startup check finishes.
- Identifies the hotfix consistently in the app, backend, signed updater feed and website.
- The files XZ Mods puts on the USB for the player are the same verified files shipped with 0.2.0. This hotfix updates the desktop app and version labels.
- Installing this app does not change your USB. Prepare or update its loader separately when ready.
- A portable folder is not replaced by the Windows installer. After installing, open XZ Mods from the desktop shortcut; to stay portable, replace the folder with the current ZIP.
- Mac previews remain ad-hoc signed and not notarized. My House still needs a game engine that does not run on the XZ.

## 0.2.0 - 2026-10-02

- Apple Silicon and Intel Mac utilities for macOS 14 or later. Apple Silicon can generate stems; both architectures can import stems and use USB, settings, waveform and game tools.
- Play stems in the current OverCue FLAC format as well as the older format. Generate and Import now write FLAC stems and check all seven playback mixes.
- Prepare selected tracks as a batch, reuse verified stems, see each track's progress, cancel safely, and retry failed tracks.
- Layered Vocals, Drums and Harmonics waveforms with a live playhead, plus a 3-band waveform preparation tool with a backup.
- Beat Arcade rhythm games and Wave Rider, a jog-wheel game driven by the loaded track's real waveform. Open them when wanted through MODS > Extras > Games.
- Games setup downloads Doom shareware, Chex Quest and Freedoom from their own distributions, checks the downloads, and prepares supported data on the selected USB. Bring your own licensed commercial IWAD.
- Long tracks are prepared through files on disk, so the app no longer holds whole tracks and seven mixes in memory.
- Vocal focus uses less memory, and long FLAC sources are read in parts.
- Waveforms refresh more reliably and themes draw better, with deck transport and stem controls unchanged.
- Beat Arcade and Wave Rider stay closed during normal startup. The automatic game opening switch is only a local test option.
- Installing or updating the desktop app does not update an existing USB. Open USB loader and use Back up and update loader.
- Mac packages use an ad-hoc signature and are not notarized. Follow the Mac setup instructions before opening the downloaded utility.
- The newest XZ Mods version on the player remains experimental. Game controls, phase alignment on real songs and long two-deck sessions are still being tested. My House stays listed as unavailable.

## 0.1.10 - 2026-10-02

- Apple Silicon and Intel Mac utilities for macOS 14 or later. Apple Silicon can generate stems; both architectures can import stems and use USB, settings, waveform and game tools.
- Play stems in the current OverCue FLAC format as well as the older format. Generate and Import now write FLAC stems and check all seven playback mixes.
- Prepare selected tracks as a batch, reuse verified stems, see each track's progress, cancel safely, and retry failed tracks.
- Layered Vocals, Drums and Harmonics waveforms with a live playhead, plus a 3-band waveform preparation tool with a backup.
- Beat Arcade rhythm games and Wave Rider, a jog-wheel game driven by the loaded track's real waveform. Open them when wanted through MODS > Extras > Games.
- Games setup downloads Doom shareware, Chex Quest and Freedoom from their own distributions, checks the downloads, and prepares supported data on the selected USB. Bring your own licensed commercial IWAD.
- Long tracks are prepared through files on disk, so the app no longer holds whole tracks and seven mixes in memory.
- Vocal focus uses less memory, and long FLAC sources are read in parts.
- Waveforms refresh more reliably and themes draw better, with deck transport and stem controls unchanged.
- Beat Arcade and Wave Rider stay closed during normal startup. The automatic game opening switch is only a local test option.
- Installing or updating the desktop app does not update an existing USB. Open USB loader and use Back up and update loader.
- Mac packages use an ad-hoc signature and are not notarized. Follow the Mac setup instructions before opening the downloaded utility.
- The newest XZ Mods version on the player remains experimental. Game controls, phase alignment on real songs and long two-deck sessions are still being tested. My House stays listed as unavailable.

## 0.1.9 - 2026-09-28

- Play-screen artwork for the Game Boy, Super Nintendo, Windows 95, Game Boy Color, Aqua and DMG theme families.
- Dedicated Glass panels with readable data wells and clearer focus states.
- Separate green LCD waveform areas for DMG, and theme-specific title bars for Windows 95, RPG, Aqua and DMG.
- More saturated LCARS orange and violet, connected panel rails, and corrected deck-2 title colors.
- Fixed title text contrast and antialiasing against each theme's background.
- Preserved stronger waveform and cue colors instead of washing them toward the text color.
- Reduced oversized Glass menu capsules and overpowering background gradients.
- Kept cue regions, playheads, tempo readouts and transparent artwork masks protected.
- After installing, use USB loader > Back up and update loader to put these changes on an existing USB. Installing the app alone does not update the USB.
- Theme IDs and saved selections are unchanged.

## 0.1.8 - 2026-09-28

- Install XZ Mods with the Windows installer, or download the optional portable ZIP.
- The app checks for updates at startup. Review the release notes, then download and install a signed update.
- Update an existing USB loader with a backup and restore option, and edit the settings saved on your USB.
- Use 24 themes with consistent MODS menu positions and a top-right Close button.
- Customize four backward/forward Beat Jump pairs on each of two pages under Controls. Hold Shift and tap Beat Jump for page 2.
- Page 2 is enabled by default with 16, 32, 64 and 128 beats. Labels show both beats and bars; red/orange pad shades identify the second page.
- Assign stem shortcuts through an eight-pad diagram in Controls.
- Browse your exported USB library with automatic drive selection, free space and stem status. Generate stems with Open-Unmix HQ or Vocal focus.
- Import multiple instrument stems, assign them to Drums, Vocals or Harmonics, and combine them into OverCue files for an exported track.
- Fixed track-title contrast in LCARS and Analog themes.
- Keep theme, pad and Beat Jump settings saved between restarts when the settings USB is writable.
- Fixed the narrow, vertically wrapped Prepare a new USB card and rewrote the app instructions.
- Fixed Open-Unmix model loading and Vocal focus processing on Windows CPUs.
- Allow exFAT music libraries in the stem browser and importer, separately from the loader USB requirements.
- Removed the 3-band waveform, DJ branding and VJ.Tools pages from the Windows app.

## 0.1.7 - 2026-09-28

- Browse tracks from your exported USB library with automatic drive selection, free space and stem status.
- Generate stems from the track list using Open-Unmix HQ or Vocal focus.
- Import multiple instrument stems, assign them to drums, vocals or harmonics, and combine them into OverCue files.
- Check for app updates from the Builder.
- Download and install updates through the Windows installer. A portable ZIP is also available.
- Read new features and fixes before updating. The app opens the release notes after an update.
- Finished jobs now release their files before another operation or app update starts.
- Fixed the narrow, vertically wrapped text in Prepare a new USB.
- Rewrote app instructions and simplified the About page.
- Fixed Open-Unmix HQ failing to load its model files.
- Fixed Vocal focus failing on Windows CPUs.
- Read and prepare stems on exFAT music USBs, which the loader USB itself does not allow.
- Removed the 3-band waveform tools while they are being corrected.

## 0.1.6 - 2026-09-28

- Update an existing USB loader with a verified backup and restore option.
- Edit the themes, stem shortcuts and beat-jump sizes saved on a USB.
- Use separate eight-pad setup views for Stem Shortcuts and Beat Jump in Controls.
- Restore readable track titles in LCARS and Analog themes.
- Save theme, pad-bank and stem-row settings between reloads.
- Keep beat-jump page 2 enabled by default and distinguish its pads with red/orange shades.
- Move the MODS Close button to the top-right corner.
- Remove DJ branding and VJ.Tools pages from the Windows Builder and simplify its wording.

## 0.1.5 - 2026-09-25

Prepare the loader with one USB selection.

- Add **Choose USB and prepare loader**. Choose a suitable FAT/FAT32 USB and the app gets and checks the files it needs, then builds the loader and writes `autoexec.bin` to the USB root. You no longer need to collect the files yourself.
- Unsuitable drives are rejected before downloading. Music stays in place, and preparation refuses to replace an existing loader.
- Checked files are kept for later builds. Local-file and staging-folder options remain in Advanced.

The owner prepared a USB with this version and started the XDJ-XZ with MODS running. Long performances and experimental controls are tested separately.

## 0.1.4 - 2026-09-25

USB startup fix.

- A line-ending fix in the loader files lets startup continue to MODS.
- The audio, stem controls, themes and pad colors from 0.1.3 stay the same.

USB loaders made with 0.1.3 need rebuilding. The fix is also in 0.1.5 and later.

## 0.1.3 - 2026-09-25

See both decks, with clearer stem controls.

- Two compact stem rows sit below the waveforms. Tap to mute and drag horizontally to adjust volume.
- Both pad banks get corrected brightness and primary stem colors that stay independent of your screen theme.
- The standalone builder and the VJ.Tools Library use the same checked files.
- Real XZ screenshots and an illustrated user guide explain the controls and preparation.

Stem rows, clean playback and pad colors were checked on the player. The USB startup issue in this version was fixed in 0.1.4.

## 0.1.2 - 2026-09-23

Choose your stem pads and keep the other bank's hot cues.

- Choose A-D or E-H. The saved pad-bank option puts Vocals, Harmonics, Drums and Bypass on your chosen bank. The inactive bank keeps its normal hot cues.
- Screen controls, stem waveforms and pad colors use the same order, with brighter default colors.
- The VJ.Tools corner button appears only with a live, enabled connection. Touches stay with the view on screen.

## 0.1.1 - 2026-09-23

Play prepared OverCue stems on the XZ. Developer preview for XDJ-XZ firmware 1.26.

- XZ Mods reads OverCue stem bundles beside the matching exported track. **Check OverCue track** checks the original and all seven prepared mixes without changing the USB.
- Touch and pad mute state agree, both deck waveforms stay visible, and hiding the stem rows leaves the audio settings as they are.
- Background streaming and prepared combinations reduce decoder work and stop a loop wrap from bringing back a muted stem.

A checked file does not prove audible alignment or long two-deck sessions.
