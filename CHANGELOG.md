# Changelog

## Unreleased local changes

- Live Streamer offers a saved USB-browsing cover. It replaces unrecognized screens with the XZ Mods logo before sending either OBS frames or still images, and restores recognized stock performance screens automatically. Menus and custom themes remain covered. Physical-controller acceptance remains pending.

- Preview.11 rebuilds the desktop UI as a device utility. The sidebar is USB, Stems, XZ Screen and Tools (a list that opens DJ Profile, Repair library, Network updates, XZ Mods version, App updates and About). The USB page finds a plugged-in USB through the new `list_volumes` host command (no backend job per poll) and opens it when there is one; it shows one status with one action, three tiles (DJ profile, games, stems) and the read-only file tree behind a disclosure. Writes run as a short flow (profile, review, writing, done) that sends the same backend requests as before. macOS: `managed_usb.discover` keeps only local `/dev/disk` volumes, `runtime_ota.network_addresses` puts 169.254.168.58 first, the Local Network prompt covers Live Streamer, App updates points to the downloads page, native fonts. `tools/verify_setup_wizard.mjs` 77 checks, `verify_dj_profile.mjs` 95, `verify_stems_page.mjs` 57.

- Preview.10 changes Smart Cue to update the native main-cue position without cancelling playback reservations or ending slip/audible-pause state. Turning Smart Cue off resolved the owner's Hot Cue stop-on-release reproduction with Gate Cue off. The old adapter fails the new real-ARM emulation check; the replacement passes 53 cases on each supported 1.26 executable. Hardware acceptance remains pending.

- Windows upgrades now archive the complete previous resource folder, including older runtime helpers and generated caches, before installing the new package. An explicit verified offline catalog input retains rollback versions in clean builds. Isolated installer checks cover fresh installs, repeated upgrades and refusal to move unidentified resources.

- Preview.9 targets 30 fps for Live Streamer and OBS with lossless RGB565 compression, bounded delivery and GPU drawing. The rate badge reports measured frames. Legacy loaders retain their working preview. Sanitized native codec/handler checks and Windows transport tests pass; a three-minute PC replay sustained 30 fps. Actual XZ performance and audio/control acceptance remain open.

- Preview.8 adds Live Streamer screen viewing and a local OBS Browser Source URL, with a read-only controller endpoint and a one-frame-per-second preview. Disconnects clear stale frames and reconnect automatically.
- Adds automatic playback support for OverCue `OVPGZ004` 44.1 kHz FLAC stem sets while retaining older zlib and 96 kHz FLAC sets. The desktop checker verifies the declared rate, every decoded page, complete PCM hashes and source identity.
- USB stem controls report the actual readiness reason, and turning stems on no longer requires a repeated setup sequence. The MODS menu distinguishes USB stems from PC stems. Physical controller acceptance is recorded separately from host verification.

- Repair library detects and clears invalid data in unused Device Library pages, the correction that restored XZME in Windows rekordbox. Every active page, track ID, playlist and file length is preserved. Ambiguous layouts are refused; verified backups and exact undo cover this repair too. The app now describes the specific repair it offers. Controller playback acceptance remains separate.

- Repair library now backs up and checks OneLibrary's transaction journals alongside its main database. New or changed journals block stale repairs and undo. Sixteen repair tests pass. This fixes backup coverage; it does not yet repair the reported XZME corruption warning in Windows rekordbox.

