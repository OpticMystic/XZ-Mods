---
interface: both
settings: interface
---

XZ Mods is software for the Pioneer DJ XDJ-XZ that starts from a USB stick. It adds a MODS menu, stems you can mute live, custom beat jump, four decks over a loopback cable, DJ profiles, themes and games. It can also replace the whole play screen with its own MODERN interface.

## What changes and what stays stock

XZ Mods runs in the player's memory. It never writes to the player's firmware, so the XDJ-XZ itself stays stock.

- The player picks up the XZ Mods loader (`autoexec.bin`) from the USB at start and runs XZ Mods from there.
- Your music, your Rekordbox export and your playlists stay as they are. XZ Mods adds its own folders beside them.
- The player's own settings are still there. Hold [[MENU]] to open UTILITY, exactly as on a stock XDJ-XZ.
- Settings you change in MODS are saved on the USB, so they travel with it to another XZ running XZ Mods.

### Go back to stock

1. Switch the player off.
2. Start it without the XZ Mods USB, or with a USB that has no `autoexec.bin` on it.

The player starts stock. Plug the XZ Mods USB back in and start again to load XZ Mods. If you play from a separate music stick, that stick works on the stock player as before.

## What you need

| Item | What you need |
|---|---|
| Player | Pioneer DJ XDJ-XZ on firmware 1.26. No other firmware version is supported |
| Computer | 64-bit Windows with WebView2, or a Mac with macOS 14 or later, to prepare the USB |
| Stems | Made on a Windows computer or an Apple silicon Mac. An Intel Mac can import stems made elsewhere |
| USB | A USB stick for the loader. It can be your music USB, or a separate loader stick |
| Four decks | A USB-B to USB-A cable. See [Four decks](four-decks#what-you-need) {experimental} |

The app downloads the official firmware it needs from the manufacturer and checks it. No firmware or boot files come with the XZ Mods download.

> **Caution.** XZ Mods is a preview. Test every new USB at home before you use it in a set.

## Prepare a USB

You prepare the USB in the XZ Mods app on your computer. It does not format the drive or touch your music.

1. Install the XZ Mods app and open it.
2. Plug in your USB. The app finds it and opens it. With several USBs plugged in, choose yours from the list.
3. Choose **Install XZ Mods**.
4. Follow the short guide: your DJ profile, the screen the XZ starts with, the four-deck cable guide, then a checklist of what it will write. **Back** returns to the step before.
5. Choose **Write to USB**, then **Eject USB**.

```figure app:install-review XZ Mods app · Install XZ Mods
The checklist before the write. Music, playlists and prepared stems are never touched.
1 @ 580,227 Loader | XZ Mods itself.
2 @ 706,273 Games | Free games the app downloads and checks.
3 @ 540,319 Interface | The screen the XZ starts with.
4 @ 890,728 Write to USB | Writes everything in the checklist.
```

Every step of the app is in [The XZ Mods app](desktop-app#usb). To add stems to your tracks, see [Stems](stems).

## First start

1. Plug the USB into **USB 1** or **USB 2** on the top of the XDJ-XZ.
2. Switch the player on.
3. Wait for XZ Mods to start. Load a track as usual and check it plays.
4. Press [[MENU]] on the play screen, or touch **MODS** at the top right, to open the MODS menu. HOME is the first page.

If you use a separate loader stick, keep it in until XZ Mods has started. Keep the USB with your music and stems in while you play.

### MODERN on first start {modern}

MODERN draws the whole play screen. The player's controls work as before, and the top bar adds a few buttons.

```figure modern:play-full MODERN · Play screen
The MODERN play screen with a track on each deck.
1 @ 772,32 MODS | Opens the MODS menu, like a short press of MENU.
2 @ 708,32 STEMS | Shows or hides the stem rows.
3 @ 565,32 DECKS | Chooses two or four decks.
4 @ 62,105 Deck lanes | One waveform lane per deck, with its source and key.
5 @ 200,288 Deck panel | The track, time, tempo and BPM of each deck.
```

The rest of the play screen is in [The play screen](play-screen).

### CLASSIC on first start {classic}

CLASSIC keeps the player's own play screen and adds MODS to it. Touch **MODS** at the top right, or press [[MENU]], to open the menu. A classic theme can restyle the screen. See [Screen, interfaces and themes](screen-and-themes).

## Choose MODERN or CLASSIC

The XZ starts in the interface saved on the USB. You choose it in the app before the first start, and you can change it on the player at any time. Nothing else changes when you switch. Your settings, stems and decks stay as they are.

| Interface | What you see |
|---|---|
| MODERN | XZ Mods draws the whole screen: bigger waveforms, a browse sidebar and a stems row with mute buttons. Marked Beta in the app |
| CLASSIC | The player's own play screen, with MODS added. Classic themes can restyle it |

A new USB starts in MODERN unless you choose the standard interface in the app.

```figure app:install-screen XZ Mods app · Install XZ Mods
Choose the screen the XZ shows. New interface is MODERN, Standard XDJ-XZ interface is CLASSIC.
1 @ 486,452 New interface | MODERN.
2 @ 1012,332 Standard XDJ-XZ interface | CLASSIC.
```

### Switch on the player

There are two ways to switch.

- Press the button next to LINK. Out of the box it switches between MODERN and CLASSIC, and the screen says which one you are in. You can give it another job in **PLAY** › **BUTTON NEXT TO LINK**.
- Open MODS, go to **SCREEN** and choose **CLASSIC** or **MODERN** under **INTERFACE**.

```figure classic:theme-{look}-screen MODS · SCREEN
INTERFACE is the first choice on the SCREEN page.
1 @ 200,53 INTERFACE | The interface the XZ shows. The change saves to the USB.
2 @ 199,140 CLASSIC | The player's own screen with MODS.
3 @ 329,140 MODERN | The full-screen XZ Mods layout.
4 @ 360,164 THEME | The classic theme. See Screen, interfaces and themes.
5 @ 165,425 Explanation | What the highlighted choice does.
6 @ 715,442 Save state | Confirms the change is saved to the USB.
```

> **Tip.** You can always reach MODS with the hardware alone. In CLASSIC, the button next to LINK takes you to MODERN, where [[MENU]] opens MODS, and the encoder and [[BACK]] drive it.

## Updating

There are two things to update, and they update separately.

| What | How |
|---|---|
| The app on your computer | On Windows the app checks at launch and offers the update. On a Mac, download the new version from the release page. See [App updates](desktop-app#app-updates) |
| XZ Mods on your USB | Open the USB in the app. When it says **Update available**, choose **Update USB**. The app backs up the old loader first |
| XZ Mods over the network | Send the update from the app and download it on the player. See [Updates over the network](connect#updates-over-the-network) {experimental} |

> **Caution.** Updating the app does not change the USB. After an app update, open the USB page and choose **Update USB**.

Music, Rekordbox data and your saved settings stay in place through an update. A USB built with **Keep this version on the USB** stays on its build and ignores network updates. See [XZ Mods version](desktop-app#xz-mods-version).

If something does not work after an update, see [Messages and troubleshooting](troubleshooting).
