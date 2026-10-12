# RAM tracks on XDJ-XZ

The firmware 1.26 adapter holds one complete original track per internal deck
in pinned system RAM. It keeps the stock decoder, tempo processing, cues and
transport. The MODS panel provides loading progress, cancellation and retry.

## Use the control

1. Load a local USB track on Deck 1 or Deck 2.
2. Open **MODS > ADVANCED > RAM TRACKS** and choose that deck.
3. Tap **LOAD TRACK INTO RAM**. Keep the USB connected during copying.
4. Wait for **ORIGINAL AUDIO IN RAM**. Copy failure leaves USB playback intact.

The removal adapter retains a completed RAM track instead of entering the
stock emergency loop or unloading it. Loading another track invalidates the
old copy immediately. After USB removal, the copy stays held until a new load.
**RELEASE RAM COPY** returns to USB reads only after the worker verifies that
the original source is still present.

Each original file must fit within 64 MiB. Both decks share a 128 MiB limit,
and allocation leaves at least 64 MiB of free system RAM. Memory is pinned
before copying; a failed pin or insufficient memory never reports readiness.
Prepared stem files and USB waveform assets are separate dependencies and
are not included in the original-track copy.

## Verification

From the repository root on Windows:

```powershell
python packages/xdj-xz-toolkit/mods/ram/verify.py --zig .tmp/cdj-zig/ziglang/zig.exe --output E:/Codex/xz-ram-verification
```

The verifier checks the exact firmware hash and eight ARM prologues, rejects
disabled assertions, runs the worker with ASan/UBSan, then runs its ARM32 build
under QEMU through the Ubuntu WSL distribution. Tests cover short reads,
partial-copy failure, cancellation, source deletion, byte identity, seeks,
EOF, two-deck eject acknowledgement, normal unload, replacement and cleanup.
The ARM test exercises the actual source-stream capture adapter with a real
`FILE*`, not just the portable copy helper.

Physical USB eject, uninterrupted real-song audio, jog/cue behavior after
removal and cold USB boot remain separate acceptance gates. Tests do not
modify the controller, a physical USB or firmware.

## Firmware removal boundary

USB kinds 2 and 3 use `UsbStorageManager` states 3 and 4. The adapter scopes
removal, safe-eject and recording-completion dispatches. It suppresses
`PlayerSkeleton::unload` and `emergency` only for the exact completed RAM deck
within that scope. Device IDs, manager kind, deck pointer and phase must match.
All other calls retain stock behavior.

Acknowledgements go to the manager only after its original dispatcher returns.
The adapter clears its thread-local scope during those callbacks. It never
fabricates the full player unload callback, which would clear the retained
track and UI. The decoder's source stream reads the immutable compressed bytes
at its stock virtual file position; a worker alone opens, copies and retires
the allocation. The complete file lives in an unlinked, preallocated tmpfs
file with its mapped pages pinned. Before reporting readiness, the worker
drains earlier USB reads and replaces the stock decoder's file descriptor
with that RAM file. The original `FILE*` and virtual position remain intact.
Release verifies the source and restores its descriptor on the worker.
