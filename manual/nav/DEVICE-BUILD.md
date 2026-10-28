# Device object identity

Baseline `00f16052`; Zig target `arm-linux-gnueabi.2.13`, CPU `cortex_a9`, O2, PIC, hidden visibility. Each baseline source was byte-compared with current source before compilation. Both compilations used the same flags and mapped source paths. This verifies the listed objects, not a full linked runtime.

| Object source | Baseline and current SHA-256 |
|---|---|
| `deck_select.c` | `4e0520efcbc44fcd939cd4e3b9539f8d269ec692c0d97494f9f8ff358a84bed5` |
| `ui/stem_pads.c` | `df9c46bcfddd0ade818d7143ea24bb3c212345bcfb75a21da541472ae6114921` |
| `ui/beat_jump.c` | `1d846dde2b655783029ae512a4098baf06706ff6d9a98698abd0bcfd42740bc6` |
| `ui_runtime.c` | `df119e7d2a74ff1a309027af6e0b89b29447bc4fde62a62d7b62c96c30a4e00f` |
| `doom/native_bridge.c` | `b11c7ecf683f77278b94d96ac4cc0cc340e14388978229f8d25a0493a0cd339e` |
| `ui/deck_view.c` | `adb7655431b78c63153c6fcca7fc68c53829ac23f87c54bbc6d24690e0b4cf5d` |
| `ui/key_strip.c` | `2614c4cb1377bdc65baf096e409bede81f5c96b94294449924c7198ffd1dec23` |
| `ui/ui_profile.c` | `b62e63bdbfa22b18d7c6f6312e0a2023a10ccf47136f1d90700ba7a89e4128d9` |
| `ui/ui_support.c` | `a7c538ea18be744d7181a32ed309a8334df4f47f21d0bfe1a5e738797f208a19` |
| `ui/ui_network.c` | `ce1132e8769a463cd9c863078f30f699a3e55f9f92d8fd0a01a40f9d36b9e788` |
| `shell/shell.c` | `bfcf268d86228caad7369a6975442351a1649d80ee3c6bbfd0514ce1d5cb5760` |
| `shell/shell_browse.c` | `0229470f55307bd351f1e3121444d65ed92e9791b5fae28842cc15a067311266` |

Re-run `python tools/verify_nav_device_objects.py --zig <zig.exe> --out <proof.json>`. The new `shell/nav_capture.c`, `shell/nav_runtime.c` and `shell/nav_clock.c` files are host-only and excluded from the device source lists. Production files and source lists were not edited.

## Third capture pass

The host capture build substitutes `shell/nav_browse.c` for `shell/shell_browse.c` only in its explicit GCC source set. The wrapper includes the production source unchanged and exports labels and geometry from its private panel layout. It does not assign navigation state. No device source list includes the wrapper.

New seams use the following unchanged ARM objects. The repeated before/after object comparison passed for all 13 objects. Retained proof: `E:/Claude/xz-beta-rc-scratch/nav-arm-objects-3.json`.

| Host fixture or adapter | Device object evidence |
|---|---|
| Browse panel labels, column masks, tags, cue/loop requests | `shell/shell_browse.c`, same hash as above |
| Editable INFO rating snapshot | `shell/shell_info.c`, `643ed12fb5235ca0126d89ad40021eb34bf82af097855d3d3b38092c0c0f7192` |
| Additional display-setting action dispatch | `ui_runtime.c`, same hash as above |
| Logo inventory, preview, save-failure callbacks | `ui/ui_profile.c`, same hash as above |
| Dock report entry, edit and retry callbacks | `ui/ui_support.c`, same hash as above |
| Static keypad, review publication consumption, trial and restore callbacks | `ui/ui_network.c`, same hash as above |

All fixture outcomes remain local snapshots. The network apply adapter calls `xz_ui_network_take_apply` before supplying its result. This proves the UI review guard ran; it does not prove an OS network change. The checkerboard logo is a deterministic decoded-image fixture, not proof of reading or decoding an actual USB image.

## Chapter A adapters

Rebuilt before/after ARM objects against `00f16052` with the same target and
flags above: **18/18 byte-identical**. Retained machine-readable hashes:
`E:/Claude/xz-beta-rc-scratch/nav-arm-objects-a.json`.

The previous 13 objects plus `cue/cue.c`, `ui/ui.c`, `ui/extra_detail.c`,
`ui/extra_lane.c` and `shell/shell_play.c` were compared. The cue adapter executes
the unchanged cue state machine; CLASSIC detail uses the unchanged runtime touch
hook and four-deck drawing functions. Their host callbacks live in new
`nav_adapter_perf.h` and `nav_adapter_classic.h`, included only by `nav_capture.c`
and `nav_runtime.c`. Device source lists and production files are unchanged.
This is object identity only; no linked device build, installation or hardware
acceptance is claimed.
