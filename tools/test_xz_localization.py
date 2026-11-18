import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('xz_localization', Path(__file__).with_name('xz_localization.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class InventoryTests(unittest.TestCase):
    def test_tracks_changes_additions_and_removals(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name, text in [('manual/manual.json', '{}'), ('manual/chapters/a.md', 'Hello\n'),
                               (module.MODS + '/settings-schema.json', '{}'), ('ui/index.html', 'Hello')]:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding='utf-8')
            baseline = module.inventory(root)
            chapter = root / 'manual/chapters/a.md'
            chapter.write_bytes(b'Hello\r\n')
            self.assertEqual(baseline, module.inventory(root))
            chapter.write_text('Changed', encoding='utf-8')
            self.assertNotEqual(baseline, module.inventory(root))
            chapter.write_text('Hello\n', encoding='utf-8')
            extra = root / 'ui/extra.js'
            extra.write_text('new', encoding='utf-8')
            self.assertNotEqual(baseline, module.inventory(root))
            extra.unlink()
            (root / 'ui/index.html').unlink()
            self.assertNotEqual(baseline, module.inventory(root))

    def test_missing_tree_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                module.inventory(tmp)


if __name__ == '__main__':
    unittest.main()
