# Repair a Device Library history chain

Open **Repair library** in XZ Mods, then select the USB and choose **Check library**.
Checking does not write to the USB. The same page is available from **Prepare stems**.

If XZ Mods offers a repair:

1. Close Rekordbox. Use **Prepare for repair** to check that it has closed.
2. Review the proposed change, then choose **Back up and repair**.
3. Reopen Rekordbox and check the USB's tracks and playlists. Test playback on the player before using it in a set.

The supported repair reconnects intact playback-history pages across specific blank-page gaps in `export.pdb`. It changes page links only. Track IDs, track records, playlists, prepared stems, stem mappings, analysis files and the loader stay unchanged. Entries already missing from blank history pages cannot be recovered by this operation.

XZ Mods backs up the device catalogs and stem index to the computer before replacement. The result shows the backup folder. **Undo this repair** restores the exact original Device Library database, including its damage. Undo is available only while the catalog hashes still match the repaired state. Rekordbox can change these files when it opens the USB, so keep the backup even if Undo is no longer offered.

The backup and change checks include OneLibrary's `exportLibrary.db-wal`, `exportLibrary.db-shm`, and `exportLibrary.db-journal` files when present. The WAL can contain committed tracks that are not yet in the main database. History repair preserves all these files byte for byte. A journal appearing, disappearing, or changing invalidates the inspection and blocks repair or undo. Older receipts without journal fingerprints remain on disk but do not qualify for automatic undo.

For other damage, choose **Save report**. This tool does not rebuild arbitrary damaged tables or validate the encrypted OneLibrary database. A passing structural check does not establish Rekordbox or player acceptance.

## Development verification

Run source tests from `source/xdj-xz-toolkit`:

```powershell
python -m unittest builder.tests.test_library_repair builder.tests.test_rekordbox_pdb builder.tests.test_stem_library builder.tests.test_stem_batch
```

Close Rekordbox before checking the packaged backend. Choose a fixture parent on a different disk from the computer's temporary directory. The verifier creates and removes its own synthetic media directory. It never selects a real USB.

```powershell
python tools/verify_library_repair.py --resources resources --fixture-parent E:/CodexTemp --evidence E:/CodexTemp/library-repair-check.json
```

The packaged check covers detection, repair, preserved files, verified backup, rejection of a stale inspection, exact undo and report creation. The source tests also cover process and device guards, unsupported damage, cancellation and interrupted publication.