- Loader updates and build changes accept the current 515-byte default settings profile. The switch now uses the settings parser's 1,023-byte limit; oversized records remain untouched. Current-profile and oversized-profile regressions cover the boundary. Physical controller acceptance remains separate.
- Shortens Set up USB to five steps and moves game choice into the Write step (0.3.0-preview.7). NonEssentials, Settings and the Games page are removed; the settings editor (`ui/settings-menu.js`) is deleted and `profileChip` moved to `ui/profile-chip.js`. The Write step has a Custom installation accordion (`customInstall()` in `ui/setup.js`) drawing the games chooser: a USB without the loader starts with every working free game ticked (`defaultGames`), a USB with the loader keeps what it has. Built-in games are one line until the loader schema carries `games_menu`, then rows with switches. My House is removed from the UI and from the builder catalog (toolkit a6eef1c). `tools/verify_setup_wizard.mjs` 56 checks, `tools/verify_dj_profile.mjs` 94.
- Adds a DJ Profile step to Set up USB and makes the profile editor low-friction (0.3.0-preview.6). The wizard is seven steps (USB, Loadout, DJ Profile, NonEssentials, Settings, Write, Done). The step draws the same editor state as the DJ Profile page (`ui/dj-profile.js`) into a slot; its draft is not saved on its own but by the Write step (`djProfilePage.pending()` / `writePending()`), and the loadout tree plans the DJ profiles row as Will add or Will update. An empty step offers Skip for now; a USB that has a profile shows it with Edit. The editor leads with name, a logo drop target (Tauri drag-drop paths from anywhere in the window, or Choose file; several files become alternates) and colour: rekordbox's eight USB colours as swatches with the stick's own colour (`stick_colour`) marked and preselected, plus a custom picker. Tagline, links, other names and note fold under one optional heading. An empty USB opens the editor straight away on the DJ Profile page. The settings menu's DJ chip goes to the wizard step. `tools/verify_dj_profile.mjs` runs 96 checks (drag-drop, swatches, the step through Write and Done); `tools/verify_setup_wizard.mjs` 83.
- Adds the DJ Profile page and the profile track wizard (0.3.0-preview.5). `ui/dj-profile.js` is one state machine (no USB, loading, list, editing, saving, track) over the toolkit's `read_dj_profiles`, `read_logo`, `save_dj_profile`, `set_active_dj_profile` and `write_profile_track` methods, and shares the USB chosen in Set up USB. The editor validates the backend limits inline (160-character name, 80-character tagline, eight logos, 24 MB of logos, https links) and previews the MODS header chip and the 160 x 160 tile in the USB's theme. A revision conflict, for example a profile switched on the XZ, shows one Reload that keeps the draft. The profile track wizard saves the silent MP3 for rekordbox (optionally also on the USB), reports embedded logos and the silence check, and lists the rekordbox steps. The loadout tree gains a read-only DJ profiles row, the settings menu header carries the active DJ chip like the XZ, and `pick_path` takes kind `image` (PNG or JPEG). `tools/verify_dj_profile.mjs` runs 74 checks under the app CSP.
- Adds the NonEssentials step, a Games page and an update preview (0.3.0-preview.5). Set up USB is six steps; games are chosen in NonEssentials from one catalog (`ui/games.js`) that covers the four games built into the loader and the four free downloads, and the Games page draws the same chooser on the same state, so the Write step stays the one write path. A `games_menu` visibility switch appears per game only when the loader's settings schema carries that field. Under the loader row, `ui/mod-update.js` lists what changes on the XZ between the USB's recorded build and the chosen one, from `ui/mod-changes.js`, which `tools/export_mod_changes.py` generates from `mod-changes.json` by git ancestry in VJToolsLibrary (`tools/test_export_mod_changes.py` covers it). Fixes the installed app dropping every inline style under its CSP (the tree collapsed to one column): `el()` now sets styles through the CSSOM, and the three UI verifiers serve `ui/` with the app's real Content-Security-Policy and fail on any violation. `tools/verify_setup_wizard.mjs` runs 82 checks.
- Shows the USB loadout as a file tree (0.3.0-preview.4). The Loadout step replaces the Loader and Games steps: one row per loader, settings file, backups folder, game, prepared stems folder and Rekordbox library, each with a status dot and badge, a "Will add/update/change" marker when it is part of the write, and a tick box only where there is a choice. The Write step shows the same tree with only the planned rows lit; Done shows it again from the read-back. `inspect_usb` now reports free space, prepared stem folders and whether a Rekordbox export exists. `tools/verify_setup_wizard.mjs` covers the five-step flow (57 checks).
- Set up USB is one guided flow (USB, loader, games, settings, write, done) and the first page. Games are opt-in per title; the backend only copies the games the request lists and reports which games a USB already holds. The settings editor mirrors the on-device MODS menu (five tabs, same rows, pad diagram, info dock, drawn in the chosen theme) and covers every saved field, including the browse columns the XZ never shows. USB loader, USB settings and Games pages are gone; mod version, loader backups, firmware-file builds and single game downloads live under Advanced tools. `tools/verify_setup_wizard.mjs` drives the whole flow headless (44 checks); `tools/export_device_themes.py` regenerates `ui/device-themes.js` from the native theme table.
- Prepares MP3 stems on the player's timeline: the decoder keeps every MP3 frame instead of trimming the LAME encoder delay, so bundles from tagged MP3s no longer start 25 ms early. The stems page marks MP3 bundles made before this build with "Prepare again · MP3 timing"; each MP3 manifest records its source rate and LAME timing. Mirrors the OverCue Desktop 2.2.5 fix of 2026-10-10.
- Prepares stems from MP3 exports, and from 24-bit, float or any-rate WAV and FLAC. AIFF and AAC are listed as not yet supported instead of pointing elsewhere.
- Estimates the USB space every track's stems will take, per role, and learns from each bundle written on this computer. Estimates include the new waveform files and the 4096-page and 4 MiB index limits.
- Replaces the CPU-only Open-Unmix pipeline with a separation engine that uses the GPU when present: Balanced (Mel-Band RoFormer vocals, fine-tuned Demucs drums), Fast and Light, all MIT-licensed weights, one worker per batch, a percentage and a time estimate learned on this computer. The Vocal focus preset is retired.
- Writes per-stem waveform analysis into every bundle, so the XDJ-XZ STEMS scrolling waveform and stem overview strips work for tracks XZ Mods prepared. Existing XZ Mods bundles can get them without separating again.
- Decodes imported VBR MP3 stems in one read; libsndfile returned wrong samples when reading them in parts. Adding waveforms never rewrites a bundle's manifest in place.
- Rebuilds Prepare stems as one page. A sticky bar shows the USB, a storage meter, the size of the current selection with the space left after it, and one button. When a selection does not fit, the button prepares the tracks that do. Rows show Missing, Queued, Preparing with a percentage, Ready, Failed and the reason, and change in place while a batch runs. Filters, search and a details drawer per track replace the separate batch, import and check panels.
- Adds a stem engine setup that checks the computer, recommends a quality, downloads once, and tests on 30 seconds of your music before the first batch. Every setup failure names one cause and one fix.
- Adds an import window for stems made elsewhere (Suno, Demucs, UVR or any WAV, FLAC, MP3 or AIFF set, or a ZIP). It places files by name, lines them up with the track on the USB, and asks only when a file needs a place or the stems do not match. A vocals and instrumental pair continues after engine setup.
- Shows progress with the current step, one forward-only percentage per track and the time left. Cancel, an unplugged USB and a full USB each end with a summary and the one next step.

