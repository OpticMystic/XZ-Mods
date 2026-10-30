# XZ Mods

XZ Mods is a desktop app for preparing XDJ-XZ USB loaders, saved settings,
and stems. It targets firmware 1.26.

The current release is version 0.2.1, with a Windows installer and utilities for
Apple Silicon and Intel Macs running macOS 14 or later.

[Windows installer](https://github.com/OpticMystic/XZ-Mods/releases/download/v0.2.1/XZ.Mods_0.2.1_x64-setup.exe) | [Portable ZIP](https://github.com/OpticMystic/XZ-Mods/releases/download/v0.2.1/XZ-Mods-Builder-preview-win64.zip) | [Mac downloads](https://github.com/OpticMystic/XZ-Mods/releases/tag/v0.2.1) | [Matching source](https://github.com/OpticMystic/XZ-Mods/releases/download/v0.2.1/XZ-Mods-Builder-preview-source.zip)

- Prepare a new USB, update an existing loader with a backup, restore a loader,
  and edit saved controller settings.
- Choose 24 controller themes, eight-pad stem shortcuts and two Beat Jump pages.
- Browse the tracks your DJ software exported to the USB, generate stems with Open-Unmix HQ or Vocal
  focus, import grouped instrument stems, or prepare selected tracks as a batch.
- Read new OverCue FLAC pages and existing zlib pages. Generate and Import write
  FLAC and verify all seven playback mixes while preserving the original track.
- Display layered stem waveforms and prepare backed-up 3-band waveform colours.
- Open Beat Arcade and the real-waveform Wave Rider game from MODS > Extras >
  Games. The XZ still starts on the DJ screen.
- Download and prepare Doom shareware, Chex Quest and Freedoom data from their own
  distributions through Games. Commercial IWADs must come from the user. My House
  stays unavailable: it needs a game engine that does not run on the XZ.
- Install signed app updates through App updates.

Apple Silicon supports generation with the current models. Intel Macs support
stem imports and the USB, settings, waveform and game tools. The Mac packages
are ad-hoc signed and are not notarized. See [the Mac instructions](docs/USER-GUIDE.md#install-the-mac-utility).

XZ Mods is experimental. Test it with your own tracks before a set, and keep a
known working USB. Long two-deck sessions and phase alignment on real songs are
still being tested. See [the changelog](CHANGELOG.md).

## Repositories

- This app: https://github.com/OpticMystic/XZ-Mods
- Toolkit (the loader builder and the XZ Mods code that runs on the player):
  https://github.com/OpticMystic/xdj-xz-toolkit

Clone both side by side so the app finds the toolkit automatically:

```powershell
git clone https://github.com/OpticMystic/XZ-Mods
git clone https://github.com/OpticMystic/xdj-xz-toolkit
```

or point the resource builder at any toolkit checkout:

```powershell
$env:XZ_TOOLKIT_DIR = "C:\path\to\xdj-xz-toolkit"
```

## Earlier release: 0.1.5

The [Windows release](https://github.com/OpticMystic/XZ-Mods/releases/tag/v0.1.5)
is available for download. Extract the ZIP and run `XZ Mods.exe` with its `resources` folder
beside it. Windows requires WebView2. VJ.Tools and a developer Python
installation are not required.

Plug in a FAT/FAT32 USB and choose **Prepare USB → Choose USB and prepare loader**.
The app gets and checks the files it needs, then writes and checks `autoexec.bin`
in the USB root. It never formats the drive or replaces an existing loader.
Checked files are kept for later builds. A local-file option and a staging folder
remain available under Advanced options.

This update includes two taller stem rows, horizontal volume gestures,
independent deck pads, A-D/E-H banks, corrected pad brightness and colors,
twelve display styles and XZ Mods loading artwork. It keeps OverCue stem
playback. See [the changelog](CHANGELOG.md) for what was tested.

## Use prepared OverCue tracks

Keep the matching exported `Contents` and OverCue `CDJMODS` folders on the same
USB. In **Prepare stems**, choose **Check OverCue track**, then select the original
track inside `Contents`. The builder checks source identity and every compressed
audio page using the same decoder as the player. It does not change the USB.

Supported prepared format: the open OverCue format (`overcue-index/1` and
`overcue-stems/4`), including all seven prepared mixes. The source track must match
the one the stems were made from. Other formats are refused.

[OverCue](https://overcue.gg/) is the recommended way to prepare stems.
XZ Mods also includes a beta stem builder. Its separation and aligned-stem import
workflows write the [open OverCue format](https://github.com/OverCue-gg/overcue-stems-format)
under `CDJMODS` and never replace stems OverCue made. The source must be a
44.1 kHz stereo WAV or FLAC track that your DJ software already exported to the USB's
`Contents` folder. Separation runs on the CPU. The beta is not hardware-qualified,
so check playback alignment on the player.

## Stem controls

XZ Mods shows two taller rows, one below each deck's waveform, with Vocals,
Harmonics, Drums and Bypass. Tap to mute and drag horizontally to adjust volume.
Split rows, clean audio and bright pads were checked on the player. After
release, the owner prepared a USB with 0.1.5 and started the XDJ-XZ with MODS
running. Long two-deck sessions, pads, loops and focus are still being tested. To update
an older USB, back up and remove its existing `autoexec.bin` before preparing it
with 0.1.5; updating source alone does not update a USB stick.

Read the [user guide with actual hardware screenshots](docs/USER-GUIDE.md) for
USB preparation, pad assignments, the controller display themes,
recovery and [related community projects](docs/USER-GUIDE.md#related-projects).
The public website is [vj.tools/xz-mods](https://vj.tools/xz-mods).

In MODS > Controls, choose whether stems use hot cues A-D or E-H. The selected
bank maps its first three pads to Vocals, Harmonics and Drums, followed by
bypass. The other bank keeps its normal hot cues.

The top-left VJ.Tools button appears only while the MODS VJ.TOOLS CONNECTION setting
is enabled and the VJ.Tools network connection is live. Exit VJ returns to
the play screen. The VJ.Tools settings remain in MODS when offline, and view
choice is retained when the settings USB is writable.

## Release status

The app, the backend and the portable package are complete. The owner confirmed
USB preparation and starting the XDJ-XZ with 0.1.5. Selectable pad banks, long
two-deck sessions, loops, focus and the stem model matrix still need testing. A
file check does not prove playback alignment.

Nothing from the player's maker is part of the XZ Mods download. The app gets and
checks the files it needs while it prepares the loader, or uses a firmware 1.26
file you choose under Advanced options (see `BUILDING.md`). Other firmware
versions are refused, and the app does not claim to support them.
