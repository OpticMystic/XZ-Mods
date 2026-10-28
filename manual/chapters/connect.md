---
interface: both
settings: fb_takeover
---

CONNECT is the MODS section for people and computers: your DJ profile, the VJ.Tools link, the player's network and updates over the network. Everything here is optional. The player plays the same with all of it switched off.

Open MODS with [[MENU]], then choose **CONNECT** in the section rail.

## The CONNECT page

```figure classic:theme-{look}-connect MODS · CONNECT
The four parts of CONNECT. The highlighted card is explained in the dock.
1 @ 230,88 DJ PROFILE | Your DJ name and logo from the USB, for VJ screens.
2 @ 560,88 VJ.TOOLS | The optional link to VJ.Tools on a computer, and the VJ.Tools screen view.
3 @ 230,266 NETWORK | The player's network adapters and addresses.
4 @ 560,266 UPDATES | Signed XZ Mods updates over the network. An update starts at the next power cycle.
5 @ 125,425 Dock | Explains the highlighted card before you open it.
```

The HOME card for CONNECT shows your active DJ profile and its USB, then the state of the VJ.Tools link, for example **DJ Kai (USB 1) | VJ.Tools waiting**. With no profile it reads **No DJ profile**.

## DJ profile

A DJ profile is your DJ name and logo, with an optional tagline. It lives on your USB, next to your music. VJ software such as VJ.Tools shows the profile of the USB that is playing on the master deck, so the screens behind you show who is playing. Nothing is stored on the XZ and nothing is uploaded.

DJ Profile is an open standard. Any player, mixer or VJ program can read the same folder.

### Make a profile on a computer

1. On the USB, make a folder named `DJPROFILE` at the top level.
2. Inside it, make a folder with your DJ name, for example `DJPROFILE/DJ Kai`.
3. Put your logo in that folder as `logo.png` or `logo.jpg`.
4. Put the USB in the XZ, open **CONNECT** › **DJ PROFILE** and push **CHECK USB FOR PROFILES**.

A folder with a usable logo is a profile. A folder without one is skipped. A USB shows at most 16 profiles. The XZ Mods app can also make and edit profiles. See [The XZ Mods app](desktop-app).

### Make a profile on the XZ

You can type a profile on the player with the encoder or by touch.

1. Open **CONNECT** › **DJ PROFILE**.
2. Push **CREATE ON THIS XZ**, or **NEW PROFILE** if the USB already has one.
3. Type your DJ name on the on-screen keyboard, then push **NEXT: LOGO**.
4. Pick a PNG or JPEG image from the USB, or **NO LOGO**. Images in your own `DJPROFILE` folder are listed first. Then push **NEXT: TAGLINE**.
5. Type a few words, such as your genre, city or crew, and push **NEXT: REVIEW**. To leave the tagline out, push **SKIP TAGLINE**.
6. Check the review, then push **SAVE TO USB 1** or **SAVE TO USB 2**.

A new profile becomes the active profile on the USB you save it to. Only the `DJPROFILE` folder on that USB is written. Your music, playlists and stems are not touched.

| Step | Controls |
|---|---|
| Name and tagline | The keyboard, **SHIFT** for capitals, **#+=** for symbols (**ABC** goes back), **SPACE**, **DELETE**, **BACK** to the step before |
| Logo | One row per image on the USB, **PREV** and **NEXT** for more, **RESCAN** (or **CHECK USB AGAIN**), **NO LOGO**, **KEEP CURRENT** when you edit |
| Review | **CHANGE NAME**, **CHANGE LOGO**, **CHANGE TAGLINE**, then **SAVE TO USB 1** or **SAVE TO USB 2** |

- A name holds up to 64 characters and a tagline up to 80.
- With **NO LOGO**, your initial is shown on the accent colour.
- Logos are shown square over black.
- Nothing is saved until you push SAVE. The bottom line says **NOTHING IS SAVED UNTIL YOU PRESS SAVE** on the review step.

### Choose and switch profiles

With profiles on the USB, the DJ PROFILE page shows one profile at a time: its logo, name and tagline, which USB it is on, and whether it is active there.

| Control or line | What it means |
|---|---|
| **< PREV**, **NEXT >** | Step through the profiles on both USBs. **2 OF 3** shows where you are |
| **EDIT** | Change this profile's name, logo or tagline |
| **MAKE ACTIVE** | Makes this the active profile on its USB. Only the USB is written |
| **NEW PROFILE** | Makes another profile on the USB, for an alias or a back to back set |
| **CHECK USB** | Reads both USB ports again |
| ACTIVE ON THIS USB | This profile is the one its USB says is playing |
| SHOWN TO THE VJ NOW | This is the profile VJ software sees right now |
| FROM ITS FOLDER IN DJPROFILE | A profile made from a folder and a logo |
| OLDER CARD: EDIT IT IN VJ.TOOLS | An older profile card. Edit it in VJ.Tools, or make a new profile |