- Adds persistent mod-version selection with immutable local archives, matched runtime and receiver files, and an option to keep a USB loader pinned across network updates.
- Offers backed-up settings adaptation when an older mod cannot read newer preferences. Retains the existing loader-restore path and uses the recorded mod's schema when editing its settings.

- Adds a Repair library utility for Device Libraries with blank pages interrupting playback-history chains. It preserves track IDs, playlists and prepared stems, backs up catalogs on the computer, and supports exact undo while the catalog is unchanged.
- Rejects unsupported damage, stale inspections, changed USB identities and repairs while Rekordbox is open. Structural checks do not replace testing in Rekordbox and on the player.

## 0.2.1 - 2026-10-02

Hotfix for 0.2.0.

### Included features

- XZ Mods 0.2.1 is the hotfix for 0.2.0. Use this desktop preparation tool for new loaders and existing-loader updates.
- Includes the 0.2.0 feature set: FLAC stems, selected-track batches, layered waveforms, Beat Arcade, Wave Rider and game downloads.

### Bug fixes

- Corrects the old 0.1.9 labels in the bundled app so its version is correct before the startup check finishes.
- Identifies the hotfix consistently in the app, Python backend, signed updater feed and website.

### Installation and verification

- The paired controller runtime is the same verified payload shipped with 0.2.0. This hotfix updates the desktop package and version labels.
- Installing this app does not change your USB. Prepare or update its loader separately when ready.
- A portable folder is not replaced by the Windows installer. After installing, open XZ Mods from the desktop shortcut; to stay portable, replace the folder with the current ZIP.
- Mac previews remain ad-hoc signed and not notarized. My House still requires a qualified native GZDoom port.

