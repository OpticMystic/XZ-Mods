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

## Work and release gates

- [x] Audit standalone boundaries and the upstream stem-cache contract.
- [x] Import the tested, atomic upstream-compatible cache writer.
- [ ] Build the simplified native GUI and cancellable local job service.
- [ ] Stage and verify a USB image from explicitly supplied local inputs.
- [ ] Integrate two licensed separation profiles and cache export.
- [ ] Package the independent backend, runtime, notices and installer.
- [ ] Verify the complete GUI and generated artifacts without VJ.Tools.
- [ ] Complete physical XZ controls, audio and boot qualification when testing resumes.

Public release means a new machine can install this app without VJ.Tools,
prepare compatible audio/cache files, review the USB target, build a verified
image without replacing unrelated files, and boot a hardware-qualified mod.
An attractive GUI or a passing ARM build alone does not meet that gate.

Current release status is a developer preview. The native inline layout and
several advertised upstream features remain incomplete. Only the qualified
feature subset may be promoted in a release; the remaining features stay
visible with their actual readiness.

Public package inputs are allowlisted. Firmware application binaries, boot
keys, private boot images and firmware-extracted graphics are not shipped —
supply the official XDJXZ.UPD plus your local boot key at build time (see
`BUILDING.md` and the toolkit's `vendor/decrypted_iso/README.md`).
The `builder/firmware.py::import_application` path accepts the official 1.26
`.UPD` (or the outer ZIP containing exactly one `.UPD`); unknown versions fail
closed. The GUI must not claim broader firmware support.

User constraint: no XZ restarts, hardware tests or changes to the running
VJ.Tools application during this development session.
