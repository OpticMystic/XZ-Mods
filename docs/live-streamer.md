# View the XZ screen on your computer

Live Streamer is in the XZ Mods sidebar. It displays the controller's screen and gives OBS a local Browser Source URL.

1. Update the XZME loader with this build. With both decks stopped, safely eject the USB, put it back in the XZ and power cycle the controller.
2. Connect the computer and XZ to the same trusted network. Open **Live Streamer → Set up Live Streamer**. The app looks for the XZ automatically; select its address if more than one is found, then connect.
3. The picture appears after the first frame. **Fullscreen** expands the view. **Copy OBS URL** provides the address for an OBS Browser Source, sized 800 × 480.

This is a screen preview at about one frame per second, without audio. The computer and OBS share one controller capture. When the connection fails, stale images are hidden and the app retries automatically. **Stop stream** closes the preview server; a new session gets a new local URL.

If the XZ is reachable but its screen service is unavailable, the page offers **Update the USB loader**. The app update and the USB loader update are separate operations.

# USB stem status

Prepared USB stems load automatically with the track. There is no per-track Detect → Enable → Verify setup routine. A saved Off setting remains Off; **Turn on** enables USB stems directly for all decks.

The deck row distinguishes missing tracks, loading, automatic alignment, missing preparation and errors. **Play to align** means the prepared files were found and the running audio needs a few seconds to establish alignment. It does not mean the files are missing. Error details identify the affected deck and retain the existing stem files.

USB stems play on the controller. PC stems control rekordbox PERFORMANCE mode through its USB connection; they have separate setup and readiness.

This build reads `OVPGZ001` zlib, `OVPGZ003` 96 kHz FLAC and `OVPGZ004` 44.1 kHz FLAC stem sets. Existing files do not need to be regenerated to use the added format support.

# Development verification

The read-only observer uses TCP 50010; discovery uses the existing UDP 50006 receiver protocol. No shell connection is required. The screen worker does not enter audio or drawing callbacks.

`tools/capture_xz_screen.py --host <XZ address> --output <new evidence folder> --frames 3` captures frames and the runtime's stem readiness snapshot. Add `--expect-prepared-deck 1` to fail if the loaded first deck has no active prepared set. The tool sends only observation requests and never changes controller state.

Controller acceptance is separate from host decoder, protocol, menu and installed-app tests. The shared viewer implementation also serves the Mac app; macOS packaging must be checked on a Mac.
