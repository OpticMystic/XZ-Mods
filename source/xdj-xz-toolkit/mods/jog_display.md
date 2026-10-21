# Jog display packet adapter, offline candidate

This module is not in the runtime build and installs no hook. It performs no
device I/O, address patching, allocation, locking, network access or file I/O.
It has host sanitizer and ARMv7 QEMU behavioral proof against actual native
ARM packet-producer fixtures. Physical MCU rendering remains untested.

## Supported minimum

One state and629248-byte caller-owned storage arena per physical side.
Only the native communications thread for that side may call its API.
Channel1 remains the left physical transport and channel2 remains right.
Selection3/4 changes display identity and content only.

Extra ownership requires a nonzero authoritative native media epoch and
complete retained native batches for INFO, cue, hot cue, memory hot cue,
waveform and artwork. Missing or oversized families reject entry. Never
invent a native clear batch to satisfy readiness. Initialize before native
startup updates, or prime from complete known-current native producer buffers.

`xz_jd_step` receives the original native148-byte assembled frame, before any
override and before native memcmp/copy/transmission. It observes fresh native
local64-byte packets, maintains double-buffered complete family batches, then
selects the output. A native image update starts at packet1 and commits only
after all packets arrive in order. Native artwork producer caps images at4784
packets; wave capacity is bounded at128 and larger inputs refuse readiness.

On extra selection or software media-generation change, five exact native
clear commands precede fresh INFO. INFO supports software elapsed position,
length, ordinary playing/paused, explicit signed native-wire tempo rate,
tempo range and supplied vinyl/master-tempo facts. `tempo_known=1` is required;
the caller must derive wire units correctly from actual software state. Zero
is the tested neutral wire value;10000 is not assumed unity. Range0 disables
the range indication. It claims no native track number, BPM,
cue, loop, slip, sync, artwork or waveform. INFO mode is forced per frame;
stored user settings are untouched.

On telemetry expiry, future timestamps, missing snapshots or invalid timing,
the selected extra deck remains3/4. Five clears precede native source-invalid
INFO with no stale time or track content. `XZ_JD_STALE` lets the central UI
state that display telemetry is unavailable. It must not silently change
physical input selection. Fresh coherent telemetry restarts clearing and INFO.

Only explicit native1/2 selection starts restoration. Five physical-side
clears precede retained INFO and complete cue/wave/art batches. Paused native
content restores without another playhead update. INFO is interleaved every
eight replay steps so moving native time can update without starving image
replay. Native display mode resumes after replay. If a native media epoch is
invalidated during replay, source-invalid native INFO replaces stale data
until complete current batches are captured.

The native local sequence counter is observed separately from the emitted
sequence. After ownership begins, the adapter maintains a coherent output
sequence when replaying and later passing through native packets. CRC16 is
recomputed using the native 32-bit-word reversal and upper-halfword layout.

## Ring/status proof

`prove_outer.py` runs the full native `JogCpuCom::assemble` against34 LED
rows. All changing native LED fields collapse through the adapter's quiet
mask to the exact native LED-off/ring-disabled result. This disables native
deck1 animation under extra3 rather than estimating software rotation.
Native ring-disabled position fields at frame0x5d/0x5e are0x7f, not0.
Unknown fields, physical routing, screensaver/calibration controls and
brightness configuration remain native. PC/HID or calibration ownership
returns `XZ_JD_UNSUPPORTED` without modifying the frame.

## Integration gates

1. Fingerprint the exact ELF before any future hook. Evidence input SHA256 is
   `3a7c6c507ea67b2484d0cbc58136a495374c8ede5eeef1ffdffcfe86c7a3dae3`.
2. Keep all adapter calls in each side's existing native serializer thread.
   Snapshot publishers must supply a coherent immutable copy via their own
   existing synchronization. Never mutate this state from UI/player threads.
3. Native media epoch must come from actual native media identity, not track
   number or duration. Assign that epoch only at a proven native queue reset
   or producer-generation boundary. Old queued packets must not be tagged as
   a new media epoch. The packet bytes cannot independently prove identity.
4. Retained native cache priming must finish before enabling3/4. Current code
   deliberately refuses ownership if boot capture or current-image priming
   is incomplete. Source buffers are not read by address in this module.
5. Coordinate selection with the existing input/deck owner. Feed fresh
   software generation, monotonic capture time, elapsed milliseconds,
   length, paused flag and known tempo/mode facts; handle `STALE`, `NOT_READY` and `UNSUPPORTED`
   visibly in the main UI.
6. Separately qualify MCU logical3/4 command-header acceptance, clear command
   rendering, timing cadence and physical return-to-native behavior. Host,
   ARM and QEMU checks do not establish physical acceptance.

These gates are explicit hook integration work. This packet/state module is
complete as an offline candidate; it does not make the physical feature ready.

## Reproduce

From the monorepo run
`python tools/xz-four-deck-lab/jog-display/test_c_adapter.py MODS EVIDENCE`.
MODS is this directory; EVIDENCE is
`E:/Codex/xz-jog-offline-20261021/display`.
The runner uses independently emitted ARM JSON fixtures, GCC C11 with
`-Wall -Wextra -Werror`, host ASan/UBSan, then static ARMv7 GCC and QEMU.
It writes exact source, fixture and binary hashes to
`EVIDENCE/c-adapter/receipt.json`.
