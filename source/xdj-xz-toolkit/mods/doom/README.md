# Native Doom on XDJ-XZ

The game runs as a separate ARM32 process on the XZ. It renders classic Doom at
320 by 200 pixels and sends an 800 by 480 RGB565 frame through loopback IPC to
the resident XZ Mods display receiver. No computer renders or supplies frames.
The existing firmware-1.26 physical-key hook forwards game input through local
shared memory before stock DJ dispatch. Audio is disabled in this first port.

The native menu is **MODS > Extras > Doom**. Extras also contains **VJ.Tools**;
the VJ.Tools connection page is no longer in Advanced.

## Controls

| XZ control | Doom action |
| --- | --- |
| Either jog wheel | Turn left or right |
| Play, held | Walk forward |
| Cue, held | Fire |
| Hot cue A, held | Walk forward |
| Hot cue B, held | Walk backward |
| Hot cue C, held | Fire |
| Hot cue D, held | Use, including opening doors |
| Hot cue E / F, held | Strafe left / right |
| Hot cue G | Enter |
| Hot cue H | Game menu |
| Touch EXIT | End the game and restore the previous screen mode |

Play/A and Cue/C use independent source states. Releasing one keeps its game
action held while the other remains down. Jog inactivity releases turning.
Game ownership expires after 750 ms without a heartbeat. Releases of buttons
owned by the game are consumed after exit so they do not reach the DJ engine.
Unrelated controls retain their stock dispatch.

## USB game files

The USB-image builder includes the optional `xz-doom` executable from its
verified runtime resources. The loader checks its digest and copies it to
`/dev/shm/xz-doom` before the firmware image unmounts. Privately selected WAD
data follows the same path and stages to `/dev/shm/doom1.wad`.

Alternatively, provide your own compatible IWAD as
`VJTOOLS/doom/doom1.wad` on the boot USB. The native setup page checks for the
engine and game data and exposes one setup button if either is missing.

The local Windows resource builder accepts `--doom-wad <local IWAD>`. Omitting
that argument removes any previously staged private WAD from the next resource
build. It does not remove the original selected file. Engine source is GPL;
commercial Doom and Doom II game data have separate licenses and are not covered
by that source license. The current local trial uses Doom 1.9 shareware data.

## Build

Run from the repository root:

```powershell
python packages/xdj-xz-toolkit/mods/doom/build.py --zig <zig.exe> --output <new-build-directory>
```

The build verifies the vendored upstream hashes in `source.json`, patches the
generic port's normal-quit termination in a generated translation unit, and
checks ARM32, the soft-float calling convention, library dependencies, and
glibc versions against the XZ's glibc 2.13. The platform adapter uses monotonic
time, bounded display writes, and frame/sleep heartbeats during Doom wipes.

`device_verify.py` runs the game and input tests in isolated XZ RAM without
restarting the DJ application. `live_trial.py` stages a reviewed runtime pair
and an exact rollback before restarting the DJ application. The live trial
stops tracks playing on the XZ. Its previous environment stays in a private
build artifact and never enters public evidence.

## Source and license

The vendored engine is [doomgeneric](https://github.com/ozkl/doomgeneric) at
`dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284`, with its GPL-2.0-or-later notices.
The engine adapter and linked control renderer are GPL-2.0-or-later. The
process-neutral protocol and native DJ-side bridge are MIT and contain no Doom
engine code. Binary packages include engine/adapter source, build scripts, the
font renderer source, and the GPL text.

MyHouse requires a different engine. Both author versions use GZDoom features
that this classic port does not implement. See the
[MyHouse compatibility investigation](../../../../docs/research/2026-10-01-xz-myhouse-compatibility.md).

Current evidence distinguishes native game execution, framebuffer scanout,
physical control tests, and fresh USB boot. See the
[hardware and installed-loader evidence](../../../../docs/evidence/2026-10-01-xz-native-doom/README.md).
