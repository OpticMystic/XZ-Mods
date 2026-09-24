# XZ Mods

Independent Windows Tauri application for XDJ-XZ firmware 1.26. VJ.Tools
branding and connection support are built in; VJ.Tools installation, account,
library database and release process are not dependencies.

> Developer preview. Only the qualified feature subset may be promoted in a
> release; the remaining features stay visible with their actual readiness.

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

## 0.1.2 preview candidate

The Windows package is built locally, but this candidate is not posted for
download yet. Hardware acceptance for the new mixer EQ path is still pending.
Once released, extract the ZIP and run `XZ Mods.exe` with its `resources` folder
beside it. Windows requires WebView2. VJ.Tools and a developer Python
installation are not required.

This update includes the native waveform stem strip, independent deck pad
controls, selectable A-D/E-H stem banks, default-on stems, brighter default stem
cue colors, overlay toggle, loop-start cache fixes and OverCue v4 playback
adapter. See [the changelog](CHANGELOG.md) for changes and verification limits.

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

## Standalone EQ and screen control

In MODS > Controls, enable Spare Channel Stem EQ. Channel 3 controls Deck 1,
channel 4 controls Deck 2. HIGH controls Vocals, MID Harmonics, LOW Drums.
This preview reads the XZ's normal mixer MIDI reports inside the player and
passes them through unchanged. It does not need a DJ application mapping or a
desktop relay. In Utility, set **Mixer MIDI Message** to **Send** or **Send with
Time Param**. Each deck must have a local USB track loaded, and its spare mixer
channel must be set to PC. LINK/PC-deck sources and external channel selections
suspend the matching control. The EQ status stays at **Waiting** until a valid
knob report arrives.

Play prepared tracks briefly to establish native audio alignment. Centre is full
stem volume, left fades to silence, and right adds no boost. Move a knob through
the current stem level to pick it up without a sudden jump.

In MODS > Controls, choose whether stems use hot cues A-D or E-H. The selected
bank maps its first three pads to Vocals, Harmonics and Drums, followed by
bypass. The other bank keeps its normal hot cues.

The top-left VJ.Tools button appears only while the MODS VJ.TOOLS CONNECTION setting
is enabled and the VJ.Tools network connection is live. Exit VJ returns to
native playback. The VJ.Tools settings remain in MODS when offline, and view
choice is retained when the settings USB is writable.

## Release status

The native GUI, cancellable backend, cache import and portable packaging are
implemented. This remains a developer preview. The passive mixer telemetry and
selectable pad banks need live-player acceptance before release. Final
native-player pad, loop, focus, two-deck and cold-boot acceptance remains
pending, as does the real model execution matrix. A successful file check does
not prove playback alignment.

Public package inputs are allowlisted. Firmware application binaries, boot
keys, private boot images and firmware-extracted graphics are not shipped —
supply the official XDJXZ.UPD plus your local boot key at build time (see
`BUILDING.md` and the toolkit's `vendor/decrypted_iso/README.md`).
The `builder/firmware.py::import_application` path accepts the official 1.26
`.UPD` (or the outer ZIP containing exactly one `.UPD`); unknown versions fail
closed. The GUI must not claim broader firmware support.
