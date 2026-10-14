"""Conservative Device Library history-chain repair, with local backups.

The supported failure has intact, ordered history pages separated by zeroed
pages. Only next-page pointers change. This cannot recover the missing rows,
repair arbitrary PDB damage, or validate the encrypted OneLibrary database.
Page layout: https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/exports.html
"""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import uuid

from . import cache, managed_usb, rekordbox_pdb as pdb
from .engines import data_root
from .jobs import exclusive_lock

PDB = 'PIONEER/rekordbox/export.pdb'
GUARDED = (PDB, 'PIONEER/rekordbox/exportExt.pdb',
           'PIONEER/rekordbox/exportLibrary.db',
           'PIONEER/rekordbox/exportLibrary.db-wal',
           'PIONEER/rekordbox/exportLibrary.db-shm',
           'PIONEER/rekordbox/exportLibrary.db-journal', 'CDJMODS/index.json')
REPAIR_ID = 'history-blank-gaps-v1'
MAX_BYTES = 128 * 1024 * 1024


def _require(ok, message):
    if not ok:
        raise ValueError(message)


def _read(path):
    path = cache._regular(path)
    _require(path.stat().st_size <= MAX_BYTES, 'Library file is too large for this repair tool.')
    return path.read_bytes()


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def rekordbox_running():
    if os.name == 'nt':
        result = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq rekordbox.exe', '/FO', 'CSV', '/NH'],
                                capture_output=True, timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
        _require(result.returncode == 0, 'Could not check whether Rekordbox is closed.')
        return b'"rekordbox.exe"' in result.stdout.lower()
    result = subprocess.run(['pgrep', '-ix', 'rekordbox'], capture_output=True, timeout=15)
    _require(result.returncode in (0, 1), 'Could not check whether Rekordbox is closed.')
    return result.returncode == 0


def _tables(data):
    _require(28 <= len(data) <= MAX_BYTES, 'Invalid Device Library size.')
    zero, size, count = struct.unpack_from('<III', data)
    _require(zero == 0 and size == 4096 and 0 < count <= 64
             and len(data) % size == 0 and 28 + count * 16 <= size,
             'Unsupported or damaged Device Library header.')
    tables = [struct.unpack_from('<IIII', data, 28 + n * 16) for n in range(count)]
    _require(len({t[0] for t in tables}) == count, 'Duplicate database table definitions.')
    return size, tables


