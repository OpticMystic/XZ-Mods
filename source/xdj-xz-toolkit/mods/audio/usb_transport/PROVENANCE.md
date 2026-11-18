# Provenance

`usb_transport.c`, `usb_transport.h` and `tests/test_usb_transport.c` are byte-identical copies of
VJToolsLibrary `packages/xdj-xz-toolkit/mods/audio/usb_transport/`, commit
`1a99048022b5d1b3ec18a29f91b4e64eacb89169` (GPL-2.0-only, owned by Codex). Never edit them here;
transport changes land upstream first and are copied again with new hashes.

| file | SHA-256 |
| --- | --- |
| `usb_transport.c` | `4b9a976806ea0e52e686e99c0a817cedf6f990c8ecb73a9855a605cacc5f8d41` |
| `usb_transport.h` | `c852141ba70502a2e7084d6352539d97fb81af8b3ef54dc387d5191749247436` |
| `tests/test_usb_transport.c` | `14be766501dd872d3f6b9acb22f21252a6539983d1e7036d7e9f2144b02d2f23` |

The files keep their upstream LF line endings.

Build note. The transport calls `clock_gettime`, which glibc 2.13 exports only from librt, and the
runtime library may not depend on librt. `mods/build.py` compiles the unchanged source with
`-Dclock_gettime=xz_usb_clock_gettime`; `mods/extra_decks.c` defines that function as the direct
`SYS_clock_gettime` system call the rest of the runtime already uses.

`mods/verify_extra_player.py` runs `tests/test_usb_transport.c` under WSL gcc with
AddressSanitizer and UndefinedBehaviorSanitizer when WSL is present.
