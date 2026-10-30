# Changelog

What changed in each version of XZ Mods for the XDJ-XZ, newest first. XZ Mods is independent and experimental:
test each build with your own tracks before a set, and keep a known working USB.

## 0.2.1 - 2026-10-02

- XZ Mods 0.2.1 is the hotfix for 0.2.0. Use it for new loaders and loader updates.
- Includes the 0.2.0 feature set: FLAC stems, selected-track batches, layered waveforms, Beat Arcade, Wave Rider and game downloads.
- Corrects the old 0.1.9 labels in the bundled app so its version is correct before the startup check finishes.
- Identifies the hotfix consistently in the app, backend, signed updater feed and website.
- The files XZ Mods puts on the USB for the player are the same verified files shipped with 0.2.0. This hotfix updates the desktop app and version labels.
- Installing this app does not change your USB. Prepare or update its loader separately when ready.
- A portable folder is not replaced by the Windows installer. After installing, open XZ Mods from the desktop shortcut; to stay portable, replace the folder with the current ZIP.
- Mac previews remain ad-hoc signed and not notarized. My House still needs a game engine that does not run on the XZ.

## 0.2.0 - 2026-10-02

- Apple Silicon and Intel Mac utilities for macOS 14 or later. Apple Silicon can generate stems; both architectures can import stems and use USB, settings, waveform and game tools.
- Play stems in the current OverCue FLAC format as well as the older format. Generate and Import now write FLAC stems and check all seven playback mixes.
- Prepare selected tracks as a batch, reuse verified stems, see each track's progress, cancel safely, and retry failed tracks.
- Layered Vocals, Drums and Harmonics waveforms with a live playhead, plus a 3-band waveform preparation tool with a backup.
- Beat Arcade rhythm games and Wave Rider, a jog-wheel game driven by the loaded track's real waveform. Open them when wanted through MODS > Extras > Games.
- Games setup downloads Doom shareware, Chex Quest and Freedoom from their own distributions, checks the downloads, and prepares supported data on the selected USB. Bring your own licensed commercial IWAD.
- Long tracks are prepared through files on disk, so the app no longer holds whole tracks and seven mixes in memory.
- Vocal focus uses less memory, and long FLAC sources are read in parts.
- Waveforms refresh more reliably and themes draw better, with deck transport and stem controls unchanged.
- Beat Arcade and Wave Rider stay closed during normal startup. The automatic game opening switch is only a local test option.
- Installing or updating the desktop app does not update an existing USB. Open USB loader and use Back up and update loader.
- Mac packages use an ad-hoc signature and are not notarized. Follow the Mac setup instructions before opening the downloaded utility.
- The newest XZ Mods version on the player remains experimental. Game controls, phase alignment on real songs and long two-deck sessions are still being tested. My House stays listed as unavailable.

## 0.1.10 - 2026-10-02

- Apple Silicon and Intel Mac utilities for macOS 14 or later. Apple Silicon can generate stems; both architectures can import stems and use USB, settings, waveform and game tools.
- Play stems in the current OverCue FLAC format as well as the older format. Generate and Import now write FLAC stems and check all seven playback mixes.
- Prepare selected tracks as a batch, reuse verified stems, see each track's progress, cancel safely, and retry failed tracks.
- Layered Vocals, Drums and Harmonics waveforms with a live playhead, plus a 3-band waveform preparation tool with a backup.
- Beat Arcade rhythm games and Wave Rider, a jog-wheel game driven by the loaded track's real waveform. Open them when wanted through MODS > Extras > Games.
- Games setup downloads Doom shareware, Chex Quest and Freedoom from their own distributions, checks the downloads, and prepares supported data on the selected USB. Bring your own licensed commercial IWAD.
- Long tracks are prepared through files on disk, so the app no longer holds whole tracks and seven mixes in memory.
- Vocal focus uses less memory, and long FLAC sources are read in parts.
- Waveforms refresh more reliably and themes draw better, with deck transport and stem controls unchanged.
- Beat Arcade and Wave Rider stay closed during normal startup. The automatic game opening switch is only a local test option.
- Installing or updating the desktop app does not update an existing USB. Open USB loader and use Back up and update loader.
- Mac packages use an ad-hoc signature and are not notarized. Follow the Mac setup instructions before opening the downloaded utility.
- The newest XZ Mods version on the player remains experimental. Game controls, phase alignment on real songs and long two-deck sessions are still being tested. My House stays listed as unavailable.

## 0.1.9 - 2026-09-28

- Play-screen artwork for the Game Boy, Super Nintendo, Windows 95, Game Boy Color, Aqua and DMG theme families.
- Dedicated Glass panels with readable data wells and clearer focus states.
- Separate green LCD waveform areas for DMG, and theme-specific title bars for Windows 95, RPG, Aqua and DMG.
- More saturated LCARS orange and violet, connected panel rails, and corrected deck-2 title colors.
- Fixed title text contrast and antialiasing against each theme's background.
- Preserved stronger waveform and cue colors instead of washing them toward the text color.
- Reduced oversized Glass menu capsules and overpowering background gradients.
- Kept cue regions, playheads, tempo readouts and transparent artwork masks protected.
- After installing, use USB loader > Back up and update loader to put these changes on an existing USB. Installing the app alone does not update the USB.
- Theme IDs and saved selections are unchanged.

## 0.1.8 - 2026-09-28

