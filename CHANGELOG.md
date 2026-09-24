# Changelog

## 0.1.2 - 2026-09-23

- Match screen controls, waveforms and pad colors to HOT CUE A Vocals, B Harmonics, C Drums, D bypass.
- Add a saved Controls option for STEMS ON A-D or STEMS ON E-H. The inactive bank keeps its normal hot cues, including when a press is released after switching banks.
- Enable stems by default when no saved USB setting exists, use brighter default red/blue/green stem cue colors, and show the native-screen VJ.Tools corner button only while the network connection is live. VJ.Tools settings remain in MODS offline.
- Place inline bypass after the three stems and share the widget geometry with waveform drawing.
- Prepare the standalone app and built-in Library loader from one hash-verified runtime bundle. Reject mismatched library pairs or bootstrap files.

- Show the native-screen VJ.Tools / Exit VJ button only while the network connection is live. Keep the VJ.Tools settings in MODS offline; native-view touches no longer reach the desktop, and video-view gestures no longer leak into native playback.

The 0.1.2 runtime remains an experimental preview. Cold-boot and full physical playback qualification are recorded separately from the build checks.

## 0.1.1 - 2026-09-23

Developer preview for XDJ-XZ firmware 1.26.

- Read prepared OverCue `overcue-stems/4` bundles through `CDJMODS/index.json`, with 96 kHz stereo paged PCM, source identity checks, page checksums and prepared stem waveforms.
- Add **Check OverCue track** to the Windows builder. It uses the device decoder to verify the original track and every page of all seven prepared mixes without changing the USB.
- Keep both native deck waveforms visible with stem toggles and level controls underneath. The STEMS button changes overlay visibility independently of audio.
- Restore independent HOT CUE A/B/C/D stem controls on both decks. A controls Drums, B Harmonics, C Vocal, and D bypass while stems are enabled. Turn the audio STEMS setting off to restore normal hot cues.
- Share touch and pad mute state, retain the control strip during UI contention, and match the stem colors to pad feedback.
- Use bounded background streaming and prepared combinations to reduce decoder work. Preserve current and pending loop-start mixes so a loop wrap cannot restore a muted stem.
- Retain the loader startup and saved-settings fixes in the paired runtime.
- Repair standalone source packaging and include the new decoder dependency notices.

The included separation and aligned-stem import workflows still produce the legacy `stemd-cache/1` format. They do not export OverCue bundles. Use OverCue to prepare `CDJMODS`, keep it beside the matching Rekordbox `Contents` folder, then check the original track in XZ Mods.

The ARM runtime matches the previously isolated-device-tested candidate:
`be323516d42f24ae232478cbf39fa0124cddcb3838ce31c8a1c7145f50a54700`.
Its paired receiver is
`dac426e2a3856ab08b60060d4c0c117ff5f828d6dd32bef03d558ccfdbfe83e6`.

Final native-player loop, pad, focus, two-deck and cold-boot qualification remains pending. Prepared-file verification does not establish audible alignment or playback performance. Model execution qualification also remains incomplete. Firmware, boot keys, model weights and personal boot images are not included.
