# XZ stem stability candidate

Status: source regressions pass; controller acceptance and local installation are pending the shared Hot Cue test window. No public publication is authorized.

## Observed failure

Read-only screen captures at `output/xz-stem-position-20261015` show an earlier outer/full stem layout with metrics enabled and a later below/full layout with metrics disabled. They do not capture the exact transition or identify its cause. Deck 1 reports aligning before becoming ready in a separate series. Do not assume it was playing throughout that series from readiness alone.

The summary reclassifies the stored BMP pixels using the corrected border classifier from `tools/xz-stem-display-proof.py` at commit `d3753413`. The original paired JSONL records retain their earlier unknown classifications. Busy status replies are missing observations, not empty decks.

## Reproduced source defects

1. Drawing another USB track list selected that USB for settings restoration. The settings worker could apply its saved layout, metrics and audio settings without a MODS action. The actual worker regression failed before correction. Browsing now selects the save destination only. Boot restore and explicit profile actions remain. Revision-bound queued edits survive a source change by saving on the XZ; stale edits cannot overwrite another USB.
2. A physical stem pad could change engine levels after a track generation changed but before the renderer observed it. The renderer then cleared its mute indicator without restoring the engine. The real pad/runtime regression produced UI mute 0 with audio vocals 0. Generation reconciliation now happens before the physical action and resets UI and engine levels together. A later render preserves that new pad press.

The first attempt deferred USB restoration using loaded audio paths. Independent review found load-transition and persistence holes. That attempt and ARM artifact `73a7e352` are superseded and must not be installed.

## Checks

From a Linux host, run:

```sh
python3 packages/xdj-xz-toolkit/mods/tests/verify_profiles.py
python3 packages/xdj-xz-toolkit/mods/ui/verify_runtime_controls.py
python3 packages/xdj-xz-toolkit/mods/audio/tests/verify_overcue.py --cc gcc --sanitizer address
```

The pad/runtime suite passes the new first-pad regression on both decks plus existing PC routing, touch ownership, Beat Jump and waveform checks. Decoder/mixer tests pass for zlib and FLAC fixtures, including alignment, selected mixes, loop returns, unity passthrough and loading continuity. These are software checks, not audible controller acceptance.

The read-only observer now reports the saved layout, change revision, last origin, layout-control count, profile-apply count, save USB, metrics state and confirmed alignment fields. Origins distinguish boot on the XZ, boot from USB, app profiles, explicit profiles and layout controls.

## Hardware gate

The accepted baseline controller runs runtime `f4131758` and receiver `27c08969` while another owning thread accepts the Smart Cue correction. This thread has not changed its USB, runtime, app installation or transport. Activation waits for that thread to release the test window, even though the owner previously authorized an idle restart.

After safe activation, verify the exact candidate identity, browse and load tracks on both USB sources, cycle the saved layouts deliberately, test the first physical stem pad after track loads, and compare indicators with audible stem changes. Record the layout origin and revision at any unexpected movement. Keep Smart Cue, Gate Cue, jog, Play/Pause, main Cue and playback continuity in the acceptance pass. Release readiness remains open until those physical checks pass.
