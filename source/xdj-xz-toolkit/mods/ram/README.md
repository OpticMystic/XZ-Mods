# RAM tracks on XDJ-XZ

RAM tracks retain one complete original audio file per loaded deck in pinned,
unlinked tmpfs. Decks 1 and 2 use the native adapter. Decks 3 and 4 keep their
running software decoder, buffers, PCM cache, tempo processing, key shift,
SYNC, cues, and queued output when the backing file changes.

## Load and release a track

1. Load a local USB track on the selected deck.
2. Open **MODS > PLAY > RAM TRACKS** in CLASSIC or MODERN.
3. Choose Deck 1, 2, 3, or 4. For a new deck 3/4 copy, connect the loopback audio route.
4. Tap **LOAD TRACK INTO RAM**. Keep the music USB connected until **ORIGINAL AUDIO IN RAM** appears.

Playing an eligible track also requests RAM automatically. **CANCEL COPY** or
**RELEASE RAM COPY** suppresses automatic copying for that loaded track.
An error needs an explicit retry. The setup flow detects the engine and source,
opens the existing track browser or four-deck connection page when needed,
and verifies the player's RAM attachment. It does not test by ejecting a USB.

READY requires a complete immutable copy, attachment to the existing reader,
and background closure of the old source descriptors. Cancelling after
attachment retains that live copy. RELEASE first verifies the current source
and restores it at the physical file offset without resetting the decoder.

After music-source removal, completed copies become DETACHED and stay loaded
until replacement, explicit lifecycle unload, or shutdown. Reinsertion is a
new source generation and does not make an old detached copy releasable.
Loaded software waveform, grid, and cues stay with their track; a missing
waveform stays missing. Detached cue edits remain in the session and are not
saved to a reinserted source.

Losing the loopback route keeps the RAM audio but applies the player's existing
pause/reset. Reconnection does not start playback automatically. New requests
and unattached candidates require the same live route epoch. CANCEL and
cleanup remain usable while that route is down.

## Shared memory limits

Original files on all four decks share one ledger:

- 64 MiB maximum per file, measured from the actual encoded file size.
- 128 MiB for pending and held original-file allocations together.
- At least 64 MiB free RAM after admission, including all pending reservations.

Reservations are page-rounded. Pending copies and retired mappings remain
charged until cleanup finishes. Held pages already reduce the OS free count,
so they are not subtracted twice. No held track is evicted to admit another.
The ledger is initialized independently of the native adapter.

Prepared native stems use the same ledger with separate pending/held totals.
Their existing seven-file atomic pin preserves the unused original-file
allowance. Original-audio READY does not mean that unpinned stems or new USB
analysis assets are retained. Software decks have no prepared-stem mixer.
The page shows original and prepared-stem totals separately.

Native and software copy workers share a materialization arbiter. Only one
original file copies at a time; queued manual requests precede queued automatic
copies. Playback workers only capture ownership and attach prepared descriptors.
Copying, pinning, final source closes, unmapping, and budget credit run in the
background. Replacement and shutdown drain that ownership before releasing it.

## Source-only verification

Run from the repository root with Windows Python and Ubuntu WSL:

```powershell
python source/xdj-xz-toolkit/mods/ram/verify_extra.py --out E:/scratch/ram-tests --sanitizer address,undefined
```

This verifier uses generated audio, disposable files, fake routes, and source
callbacks. It never opens firmware, discovers devices, or mounts storage.
The separate `ram/verify.py` wrapper inspects firmware and is not required for
these host checks. Its native tests also run through the source-only verifier.

Controller CPU headroom, pinned-memory availability, copy time, attachment
latency, audible continuity, physical USB removal, route reconnection, and cold
boot require separately authorized hardware checks. Host PCM equality does not
establish real-time hardware acceptance.

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
