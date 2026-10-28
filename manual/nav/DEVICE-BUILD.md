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
