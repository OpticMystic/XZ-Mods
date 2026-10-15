# XZ stem stability candidate

Status: all four canonical standalone source suites pass; independent review is pending. Controller acceptance and local installation are pending the isolated 8-bit receiver trial and a fresh stopped-state check. No public publication is authorized.

## Observed failure

Read-only screen captures at `output/xz-stem-position-20261015` show an earlier outer/full stem layout with metrics enabled and a later below/full layout with metrics disabled. They do not capture the exact transition or identify its cause. Deck 1 reports aligning before becoming ready in a separate series. Do not assume it was playing throughout that series from readiness alone.

The summary reclassifies the stored BMP pixels using the corrected border classifier from `tools/xz-stem-display-proof.py` at commit `d3753413`. The original paired JSONL records retain their earlier unknown classifications. Busy status replies are missing observations, not empty decks.

## Reproduced source defects

1. Drawing another USB track list selected that USB for settings restoration. The settings worker could apply its saved layout, metrics and audio settings without a MODS action. The actual worker regression failed before correction. Browsing now selects the save destination only. Boot restore and explicit profile actions remain. Queued edits retain their proven owner and mount destination. An edit after browsing a different existing USB saves locally, not over either USB profile. Blank targets and explicit Apply+Save remain writable. Session-only app profiles cannot discard preceding durable edits, and save status names the actual permitted destination.
2. A physical stem pad could change engine levels after a track generation changed but before the renderer observed it. The renderer then cleared its mute indicator without restoring the engine. The real pad/runtime regression produced UI mute 0 with audio vocals 0. Generation reconciliation happens before the physical action. Engine neutral reset and generation publication share the control lock; generation-bound UI writes reject stale commands. A later render preserves a valid new pad press. Count-only changes retain controls on roles that still exist.

The first loaded-path deferral candidate `73a7e352` is superseded. The next candidate `ec0d3bf9` is also blocked after review exposed profile-ownership and engine-reset races. Neither candidate has been installed or activated.

## Checks

From a Linux host, run:

```sh
python3 packages/xdj-xz-toolkit/mods/tests/verify_profiles.py
python3 packages/xdj-xz-toolkit/mods/ui/verify_runtime_controls.py
python3 packages/xdj-xz-toolkit/mods/audio/tests/verify_runtime.py
python3 packages/xdj-xz-toolkit/mods/audio/tests/verify_overcue.py --cc gcc --sanitizer address
```

The canonical standalone profile, SDK/UI, native audio-runtime and decoder/mixer suites all pass with active assertions and ASan/UBSan. The pad/runtime suite passes first-pad and count-publication regressions on both decks plus existing PC routing, touch ownership, Beat Jump and waveform checks. Decoder/mixer tests pass for zlib and FLAC fixtures, including alignment, selected mixes, loop returns, unity passthrough and loading continuity. These are software checks, not audible controller acceptance.

The read-only observer now reports the saved layout, change revision, last origin, layout-control count, profile-apply count, save USB, metrics state and confirmed alignment fields. Origins distinguish boot on the XZ, boot from USB, app profiles, explicit profiles and layout controls.

## Hardware gate

The owner accepted Smart Cue on both decks on `f4131758` / receiver `27c08969`, then authorized a separate receiver-only 8-bit trial retaining `f4131758`. This thread has not changed the controller USB, runtime, app installation or transport. Audio/UI activation waits for that isolated trial and fresh stopped-state evidence. Capture performance, 8-bit encoding and the 30 fps goal belong to another thread.

After safe activation, verify the exact candidate identity, browse and load tracks on both USB sources, cycle the saved layouts deliberately, test the first physical stem pad after track loads, and compare indicators with audible stem changes. Record the layout origin and revision at any unexpected movement. Keep Smart Cue, Gate Cue, jog, Play/Pause, main Cue and playback continuity in the acceptance pass. Release readiness remains open until those physical checks pass.

## Intentional profile actions

Boot and explicit profile application still restore settings. SDK profiles deliberately configured for Autoload also retain that existing behavior; they report an app-profile origin. The owner USB must be inspected for configured autoload profiles during acceptance. No capture yet proves whether the original jump came from browsing or an app profile.