## 0.2.0 - 2026-10-02

### New features

- Apple Silicon and Intel Mac utilities for macOS 14 or later. Apple Silicon can generate stems; both architectures can import stems and use USB, settings, waveform and game tools.
- Read the new OverCue OVPGZ003 FLAC stem pages alongside existing OVPGZ001 zlib pages. Generate and import now write FLAC pages and verify all seven playback mixes.
- Prepare selected tracks as a batch, reuse verified stems, see each track's progress, cancel safely, and retry failed tracks.
- Layered Vocals, Drums and Harmonics waveforms with a live playhead, plus a backed-up 3-band waveform preparation tool.
- Beat Arcade rhythm games and Wave Rider, a jog-wheel game driven by the loaded track's real waveform. Open them when wanted through MODS > Extras > Games.
- Games setup downloads Doom shareware, Chex Quest and Freedoom, checks the downloads, and prepares supported data on the selected USB. Bring your own licensed commercial IWAD.

### Bug fixes

- Disk-backed audio processing bounds whole-track memory instead of loading long tracks and seven mixes into RAM.
- CPU Vocal focus reduces attention memory; long FLAC source inspection uses bounded reads.
- Improved native waveform refresh and theme rendering while preserving deck transport and existing stem controls.
- Beat Arcade and Wave Rider stay closed during normal startup. The automatic game opening switch is only a local test option.

### Installation and verification

- Installing or updating the desktop app does not update an existing controller USB. Open USB loader and use Back up and update loader to install the matching runtime.
- Mac packages use an ad-hoc signature and are not notarized. Follow the Mac setup instructions before opening the downloaded utility.
- The newest native runtime remains experimental. Physical game-control feel, real-song phase alignment and extended two-deck cold-boot qualification are still pending. MyHouse stays visible as unavailable until native GZDoom support is qualified.

## 0.1.10 - 2026-10-02

### New features

- Apple Silicon and Intel Mac utilities for macOS 14 or later. Apple Silicon can generate stems; both architectures can import stems and use USB, settings, waveform and game tools.
- Read the new OverCue OVPGZ003 FLAC stem pages alongside existing OVPGZ001 zlib pages. Generate and import now write FLAC pages and verify all seven playback mixes.
- Prepare selected tracks as a batch, reuse verified stems, see each track's progress, cancel safely, and retry failed tracks.
- Layered Vocals, Drums and Harmonics waveforms with a live playhead, plus a backed-up 3-band waveform preparation tool.
- Beat Arcade rhythm games and Wave Rider, a jog-wheel game driven by the loaded track's real waveform. Open them when wanted through MODS > Extras > Games.
- Games setup downloads Doom shareware, Chex Quest and Freedoom, checks the downloads, and prepares supported data on the selected USB. Bring your own licensed commercial IWAD.

