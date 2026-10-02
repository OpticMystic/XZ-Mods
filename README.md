# XZ Mods

XZ Mods is a desktop app for preparing XDJ-XZ USB loaders, saved settings,
and stems. It targets firmware 1.26.

The current release is version 0.2.0, with a Windows installer and utilities for
Apple Silicon and Intel Macs running macOS 14 or later.

[Windows installer](https://github.com/OpticMystic/XZ-Mods/releases/download/v0.2.0/XZ.Mods_0.2.0_x64-setup.exe) | [Portable ZIP](https://github.com/OpticMystic/XZ-Mods/releases/download/v0.2.0/XZ-Mods-Builder-preview-win64.zip) | [Mac downloads](https://github.com/OpticMystic/XZ-Mods/releases/tag/v0.2.0) | [Matching source](https://github.com/OpticMystic/XZ-Mods/releases/download/v0.2.0/XZ-Mods-Builder-preview-source.zip)

- Prepare a new USB, update an existing loader with a backup, restore a loader,
  and edit saved controller settings.
- Choose 24 controller themes, eight-pad stem shortcuts and two Beat Jump pages.
- Browse exported Rekordbox tracks, generate stems with Open-Unmix HQ or Vocal
  focus, import grouped instrument stems, or prepare selected tracks as a batch.
- Read new OverCue FLAC pages and existing zlib pages. Generate and Import write
  FLAC and verify all seven playback mixes while preserving the original track.
- Display layered stem waveforms and prepare backed-up 3-band waveform colours.
- Open Beat Arcade and the real-waveform Wave Rider game from the native Games
  menu. Normal boot stays on the DJ screen.
- Download and prepare Doom shareware, Chex Quest and Freedoom data through Games.
  Commercial IWADs must come from the user. MyHouse remains unavailable until its
  native engine and controller support are qualified.
- Install signed app updates through App updates.

Apple Silicon supports generation with the current models. Intel Macs support
stem imports and the USB, settings, waveform and game tools. The Mac packages
are ad-hoc signed and are not notarized. See [the Mac instructions](docs/USER-GUIDE.md#install-the-mac-utility).

The latest runtime remains experimental. The release includes native ARM tests
and local controller screen checks; extended cold-boot, two-deck and real-song
phase qualification remains pending. See [the changelog](CHANGELOG.md).

Standalone export of new tracks without Rekordbox is a separate prototype.
The test track loaded and played with working stems on an XDJ-XZ. This exporter is not yet connected to the app interface.

## Repositories

- This app: https://github.com/OpticMystic/XZ-Mods
- Toolkit (builder source, hook source, patchers):
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
The app downloads the official 1.26 firmware and the boot support it needs, checks
their hashes, then writes and verifies `autoexec.bin` in the USB root. It never
formats the drive or replaces an existing loader. Firmware and boot support stay
in an app-local cache for later builds. A local firmware file and staging folder
remain available under Advanced options.

This update includes two taller stem rows, horizontal volume gestures,
independent deck pads, A-D/E-H banks, corrected pad brightness and colors,
twelve display styles and automatic XZ Mods loading artwork. It retains the
OverCue v4 playback adapter. See [the changelog](CHANGELOG.md) for verification limits.

## Use prepared OverCue tracks

Keep the matching Rekordbox `Contents` and OverCue `CDJMODS` folders on the same
USB. In **Prepare stems**, choose **Check OverCue track**, then select the original
track inside `Contents`. The builder checks source identity and every compressed
audio page using the same decoder as the player. It does not change the USB.

Supported prepared format: `overcue-index/1` and `overcue-stems/4`, 96 kHz stereo
signed-16-bit PCM in `OVPGZ001` zlib or `OVPGZ003` FLAC pages, including all seven prepared mixes.
The source track must match its recorded SHA-256. Unsupported schemas fail closed.

[OverCue](https://overcue.gg/) is the recommended way to prepare stems.
XZ Mods also includes a beta stem builder. Its separation and aligned-stem import
workflows write the [open OverCue format](https://github.com/OverCue-gg/overcue-stems-format)
under `CDJMODS` and never replace stems OverCue made. The source must be a
44.1 kHz stereo WAV or FLAC track that Rekordbox already exported to the USB's
`Contents` folder. Separation runs on the CPU. The beta is not hardware-qualified,
so check playback alignment on the player.

## Native stem controls

The included runtime displays two taller rows, one below each native waveform,
with Vocals, Harmonics, Drums and Bypass. Tap to mute and drag horizontally to
adjust volume. Split-row visibility, clean audio and bright physical pads were
confirmed in RAM trials, including the latest pure pad hues. After release, the
user prepared a USB with 0.1.5 and cold-booted the XDJ-XZ with MODS running.
Extended two-deck, pad, loop and focus qualification remains pending. To update
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
native playback. The VJ.Tools settings remain in MODS when offline, and view
choice is retained when the settings USB is writable.

## Release status

The native GUI, cancellable backend, cache import and portable packaging are
implemented. The user confirmed USB preparation and XDJ-XZ cold boot with 0.1.5.
Selectable pad banks, extended two-deck playback, loops, focus and the real
model execution matrix still need acceptance. A successful file check does not
prove playback alignment.

Public package inputs are allowlisted. Firmware application binaries, boot
keys, private boot images and firmware-extracted graphics are not shipped —
download from the manufacturer during preparation, or supply a local official
1.26 ZIP or UPD under Advanced options (see `BUILDING.md`).
The `builder/firmware.py::import_application` path accepts the official 1.26
`.UPD` (or the outer ZIP containing exactly one `.UPD`); unknown versions fail
closed. The GUI must not claim broader firmware support.
