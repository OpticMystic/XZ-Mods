# Build and modify XZ Mods

XZ Mods is a standalone Tauri application. The VJ.Tools Library application and
database are not build or runtime dependencies.

It pairs with the public toolkit repo
(https://github.com/OpticMystic/xdj-xz-toolkit). Clone both side by side,
or pass `--toolkit <path>` / set `$XZ_TOOLKIT_DIR`.

The current Windows development build uses Rust 1.96, Zig 0.16, Python 3.14.3,
PyInstaller 6.22.2, pycdlib 1.20.0, cryptography 48.0.0 and pyelftools. Use a
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

The real model execution matrix and full installed inference dependency lock
are not yet qualified. The current package is a developer preview, not a
completed public mod release.

## Package and verify the preview

```powershell
python tools/verify_backend.py --resources resources --evidence dist/backend-check.json
python ../xdj-xz-toolkit/builder/tests/verify_overcue_builder.py --checker resources/xz-overcue-check.exe --backend resources/backend/xz-mods-service.exe
python tools/package_preview.py --output dist/0.1.1 --exe src-tauri/target/x86_64-pc-windows-msvc/release/xz-mods-builder.exe
```

Choose a new evidence file and output directory for each run. The source ZIP
contains sibling `XZ-Mods` and `xdj-xz-toolkit` directories so the documented
build layout works after extraction. The public runtime remains experimental.

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
