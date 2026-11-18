"""Package identity collection handles the shipped profile size and rejects tampering."""
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from builder import support_sources as sources
from builder.jobs import Job


class PackagedProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.resources = self.root / 'resources'
        self.runtime = self.resources / 'runtime'
        self.runtime.mkdir(parents=True)
        (self.root / 'data').mkdir()
        env = patch.dict(os.environ, {'XZ_BUILDER_DATA': str(self.root / 'data'),
                                     'XZ_BUILDER_RESOURCES': str(self.resources)})
        env.start()
        self.addCleanup(env.stop)
        self.job = Job()

    def profile(self, size):
        profile = {'components': {name: {'sha256': digit * 64} for name, digit in
                                 [('runtime', 'a'), ('bridge', 'b'), ('module', 'c')]},
                   'source_files': ['PRIVATE_SOURCE_CANARY']}
        raw = json.dumps(profile).encode().ljust(size, b' ')
        (self.runtime / 'quad-package.json').write_bytes(raw)
        (self.runtime / 'manifest.json').write_text(json.dumps({'quad_package_sha256': hashlib.sha256(raw).hexdigest()}))

    def collect(self):
        scan = sources.discover(self.job, [])
        result = sources.collect({'scan_generation': scan['scan_generation'], 'category': 'bridge',
                                  'source_ids': ['packaged_profile']}, self.job)
        self.assertNotIn('PRIVATE_SOURCE_CANARY', json.dumps(result))
        self.assertNotIn(str(self.root), json.dumps(result))
        self.assertEqual(len(result['items']), 1)
        return result['items'][0]

    def test_current_profile_size_yields_verified_component_hashes(self):
        self.profile(200000)
        item = self.collect()
        self.assertIsNone(item['missing_reason'])
        self.assertEqual({fact['component']: fact['sha256'] for fact in item['facts']},
                         {'runtime': 'a' * 64, 'bridge': 'b' * 64, 'module': 'c' * 64})
        self.assertEqual(item['clock_confidence'], 'current')

    def test_profile_above_the_bound_is_still_refused(self):
        self.profile(262145)
        self.assertEqual(self.collect()['missing_reason'], 'oversized')

    def test_large_profile_still_requires_the_manifest_digest(self):
        self.profile(200000)
        (self.runtime / 'manifest.json').write_text(json.dumps({'quad_package_sha256': '0' * 64}))
        item = self.collect()
        self.assertEqual(item['missing_reason'], 'invalid')
        self.assertEqual(item['facts'], [])


if __name__ == '__main__':
    unittest.main()
