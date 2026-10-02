"""Real FLAC publishing, native verification and read-only library browsing."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from builder import overcue_writer as writer, stem_library
from builder.tests.test_overcue_writer import QuietJob, render_roles


class ProcessJob(QuietJob):
    def run(self, argv):
        return subprocess.run(list(map(str, argv)), check=True, capture_output=True, text=True).stdout


class FlacTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('XZ_BUILDER_RESOURCES') and os.environ.get('XZ_FLAC_PYTHON'),
                         'Set the packaged resources and managed audio interpreter for real FLAC acceptance')
    def test_publish_reuse_migrate_browse_and_corruption(self):
        resources = Path(os.environ['XZ_BUILDER_RESOURCES']).resolve()
        checker = resources / 'xz-overcue-check.exe'
        python = os.environ['XZ_FLAC_PYTHON']
        with tempfile.TemporaryDirectory(prefix='xz-flac-writer-') as temporary:
            root = Path(temporary)
            source = root / 'USB/Contents/Artist/track.wav'
            source.parent.mkdir(parents=True)
            original = b'original track bytes must remain unchanged'
            source.write_bytes(original)
            roles = root / 'roles'
            records = render_roles(roles, 40000)
            args = (source.parents[2], '/Contents/Artist/track.wav', 1, hashlib.sha256(original).hexdigest(),
                    'wav', roles, 'test-model', 'test-version', ProcessJob())
            legacy = writer.publish(*args)
            before = {p.name: p.read_bytes() for p in (source.parents[2] / 'CDJMODS/stems' / legacy['bundle']).iterdir()}
            receipt = writer.publish(*args, page_python=python)
            self.assertEqual(receipt['page_codec'], 'flac-96k')
            self.assertNotEqual(receipt['bundle'], legacy['bundle'])
            self.assertTrue(receipt['replaced_previous'])
            folder = source.parents[2] / 'CDJMODS/stems' / receipt['bundle']
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(before, {p.name: p.read_bytes() for p in (folder.parent / legacy['bundle']).iterdir()})
            for role in writer.ROLES:
                self.assertEqual((folder / writer.role_file(role)).read_bytes()[:8], b'OVPGZ003')
            checked = subprocess.run([str(checker), str(source)], capture_output=True, text=True, check=True)
            data = json.loads(checked.stdout)
            self.assertEqual((data['page_codec'], data['verified_mixes'], data['flac_mixes']), ('flac-96k', 7, 7))
            self.assertTrue(data['source_identity_verified'])
            oracle = os.environ.get('XZ_OVERCUE_ORACLE')
            if oracle:
                subprocess.run([sys.executable, oracle, str(source.parents[2]), args[1]], check=True)
            self.assertTrue(writer.publish(*args, page_python=python)['reused'])
            index = json.loads((source.parents[2] / 'CDJMODS/index.json').read_text())
            track = {'id': 1, 'file_path': args[1], 'sample_rate': 44100}
            state = stem_library.stem_status(source.parents[2], track, index, None)
            self.assertEqual(state['page_codec'], 'flac-96k')
            self.assertIn('FLAC', state['stem_label'])
            (folder / 'overcue-manifest.json').unlink()
            state = stem_library.stem_status(source.parents[2], track, index, None)
            self.assertTrue(state['can_verify'])
            checked = subprocess.run([str(checker), str(source)], capture_output=True, text=True, check=True)
            self.assertTrue(json.loads(checked.stdout)['all_pages_verified'])
            role = writer.ROLES[0]
            with self.assertRaises(ValueError):
                writer.verify_role(folder / writer.role_file(role), '0' * 64, 40000)
            role_file = folder / writer.role_file(role)
            valid = role_file.read_bytes()
            role_file.write_bytes(valid[:-1] + bytes([valid[-1] ^ 1]))
            with self.assertRaises(ValueError):
                writer.verify_role(role_file, records[role]['sha256'], 40000)
            self.assertEqual(source.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
