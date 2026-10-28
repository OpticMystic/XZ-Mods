---
interface: computer
settings:
---

The XZ Mods app runs on your computer. It puts XZ Mods on a USB, makes stems for your tracks, makes your DJ profile, keeps the app and your USB up to date, holds tools for your library, games and the XZ screen, and sends your reports and ideas to the maintainer. It never formats a drive and never changes your music or your Rekordbox export.

You need 64-bit Windows with WebView2, or a Mac with macOS 14 or later. Making stems needs a Windows computer or an Apple silicon Mac. On an Intel Mac you can still import stems you made elsewhere.

## Around the app

The rail on the left has five sections for your work, then the Manual and About and, at its foot, Feedback. The current one is outlined in yellow.

| Section | What it is for |
|---|---|
| **USB** | Install or update XZ Mods on a USB, add your DJ profile and games |
| **Stems** | Make stems for the tracks on a Rekordbox USB, or import stems you already have |
| **DJ Profile** | Your name and logo on the XZ, and the profile track |
| **Updates** | App updates, the XZ Mods version for your USB, and network updates for your XZ |
| **Tools** | Repair library, Game downloads and XZ Screen |
| **Manual** | This manual, with search, animations and your place in it |
| **About** | The app, its maker, links, disclaimers and open-source licenses |
| **Feedback** | Report a problem, request a feature or ask a question, and follow what you sent |

The top of the rail names the player firmware the app prepares USBs for, XDJ-XZ 1.26. The app version is at the bottom.

The app opens as large as its pages need, but never larger than your screen. In a narrow window the rail shows only its icons. Point at one to see its name.

## USB

USB is the first page. Plug in your XZ USB, or any USB stick, and the app finds it. When only one USB is connected, it opens by itself.

```gallery
app:usb-plug-in No USB yet. The app keeps looking, so plug one in. Write to a folder instead… prepares a folder on this computer
app:usb-choose Several USBs. Each card says whether XZ Mods is on it yet and whether it holds a Rekordbox export
```

### Choose a USB or a folder

The USB list shows every connected USB the app can write XZ Mods to. Any file system works, FAT32 or exFAT, and each card shows it beside the drive letter. Choose a card to open that USB.

```figure app:usb-list XZ Mods app · USB
Two USBs and a folder to choose from. One line names the drives the app leaves out.
1 @ 640,190 USB | A USB the app can write to, with its drive and file system.
2 @ 640,358 Write to a folder instead… | Prepares a folder on this computer instead of a USB.
3 @ 762,413 Drives not shown | A read-only USB, or a disk of this computer, with the reason.
4 @ 366,467 Look again | Searches for USBs at once.
```

**Write to a folder instead…** puts the loader and its settings in a folder on this computer. Copy everything in it to the top of a USB stick afterward. Games are added only when you write to a USB itself.

The list comes back whenever you step back from a USB: **Change USB** on the dashboard, **Cancel** in a guide, or **Esc** on the dashboard. The USB you left does not open by itself again until you plug in or remove a drive, so you can pick another one. Its card is still selected, and Enter opens it again.

### The USB dashboard

Once a USB is open, the page shows one status and one action for it.

```figure app:usb-install XZ Mods app · USB
A fresh USB. XZ Mods is not on it yet, so the one action is Install XZ Mods.
1 @ 150,209 Sections | USB, Stems, DJ Profile, Updates and Tools.
2 @ 492,121 USB | The USB's name and drive.
3 @ 662,162 Free space | The file system, the free space and a bar of how full the USB is.
4 @ 988,100 Change USB | Goes back to the USB list, to choose another USB or a folder.
5 @ 606,228 Status | What XZ Mods found on this USB.
6 @ 494,324 Action | The one thing to do next. Here it installs XZ Mods.
7 @ 364,442 DJ profile | The active profile on the USB. Add or Edit opens the profile step.
8 @ 604,442 Games | How many games are on the USB. Choose opens the games list.
9 @ 870,442 Stems | How many tracks have stems. Prepare opens the Stems section.
10 @ 430,541 Build | The XZ Mods build that goes on the USB. Change opens [XZ Mods version](#xz-mods-version) in Updates.
11 @ 1064,595 Four decks | How four decks work. It sets nothing up. See [Four decks in the app](#four-decks-in-the-app).
12 @ 464,664 Show every file | A read-only list of what XZ Mods keeps on this USB.
13 @ 152,676 Feedback | Reports and ideas for the maintainer. See [Feedback](desktop-app#feedback).
```

The status changes with what the app finds:

| Status | Action | What it means |
|---|---|---|
| XZ Mods is not on this USB yet | **Install XZ Mods** | Adds the loader, free games and your DJ profile. Music and playlists stay as they are. |
| Update available | **Update USB** | The USB has an older build. Updating backs up its loader first. **See what changes** lists what is new. |
| Up to date | none | The chosen build is already on this USB. |
| Choose which XZ Mods build goes on this USB | **Use the newest build** | No build is chosen on this computer yet. The button picks the one that came with the app. |
| No XZ Mods build is on this computer yet | **Set up mod versions** | Opens a short guide that finds or imports a build. |

```gallery
app:usb-update Update available, with the number of changes
app:usb-up-to-date Up to date
app:usb-choose-build No build chosen yet. Use the newest build picks the one that came with the app
```

### Install or update XZ Mods

**Install XZ Mods** and **Update USB** open a short guide. The steps that do not apply are left out, so the count at the top changes.

