# XZ Mods

Independent Windows Tauri application for XDJ-XZ firmware 1.26. VJ.Tools
branding and connection support are built in; VJ.Tools installation, account,
library database and release process are not dependencies.

> Version 0.1.5 is the current stable USB loader release for XDJ-XZ 1.26.
> Gate Cue, Smart Cue, Groove, X-PAD and full two-deck performance still have
> separate readiness limits in the app.

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

## 0.1.5 release

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
signed-16-bit PCM in `OVPGZ001` pages, including all seven prepared mixes.
The source track must match its recorded SHA-256. Unsupported schemas fail closed.

The included separation and aligned-stem import workflows produce legacy
`stemd-cache/1` files. They do not export OverCue bundles. Both paths remain available.

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
USB preparation, pad assignments, the twelve current-source display styles,
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