Your active profile also sits in the MODS header, with its logo and name. Tap it, or highlight it and push, to switch between the profiles on both USBs.

> **Note.** The player's own saved settings are MY SETTINGS in UTILITY. They are separate from your DJ profile.

### When a profile will not save or show

| Message | What to do |
|---|---|
| INSERT THE USB WITH YOUR MUSIC | Put your USB in. Profiles live on the USB only |
| NO DJ PROFILE ON THIS USB YET | Make one on a computer or on the XZ, as above |
| INSERT A USB FIRST | Insert a USB before you push CREATE ON THIS XZ |
| TYPE YOUR DJ NAME FIRST | The name cannot be empty |
| THE NAME IS FULL, THE TAGLINE IS FULL | You reached 64 or 80 characters |
| THAT USB IS NOT IN / PUT IT BACK | Put the USB back in the port you are saving to |
| THIS LOGO CANNOT BE USED / CHANGE LOGO | Pick another image, or NO LOGO |
| THIS PROFILE FILE IS DAMAGED / SAVE IT AGAIN FROM VJ.TOOLS | Save the profile again from VJ.Tools or the XZ Mods app |
| SAVING YOUR PROFILE TO THE USB / KEEP THE USB IN | Wait. Do not pull the USB |

## VJ.Tools {experimental}

VJ.Tools is a desktop app for VJs. The VJ.TOOLS page lets VJ.Tools on a computer connect to the player, announces the player on the network, and can show the computer's picture instead of the play screen.

```figure classic:theme-{look}-active-connection MODS · VJ.TOOLS
VJ.Tools connected and sending frames. The player still shows its own play screen.
1 @ 360,107 STATUS | CONNECTED, WAITING FOR VJ.TOOLS, DISABLED or NOT AVAILABLE.
2 @ 345,142 DEVICE IP | The player's address for VJ.Tools.
3 @ 310,176 PORT | The port VJ.Tools connects to.
4 @ 214,210 PROTOCOL | What the link carries. You only need it when you report a problem.
5 @ 320,243 RECEIVED FPS | Frames per second arriving from VJ.Tools. NOT REPORTED until frames arrive.
6 @ 290,280 Screen in use | NATIVE PLAY SCREEN ACTIVE, or COMPUTER SCREEN ACTIVE while the VJ.Tools view is on.
7 @ 660,70 VJ.TOOLS VIEW | Shows the computer's VJ.Tools screen instead of the player screen.
8 @ 600,103 SCREEN SWITCH KEY | What switches the VJ.Tools view while you play: LINK or an ONSCREEN button.
9 @ 700,206 VJ.TOOLS CONNECTION | Lets VJ.Tools on a computer connect to this player.
10 @ 660,258 DISCOVERABLE | Announces this player so VJ.Tools finds it on the network.
11 @ 545,320 vj.tools | Where to get VJ.Tools.
```

| Control | Choices | What it does |
|---|---|---|
```gallery {classic}
classic:theme-0-connection Waiting for VJ.Tools. The link controls are read only on this player
```

```gallery {modern}
classic:theme-24-connection Waiting for VJ.Tools. The link controls are read only on this player
```

| VJ.TOOLS VIEW | ON, OFF | ON shows the computer's VJ.Tools screen on the player. OFF shows the play screen |
| SCREEN SWITCH KEY | LINK, ONSCREEN | LINK makes the **LINK** key switch the view. ONSCREEN puts a button for it on the screen. See [Controls on the player](hardware) |
| VJ.TOOLS CONNECTION | ON, OFF | Lets VJ.Tools connect |
| DISCOVERABLE | ON, OFF | Lets VJ.Tools find the player without typing its address |

When a link control shows **READ ONLY**, MODS cannot change it on this player. Pushing it shows **CONTROL IS NOT AVAILABLE**.

To switch the view without opening MODS, use the key you chose under SCREEN SWITCH KEY. You can also give the [[rekordbox]] button above [[LINK]] the job **VJ.TOOLS SCREEN** in **PLAY** › **REKORDBOX BUTTON**. It needs an enabled, connected VJ.Tools link, and says so when there is none. In CLASSIC, a **VJ.TOOLS** button can appear on the top bar. It reads **EXIT VJ** while the view is on.

> **Experimental.** The VJ.Tools view has shown video on the player during development. Long playback and touch while the view is on have not been accepted yet. Keep it off during a set unless you have tested it.

## Network {experimental}

NETWORK sets up the player's network adapters for VJ.Tools and network updates. You do not need it to play.

