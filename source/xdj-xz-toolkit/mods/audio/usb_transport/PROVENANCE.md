# Provenance

`usb_transport.c`, `usb_transport.h` and `tests/test_usb_transport.c` are byte-identical copies of
VJToolsLibrary `packages/xdj-xz-toolkit/mods/audio/usb_transport/`, commit
`8b0053f05ad57368d105b5413ec350ebbe79c518` (GPL-2.0-only, owned by Codex). Never edit them here;
transport changes land upstream first and are copied again with new hashes.

| file | SHA-256 |
| --- | --- |
| `usb_transport.c` | `593116e0213c5589771aed19bdf182f8382b98219f5f11a1aeead5a7a2abcc5e` |
| `usb_transport.h` | `4245b5c4f37cb25b5cc2730e1cc1428348d63dd5cfd9ebfe565144f198b8a098` |
| `tests/test_usb_transport.c` | `6f451b8f48bae116a89b3af69b43b8ee67dd437997c8275d46ef5a3c3d47bcdd` |

The files keep their upstream LF line endings.

Build note. The transport calls `clock_gettime`, which glibc 2.13 exports only from librt, and the
runtime library may not depend on librt. `mods/build.py` compiles the unchanged source with
`-Dclock_gettime=xz_usb_clock_gettime`; `mods/extra_decks.c` defines that function as the direct
`SYS_clock_gettime` system call the rest of the runtime already uses.

`mods/verify_extra_player.py` runs `tests/test_usb_transport.c` under WSL gcc with
AddressSanitizer and UndefinedBehaviorSanitizer when WSL is present.
