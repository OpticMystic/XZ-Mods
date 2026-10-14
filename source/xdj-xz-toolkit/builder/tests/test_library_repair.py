"""Synthetic broken history chains, guarded repair and exact undo."""
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from builder import library_repair as repair
from builder.jobs import Job, Cancelled
from builder.tests import pdb_fixture as fixture


def broken_library(root):
    path = fixture.write_export(root / repair.PDB, {7: '/Contents/test.wav'})
    data = bytearray(path.read_bytes())
    data.extend(bytes(4096 * 6))
    struct.pack_into('<I', data, 8, 2)
    struct.pack_into('<I', data, 12, 10)
    struct.pack_into('<I', data, 32, 5)
    struct.pack_into('<I', data, 2 * 4096 + 12, 5)
    struct.pack_into('<4I', data, 44, 11, 9, 3, 8)
    for n, nxt, index in ((3, 4, True), (4, 6, False), (8, 9, False)):
        row = struct.pack('<I', n) + fixture._string('HISTORY 001')
        page = bytearray(fixture._index_page(n, nxt) if index else fixture._data_page(n, nxt, [row]))
        struct.pack_into('<I', page, 8, 11)
        struct.pack_into('<I', page, 16, n)
        data[n * 4096:(n + 1) * 4096] = page
    path.write_bytes(data)
    (root / 'CDJMODS').mkdir()
    (root / 'CDJMODS/index.json').write_text(json.dumps({'tracks': {'7': {'file_path': '/Contents/test.wav'}}}))
    (root / 'CDJMODS/expensive-stems.pgz').write_bytes(b'keep generated audio')
    (root / 'Contents').mkdir()
    (root / 'Contents/test.wav').write_bytes(b'keep original audio')
    (root / 'PIONEER/rekordbox/exportExt.pdb').write_bytes(b'preserve opaque companion')
    return path


def unused_page_damage(path):
    fixture.write_export(path, {7: '/Contents/test.wav'})
    data = bytearray(path.read_bytes())
    unused = len(data) // 4096
    struct.pack_into('<I', data, 12, unused + 1)
    data.extend(bytes(range(256)) * 16)
    data.extend(b'tail' * 1024)
    path.write_bytes(data)
    return unused


class LibraryRepairTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'usb'
        self.root.mkdir()
        self.backups = Path(self.temp.name) / 'backups'
        self.pdb = broken_library(self.root)
        self.original = self.pdb.read_bytes()
        self.addCleanup(patch.stopall)
        patch.object(repair, '_backup_root', return_value=self.backups).start()
        patch.object(repair, 'rekordbox_running', return_value=False).start()
        patch.object(repair, '_check_backup_volume').start()

    def request(self):
        check = repair.inspect(self.root)
        return {'volume': str(self.root), 'expected_identity': check['identity'],
                'expected_files': check['expected_files'],
                'repair_id': check['repair']['id'] if check['repair'] else repair.REPAIR_ID}

    def unused_page_damage(self):
        unused = unused_page_damage(self.pdb)
        self.original = self.pdb.read_bytes()
        return unused

    def test_unused_page_repair_preserves_all_active_pages_and_tail(self):
        unused = self.unused_page_damage()
        check = repair.inspect(self.root)
        self.assertEqual(check['verdict'], 'repairable')
        self.assertEqual(check['repair']['id'], 'unused-page-garbage-v1')
        result = repair.mutate(self.request(), Job())
        expected = bytearray(self.original)
        expected[unused * 4096:(unused + 1) * 4096] = bytes(4096)
        self.assertEqual(self.pdb.read_bytes(), expected)
        self.assertEqual(result['before'], result['after'])
        self.assertEqual(result['inspection']['verdict'], 'healthy')
        self.assertIn('Unused', result['summary'])
        self.assertEqual((Path(result['backup']['folder']) / repair.PDB).read_bytes(), self.original)
        request = self.request()
        request['backup_id'] = result['backup']['id']
        repair.mutate(request, Job(), restore=True)
        self.assertEqual(self.pdb.read_bytes(), self.original)

    def test_zero_reserved_page_does_not_need_repair(self):
        unused = self.unused_page_damage()
        data = bytearray(self.original)
        data[unused * 4096:(unused + 1) * 4096] = bytes(4096)
        self.pdb.write_bytes(data)
        self.assertEqual(repair.inspect(self.root)['verdict'], 'healthy')

    def test_reserved_page_with_recognizable_database_content_is_not_cleared(self):
        unused = self.unused_page_damage()
        data = bytearray(self.original)
        data[unused * 4096:(unused + 1) * 4096] = fixture._index_page(unused, unused + 1)
        self.pdb.write_bytes(data)
        self.assertEqual(repair.inspect(self.root)['verdict'], 'not_repairable')
        self.assertIsNone(repair.inspect(self.root)['repair'])

    def test_reserved_page_overlapping_active_data_is_not_cleared(self):
        self.unused_page_damage()
        data = bytearray(self.original)
        struct.pack_into('<I', data, 32, 2)
        self.pdb.write_bytes(data)
        self.assertEqual(repair.inspect(self.root)['verdict'], 'not_repairable')

    def test_unused_page_repair_rejects_changed_plan(self):
        self.unused_page_damage()
        request = self.request()
        request['repair_id'] = repair.REPAIR_ID
        with self.assertRaisesRegex(ValueError, 'No supported repair'):
            repair.mutate(request, Job())
        self.assertEqual(self.pdb.read_bytes(), self.original)

    def test_unused_page_repair_requires_complete_allocation_map(self):
        self.unused_page_damage()
        data = bytearray(self.original)
        struct.pack_into('<I', data, 12, 5)
        self.pdb.write_bytes(data)
        check = repair.inspect(self.root)
        self.assertEqual(check['verdict'], 'not_repairable')
        self.assertIsNone(check['repair'])

    def test_minimal_repair_preserves_rows_and_all_other_files(self):
        before = {p.relative_to(self.root): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        check = repair.inspect(self.root)
        self.assertEqual(check['verdict'], 'repairable')
        self.assertEqual(check['counts'], {'tracks': 1, 'playlists': 0, 'stem_sets': 1})
        result = repair.mutate(self.request(), Job())
        self.assertEqual(result['inspection']['verdict'], 'healthy')
        self.assertEqual(result['before'], result['after'])
        changed = [i for i, pair in enumerate(zip(self.original, self.pdb.read_bytes())) if pair[0] != pair[1]]
        self.assertEqual(changed, [4 * 4096 + 12])
        for name, data in before.items():
            if str(name).replace('\\', '/') != repair.PDB:
                self.assertEqual((self.root / name).read_bytes(), data)
        self.assertEqual((Path(result['backup']['folder']) / repair.PDB).read_bytes(), self.original)
        self.assertFalse(result['rekordbox_verified'])

    def test_undo_restores_exact_bytes_and_backs_up_repaired_version(self):
        result = repair.mutate(self.request(), Job())
        repaired = self.pdb.read_bytes()
        undo = self.request()
        undo['backup_id'] = result['backup']['id']
        restored = repair.mutate(undo, Job(), restore=True)
        self.assertEqual(self.pdb.read_bytes(), self.original)
        self.assertEqual((Path(restored['backup']['folder']) / repair.PDB).read_bytes(), repaired)
        self.assertEqual(restored['inspection']['verdict'], 'repairable')

    def test_changed_stem_index_blocks_write(self):
        request = self.request()
        (self.root / 'CDJMODS/index.json').write_text('{"tracks":{}}')
        with self.assertRaisesRegex(ValueError, 'changed since inspection'):
            repair.mutate(request, Job())
        self.assertEqual(self.pdb.read_bytes(), self.original)

    def test_onelibrary_sidecars_are_backed_up_without_changes(self):
        records = {
            'exportLibrary.db': b'encrypted main database',
            'exportLibrary.db-wal': b'committed tracks not yet in main database',
            'exportLibrary.db-shm': b'journal coordination state',
            'exportLibrary.db-journal': b'rollback journal',
        }
        for name, data in records.items():
            (self.pdb.parent / name).write_bytes(data)
        result = repair.mutate(self.request(), Job())
        folder = Path(result['backup']['folder']) / 'PIONEER/rekordbox'
        for name, data in records.items():
            with self.subTest(name=name):
                self.assertEqual((folder / name).read_bytes(), data)
                self.assertEqual((self.pdb.parent / name).read_bytes(), data)

    def test_changed_or_new_onelibrary_sidecar_blocks_write(self):
        for name in ('exportLibrary.db-wal', 'exportLibrary.db-shm', 'exportLibrary.db-journal'):
            for already_exists in (False, True):
                with self.subTest(name=name, already_exists=already_exists):
                    self.pdb.write_bytes(self.original)
                    path = self.pdb.parent / name
                    path.unlink(missing_ok=True)
                    if already_exists:
                        path.write_bytes(b'previous journal')
                    request = self.request()
                    path.write_bytes(b'new journal state')
                    with self.assertRaisesRegex(ValueError, 'changed since inspection'):
                        repair.mutate(request, Job())
                    self.assertEqual(self.pdb.read_bytes(), self.original)
                    path.unlink()

    def test_sidecar_change_after_repair_blocks_undo(self):
        result = repair.mutate(self.request(), Job())
        repaired = self.pdb.read_bytes()
        (self.pdb.parent / 'exportLibrary.db-wal').write_bytes(b'new committed tracks')
        self.assertFalse(repair.inspect(self.root)['backups'][0]['restorable'])
        request = self.request()
        request['backup_id'] = result['backup']['id']
        with self.assertRaisesRegex(ValueError, 'cannot undo'):
            repair.mutate(request, Job(), restore=True)
        self.assertEqual(self.pdb.read_bytes(), repaired)

    def test_reconnected_usb_blocks_write(self):
        request = self.request()
        request['expected_identity']['serial'] = 'other-device'
        with self.assertRaisesRegex(ValueError, 'reconnected'):
            repair.mutate(request, Job())
        self.assertEqual(self.pdb.read_bytes(), self.original)

    def test_rekordbox_open_blocks_write(self):
        request = self.request()
        with patch.object(repair, 'rekordbox_running', return_value=True):
            with self.assertRaisesRegex(ValueError, 'Close Rekordbox'):
                repair.mutate(request, Job())
        self.assertEqual(self.pdb.read_bytes(), self.original)

    def test_partial_page_or_non_history_damage_is_not_repaired(self):
        for damaged in (self.original[:-1], bytes(4096) + self.original[4096:]):
            self.pdb.write_bytes(damaged)
            self.assertEqual(repair.inspect(self.root)['verdict'], 'not_repairable')
        damaged = bytearray(self.original)
        damaged[2 * 4096:3 * 4096] = bytes(4096)
        self.pdb.write_bytes(damaged)
        self.assertEqual(repair.inspect(self.root)['verdict'], 'not_repairable')

    def test_ambiguous_order_or_nonzero_gap_is_not_repaired(self):
        for at, value in ((8 * 4096 + 16, 2), (6 * 4096, 1), (4 * 4096 + 12, 7)):
            damaged = bytearray(self.original)
            struct.pack_into('<I', damaged, at, value)
            self.pdb.write_bytes(damaged)
            self.assertEqual(repair.inspect(self.root)['verdict'], 'not_repairable')

    def test_replace_failure_leaves_original_and_verified_backup(self):
        real_replace = repair.os.replace
        def fail_destination(src, dst):
            if Path(dst) == self.pdb:
                raise OSError('injected replacement failure')
            return real_replace(src, dst)
        with patch.object(repair.os, 'replace', side_effect=fail_destination):
            with self.assertRaisesRegex(OSError, 'injected'):
                repair.mutate(self.request(), Job())
        self.assertEqual(self.pdb.read_bytes(), self.original)
        receipts = list(self.backups.glob('*/receipt.json'))
        self.assertEqual(len(receipts), 1)
        self.assertEqual((receipts[0].parent / repair.PDB).read_bytes(), self.original)
        self.assertEqual(list(self.pdb.parent.glob('.xz-library-*')), [])

    def test_cancel_after_backup_never_changes_usb(self):
        class CancelBeforeWrite(Job):
            def progress(self, stage, message, **kwargs):
                if stage == 'verify':
                    raise Cancelled()
        with self.assertRaises(Cancelled):
            repair.mutate(self.request(), CancelBeforeWrite())
        self.assertEqual(self.pdb.read_bytes(), self.original)

    def test_corrupt_backup_cannot_be_restored(self):
        result = repair.mutate(self.request(), Job())
        repaired = self.pdb.read_bytes()
        (Path(result['backup']['folder']) / repair.PDB).write_bytes(b'bad backup')
        request = self.request()
        request['backup_id'] = result['backup']['id']
        with self.assertRaisesRegex(ValueError, 'integrity'):
            repair.mutate(request, Job(), restore=True)
        self.assertEqual(self.pdb.read_bytes(), repaired)

    def test_repeated_repair_is_refused_without_changing_file(self):
        repair.mutate(self.request(), Job())
        repaired = self.pdb.read_bytes()
        with self.assertRaisesRegex(ValueError, 'No supported repair'):
            repair.mutate(self.request(), Job())
        self.assertEqual(self.pdb.read_bytes(), repaired)

    def test_invalid_lock_is_not_opened_or_modified(self):
        lock = self.root / '.xzmods-write.lock'
        lock.write_bytes(b'not a lock')
        with self.assertRaisesRegex(ValueError, 'Invalid USB write lock'):
            repair.mutate(self.request(), Job())
        self.assertEqual(lock.read_bytes(), b'not a lock')
        self.assertEqual(self.pdb.read_bytes(), self.original)

    def test_interrupted_final_receipt_still_allows_exact_undo(self):
        real_receipt = repair.managed_usb._receipt
        def fail_final_receipt(folder, record):
            if record['state'] == 'committed':
                raise OSError('receipt interrupted')
            return real_receipt(folder, record)
        with patch.object(repair.managed_usb, '_receipt', side_effect=fail_final_receipt):
            with self.assertRaisesRegex(RuntimeError, 'Recovery backup:'):
                repair.mutate(self.request(), Job())
        inspection = repair.inspect(self.root)
        self.assertEqual(inspection['verdict'], 'healthy')
        self.assertTrue(inspection['backups'][0]['restorable'])
        request = self.request()
        request['backup_id'] = inspection['backups'][0]['id']
        repair.mutate(request, Job(), restore=True)
        self.assertEqual(self.pdb.read_bytes(), self.original)


if __name__ == '__main__':
    unittest.main()
