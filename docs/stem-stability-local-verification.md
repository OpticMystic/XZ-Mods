# XZ stem stability candidate

Status: fourth independent review completed with no must-fix source defect. All five canonical software suites and ARM ABI checks pass. Frozen native candidate `1764ee87` pairs with receiver `dd12975a`; the PC candidate is installed and verified. The XZME USB loader is now updated and verified. Controller boot and physical stem acceptance remain pending. No public publication is authorized.

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

The five required suites are profile worker, pad controls, SDK/UI, native audio runtime, and decoder/mixer. Logs are retained at `E:/Codex/xz-stem-fix-20261015/r3-{profiles,controls,sdk,audio,decoder}.log`; all five completed with the expected PASS records and no sanitizer failure or Python traceback. `r3-validation.json` records hashes and counts; the decoder log retains PowerShell formatting of informational alignment stderr, which appears to explain the shell wrapper error despite three completed streaming runs. The frozen post-commit decoder rerun completed with explicit exit code 0, recorded in `r4-decoder-exit.json`; all three streaming cases pass. ARM32 / GLIBC_2.4 / NODELETE checks pass in `r3-arm-preflight.log`. The blank-target regression first failed with SAVED TO USB 2 in `r3-blank-red.log`, then passed after restricting ownership transfer. The pad/runtime suite exercises first-pad and count-publication regressions on both decks plus existing PC routing, touch ownership, Beat Jump and waveform checks. Decoder/mixer tests exercise zlib and FLAC fixtures, including alignment, selected mixes, loop returns, unity passthrough and loading continuity. These are software checks, not audible controller acceptance.

The read-only observer now reports the saved layout, change revision, last origin, layout-control count, profile-apply count, save USB, metrics state and confirmed alignment fields. Origins distinguish boot on the XZ, boot from USB, SDK app profiles and layout controls. The reserved explicit-profile API has no current production caller; explicit SDK Apply actions report app-profile. Diagnostics also include runtime_pid, boot_runtime, profile_owner_usb, profile_owner_matches_target and profile_target_empty. boot_runtime identifies boot configuration, not the hash of the mapped library. A concurrent track load can produce a missing status sample; it is not evidence of an empty deck.

## Hardware gate

Correction received at 19:33: the owner now reports Hot Cues working with Smart Cue OFF / Gate Cue OFF and is unsure of the earlier settings. Smart Cue-ON physical acceptance is not established. Earlier physical behavior reports remain preserved in the cue owner evidence; no cue-only retest is requested. Cue source from `f4131758` remains unchanged in this candidate. A separate receiver-only 8-bit trial was prepared but has no completed physical acceptance. This thread installed the PC candidate while preserving the pinned 8-bit trial selection. At the earlier PC installation, the controller USB and native runtime were unchanged; the subsequent USB staging is recorded below. The owner has now followed the poweroff/USB-transfer instructions to install the stem correction. This combined candidate cannot establish an isolated receiver performance result; activation still requires the stopped controller and verified USB loader. Capture performance, 8-bit encoding and the 30 fps goal belong to another thread.

After safe activation, verify the exact candidate identity, browse and load tracks on both USB sources, cycle the saved layouts deliberately, test the first physical stem pad after track loads, and compare indicators with audible stem changes. Record the layout origin and revision at any unexpected movement. Preserve the owner current cue settings. Check ordinary playback continuity while testing stems; do not demand a separate cue-only acceptance pass. Release readiness remains open until those physical checks pass.

## Intentional profile actions

Boot restore and explicit SDK profile application still restore settings. SDK profiles deliberately configured for Autoload also retain that existing behavior; they report an app-profile origin. Inspect configured Autoload profiles and scan/probe logs during acceptance. A successful scan that temporarily misses a package can re-arm its existing Autoload behavior, including after remount; correlate any app-profile origin with those logs. No capture yet proves whether the original jump came from browsing or an app profile.

Reinserting the owner USB changes its mount identity. Saves then stay on the XZ until reboot or explicit Apply+Save establishes that USB again. Include this existing fail-safe behavior in physical acceptance.

## Local PC delivery

The signed local installer completed with exit 0. The normal installed app runs at `C:/Users/short/AppData/Local/XZ Mods/xz-mods-builder.exe`, PID 82228, with a responding XZ Mods Builder window. Its SHA256 is `ab0aa3656b244899aeb51985dea1fff2c5f593b1ccb29b622809facb58596dc3`. All 2,262 resources match the frozen candidate with no extra files. The extracted NSIS payload matches the installed executable; only the expected three-byte Tauri bundle marker differs from the pre-package build. The initial direct-backend verification omitted the Tauri app-data override; rerunning with the actual app data confirms selection `a7324f67` remains pinned and byte-unchanged. Candidate `ab9cec84` is available but has not been selected or activated.

Private encrypted loader `2a635be4` passed module, metadata and pin readback. That earlier private test wrote no physical USB. The owner later connected XZME and authorized the physical loader update recorded below. Physical release acceptance remains open. Local evidence is in `E:/Codex/xz-stem-fix-20261015/installed-r4-verification.json`, `installed-window-r4.json`, `generated-loader-r4/verification.json` and `activation-receipt-r4.json`.

## Physical USB staging, 19:37

Fresh XZME `G:` identity was FAT32 serial `2697541434`. The managed update backed up the previous `c3cd5a26` loader on both the PC and USB, checked the current volume and loader revisions, then atomically installed candidate `1764ee87` with receiver `dd12975a`. Physical encrypted loader SHA256 is `81b9693da4cf9af3da9a69d03431b7a0c05f00ead39729cdc032f2be98f722bf`. Decrypted readback proves both embedded libraries, version metadata and pinning. All 13 protected non-loader files retain their hashes; size/time metadata for 2,996 media/library files is unchanged. No games or settings changes were made. The PC selection now pins candidate `ab9cec84`.

The USB is ready for safe eject and a controller cold boot. No controller boot or physical stem test is claimed. This combined runtime/receiver candidate does not establish isolated 8-bit performance. Source cue code remains preserved, but Smart Cue-ON physical acceptance is explicitly not established following the 19:33 owner correction.

Physical proof and rollback paths are in `E:/Codex/xz-stem-fix-20261015/usb-update-20261015/verification-summary.json`.
