# XZ stem stability candidate

Status: third independent review completed and refinements applied. Canonical profile, pad-control, SDK/UI, audio-runtime and ARM ABI checks pass. Decoder/mixer rerun is still in progress. Controller acceptance and local installation are pending the isolated 8-bit receiver trial and a fresh stopped-state check. No public publication is authorized.

## Observed failure

Read-only screen captures at `output/xz-stem-position-20261015` show an earlier outer/full stem layout with metrics enabled and a later below/full layout with metrics disabled. They do not capture the exact transition or identify its cause. Deck 1 reports aligning before becoming ready in a separate series. Do not assume it was playing throughout that series from readiness alone.

The summary reclassifies the stored BMP pixels using the corrected border classifier from `tools/xz-stem-display-proof.py` at commit `d3753413`. The original paired JSONL records retain their earlier unknown classifications. Busy status replies are missing observations, not empty decks.

## Reproduced source defects

1. Drawing another USB track list selected that USB for settings restoration. The settings worker could apply its saved layout, metrics and audio settings without a MODS action. The actual worker regression failed before correction. Browsing now selects the save destination only. Boot restore and explicit profile actions remain. Queued edits retain their proven owner and mount destination. An edit after browsing another USB saves locally without transferring the established profile owner, even if that USB is blank. A blank target is writable only when no profile owner exists; explicit Apply+Save can claim the selected target. Session-only app profiles cannot discard preceding durable edits, and save status names the actual permitted destination.
2. A physical stem pad could change engine levels after a track generation changed but before the renderer observed it. The renderer then cleared its mute indicator without restoring the engine. The real pad/runtime regression produced UI mute 0 with audio vocals 0. Generation reconciliation happens before the physical action. Engine neutral reset and generation publication share the control lock; generation-bound UI writes reject stale commands. A later render preserves a valid new pad press. Count-only changes retain controls on roles that still exist.

The first loaded-path deferral candidate `73a7e352` is superseded. The next candidate `ec0d3bf9` is also blocked after review exposed profile-ownership and engine-reset races. Neither candidate has been installed or activated.

## Checks

From a Linux host in the canonical `XZ-Mods` repository, run. The older Library mirror has other pre-existing differences and is not the build source:

```sh
python3 source/xdj-xz-toolkit/mods/tests/verify_profiles.py
python3 source/xdj-xz-toolkit/mods/ui/verify_runtime_controls.py
python3 source/xdj-xz-toolkit/mods/ui/verify_sdk_runtime.py
python3 source/xdj-xz-toolkit/mods/audio/tests/verify_runtime.py
python3 source/xdj-xz-toolkit/mods/audio/tests/verify_overcue.py --cc gcc --sanitizer address
```

The five required suites are profile worker, pad controls, SDK/UI, native audio runtime, and decoder/mixer. Logs are retained at `E:/Codex/xz-stem-fix-20261015/r3-{profiles,controls,sdk,audio,decoder}.log`; the decoder rerun is still in progress. ARM32 / GLIBC_2.4 / NODELETE checks pass in `r3-arm-preflight.log`. The blank-target regression first failed with SAVED TO USB 2 in `r3-blank-red.log`, then passed after restricting ownership transfer. The pad/runtime suite exercises first-pad and count-publication regressions on both decks plus existing PC routing, touch ownership, Beat Jump and waveform checks. Decoder/mixer tests exercise zlib and FLAC fixtures, including alignment, selected mixes, loop returns, unity passthrough and loading continuity. These are software checks, not audible controller acceptance.

The read-only observer now reports the saved layout, change revision, last origin, layout-control count, profile-apply count, save USB, metrics state and confirmed alignment fields. Origins distinguish boot on the XZ, boot from USB, SDK app profiles and layout controls. The reserved explicit-profile API has no current production caller; explicit SDK Apply actions report app-profile. Diagnostics also include runtime_pid, boot_runtime, profile_owner_usb, profile_owner_matches_target and profile_target_empty. boot_runtime identifies boot configuration, not the hash of the mapped library. A concurrent track load can produce a missing status sample; it is not evidence of an empty deck.

## Hardware gate

The owner accepted Smart Cue on both decks on `f4131758` / receiver `27c08969`, then authorized a separate receiver-only 8-bit trial retaining `f4131758`. This thread has not changed the controller USB, runtime, app installation or transport. Audio/UI activation waits for that isolated trial and fresh stopped-state evidence. Capture performance, 8-bit encoding and the 30 fps goal belong to another thread.

After safe activation, verify the exact candidate identity, browse and load tracks on both USB sources, cycle the saved layouts deliberately, test the first physical stem pad after track loads, and compare indicators with audible stem changes. Record the layout origin and revision at any unexpected movement. Keep Smart Cue, Gate Cue, jog, Play/Pause, main Cue and playback continuity in the acceptance pass. Release readiness remains open until those physical checks pass.

## Intentional profile actions

Boot restore and explicit SDK profile application still restore settings. SDK profiles deliberately configured for Autoload also retain that existing behavior; they report an app-profile origin. Inspect configured Autoload profiles and scan/probe logs during acceptance. A successful scan that temporarily misses a package can re-arm its existing Autoload behavior, including after remount; correlate any app-profile origin with those logs. No capture yet proves whether the original jump came from browsing or an app profile.