Every step ends in the same bar. **Back** on the left returns to the step before; from the first step it returns to the dashboard. The way forward is on the right. **Esc** does the same as Back. **Cancel** at the top leaves the guide without writing anything and shows the USB list, so you can choose another USB or a folder. While the USB is written there is no Back, and the done screen has **Done** instead, because a write cannot be undone.

1. **Your DJ profile.** Appears when the USB has no profile yet. Type your name and add a logo, or choose **Skip for now** and add it later.
2. **Choose the screen the XZ shows.** New interface (MODERN) or Standard XDJ-XZ interface (CLASSIC). See [Getting started](getting-started#choose-modern-or-classic).
3. **Ready to write.** A checklist of everything the write will do.
4. **Writing.** The progress of each part. Underneath, the app explains four decks while you wait.
5. **Done.** What was written, what to do on the XZ, and how four decks work.
6. **Optional support.** A thank-you page with a voluntary support link. Every feature works whether you contribute or not.

Four decks is not a step of the guide. Nothing about it happens while the USB is written: you set it up at the player, whenever you want. See [Four decks in the app](#four-decks-in-the-app).

```gallery
app:install-profile Your DJ profile. Skip for now leaves it for later
```

```figure app:install-screen XZ Mods app · Install XZ Mods
Choose the screen the XZ shows. The selected card plays a short clip of it.
1 @ 790,30 Progress | Which step you are on.
2 @ 1046,30 Cancel | Leaves the guide. Nothing is written.
3 @ 486,452 New interface | MODERN. XZ Mods draws the whole player screen.
4 @ 1012,332 Standard XDJ-XZ interface | CLASSIC. The player's own screen with MODS added. Classic themes can restyle it.
5 @ 782,560 Switch later | How to change your mind on the player.
6 @ 372,728 Back | The step before. From the first step, the dashboard.
7 @ 945,728 Next | The next step.
```

> **Note.** The two small pictures under the cards show an older version of the MODS menu. On the player, INTERFACE is on the [SCREEN](screen-and-themes#modern-and-classic) page.

```figure app:install-review XZ Mods app · Install XZ Mods
Ready to write, with Change games open. Under the list, a line says music, playlists and prepared stems are never touched.
1 @ 580,227 Loader | Adds the XZ Mods loader, or backs up and updates it.
2 @ 706,273 Games | The free games that will be copied.
3 @ 540,319 Interface | The screen the XZ starts with.
4 @ 478,384 Settings file | A USB without settings gets a new settings file.
5 @ 368,432 Change games | Opens or hides the games list.
6 @ 1040,501 Add all free games | Ticks every download. None unticks the ones not on the USB yet.
7 @ 638,554 Game state | WILL ADD, NOT ON USB, or already on the USB.
8 @ 890,555 Add | Ticks one game. The size is what it adds to the USB.
9 @ 372,728 Back | The step before.
10 @ 890,728 Write to USB | Writes everything in the checklist.
```

Beat Arcade, Wave Rider, Rhythm Lab and Static Surfer are built into the loader and always come with it. Doom shareware, Chex Quest and Freedoom Phase 2 are free downloads that the app checks and copies to `VJTOOLS\Games` on the USB. A fresh USB gets every free game unless you untick it. An update keeps the games the USB already has and adds none on its own. Under the checklist, **Eject the USB when writing finishes** ejects it for you so you can take it straight to the XZ.

Press **Write to USB** in the bar under the checklist. If a newer build needs a setting changed on the USB, the app first asks in **Adjust USB settings for this version**. It saves a copy of the settings file first and puts it back if the loader write fails.

```figure app:install-done XZ Mods app · Install XZ Mods
Your USB is ready. Four decks and the optional support link follow further down.
1 @ 757,180 Result | Your USB is ready, or Finished, with something to check.
2 @ 575,246 What was written | One line per part. A game that did not download says why.
3 @ 372,319 On the XZ | What to do with the USB next, and how to switch between CLASSIC and MODERN with the rekordbox button above LINK.
4 @ 490,554 Four decks | How four decks work. See [Four decks in the app](#four-decks-in-the-app).
5 @ 808,728 Done | Back to the dashboard.
6 @ 1062,728 Eject USB | Ejects the USB so you can unplug it.
```

Under Four decks, **Support XZ Mods (optional)** opens the optional support page, and **Eject the USB by itself next time** ejects the USB on its own after the next write.

If a game download failed, **Retry the failed games** tries only those, and the USB is not ejected until you have. When the write also saved a DJ profile, **Make the profile track** opens the profile track steps.

```gallery
app:install-writing Writing. The progress of each part shows inside the guide
app:install-ejected The USB is ejected and can be unplugged
```

### Four decks in the app {experimental}

[Four decks](four-decks) is set up at the player, so the app explains it rather than asking you to do anything. The USB list and the dashboard show a **Four decks** row. While a USB is written, and again on the done screen, a Four decks section shows the cable animation and the three things to do at the player.

```figure app:install-four-decks XZ Mods app · Install XZ Mods
The Four decks section on the done screen.
1 @ 490,51 Four decks | Experimental. Nothing to do now. You set it up at the player when you want it.
2 @ 560,300 Cable animation | The loopback cable from the rear USB-B socket to a top port. The animation shows USB 1; USB 2 works too.
3 @ 1006,387 Pause cable animation | Stops and starts the animation. With reduced motion turned on, the app shows a still picture.
4 @ 722,502 At the player | Cable from the rear USB-B socket to top USB 1 or 2, music in the other top port (a stick, or one USB 2.0 hub), and CH3 and CH4 set to USB.
5 @ 478,553 How four decks work | Opens the [Four decks](four-decks) chapter of this manual.
```

**How four decks work** and the **Four decks** row both open the Four decks chapter of this manual. If the build just written has no four-deck engine, the section says so and offers **Choose the four-deck build**, which opens the four-deck build choice in [XZ Mods version](#xz-mods-version). Save it, then update the USB.

## Stems

Stems makes vocal, harmonic and drum stems for the tracks on your Rekordbox USB and writes them beside each track. Plug in the USB you export to from Rekordbox. The app lists every track on it. Your tracks and your Rekordbox export stay as they are.

```figure app:stems-list XZ Mods app · Stems
Twelve tracks selected and the stem engine set up.
1 @ 355,90 USB | The Rekordbox USB. Its menu also offers Choose a folder and Look for USBs again.
2 @ 568,115 Tracks | How many tracks the USB holds and how many have stems.
3 @ 662,163 Estimate | How many tracks you selected, about how much space their stems take and what is left after.
4 @ 935,92 Prepare | Makes stems for the selected tracks. The label counts them.
5 @ 772,160 Engine | The stem engine and the quality in use. Change sets another quality.
6 @ 465,190 Tabs | Show All, Missing, Ready, Can't prepare, or only the Selected tracks.
7 @ 900,213 Search | Finds a title or artist.
8 @ 997,188 Import stems | Brings in stems you made elsewhere.
9 @ 245,447 Select | Ticks a track. The box in the header ticks every Missing track in view.
10 @ 852,446 Status | Ready, Missing, Not yet, No audio file, Preparing, Queued, Failed or Damaged.
11 @ 1065,446 Details | Opens the track's details.
```

| Status | What it means | What to do |
|---|---|---|
| Ready | Stems are on the USB | Nothing. Open the details to Verify them |
| Missing | No stems yet | Select it and prepare |
| Not yet | XZ Mods cannot prepare this format yet, such as AIFF | Export the track as WAV, FLAC or MP3 |
| No audio file | The track's file is not on the USB | Export the track from Rekordbox again |
| Failed | Preparing stopped, with the reason under it | Fix the reason, then **Retry** |
| Damaged | Verify found a broken stem page | Select it and prepare it again |
| Prepare again | Made by an older version | Open the details and choose **Prepare again** |

A batch holds up to 100 tracks. The estimate turns amber when space gets tight and red when the selection does not fit. The button then reads **Prepare N that fit** and prepares the tracks in order until the next one would not fit.

While a batch runs, each row moves from Queued to Preparing to Ready, and the bar shows the track, the step and about how long is left. The first time on a new computer it says **Timing this computer** until it has measured. **Cancel** stops within a second. Finished tracks are kept. When the batch ends, the bar offers the next step.

```gallery
app:stems-connect No Rekordbox USB found. Connect a USB opens a short guide to find or choose one
app:stems-does-not-fit The selection does not fit. Prepare 7 that fit
app:stems-preparing A batch running, with the step and the time left
app:stems-finished-failed Finished with one failure. Retry 1 failed
```

| After a batch | What it offers |
|---|---|
| A track failed | **Retry N failed** |
| You pressed Cancel | **Prepare N remaining** |
| The USB was unplugged | **Continue**. Nothing is left half-written |
| The USB filled up | **Choose fewer tracks** |

Open a row for its details. A Ready track shows who prepared it and offers **Verify**, which reads every stem page without changing the USB, **Import stems** to replace them, and **Prepare again**. Prepare again keeps the current stems until the new ones pass their check.

### Set up the stem engine

The first time, the button reads **Set up stem engine**. It opens a short guide that ends on a working engine.

1. **Check.** The app looks at this computer's graphics card or processor.
2. **Choose.** Pick a quality. The recommended one is marked for this computer.
3. **Install.** The app downloads what the engine needs once and checks every file.
4. **Test.** It separates 30 seconds of your first selected track and tells you how long a whole track will take. Nothing is written to the USB.

| Quality | Use it for |
|---|---|
| Balanced | The best quality. Recommended with a graphics card or Apple silicon |
| Fast | Good separation in about half the time. Recommended without a graphics card |
| Light | Computers without a graphics card and with little memory. Vocals bleed more |

If setup cannot finish, it names the one reason and offers the fix. For example, when the graphics driver is too old, **Use the processor** sets up the engine on the processor instead.

```gallery
app:stems-engine-needed Set up stem engine replaces Prepare until the engine is set up
app:stems-engine-check Step 1. The graphics card the engine will use
app:stems-engine-quality Step 2. Choose quality
app:stems-engine-download Step 3. What it downloads, and the free space on this computer
app:stems-engine-ready Step 4. The test result, then Prepare 12 tracks
app:stems-engine-driver The driver is too old. Use the processor, or update the driver and check again
```

### Import stems you already have

Choose **Import stems** in the toolbar, or **Import stems made elsewhere** in a track's details. Drop stem files or a ZIP into the window, or choose files. WAV, FLAC, MP3 and AIFF sets work.

1. **Add files.** From the toolbar, also pick which track the stems are for.
2. **Check.** The app places each file on VOCALS, DRUMS or HARMONICS by its name and lines the stems up with the track on your USB. Change any **Goes to** choice you disagree with. **Leave out** skips a file.
3. **Write.** **Write stems to USB** writes the stems and checks them.

```gallery
app:stems-import-place Two files need a place. Play a file to hear what it is
app:stems-import-done Stems written and checked
```

A vocals and instrumental pair needs the stem engine to pull the drums out of the instrumental. If the engine is not set up yet, the import offers **Set up stem engine** and carries on afterward. To use stems on the player, see [Stems](stems).

## DJ Profile

DJ Profile makes and edits the profiles on a USB. A profile is your name, logo and colour. The XZ shows each profile as a logo tile, and the active one is the DJ the XZ and VJ software on the same network see. Profiles are saved on the USB only. Nothing is uploaded. To use them on the player, see [Connect and DJ profiles](connect#dj-profile).

```figure app:dj-profile-new XZ Mods app · DJ Profile
A new profile.
1 @ 340,213 DJ name | Your name as it should read on the XZ.
2 @ 530,508 Logo | A PNG or JPEG up to 8 MB. A transparent PNG looks best. You can add alternate logos.
3 @ 572,649 Colour | The profile colour. The first swatch matches the USB's colour in rekordbox.
4 @ 778,213 On the XZ | The header and the logo tile as the XZ draws them.
5 @ 852,506 What VJ software sees | Your name and links as VJ software reads them.
6 @ 503,751 More | An optional tagline, links and more.
```

```figure app:dj-profile-list XZ Mods app · DJ Profile
The profiles on a USB after a save.
1 @ 1050,183 Create profile track | The next step after a save.
2 @ 540,432 Profile tile | One tile per profile.
3 @ 430,546 Active | The profile the XZ uses.
4 @ 520,546 Edit | Changes this profile.
5 @ 415,613 New DJ profile | Adds another profile to the USB.
6 @ 505,677 Profile track | A track that carries your profile to players that cannot read extra files.
```

Some newer players block reading extra files from the USB. A profile track is a silent song that carries your name, logos and links, so it reaches them through rekordbox like any other track.

```figure app:dj-profile-track XZ Mods app · DJ Profile
Making a profile track.
1 @ 625,166 Steps | Where, Create, then Add it to rekordbox.
2 @ 565,299 This computer | Saves the track in your Music folder, ready for rekordbox.
3 @ 979,289 Choose folder | Saves the track somewhere else.
4 @ 510,360 Copy on the USB | Also puts a copy in your profile folder on the USB.
5 @ 525,434 Create or Cancel | Makes the track, or leaves without one.
```

## Updates

Updates keeps every version in one place, on three tabs: **App updates** for this app, **XZ Mods version** for the build that goes on your USB, and **Network updates** to send that build to XZs on your network. Updates opens on App updates. A yellow dot on Updates and on its App updates tab means a newer app is ready.

### App updates

```figure app:app-updates XZ Mods app · App updates
The app is up to date.
1 @ 612,77 Tabs | App updates, XZ Mods version and Network updates. Updates opens on App updates.
2 @ 432,155 Version | The app version on this computer.
3 @ 430,186 Status | Up to date, or the newer version on offer.
4 @ 435,301 Check for updates | Checks now. Download and install update appears when there is one.
5 @ 385,441 What's new | The release notes.
6 @ 435,533 Other downloads | The installer and the portable ZIP.
```

On Windows the app checks for updates by itself at launch and shows **Review update** at the top of the page when one is ready. The installer updates the app and opens it again. Mac versions download from the release page.

> **Caution.** Updating the app does not change the loader on your USB. Open the USB page afterward and choose **Update USB**.

### XZ Mods version

XZ Mods version chooses which build goes on your USB the next time you install or update it. Saving your choice does not write to any USB.

```figure app:mod-version XZ Mods app · XZ Mods version
The saved build choice.
1 @ 885,157 Verified | Says whether the build was checked on a real XDJ-XZ.
2 @ 430,257 Saved choice | The build your next install or update uses.
3 @ 645,293 Details | Its channel, whether it stays on the USB and where it came from.
4 @ 852,397 Change mod version | Every build on this computer.
5 @ 490,441 Keep this version on the USB | A loader built with this on skips network updates, so the USB stays on this build.
6 @ 527,505 Save choice and Import a build | Saves the choice, or adds a build from a folder.
7 @ 432,592 Set up four decks | Chooses the included four-deck build.
8 @ 582,685 Restore or update a loader by hand | Updates the loader on a USB you choose and keeps a backup of the old one.
```

Lower on the page:

- **Restore or update a loader by hand** updates the loader on a USB you choose and keeps a backup of the old one. Music and saved settings stay in place.
- **Prepare from a downloaded firmware file** builds a loader from an official XDJ-XZ 1.26 firmware file into a folder you choose.

**Import a build** takes a prepared XZ Mods `resources` folder, or a whole XZ Mods app folder that holds one. The app checks every file before it adds the build. Imported builds are not checked on a real XDJ-XZ.

### Network updates {experimental}

Network updates sends the build installed on this computer to XZs on your local network. Each XZ checks the signed update, saves it on its USB and starts it at the next boot.

```figure app:network-updates XZ Mods app · Network updates
The update server, before it starts.
1 @ 1053,221 Choose mod version | A USB built with Keep this version on the USB ignores network updates. Change that here.
2 @ 265,501 Detect adapters | Lists this computer's network connections.
3 @ 1050,440 XZ network adapter | The connection that reaches the XZ.
4 @ 580,494 Start update server | Offers the update on that network until you cancel.
5 @ 262,541 On the XZ | What to do on the player.
```

1. Prepare or update the USB loader once, so it supports network updates.
2. Choose **Detect adapters**, then the adapter connected to the XZ network.
3. Choose **Start update server**.
4. On the XZ, open **CONNECT** › **UPDATES**, then download the update. See [Updates over the network](connect#updates-over-the-network).
5. Switch the XZ off and on when you are ready to start the new version.

Network updates change XZ Mods and the game engine. They keep your music and the original USB loader, so you can always go back. **Cancel** stops the server.

> **Note.** The app's step 3 still says MODS › Extras › Network updates. On the player the page is CONNECT › UPDATES.

## Tools

Tools holds what you need now and then, grouped by what it works on: your USB and your XZ. **Tools** in the top left of each tool goes back to this list.

```figure app:tools XZ Mods app · Tools
Every tool, in two groups.
1 @ 430,138 Repair library | Fixes a Rekordbox USB the XZ cannot read.
2 @ 863,138 Game downloads | Gets the free games again and copies them to a USB.
3 @ 404,276 XZ Screen | Shows the XZ screen on this computer, ready for OBS.
4 @ 125,254 Updates | App updates, XZ Mods version and Network updates are in their own section in the rail.
5 @ 150,655 Feedback | Reports and ideas have their own section at the foot of the rail.
```

### Repair library

Repair library checks the Rekordbox library on a USB the XZ cannot read. It can clear invalid unused pages and reconnect playback history pages that survived. It keeps track records, playlists, music and prepared stems.

```figure app:repair-library XZ Mods app · Repair library
Step 1, choose the USB.
1 @ 420,219 USB | The Rekordbox USB or a folder holding a copy of it.
2 @ 1052,242 Choose folder and Find USBs | Picks a folder, or looks for USBs again.
3 @ 410,336 Connect a USB | Appears when no USB is found. Opens a short guide to connect one.
4 @ 665,370 Read only | Checking only reads the library.
```

1. **Choose the USB.** Then choose **Check library**.
2. **Library check.** The verdict, what the library holds and what is wrong. **Save report** keeps the result on this computer.
3. **Repair plan.** What changes and what stays the same. **Back up and repair** backs up the library files first.

If rekordbox is open on this computer, the check says so and offers **Prepare for repair**. Close rekordbox so both apps do not write at the same time, then check again. After a repair, **Undo this repair** puts the library files back exactly as they were before it.

### Game downloads

Game downloads gets the free games again and copies them to a USB. Use it to retry a download that failed while the app wrote your USB, or to add the games to a USB that already has XZ Mods. Games already on the USB stay in place.

```figure app:game-downloads XZ Mods app · Game downloads
The free games.
1 @ 513,266 Choose USB and set up games | Downloads and checks every free game, keeps its license notice and copies it to the USB you choose.
2 @ 712,391 One game | Download keeps the game on this computer. Download and copy to USB also puts it on a USB.
```

**Doom shareware**, **Chex Quest** and **Freedoom Phase 2** each have their own download. Put your own compatible WADs in a `WADS` folder on either USB, then choose a base game and an optional add-on in the XZ's USB WAD loader. Downloading games does not change the XZ loader.

### XZ Screen {experimental}

XZ Screen shows the XDJ-XZ screen on this computer over the network, so you can add it to an OBS scene. It sends the picture only, no audio, and the XZ keeps all its controls.

```figure app:xz-screen XZ Mods app · XZ Screen
Before the first connection.
1 @ 356,177 Set up Live Streamer | Opens the three-step connection guide.
2 @ 668,206 Stop stream and Fullscreen | Work once the XZ is connected.
3 @ 362,247 Stream color | 8-bit with 256 colors, or 16-bit with the original colors. A change applies when you reconnect.
4 @ 560,350 Status | What the connection is doing, and the frame rate it measures.
5 @ 735,588 Screen | The XZ screen at 800 × 480 once it is connected.
```

```figure app:xz-screen-setup XZ Mods app · Set up Live Streamer
The connection guide.
1 @ 395,241 Look again | Searches the network for the XZ again.
2 @ 355,283 XZ address | Found by itself, or type the address the XZ shows.
3 @ 395,352 Network details | The ports a firewall must allow, UDP 50006 and TCP 50010.
4 @ 430,517 Open USB setup | Older USB loaders have no screen service. Update the loader on the USB page.
5 @ 488,663 Status | Found your XZ, still looking, or what stopped it.
6 @ 450,710 Connect and Close | The first picture from the XZ confirms the connection.
```

1. Connect this computer and the XZ to the same network, for example the same router or a network cable between them.
2. Choose **Set up Live Streamer**. The app looks for the XZ at once.
3. If the guide asks, update the USB loader on the USB page, then switch the XZ off and on with both decks stopped. The guide never restarts the XZ.
4. Choose **Connect**.

To use the picture in OBS, add a **Browser** source, paste the **OBS Browser Source URL**, and set its width to **800** and height to **480**. **Copy OBS URL** copies it. Keep the app running. The URL works on this computer only and changes when you reconnect.

Tick **Show a logo while browsing USB** to cover the stream with the XZ Mods logo while you browse your USB. The play screen comes back on its own. Menus, custom themes and screens the app does not recognise stay covered.

With an older USB loader the picture updates about once a second and the status says it is not performance video. **Update the USB loader** appears beside it.

## Manual

Manual is this manual, inside the app. It comes with the app, so it needs no internet connection. Where the app explains a feature, **Read in the manual** opens the chapter about it.

```figure app:manual XZ Mods app · Manual
Four decks open in the Manual, at the video of how the sound of decks 3 and 4 reaches the mixer.
1 @ 125,356 Manual | Opens the manual. It sits between Tools and About in the rail.
2 @ 492,21 Search | Searches every chapter as you type. Press / to get here from anywhere on the page, and Enter to open the first result.
3 @ 450,73 Progress | How much of the manual you have read. A chapter's circle fills as you read and turns into a tick at its end.
4 @ 380,145 Start page | What XZ Mods adds, the interface you play on, and every chapter in order.
5 @ 430,452 Chapter list | Every chapter. The one you are reading is outlined and lists its sections.
6 @ 1072,30 Chapter | The chapter you are reading. Previous and Next at its end move through the manual in order.
7 @ 1010,420 Video | A clip rendered from a true-scale model of the XDJ-XZ shows how a feature works. It plays when it comes into view, and Play pauses it. A video in several steps also has arrows and numbered steps, and its caption follows the step.
```

- **Numbers on pictures.** Point at a number, or tab to it and use the arrow keys, to light up that part of the picture and read what it is. **Play** steps through every number. The list under the picture says the same.
- **Your interface.** Choose MODERN, CLASSIC or Both on the start page or at the foot of the chapter list. The manual then hides what does not apply to you. A MODS page drawn in both interfaces has a MODERN and CLASSIC switch on its picture.
- **Where you stopped.** The manual remembers the chapter and section you read last. The start page offers **Continue where you left off**.
- **Less motion.** When your computer is set to reduce motion, nothing in the manual moves by itself. A video shows a still picture of each step and plays only when you press Play.

## About

About says what XZ Mods is and who makes it, and gathers the links, the disclaimers and the open-source licenses in one place.

```figure app:about XZ Mods app · About
About.
1 @ 640,132 Version | The app version on this computer.
2 @ 775,156 Maker | XZ Mods is made by OpticMystic.
3 @ 620,280 App updates and release notes | Opens App updates in Updates.
4 @ 365,364 The maker | OpticMystic's projects: XZ Mods, VJ.Tools and DJ Profile. Make yours opens DJ Profile.
5 @ 742,364 Links | The source code, issues and releases on GitHub, xzmods.com and vj.tools. Each opens in your browser.
6 @ 805,663 Feedback | Opens [Feedback](#feedback) in the app.
7 @ 920,716 Support XZ Mods (optional) | Voluntary support on Cash App. Every feature stays available.
8 @ 378,810 Disclaimers | XZ Mods is independent of Pioneer DJ and AlphaTheta, experimental, runs from the USB, and builds its loader from the official firmware for your own use.
9 @ 960,1009 Open license notices | Every open-source project XZ Mods uses and its license.
```

## Feedback

Feedback is where you tell the maintainer about a problem, ask for a feature or ask a question, and follow what you sent. It sits at the foot of the rail on every page.

```figure app:feedback XZ Mods app · Feedback
The Feedback section.
1 @ 150,655 Feedback | Opens this section from any page. A count on it shows reports from your XZ that wait for your words.
2 @ 462,212 Report a problem | Opens the report window at its first question.
3 @ 721,212 Request a feature | Opens the report window at the kind of idea.
4 @ 963,212 Ask a question | Goes straight to the box. Answers from this manual appear as you type.
5 @ 350,322 My reports | What you sent from this computer and where each report is now.
6 @ 466,680 Maintainer inbox | For the maintainer only.
```

Reports from your XZ that wait for your words appear above My reports, under **From your XZ**. The sticks that may send them are listed under **Your XZ sticks**.

### Report a problem or share an idea

The report window sends a private report to the maintainer: a problem, something that got worse, an idea or a question. **Report a problem** in Feedback opens it at the first question. **Request a feature** and **Ask a question** answer that question for you. The only thing you have to fill in is one box. Nothing is posted publicly.

```figure app:report-topic XZ Mods app · Report a problem or share an idea
The first question.
1 @ 612,128 Steps | Where you are. The box with Send is always the last step.
2 @ 415,243 Topic | Pick the closest one. The next question depends on it.
3 @ 353,532 How do I...? | For a question. Answers from this manual appear as you type.
4 @ 735,630 Not sure | Skips the question. Every question has it.
5 @ 296,630 Cancel | Closes the window. What you typed comes back when you open it again.
```

1. Pick what it is about, or **Not sure**.
2. Answer at most two more short questions, or press **Not sure**.
3. Write what happened in your own words. One sentence is enough.
4. Press **Send privately**.

| You pick | It then asks |
|---|---|
| Something got worse | When it got worse, then for the USB sticks that were in the XZ |
| Something isn't working | What kind of problem: sound, SYNC or tempo, the screen, buttons and pads, loading and USB, four decks, a crash, or something else |
| It should work differently | Compared with what: the XZ before XZ Mods, another XZ feature, other DJ gear or software, an earlier XZ Mods version, or just your preference. For gear or another feature, which one |
| I have a new idea | What kind of idea |
| How do I...? | Nothing more. Go straight to your question |

```figure app:report-describe XZ Mods app · Report a problem or share an idea
The box, with an answer from this manual.
1 @ 664,147 Your answers | What you picked. Change goes back to the first question.
2 @ 820,190 The box | The only thing you have to fill in. Up to 4,096 bytes; a counter appears near the limit.
3 @ 524,378 Is this it? | Up to three answers from this manual that match what you typed.
4 @ 489,492 Answer buttons | Set up four decks opens that setup. That answers it closes without sending anything.
5 @ 388,554 Included details | What the app collected to send with your words. Closed until you open it.
6 @ 771,646 Send privately | Sends your words and the details shown, to the maintainer only.
```

As you type, **Is this it?** shows answers from this manual. The matching runs on your computer; your words are not sent anywhere for it. If an answer solves it, its button opens the right setup, such as **Set up four decks**, or **That answers it** closes the window. Nothing is sent.

### Included details

While you answer and type, the app reads the details that help the maintainer, for at most 10 seconds: the session log and settings on your XZ Mods USB, the diary on each stick, the app's events and the build. Everything is cleaned first. No track names, file paths, serial numbers or network addresses are kept.

```figure app:report-details XZ Mods app · Included details
Included details, opened, with one source shown and one left out.
1 @ 388,52 Included details | How many sources are included, their size and the time around the problem they cover.
2 @ 401,157 A source | What it is and where it comes from. Untick it to leave it out.
3 @ 762,168 Show | The exact content that would be sent for this source.
4 @ 434,216 Left out | An unticked source is not sent.
5 @ 700,290 Exact content | What is sent for this source, after cleaning.
```

**Send privately** sends exactly your words and the ticked details shown at that moment. If you send before the app has finished reading, it sends what it has read and notes what is missing. **Collect again** reads everything again. Ideas include no details at all, only your words. Details are deleted after 14 days.

### When something got worse

For something that got worse, the app asks you to plug in every USB stick that was in the XZ when it happened. XZ Mods versions that keep a diary write it to each stick while the XZ plays. The app reads only that diary, never your music.

```figure app:report-usb XZ Mods app · Report a problem or share an idea
Two sticks plugged in: an XZ Mods USB with a diary and a music stick without one.
1 @ 520,354 XZ Mods USB | A stick with the XZ Mods loader on it.
2 @ 436,379 Diary | The time range the diary on this stick covers.
3 @ 425,436 Music stick | A stick with music only.
4 @ 443,461 No diary on this stick | This stick has no diary, for example from a version that does not keep one.
5 @ 762,505 That's all of them | Goes on to the box.
```

Each stick appears by itself as you plug it in. Press **That's all of them** when every stick is in. A missing stick or a missing diary never stops you sending.

### After you send

```figure app:report-received XZ Mods app · Report received
The receipt after sending.
1 @ 556,285 Your reference | The permanent reference for this report, also shown in My reports.
2 @ 519,309 Status | Where the report is now.
3 @ 575,341 Copy | Copies the reference so you can quote it to the maintainer.
4 @ 688,477 Track my report | Opens it in My reports.
```

Every accepted report or idea gets its own reference, such as **R-4F7K-2M9Q**. Quote it when asking about that report. **Copy** copies the reference. The reference stays the same if you send again after a connection failure. It does not let anyone read your report or its status.

If the app cannot confirm that a report arrived, it says **Not confirmed yet**. Nothing is lost. **Set up reporting connection** checks the internet connection and the computer's clock, then **Send again** sends the same report. If the first attempt did arrive, you get its receipt and nothing arrives twice. **Save to a file** keeps a copy without sending.

```figure app:report-connection XZ Mods app · Set up reporting connection
The setup after a send that was not confirmed.
1 @ 574,306 Connection | Whether this computer is online.
2 @ 452,363 Clock | Secure connections fail when the date is wrong.
3 @ 781,551 Send again | Sends the same report again.
```

### My reports

My reports lists every report and idea sent from this computer, with its kind and where it is now, in plain words. It is the list under the buttons in Feedback.

```figure app:my-reports XZ Mods app · My reports
Four reports, one with news.
1 @ 338,258 Kind | Such as Problem, Got worse, Works differently, Idea or Question.
2 @ 668,245 Your words | The start of what you wrote. It stays on this computer.
3 @ 586,515 Status | Where the report is now. New marks a change you have not opened yet.
4 @ 893,256 Reference and date | Your report reference and the day you sent it. Part of XZ-1042 appears when the maintainer assigns an item.
5 @ 930,155 Check now | Checks every report at once, and asks for new reports from your XZ.
```

When a report's status changes, a notice appears at the top of whichever page is open.

```figure app:report-notice XZ Mods app · App updates
A status change, shown while App updates is open.
1 @ 248,154 Notice | Appears when the status of one of your reports changes.
2 @ 613,154 View | Opens that report.
```

The app checks for news when it starts, after it checks for app updates, and when you open My reports, at most every 15 minutes for each report. **Check now** checks at once. If you are offline, the last known status stays.

Open a report to see where it is. The view is read-only. Once the maintainer assigns it, **Part of XZ-1042** names the bug, feature or question it belongs to. Reports about the same item share that item number. If the maintainer moves your report to another item, this number changes; your R reference stays the same. Item numbers remain after the report's details are deleted.

```figure app:report-tracking XZ Mods app · My reports
A problem the maintainer confirmed, with a fix in progress.
1 @ 550,177 Progress | Received, Confirmed, Fix in progress, Ready to test, In an update. Ideas and questions show Received, Looked at, Decided.
2 @ 467,238 Reference | This report's permanent reference and kind.
3 @ 510,309 Your words | The start of what you wrote.
4 @ 428,355 Status | Where it is now, in plain words.
5 @ 600,425 Dates | When you sent it, the last change, when its details are deleted and how long tracking lasts.
6 @ 443,497 Status link and forgetting | The status link, and Forget this report.
7 @ 711,524 Check now | Checks this report at once.
8 @ 370,267 Part of | The item this report belongs to, such as XZ-1042.
```

- If the maintainer needs one more detail, the question appears in the report. **Answer in a new report** starts a report that names this one.
- **Open status page** and **Copy status link** give a read-only web page with the same status, report reference and item number. It shows none of your words or details. Anyone with the link can read the status. **Turn off status links** stops every link for this report; the app still checks it.
- **Forget this report** removes its receipt from this computer. The maintainer keeps the report.
- A report saved, but not sent, by an older version of the app appears under **Saved, not sent**. **Use this text** starts a new report with its words.

Details are deleted 14 days after you send them. Tracking lasts while a report is open, up to one year without news, then 180 days after it closes. A fix in a local test build is not a published update.

### From your XZ

A report sent from the XZ waits in Feedback for your words. **From your XZ** appears above My reports while one waits. The XZ notes the screen you were on as names only. It keeps no picture and no text from the screen.

```figure app:xz-report XZ Mods app · My reports
A report from the XZ, waiting for your words.
1 @ 556,48 From your XZ | Reports from your XZ that wait for your words.
2 @ 676,118 Reference and time | The report's reference and when the XZ sent it. It stays the same when you add your words.
3 @ 662,168 Screen | The screen the XZ showed just before you reported, with any error code on it.
4 @ 606,188 Stick | The stick the report came from, and when XZ Mods wrote it.
5 @ 597,208 Typed on the XZ | What you typed on the XZ, if anything.
6 @ 960,335 What happened? | Starts with what you typed on the XZ. Change it or add to it.
7 @ 436,416 Collected details | What the XZ collected and sent with the report. Show displays each part exactly.
8 @ 392,469 Stop reminding | Ends the reminders for this report. It stays in the list.
9 @ 830,503 Send without comments | Finishes the report without words from you.
10 @ 972,503 Send | Adds your words to the same report.
```

- **Send** adds your words to the same report, under the same reference. The report then moves to your sent reports, where you follow it like any other.
- **Send without comments** finishes the report as it is.
- If the app can't reach the report service, it keeps your words and sends them by itself later. You can close the app.
- The XZ already sent the collected details. They are deleted 14 days after it sent them. After that the report says so, and your words still reach the maintainer.
- The maintainer sees the report as soon as the XZ sends it, marked as waiting for your comments, and can start from the logs.

The app tells you when a report from the XZ waits:

- While the app is open, a new report brings a Windows notification, such as "Your XZ sent a report, R-4F7K-2M9Q. Add what happened?" Click it to open that report.
- **Feedback** in the rail shows how many reports wait.
- When the app starts with reports waiting, a callout under the page title names them.

```figure app:xz-callout XZ Mods app · From your XZ
The callout when the app starts with a report waiting.
1 @ 472,150 Callout | Names the report your XZ sent. It never covers the window or takes the keyboard.
2 @ 762,150 Add what happened | Opens the report in Feedback with its box ready.
3 @ 889,139 Remind me later | Hides the callout until the app starts again.
4 @ 990,139 Stop reminding | Ends the reminders for these reports. They stay in Feedback.
5 @ 240,636 Badge | How many reports from your XZ wait for your words.
```

Reminders end when you choose **Stop reminding**, or 7 days after a report arrives. Each report brings one notification at most. If Windows notifications are off for XZ Mods, or your copy of the app is not installed, the badge and the callout tell you instead.

### Your XZ sticks

Every stick XZ Mods writes gets a loader ID, such as **L-7K2M-9Q4F**. It lets the XZ send reports from that stick to this computer, and to nothing else. **Your XZ sticks**, near the bottom of Feedback, lists them.

```figure app:xz-sticks XZ Mods app · My reports
Three sticks: one waiting to register, one ready and one revoked.
1 @ 373,418 Your XZ sticks | The sticks XZ Mods wrote on this computer.
2 @ 422,486 Loader ID | Reports from this stick carry it.
3 @ 433,509 Written | When XZ Mods last wrote the stick.
4 @ 893,517 State | Can send reports, Registers when online, Plug it in to finish, Revokes when online or Revoked.
5 @ 997,595 Revoke | Stops this stick sending reports. The app asks first.
```

- Revoke a stick you lost or gave away. The XZ can no longer send reports from it. If you are offline, the stick is revoked on this computer at once and the report service hears of it later.
- Writing XZ Mods to that stick again gives it a new key, and it can send reports again.
- **Plug it in to finish** means a write to that stick was not confirmed. Plug the stick in with the app open. If the state stays, write XZ Mods to the stick again.
- With no sticks yet, **Set up a USB** opens the USB setup.

### Maintainer inbox

**Maintainer inbox**, at the bottom of Feedback, is for the maintainer. Connect it with the report-only owner key. The short setup checks that key by reading the inbox.

Enter a report reference or item number in the search box and choose **Find**. You can type either case, with or without spaces and dashes. For a report reference, I or L also means 1, and O means 0. A report reference finds one report; an item number finds the reports assigned to that item. **Clear** returns to the inbox.

```figure app:maintainer-inbox XZ Mods app · Maintainer inbox
The inbox, scrolled right to show Reference and Item.
1 @ 997,141 Find | Searches the whole inbox by report reference or item number.
2 @ 774,236 Reference | The permanent reference for each report.
3 @ 991,236 Item | The bug, feature or question the report belongs to. A dash means it has not been assigned yet.
4 @ 670,594 Scroll | Scroll right or left to see every column.
```

The reference and item number identify a report; the owner key is still required to read the inbox.
