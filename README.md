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

## 0.1.1 developer preview

[Download the Windows preview](https://github.com/OpticMystic/XZ-Mods/releases/tag/v0.1.1).
Extract the ZIP and run `XZ Mods.exe`. Keep its `resources` folder beside it.
Windows requires WebView2. VJ.Tools and a developer Python installation are not required.

This update includes the native waveform stem strip, independent deck pad
controls, overlay toggle, loop-start cache fixes and OverCue v4 playback adapter.
See [the changelog](CHANGELOG.md) for changes and verification limits.

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

## Release status

The native GUI, cancellable backend, cache import and portable packaging are
implemented. This is still a developer preview. Final native-player pad, loop,
focus, two-deck and cold-boot acceptance remains pending, as does the real model
execution matrix. A successful file check does not prove playback alignment.

Public package inputs are allowlisted. Firmware application binaries, boot
keys, private boot images and firmware-extracted graphics are not shipped —
supply the official XDJXZ.UPD plus your local boot key at build time (see
`BUILDING.md` and the toolkit's `vendor/decrypted_iso/README.md`).
The `builder/firmware.py::import_application` path accepts the official 1.26
`.UPD` (or the outer ZIP containing exactly one `.UPD`); unknown versions fail
closed. The GUI must not claim broader firmware support.