### Bug fixes

- Disk-backed audio processing bounds whole-track memory instead of loading long tracks and seven mixes into RAM.
- CPU Vocal focus reduces attention memory; long FLAC source inspection uses bounded reads.
- Improved native waveform refresh and theme rendering while preserving deck transport and existing stem controls.
- Beat Arcade and Wave Rider stay closed during normal startup. The automatic game opening switch is only a local test option.

### Installation and verification

- Installing or updating the desktop app does not update an existing controller USB. Open USB loader and use Back up and update loader to install the matching runtime.
- Mac packages use an ad-hoc signature and are not notarized. Follow the Mac setup instructions before opening the downloaded utility.
- The newest native runtime remains experimental. Physical game-control feel, real-song phase alignment and extended two-deck cold-boot qualification are still pending. MyHouse stays visible as unavailable until native GZDoom support is qualified.

## 0.1.9 - 2026-09-28

### New features

- Native player artwork for the Game Boy, Super Nintendo, Windows 95, Game Boy Color, Aqua and DMG theme families.
- Dedicated Glass panels with readable data wells and clearer focus states.
- Separate green LCD waveform areas for DMG, and theme-specific title bars for Windows 95, RPG, Aqua and DMG.

### Bug fixes

- More saturated LCARS orange and violet, connected panel rails, and corrected deck-2 title colors.
- Fixed title text contrast and antialiasing against each theme's background.
- Preserved stronger waveform and cue colors instead of washing them toward the text color.
- Reduced oversized Glass menu capsules and overpowering background gradients.
- Kept cue regions, playheads, tempo readouts and transparent artwork masks protected.

### Other changes

- After installing, use USB loader > Back up and update loader to put these changes on an existing USB. Installing the app alone does not update the controller loader.
- Theme IDs and saved selections are unchanged.

Verified: portable native renderer tests, AddressSanitizer/UndefinedBehaviorSanitizer integration, ARM build and isolated controller tests, and bounded ARM waveform benchmarks. Live visual acceptance of every new theme is still pending. The controller USB was not rewritten during development.

## 0.1.8 - 2026-09-28

### New features

- Install XZ Mods with the Windows installer, or download the optional portable ZIP.
- The app checks for updates at startup. Review the release notes, then download and install a signed update.
- Update an existing USB loader with a backup and restore option, and edit the settings saved on your USB.
- Use 24 controller themes with consistent MODS menu positions and a top-right Close button.
- Customize four backward/forward Beat Jump pairs on each of two pages under Controls. Hold Shift and tap Beat Jump for page 2.
- Page 2 is enabled by default with 16, 32, 64 and 128 beats. Labels show both beats and bars; red/orange pad shades identify the second page.
- Assign stem shortcuts through an eight-pad diagram in Controls.
- Browse the Rekordbox USB library with automatic drive selection, free space and stem status. Generate stems with Open-Unmix HQ or Vocal focus.
- Import multiple instrument stems, assign them to Drums, Vocals or Harmonics, and combine them into OverCue files for an exported track.

### Bug fixes

- Fixed track-title contrast in LCARS and Analog themes.
- Keep theme, pad and Beat Jump settings saved between controller restarts when the settings USB is writable.
- Fixed the narrow, vertically wrapped Prepare a new USB card and rewrote the app instructions.
- Fixed Open-Unmix model loading and Vocal focus processing on Windows CPUs.
- Allow exFAT music libraries in the stem browser and importer, separately from boot-loader requirements.

### Other changes

- Removed the 3-band waveform, DJ branding and VJ.Tools pages from the Windows app.

Validation: controller menu/title and settings checks, native/editor settings parity, real model runs on short test audio, grouped import and OverCue page verification. The separate standalone new-track export prototype loaded and played on the user's XDJ-XZ with working stem controls; that exporter is not yet connected to the app interface.

## Unreleased