def _page(data, size, number, kind):
    _require(0 < number < len(data) // size, f'Table {kind} points outside the database.')
    page = data[number * size:(number + 1) * size]
    if not any(page):
        raise ValueError(f'Table {kind} links to blank page {number}.')
    _require(struct.unpack_from('<III', page) == (0, number, kind),
             f'Table {kind} has a damaged page header at {number}.')
    _require(page[27] in (0x24, 0x34, 0x64), 'Unsupported database page flags.')
    if not page[27] & 0x40:
        offsets = int.from_bytes(page[24:27], 'little') & 8191
        heap_end = size - ((offsets + 15) // 16) * 36
        _require(heap_end >= 40, 'Database row index exceeds its page.')
        for row in pdb._row_positions(page, offsets):
            _require(40 <= row < heap_end, 'Database row points outside its heap.')
    return page


def _chain(data, size, table):
    kind, _, number, last = table
    seen = set()
    while True:
        _require(number not in seen, f'Table {kind} contains a page loop.')
        seen.add(number)
        page = _page(data, size, number, kind)
        if number == last:
            return seen
        number = struct.unpack_from('<I', page, 12)[0]


def _history_candidate(data, size, tables, broken):
    _require(broken and set(broken) <= {11, 12}, 'Damage is outside the supported history-page repair.')
    candidate = bytearray(data)
    patches = []
    for table in tables:
        kind, _, first, last = table
        if kind not in broken:
            continue
        pages = [n for n in range(1, len(data) // size)
                 if struct.unpack_from('<II', data, n * size + 4) == (n, kind)]
        _require(pages and pages[0] == first and pages[-1] == last,
                 'History page order is ambiguous; no automatic repair is available.')
        prior_sequence = -1
        for pos, number in enumerate(pages):
            page = _page(data, size, number, kind)
            sequence = struct.unpack_from('<I', page, 16)[0]
            _require(sequence > prior_sequence, 'History page sequence is ambiguous.')
            prior_sequence = sequence
            _require(bool(page[27] & 0x40) == (pos == 0), 'Unexpected history index pages.')
            if pos:
                offsets = int.from_bytes(page[24:27], 'little') & 8191
                heap_end = size - ((offsets + 15) // 16) * 36
                for row in pdb._row_positions(page, offsets):
                    _require(row + (5 if kind == 11 else 12) <= heap_end, 'Damaged history row.')
                    if kind == 11:
                        _require(pdb._string(page, row + 4).startswith('HISTORY '),
                                 'Unrecognized history table contents.')
            if number == last:
                continue
            following = pages[pos + 1]
            old = struct.unpack_from('<I', page, 12)[0]
            if following == old:
                continue
            _require(number < old < following and following - old == 2
                     and not any(data[old * size:(old + 1) * size]),
                     'History gap is not the supported blank-page pattern.')
            patches.append({'table': kind, 'page': number, 'blank_page': old,
                            'successor': following, 'offset': number * size + 12})
            struct.pack_into('<I', candidate, number * size + 12, following)
    _require(0 < len(patches) <= 64, 'No bounded history repair is available.')
    for table in tables:
        _chain(candidate, size, table)
    _require(pdb._track_paths(data) == pdb._track_paths(candidate), 'Repair changed track identities.')
    allowed = {p['offset'] + n for p in patches for n in range(4)}
    _require(all(a == b or n in allowed for n, (a, b) in enumerate(zip(data, candidate))),
             'Repair changed content outside the history pointers.')
    return bytes(candidate), patches


def analyze(data):
    size, tables = _tables(data)
    findings, broken = [], []
    for table in tables:
        try:
            _chain(data, size, table)
        except ValueError as exc:
            broken.append(table[0])
            findings.append({'code': 'damaged_table', 'summary': str(exc)})
    tracks = pdb._track_paths(data)
    playlists = 0
    for table in tables:
        if table[0] == 7 and 7 not in broken:
            for n in _chain(data, size, table):
                page = _page(data, size, n, 7)
                if not page[27] & 0x40:
                    playlists += sum(1 for _ in pdb._row_positions(page, int.from_bytes(page[24:27], 'little') & 8191))
    candidate, patches = None, []
    if broken:
        try:
            candidate, patches = _history_candidate(data, size, tables, broken)
        except (ValueError, pdb._Unreadable, struct.error, UnicodeError) as exc:
            findings.append({'code': 'unsupported_damage', 'summary': str(exc) or 'Unknown database damage.'})
    return {'verdict': 'repairable' if candidate else 'not_repairable' if broken else 'healthy',
            'counts': {'tracks': len(tracks), 'playlists': playlists}, 'findings': findings,
            'patches': patches, 'candidate': candidate}


def _backup_root():
    return cache._safe_path(data_root() / 'library-repair-backups')


def _files(volume):
    return {name: managed_usb.revision(cache._safe_path(volume / name)) for name in GUARDED}


def _backups(identity, current):
    root = _backup_root()
    if not root.exists():
        return []
    records = []
    for entry in root.iterdir():
        if not re.fullmatch('[0-9a-f]{32}', entry.name):
            continue
        try:
            record = json.loads(_read(entry / 'receipt.json'))
            if record['identity'] != identity or record['operation'] != 'repair':
                continue
            records.append({'id': entry.name, 'folder': str(entry), 'created': record['created'],
                            'restorable': record['state'] in ('prepared', 'committed') and current == record['after_files']})
        except (OSError, ValueError, KeyError):
            continue
    return sorted(records, key=lambda r: r['created'], reverse=True)[:20]


def inspect(volume):
    volume = cache._safe_path(volume)
    identity = managed_usb.identity(volume, media=True)
    files = _files(volume)
    result = {'format': 'xz-library-inspected/1', 'volume': str(volume), 'identity': identity,
              'expected_files': files, 'counts': {'tracks': 0, 'playlists': 0, 'stem_sets': 0},
              'verdict': 'no_library', 'findings': [], 'repair': None,
              'backup_root': str(_backup_root()), 'rekordbox_running': rekordbox_running()}
    if files[PDB]['exists']:
        try:
            analysis = analyze(_read(volume / PDB))
            result.update({key: analysis[key] for key in ('verdict', 'findings')})
            result['counts'].update(analysis['counts'])
            if analysis['candidate']:
                result['repair'] = {'id': REPAIR_ID, 'title': 'Reconnect surviving playback-history pages',
                    'changes': [f"{len(analysis['patches'])} page links in {PDB}"],
                    'keeps': ['Track IDs, track records and playlists', 'Music, prepared stems and stem mappings',
                              'Waveforms, cues, OneLibrary and the USB loader'],
                    'history_warning': 'History entries in the blank pages are already missing. This repair does not recover them.'}
        except (ValueError, pdb._Unreadable, struct.error, UnicodeError) as exc:
            result.update(verdict='not_repairable', findings=[{'code': 'unreadable_library',
                          'summary': str(exc) or 'The Device Library cannot be read safely.'}])
    if files['CDJMODS/index.json']['exists']:
        try:
            index = json.loads(_read(volume / 'CDJMODS/index.json'))
            _require(isinstance(index.get('tracks'), dict), 'Invalid stem index.')
            result['counts']['stem_sets'] = len(index['tracks'])
        except (ValueError, AttributeError) as exc:
            result['findings'].append({'code': 'stem_index_unreadable', 'summary': str(exc)})
            result['verdict'] = 'not_repairable'
            result['repair'] = None
    _require(_files(volume) == files, 'The library changed while checking it. Close Rekordbox and check again.')
    result['backups'] = _backups(identity, files)
    return result


def _closed():
    _require(not rekordbox_running(), 'Close Rekordbox, then check the USB again before repairing or undoing a repair.')


def _check(volume, identity, files):
    managed_usb.check_target(volume, identity, media=True)
    _require(_files(volume) == files, 'The USB library changed since inspection. Check it again before writing.')


def _check_backup_volume(volume, root):
    _require(root.stat().st_dev != volume.stat().st_dev,
             'The recovery backup must be on this computer, on a different volume from the USB.')


def _commit(volume, identity, files, replacement, operation, job):
    _closed()
    _check(volume, identity, files)
    root = _backup_root()
    root.mkdir(parents=True, exist_ok=True)
    _check_backup_volume(volume, root)
    required = sum(f.get('bytes', 0) for f in files.values()) + len(replacement) + 1048576
    _require(shutil.disk_usage(root).free >= required, 'Not enough computer space for the library backup.')
    _require(shutil.disk_usage(volume).free >= len(replacement) + 1048576, 'Not enough USB space for atomic replacement.')
    ident = uuid.uuid4().hex
    folder = root / ident
    folder.mkdir()
    after_files = {**files, PDB: {'exists': True, 'sha256': _sha(replacement), 'bytes': len(replacement)}}
    record = {'format': 'xz-library-backup/1', 'id': ident, 'operation': operation,
              'created': datetime.now(timezone.utc).isoformat(), 'identity': identity,
              'before_files': files, 'after_files': after_files, 'state': 'preparing'}
    managed_usb._receipt(folder, record)
    job.progress('backup', 'Backing up the device catalogs and stem mappings to this computer')
    for name, revision in files.items():
        job.check()
        if revision['exists']:
            dest = folder / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            managed_usb._write(dest, _read(volume / name))
            _require(managed_usb.revision(dest) == revision, 'Backup verification failed; USB was not changed.')
    _check(volume, identity, files)
    record['state'] = 'prepared'
    managed_usb._receipt(folder, record)
    destination = cache._regular(volume / PDB)
    temporary = None
    published = False
    try:
        with tempfile.NamedTemporaryFile(prefix='.xz-library-', suffix='.tmp', dir=destination.parent, delete=False) as out:
            temporary = Path(out.name)
            out.write(replacement)
            out.flush()
            os.fsync(out.fileno())
        _require(_sha(_read(temporary)) == after_files[PDB]['sha256'], 'Staged repair verification failed.')
        job.progress('verify', 'Verifying the exact replacement and unchanged catalogs')
        _closed()
        _check(volume, identity, files)
        job.check()
        os.replace(temporary, destination)
        temporary = None
        published = True
        _check(volume, identity, after_files)
        record['state'] = 'committed'
        managed_usb._receipt(folder, record)
    except Exception as exc:
        if published:
            raise RuntimeError(f'The replacement was written but verification did not finish. Check the USB again. Recovery backup: {folder}. {exc}') from exc
        raise
    finally:
        if temporary and temporary.exists():
            cache._regular(temporary).unlink()
    return {'id': ident, 'folder': str(folder)}


def mutate(request, job, *, restore=False):
    volume = cache._safe_path(request['volume'])
    identity, files = request['expected_identity'], request['expected_files']
    _closed()
    _check(volume, identity, files)
    lock = cache._safe_path(volume / '.xzmods-write.lock')
    if lock.exists():
        lock_stat = cache._regular(lock).stat()
        _require(lock_stat.st_nlink == 1 and lock_stat.st_size <= 1
                 and lock.read_bytes() in (b'', b'0'), 'Invalid USB write lock.')
    with exclusive_lock(lock, 'Another XZ Mods operation is writing to this USB'):
        _check(volume, identity, files)
        before = inspect(volume)
        if restore:
            ident = request['backup_id']
            _require(isinstance(ident, str) and re.fullmatch('[0-9a-f]{32}', ident), 'Invalid backup identifier.')
            folder = _backup_root() / ident
            record = json.loads(_read(folder / 'receipt.json'))
            _require(record.get('format') == 'xz-library-backup/1' and record.get('operation') == 'repair'
                     and record.get('identity') == identity and record.get('state') in ('prepared', 'committed')
                     and record.get('after_files') == files, 'This backup cannot undo the current library.')
            replacement = _read(folder / PDB)
            _require(_sha(replacement) == record['before_files'][PDB]['sha256'], 'Backup integrity check failed.')
        else:
            _require(request.get('repair_id') == REPAIR_ID and before['verdict'] == 'repairable',
                     'No supported repair is available. Check the USB again.')
            replacement = analyze(_read(volume / PDB))['candidate']
            _require(replacement is not None, 'No safe repair candidate.')
        backup = _commit(volume, identity, files, replacement, 'undo' if restore else 'repair', job)
        after = inspect(volume)
        if not restore:
            _require(after['verdict'] == 'healthy' and before['counts'] == after['counts'],
                     'Post-repair validation failed. Keep the recovery backup and inspect the USB.')
        return {'format': 'xz-library-restored/1' if restore else 'xz-library-repaired/1',
                'backup': backup, 'before': before['counts'], 'after': after['counts'], 'inspection': after,
                'rekordbox_verified': False,
                'summary': 'Original library restored. It may contain the original damage.' if restore else
                'History links repaired and structural checks passed. Track IDs, playlists and stem mappings are unchanged. Reopen Rekordbox to verify the USB.'}


def save_report(volume):
    report = inspect(volume)
    folder = cache._safe_path(data_root() / 'library-repair-reports')
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / (uuid.uuid4().hex + '.json')
    managed_usb._write(path, (json.dumps(report, indent=2) + '\n').encode())
    return {'format': 'xz-library-report/1', 'report_path': str(path)}