- Install XZ Mods with the Windows installer, or download the optional portable ZIP.
- The app checks for updates at startup. Review the release notes, then download and install a signed update.
- Update an existing USB loader with a backup and restore option, and edit the settings saved on your USB.
- Use 24 themes with consistent MODS menu positions and a top-right Close button.
- Customize four backward/forward Beat Jump pairs on each of two pages under Controls. Hold Shift and tap Beat Jump for page 2.
- Page 2 is enabled by default with 16, 32, 64 and 128 beats. Labels show both beats and bars; red/orange pad shades identify the second page.
- Assign stem shortcuts through an eight-pad diagram in Controls.
- Browse your exported USB library with automatic drive selection, free space and stem status. Generate stems with Open-Unmix HQ or Vocal focus.
- Import multiple instrument stems, assign them to Drums, Vocals or Harmonics, and combine them into OverCue files for an exported track.
- Fixed track-title contrast in LCARS and Analog themes.
- Keep theme, pad and Beat Jump settings saved between restarts when the settings USB is writable.
- Fixed the narrow, vertically wrapped Prepare a new USB card and rewrote the app instructions.
- Fixed Open-Unmix model loading and Vocal focus processing on Windows CPUs.
- Allow exFAT music libraries in the stem browser and importer, separately from the loader USB requirements.
- Removed the 3-band waveform, DJ branding and VJ.Tools pages from the Windows app.

## 0.1.7 - 2026-09-28

- Browse tracks from your exported USB library with automatic drive selection, free space and stem status.
- Generate stems from the track list using Open-Unmix HQ or Vocal focus.
- Import multiple instrument stems, assign them to drums, vocals or harmonics, and combine them into OverCue files.
- Check for app updates from the Builder.
- Download and install updates through the Windows installer. A portable ZIP is also available.
- Read new features and fixes before updating. The app opens the release notes after an update.
- Finished jobs now release their files before another operation or app update starts.
- Fixed the narrow, vertically wrapped text in Prepare a new USB.
- Rewrote app instructions and simplified the About page.
- Fixed Open-Unmix HQ failing to load its model files.
- Fixed Vocal focus failing on Windows CPUs.
- Read and prepare stems on exFAT music USBs, which the loader USB itself does not allow.
- Removed the 3-band waveform tools while they are being corrected.

## 0.1.6 - 2026-09-28

- Update an existing USB loader with a verified backup and restore option.
- Edit the themes, stem shortcuts and beat-jump sizes saved on a USB.
- Use separate eight-pad setup views for Stem Shortcuts and Beat Jump in Controls.
- Restore readable track titles in LCARS and Analog themes.
- Save theme, pad-bank and stem-row settings between reloads.
- Keep beat-jump page 2 enabled by default and distinguish its pads with red/orange shades.
- Move the MODS Close button to the top-right corner.
- Remove DJ branding and VJ.Tools pages from the Windows Builder and simplify its wording.

## 0.1.5 - 2026-09-25

Prepare the loader with one USB selection.

- Add **Choose USB and prepare loader**. Choose a suitable FAT/FAT32 USB and the app gets and checks the files it needs, then builds the loader and writes `autoexec.bin` to the USB root. You no longer need to collect the files yourself.
- Unsuitable drives are rejected before downloading. Music stays in place, and preparation refuses to replace an existing loader.
- Checked files are kept for later builds. Local-file and staging-folder options remain in Advanced.

The owner prepared a USB with this version and started the XDJ-XZ with MODS running. Long performances and experimental controls are tested separately.

## 0.1.4 - 2026-09-25

USB startup fix.

- A line-ending fix in the loader files lets startup continue to MODS.
- The audio, stem controls, themes and pad colors from 0.1.3 stay the same.

USB loaders made with 0.1.3 need rebuilding. The fix is also in 0.1.5 and later.

## 0.1.3 - 2026-09-25

See both decks, with clearer stem controls.

- Two compact stem rows sit below the waveforms. Tap to mute and drag horizontally to adjust volume.
- Both pad banks get corrected brightness and primary stem colors that stay independent of your screen theme.
- The standalone builder and the VJ.Tools Library use the same checked files.
- Real XZ screenshots and an illustrated user guide explain the controls and preparation.

Stem rows, clean playback and pad colors were checked on the player. The USB startup issue in this version was fixed in 0.1.4.

## 0.1.2 - 2026-09-23

Choose your stem pads and keep the other bank's hot cues.

- Choose A-D or E-H. The saved pad-bank option puts Vocals, Harmonics, Drums and Bypass on your chosen bank. The inactive bank keeps its normal hot cues.
- Screen controls, stem waveforms and pad colors use the same order, with brighter default colors.
- The VJ.Tools corner button appears only with a live, enabled connection. Touches stay with the view on screen.

## 0.1.1 - 2026-09-23

Play prepared OverCue stems on the XZ. Developer preview for XDJ-XZ firmware 1.26.

- XZ Mods reads OverCue stem bundles beside the matching exported track. **Check OverCue track** checks the original and all seven prepared mixes without changing the USB.
- Touch and pad mute state agree, both deck waveforms stay visible, and hiding the stem rows leaves the audio settings as they are.
- Background streaming and prepared combinations reduce decoder work and stop a loop wrap from bringing back a muted stem.

A checked file does not prove audible alignment or long two-deck sessions.