- Add an experimental **Prepare layered waveforms** operation. It stores actual PWV7 low/mid/high data beside the tracks without modifying Rekordbox files. The new native scrolling-wave renderer draws blue outer bands, amber/brown mid bands and a white core. Existing loaders must be rebuilt or replaced to use it. The bottom overview remains stock.
- Retain the earlier recolouring operation under **Legacy RGB colour adaptation**. It is not layered rendering.

Layered-renderer verification: 92 builder tests passed with one real export-database fixture skipped. Production C renders of five real tracks, the native adapter fixture with ASan/UBSan, the existing portable UI suite and ARM ABI checks passed. Actual scrolling, zoom, cues, loops, themes and performance still require an XZ trial.

- Recommend OverCue on **Prepare stems**. Link to overcue.gg and keep **Check OverCue track**.
- Add the beta stem builder. Separation and aligned-stem import now write the open OverCue format (`overcue-index/1`) under `CDJMODS` instead of a legacy `stemd-cache/1` cache.
- Take the USB from the chosen track. The track must be a 44.1 kHz stereo WAV or FLAC file that Rekordbox already exported to `Contents`. The separate output folder field is gone.
- Leave stems OverCue made unchanged. Report when matching stems were reused or earlier XZ Mods stems were replaced.
- Add **3-band waveforms**. It recolours the waveforms Rekordbox exported to the USB with CDJ-3000 style 3-band colours. Cues and music are unchanged. **Restore Rekordbox colours** puts back the originals kept in `CDJMODS/waveform-rgb-originals`.
- Rework Super Nintendo, Windows 95, Game Boy Color and Aqua / iTunes. Keep the original Game Boy green palette and fix lowercase, baseline and narrow-glyph rendering. Latin-1 accented letters use readable base letters in the pixel font.
- Add light/dark pairs for all seven styled families, including a second Game Boy DMG shell and Liquid Glass. The Appearance page keeps every style visible with separate Light and Dark buttons. Existing saved theme IDs retain their meaning.
- Correct Aqua selection contrast, reduce its brushed-metal noise, darken Graphite highlights, and keep the full Game Boy Color label visible. Windows 95 Dark uses a readable purple selected state.
- Reject empty waveform analysis sections per track, bound colour-cache memory and check cancellation between tracks.

Offline verification: 88 builder tests passed with one real export-database fixture skipped; all 21 theme renders and 441 theme selection combinations passed. Native surface, runtime-control and skin integration tests passed, and the ARM runtime passed ABI checks. The frozen backend applied and restored five copied real tracks byte-for-byte and preserved later cue edits in a separate fixture. No new theme or waveform was loaded on the XZ. This is single-colour-per-column adaptation, not native layered 3-band rendering; hardware appearance and overview interpretation remain unverified.

Beta stems pass file verification only. Playback alignment on the XZ has not been tested on hardware.

## 0.1.7 - 2026-09-28 (local build)

### New features

- Browse tracks from the Rekordbox USB database with automatic drive selection, free space and stem status.
- Generate stems from the track list using Open-Unmix HQ or Vocal focus.
- Import multiple instrument stems, assign them to drums, vocals or harmonics, and combine them into OverCue files.
- Check for app updates from the Builder.
- Download and install updates through the Windows installer. A portable ZIP is also available.
- Read new features and fixes before updating. The app opens the release notes after an update.

### Bug fixes

- Finished jobs now release their files before another operation or app update starts.
- Fixed the narrow, vertically wrapped text in Prepare a new USB.
- Rewrote app instructions and simplified the About page.
- Fixed Open-Unmix HQ failing to load its model files.
- Fixed Vocal focus failing on Windows CPUs.
- Read and prepare stems on exFAT music USBs without applying the boot-loader filesystem restriction.

### Other changes

- Removed the 3-band waveform tools while they are being corrected.

