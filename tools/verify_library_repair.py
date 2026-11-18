"""Exercise the packaged repair backend on synthetic media, never a real USB.

Use --fixture-parent on a different disk from the temporary app-data directory.
Close Rekordbox before running. Its real process guard is deliberately retained.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

APP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(APP / 'source/xdj-xz-toolkit'))
from builder.tests.test_library_repair import broken_library, unused_page_damage
from builder.library_repair import PDB, rekordbox_running


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources', type=Path, default=APP / 'resources')
    parser.add_argument('--fixture-parent', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--fixture-kind', choices=('history', 'unused'), default='history')
    args = parser.parse_args()
    assert not rekordbox_running(), 'Close Rekordbox before this packaged-backend check.'
    backend = args.resources / 'backend' / ('xz-mods-service.exe' if os.name == 'nt' else 'xz-mods-service')
    checks = []
    with tempfile.TemporaryDirectory(prefix='xz-library-test-', dir=args.fixture_parent) as media_dir, tempfile.TemporaryDirectory(prefix='xz-repair-appdata-') as state_dir:
        media = Path(media_dir)
        database = broken_library(media)
        unused = unused_page_damage(database) if args.fixture_kind == 'unused' else None
        for suffix in ('', '-wal', '-shm', '-journal'):
            (database.parent / ('exportLibrary.db' + suffix)).write_bytes(
                ('preserve OneLibrary main database and journal ' + suffix).encode())
        original = database.read_bytes()
        protected = {str(p.relative_to(media)): p.read_bytes() for p in media.rglob('*') if p.is_file() and p != database}
        env = {**os.environ, 'XZ_BUILDER_RESOURCES': str(args.resources.resolve()), 'XZ_BUILDER_DATA': state_dir}

        def call(method, **params):
            request = {'method': method, 'volume': str(media), **params}
            result = subprocess.run([str(backend.resolve())], input=json.dumps(request), text=True,
                                    capture_output=True, env=env, timeout=30)
            assert result.returncode == 0, result.stderr
            return json.loads(result.stdout.splitlines()[-1])

        checked = call('inspect_device_library')
        assert checked['ok'] and checked['result']['verdict'] == 'repairable', checked
        checks.append('detects ' + ('unused-page garbage' if unused is not None else 'broken history chain'))
        state = checked['result']
        guard = {'expected_identity': state['identity'], 'expected_files': state['expected_files'], 'repair_id': state['repair']['id']}
        repaired = call('repair_device_library', **guard)
        assert repaired['ok'], repaired
        result = repaired['result']
        assert result['before'] == result['after'] and result['inspection']['verdict'] == 'healthy'
        assert result['rekordbox_verified'] is False
        assert database.read_bytes() != original
        if unused is not None:
            expected = bytearray(original)
            expected[unused * 4096:(unused + 1) * 4096] = bytes(4096)
            assert database.read_bytes() == expected, 'Unused-page repair changed active data or trailing bytes'
        assert (Path(result['backup']['folder']) / PDB).read_bytes() == original
        for name in ('exportExt.pdb', 'exportLibrary.db', 'exportLibrary.db-wal',
                     'exportLibrary.db-shm', 'exportLibrary.db-journal'):
            assert (Path(result['backup']['folder']) / 'PIONEER/rekordbox' / name).read_bytes() == (database.parent / name).read_bytes()
        assert all((media / name).read_bytes() == data for name, data in protected.items())
        checks.append('repair preserves tracks, stems, companion files and exact backup')
        stale = call('repair_device_library', **guard)
        assert stale['ok'] is False and 'changed since inspection' in stale['error']
        checks.append('stale inspection rejected')
        journal = database.parent / 'exportLibrary.db-wal'
        journal_before = journal.read_bytes()
        state = result['inspection']
        journal.write_bytes(journal_before + b'new commit')
        changed = call('restore_device_library', expected_identity=state['identity'],
                       expected_files=state['expected_files'], backup_id=result['backup']['id'])
        assert changed['ok'] is False and 'changed since inspection' in changed['error']
        journal.write_bytes(journal_before)
        checks.append('OneLibrary journal change rejects stale undo')
        state = result['inspection']
        restored = call('restore_device_library', expected_identity=state['identity'],
                        expected_files=state['expected_files'], backup_id=result['backup']['id'])
        assert restored['ok'] and database.read_bytes() == original, restored
        assert all((media / name).read_bytes() == data for name, data in protected.items())
        checks.append('undo restores exact original database')
        report = call('save_device_library_report')
        assert report['ok'] and Path(report['result']['report_path']).is_file(), report
        checks.append('diagnostic report saved on computer')
    evidence = {'backend': str(backend.resolve()), 'checks': checks, 'passed': len(checks), 'physical_usb_written': False}
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps(evidence, indent=2) + '\n')
    print(json.dumps(evidence))


if __name__ == '__main__':
    main()
