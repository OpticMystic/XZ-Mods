# Unused-page repair verification

Verified locally on 2026-10-14. Source commit: `fa22fb3`.

Windows rekordbox 7.2.17 rejected XZME with “Device library is corrupted. Do you want to delete the device library?” Windows filesystem repair changed the volume's health status but left the catalog files byte-for-byte unchanged. All active Device Library chains were readable. Pages 95, 104 and 105 were reserved for future use and contained malformed data.

The guarded repair cleared only those three pages. Every active page, the file length, trailing bytes, companion catalogs and stem mappings were preserved. The owner confirmed that rekordbox accepted the repaired USB. The origin of the garbage has not been established.

The integrated repair produces exactly the successful candidate bytes from the saved original library. It rejects ambiguous allocation maps, overlapping page ownership and pages that might contain database records. Automatic repair of combined history-chain and unused-page damage remains unsupported.

## Checks

- Source: 46 tests passed, one platform-specific test skipped. This includes 22 repair tests.
- Frozen source: 995 recorded file hashes verified against the paired runtime.
- Packaged backend: six checks for unused-page repair and six for history repair, including backup, stale-state rejection, journal guards and exact undo.
- Installer: all 2,032 resource hashes match the frozen package. The installed executable matches the extracted installer executable.
- Installed UI: 15 checks on a private copy of the original XZME library. Repair produced the accepted candidate exactly; counts remained 301 tracks, one playlist and 41 stem sets. Companion files and journals were preserved and backed up. Undo restored the exact original damaged database.

The installed UI test did not write to the physical USB. The owner removed XZME after confirming the earlier guarded repair. These checks do not qualify controller playback, sync or jog behavior.

## Local artifacts

- Installed application: `C:\Users\short\AppData\Local\XZ Mods\xz-mods-builder.exe`
- Executable SHA-256: `ed9719ce188c4e09fba3028fa07011a00b9bce4e2e2627d42d978c1bab4141e9`
- Installer SHA-256: `5596fafb3b25540ce391764dcc32750f47c1d6d56947adf5cb08175841465bd0`
- Evidence and original backup: `E:\Codex\xz-library-repair-20261014`
- Installed UI evidence: `installed-repair-ui.json`, `installed-repair-plan.png`, `installed-repair-result.png`
- Physical repair backup: `candidate-backup-state/library-repair-backups/6f17e7295baf4f22adde81c7415a8575`

This is a local installation of preview.7. No public repository push, release or update feed was published.