Verified: builder and updater tests, actual USB library reading, both model pipelines on a short synthetic track, and grouped imports with all seven OverCue roles checked. Standalone new-track export is a separate offline prototype, pending XDJ-XZ playback testing. No public update feed was published.

## 0.1.6 - 2026-09-28 (local build)

- Update an existing USB loader with verified backup, changed-file checks and
  restore. The original prepare-new flow still refuses to overwrite a loader.
- Edit the complete current USB settings format, including 24 themes, stem
  shortcuts, two beat-jump pages and stem-row visibility.
- Remove the Windows DJ branding and VJ.Tools pages and simplify the copy.
- Put Close at the top right of MODS. Place Stem Shortcuts and Beat Jump in
  separate Controls views with eight-pad diagrams.
- Restore title contrast for LCARS untagged tracks and Analog title panels.
  Keep menu positions consistent across themes and retain persistent USB settings.

Verified: native/editor settings parity, safe-update failure cases, all-theme
layout/input tests, native contrast regression, ARM load tests, and the actual
Windows app update/backup/restore/settings flow using real encrypted loaders
in a disposable NTFS folder. Physical FAT USB replacement and a fresh boot of
this new loader are not claimed. Controller menu/title changes are in a RAM
trial; the loaded-title visual check is recorded separately in task evidence.
No public release was published by this build.

## 0.1.5 - 2026-09-25

- Add **Choose USB and prepare loader**: one selection downloads the official XDJ-XZ 1.26 firmware, prepares boot support from Pioneer’s published source, builds the mod image and verifies its contents before writing `autoexec.bin` to the FAT/FAT32 USB root.
- Pin downloaded inputs by size and SHA-256. Cache verified downloads for later builds. Keep existing loader files and music untouched; reject an unsuitable destination before downloading.
- Keep local firmware and staging builds in Advanced options. Ship the Deflate64 decoder with its LGPL notice and source, while excluding firmware, boot files and generated images from the public download.

The automatic path passed the actual published downloads, boot-support extraction and a complete encrypted-image round trip in a disposable folder. No USB drive was mounted during the release build checks. After publication, the user prepared a USB with 0.1.5 and cold-booted the XDJ-XZ with MODS running. This confirms the loader path on that setup; extended two-deck and experimental controls remain separate acceptance work.

## 0.1.4 - 2026-09-25

- Fix USB startup stopping before MODS appeared: the artwork checksum list now uses POSIX line endings when built on Windows.
- Keep the tested 0.1.3 audio runtime, stem controls, themes and pad colors.
- Add a regression check for every generated artwork checksum entry. The generated files also pass Linux `md5sum -c`.

Rebuild any USB made with 0.1.3 using this version. The failure left the normal player running; it did not install the MODS runtime. Final hardware boot acceptance is recorded separately.

## 0.1.3 - 2026-09-25

- Ship the tested runtime and matching display bridge in the Windows builder, shared with the VJ.Tools USB loader.
- Include the XZ Mods splash and loading banner automatically. The loader applies the original artwork to a validated GUI copy in RAM.

- Document the current toolkit's two 48-pixel stem rows, tap-to-mute and horizontal volume gestures.
- Document the streamlined Controls, Appearance, VJ.Tools and Advanced pages and five added display themes.
- Correct physical pad brightness and cover both pad banks in the toolkit. Use native primary colors independently of display themes.
- Add actual XZ screenshots, a version-specific user guide and links to CDJ3K-Mods, OverCue, XDJ-RX3 Toolkit and XDJ-AZ Mods.

Split rows, clean playback, bright pads and the final primary pad colors were confirmed in RAM trials. Both packaged builders pass image round-trip checks and contain the same runtime, SHA256 `218aca8d00d354d67e780e061d3cc0727eb5a82f530abf0534c719018316c134`. The automatic RAM artwork patch changes only the two permitted image regions. Full theme coverage and cold-boot/two-deck qualification remain pending; this remains a developer preview.

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
