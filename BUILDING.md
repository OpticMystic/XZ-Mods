# Build and modify XZ Mods

XZ Mods is a standalone Tauri application. The VJ.Tools Library application and
database are not build or runtime dependencies.

The exact toolkit source used for 0.2.1 is included in `source/xdj-xz-toolkit`.
Use `--toolkit source/xdj-xz-toolkit` to build from that immutable snapshot.
`release-source.json` records each input hash and the paired native runtime.
The matching release source ZIP contains the app and toolkit side by side.

It also pairs with the public toolkit repo
(https://github.com/OpticMystic/xdj-xz-toolkit). Clone both side by side,
or pass `--toolkit <path>` / set `$XZ_TOOLKIT_DIR`.

The current Windows development build uses Rust 1.96, Zig 0.16, Python 3.14.3,
PyInstaller 6.22.2, pycdlib 1.20.0, cryptography 48.0.0, inflate64 1.0.4 and pyelftools. Use a
dedicated Python virtual environment for development. Pillow generates the
optional UI font atlas; NumPy and FFmpeg are used by the relevant offline tests.
Neither FFmpeg nor the installed developer Python is required by the packaged
builder. The bundled native audio helper decodes WAV/FLAC.

From this repository root (with the toolkit alongside):

```powershell
python ..\xdj-xz-toolkit\mods\build.py --zig <zig.exe> --output <runtime-build>
python tools\prepare_resources.py --runtime-build <runtime-build> --zig <zig.exe>
python tools\build_native.py --release
```

With an explicit toolkit path:

```powershell
python tools\prepare_resources.py --runtime-build <runtime-build> --zig <zig.exe> --toolkit C:\path\to\xdj-xz-toolkit
```

The resource builder stages only the selected runtime, receiver, original
builder code, original logo, decoder and licensed dependencies. It rejects
firmware applications, boot keys, personal boot images and model checkpoints
inside the public resource tree. Model weights and official firmware are
downloaded separately from their original publishers when requested.
The official 1.26 firmware and the manufacturer's published source archives are
downloaded on first USB preparation and checked against pinned sizes and hashes.
The source ZIPs use Deflate64, so the frozen backend includes inflate64. Its
LGPL notice and exact source archive ship in `resources/licenses/`.

For a builder-only update that retains the paired ARM runtime:

```powershell
python tools/build_backend.py --toolkit ..\xdj-xz-toolkit
python tools/build_native.py --release
```

## Covered source and notices

The package carries the exact mod/receiver source and headers under
`resources/source/xdj-xz-toolkit`, including MPL-covered files and their
license. Rebuild those libraries with `mods/build.py` in that source layout.
The generated font atlas is accompanied by its source font, generator and OFL.

The pycdlib library is LGPL-2.1-only. Its exact Python sources are included under
`resources/licenses/source/pycdlib`. The backend is frozen with PyInstaller's
`--debug=noarchive` option so its Python modules remain separate. You may replace
or modify that library and rebuild the backend using `prepare_resources.py`.
The PyInstaller bootloader's commercial-distribution exception and the other
dependency notices are included. No additional restriction is imposed on
modifying or rebuilding the included LGPL/MPL components.

Our original builder code is MIT-licensed. Receiver/key-shifter files retain
MPL-2.0; cache/mixer code retains its MIT-or-Apache notices; the font retains OFL.
Smule Windowed RoFormer and the selected Open-Unmix UMX-HQ checkpoints have
separate original-source MIT grants recorded in `builder/models.json`.
Demucs research-only weights, UMXL non-commercial weights and unclear model
mirrors are not approved presets.

The user confirmed USB preparation and XDJ-XZ cold boot with 0.1.5. The real
model execution matrix, extended two-deck performance and experimental
controls still need separate qualification. The 0.1.5 asset retains its
original preview filename; GitHub now marks the tested release as latest.

## Package and verify 0.1.5

```powershell
python tools/verify_backend.py --resources resources --evidence dist/backend-check.json
python ../xdj-xz-toolkit/builder/tests/verify_overcue_builder.py --checker resources/xz-overcue-check.exe --backend resources/backend/xz-mods-service.exe
python tools/package_preview.py --output dist/0.1.5 --exe src-tauri/target/x86_64-pc-windows-msvc/release/xz-mods-builder.exe
```

Choose a new evidence file and output directory for each run. The source ZIP
contains sibling `XZ-Mods` and `xdj-xz-toolkit` directories so the documented
build layout works after extraction. The public runtime remains experimental.

For waveform changes, exercise the frozen backend on disposable fixtures:

```powershell
python tools/verify_waveforms.py --resources resources --evidence dist/waveform-check.json
```

Add `--corpus <analysis-copy-folder>` to verify copied real `.EXT` / `.2EX` pairs.
The verifier copies these into a temporary folder, checks Apply and Restore,
then verifies the source files were not changed. It never connects to the XZ.

## Shared runtime parity

Release packaging uses the paired bundle from `tools/build-xz-mods-bundle.py`
in VJ.Tools Library, or `tools/build_bundle.py` in the toolkit checkout.
Pass that same directory to the standalone builder:

```powershell
python tools/prepare_resources.py --runtime-bundle <paired-bundle> --zig <zig.exe> --toolkit <toolkit-checkout>
```

VJ.Tools packages that bundle from `packages/xdj-xz-toolkit/runtime`.
Both packaging paths verify its hashes and reject native source or bootstrap
changes that have not been rebuilt. `tools/verify-xz-runtime-parity.py` in
VJ.Tools verifies standalone resources against bundled or installed loader files.
`--runtime-build` remains a development input; it does not establish release parity.

## Loader update and settings checks

Run `python -m unittest builder.tests.test_managed_usb -v` from the toolkit.
The frozen-backend and packaged-app checks must use the matching prepared
resources. `settings-schema.json` is hashed in the runtime manifest; do not
copy an older sibling settings parser over the paired runtime source.

On a Linux host or WSL, run
`python3 tools/verify_settings_contract.py --resources resources --toolkit ../xdj-xz-toolkit`.
It compares Python parsing, exact defaults, old record lengths and range
rejection with the C parser carried by that resource bundle.

When the paired native source and Python builder live in different checkouts,
pass `--runtime-source <native-checkout>` and `--toolkit <builder-checkout>` to
`prepare_resources.py`. Each must match its packaged artifact; the source ZIP
keeps the actual paired native source and current builder source.


## Windows installer and app updater

Run `python tools/build_release.py --output dist/<version> --portable` after preparing resources and rebuilding the frozen backend. This creates the primary NSIS installer, its updater signature, latest.json, checksums and an optional portable ZIP. It does not publish them.

Keep the updater private key outside the repository and release packages. Set TAURI_SIGNING_PRIVATE_KEY to its file path. The build checks that its public key matches the app configuration. Preserve this key across releases.

The updater reads the GitHub latest release and accepts only a newer signed installer from this repository. Publish the installer and signature first, then latest.json. Release notes come from release-notes.json, with new features before bug fixes; keep the matching CHANGELOG.md entry consistent. A local build is not proof that the public updater feed is published.

The updater-test Cargo feature allows loopback test endpoints through XZ_UPDATER_TEST_API and XZ_UPDATER_TEST_ENDPOINT. Never use this feature in published builds.

## Build a private Mac preview

Build on macOS 14 or later, using the native architecture. Apple Silicon and Intel
packages contain their own Python backend, audio helpers and uv. The controller
libraries remain ARM32 in both packages. Current PyTorch wheels require Apple
Silicon for generation; grouped stem import works on both architectures.

Install Rust, Node 22 and Python 3.13, then install the build dependencies:

```sh
python3 -m pip install pyinstaller==6.22.2 pycdlib==1.20.0 cryptography==48.0.0 inflate64==1.0.4 pyelftools==0.33 pillow==12.3.0 certifi==2026.2.25
```

Place the paired runtime bundle and its signed `ota-release` directory beside
each other. The private source snapshot supplies both, plus the matching toolkit.
It carries no private signing keys, firmware images or game WADs.

```sh
python3 tools/build_macos.py --toolkit ../xdj-xz-toolkit --runtime-bundle ../runtime-bundle --output ../mac-packages
```

The script verifies input hashes, builds the native helpers and frozen backend,
preserves Python framework symlinks, signs the package ad hoc, and creates an app
archive and DMG. It exercises imports through the packaged backend, a disposable
FAT32 disk image, exclusive publication and a visible app window. Apple Silicon
also runs both approved models through FLAC output and complete audio verification.
Checksums and check records ship beside the private packages.

The public Mac preview uses an ad-hoc signature and is not notarized.
A Developer ID signature and notarization remain a separate distribution task.
The private build does not publish a GitHub release or update feed. Mac update
downloads must be signed `.app.tar.gz` bundles from this repository; a Windows
installer is rejected by the Mac updater.

macOS FAT32 volumes do not implement exclusive rename. New files use exclusive
creation and complete readback; existing files and folders are never replaced by
this path. Stem indexes are published after their complete bundles. The first
write of a new `autoexec.bin` is not crash-atomic on FAT32. An interrupted first
write may leave an incomplete new loader; inspect and repair it before booting
the player. Updates to existing loaders retain the verified backup and replacement
workflow.
