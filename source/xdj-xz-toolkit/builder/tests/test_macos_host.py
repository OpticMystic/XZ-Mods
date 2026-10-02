"""Exercise real Darwin system aliases without allowing user-controlled links."""
from pathlib import Path
import sys
import tempfile
import unittest
from builder import cache


@unittest.skipUnless(sys.platform == 'darwin', 'Requires real macOS aliases')
class MacPaths(unittest.TestCase):
    def test_system_temporary_alias_is_usable_but_nested_links_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = cache._safe_path(temporary)
            self.assertEqual(root.resolve(), Path(temporary).resolve())
            file = root / 'track.wav'
            file.write_bytes(b'test')
            link = root / 'linked.wav'
            link.symlink_to(file)
            with self.assertRaisesRegex(ValueError, 'Links and junctions'):
                cache._regular(link)


if __name__ == '__main__':
    unittest.main()