```figure classic:theme-{look}-network MODS · NETWORK
The NETWORK page before the adapters have been read.
1 @ 440,86 Status line | What the page knows now. CHANGES LAST UNTIL REBOOT reminds you that nothing here is permanent.
2 @ 450,285 What is known | Which parts the player reports, and that nothing changes until you apply.
3 @ 300,337 SET UP NETWORK | Starts the setup: read the adapters, choose, review, apply.
4 @ 650,337 DETECT ADAPTERS AGAIN | Reads the adapters again, for example after you plug in a cable.
5 @ 300,385 NETWORK UPDATES | Opens UPDATES.
```

```gallery {classic}
classic:theme-0-network-setup NETWORK SETUP before the adapters have been read
```

```gallery {modern}
classic:theme-24-network-setup NETWORK SETUP before the adapters have been read
```

Setup runs in four steps. Every change lasts until the player is switched off.

1. **Detect.** The page lists each adapter with its link state and current address.
2. **Choose.** Pick an adapter and how it gets an address: **KEEP CURRENT**, **DHCP ON** or **STATIC IP**. With STATIC IP, use **EDIT IP**, **EDIT MASK / PREFIX** and **EDIT GATEWAY**. **PRIORITY UP** and **PRIORITY DOWN** set which adapter XZ Mods prefers.
3. **Review.** Push **REVIEW CHANGES** and read the summary.
4. **Apply.** Push **APPLY FOR THIS BOOT**. The page then checks the result. **REVERT TRIAL / MODS DHCP OFF** undoes what this setup added.

```figure classic:theme-{look}-network-edit EDIT IPV4
The keypad for an address, mask or gateway.
1 @ 175,116 Field | Which value you are typing and what you have typed.
2 @ 250,170 Digits and point | Type the address one character at a time.
3 @ 740,274 BACKSPACE | Deletes the last character.
4 @ 230,326 CLEAR | Empties the field.
5 @ 400,326 DONE | Keeps the value and goes back to the setup.
6 @ 570,326 CANCEL | Leaves the value as it was.
```

| Message | What to do |
|---|---|
| DETECT ACTUAL ADAPTERS FIRST | Push DETECT ADAPTERS AGAIN, then choose |
| ENTER A VALID IP AND CONTIGUOUS MASK / PREFIX | Check the address and mask you typed |
| ENTER A VALID GATEWAY OR KEEP ROUTE OFF | Type a valid gateway, or set **MODS ROUTE: OFF** |
| NETWORK CHANGED / DETECT AND CHOOSE AGAIN | A cable or adapter changed. Start the setup again |
| RELOAD A PRIVATE USB IMAGE WITH NETWORK SUPPORT | This USB loader has no network support. Update the loader with the XZ Mods app, then push **VERIFY RELOADED LOADER** |

> **Experimental.** The NETWORK pages still use technical wording and may change. Your changes never outlive a power cycle, so switching the player off and on always brings back its own network setup.

## Updates over the network {experimental}

UPDATES fetches a newer signed XZ Mods runtime from the XZ Mods app on your computer and saves it to the USB. The update starts at the next power cycle. Your music stays where it is.

```figure classic:theme-{look}-network-updates MODS · UPDATES
UPDATES before the update server has been found.
1 @ 375,93 What it does | Downloads a signed runtime to the USB. Power cycle to activate.
2 @ 425,168 Recovery | Updates change XZ Mods only. The original USB loader is the way back.
3 @ 700,216 SET UP NETWORK UPDATES | Opens UPDATE SETUP, which finds the update server.
4 @ 360,272 DOWNLOAD UPDATE | Downloads the update to the USB. It activates after a power cycle.
5 @ 690,272 PREVIOUS VERSION | Goes back to the previous XZ Mods version at the next power cycle.
6 @ 360,356 REPORT AN ISSUE | Opens REPORT AN ISSUE in EXTRAS.
7 @ 690,356 NETWORK SETUP | Opens NETWORK.
```

```figure classic:theme-{look}-update-setup UPDATE SETUP
The three things the player needs before it can update over the network.
1 @ 420,125 Steps | Use the updated USB loader, start network updates in the desktop app, connect the XZ to that network.
2 @ 700,264 DETECT UPDATE SERVER | Asks the update server for a newer signed runtime.
3 @ 440,356 BACK TO UPDATES | Returns to UPDATES.
```

1. Prepare the USB with a current XZ Mods app, so the loader supports network updates.
2. In the XZ Mods app, start network updates. See [The XZ Mods app](desktop-app).
3. Connect the XZ to the same network as the computer. Use NETWORK if it needs an address.
4. Open **CONNECT** › **UPDATES** › **SET UP NETWORK UPDATES** and push **DETECT UPDATE SERVER**.
5. Back on UPDATES, push **DOWNLOAD UPDATE**.
6. Switch the player off and on to start the new version.

> **Caution.** Never switch the player off while the update is being written to the USB, and never update during a set.

To undo an update, push **PREVIOUS VERSION** and switch the player off and on. Network updates change XZ Mods only. If the player will not start XZ Mods at all, restore or update the USB loader with the XZ Mods app.
