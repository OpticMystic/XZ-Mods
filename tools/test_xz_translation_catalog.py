import unittest
from tools.xz_translation_catalog import LOCALES, validate_manual


class CatalogTests(unittest.TestCase):
    def test_requires_all_locales_and_current_nonempty_content(self):
        inv = {'schema': 1, 'kind': 'xz-source-inventory', 'files': [
            {'path': 'manual/manual.json', 'sha256': 'a'},
            {'path': 'manual/chapters/start.md', 'sha256': 'b'}]}
        catalog = {locale: {'manual/manual.json': {'source_sha256': 'a', 'text': 'translated'},
                            'manual/chapters/start.md': {'source_sha256': 'b', 'text': 'translated'}}
                   for locale in LOCALES}
        self.assertEqual([], validate_manual(inv, catalog))
        catalog['ar']['manual/chapters/start.md']['source_sha256'] = 'old'
        self.assertTrue(validate_manual(inv, catalog))
        del catalog['ar']
        self.assertTrue(validate_manual(inv, catalog))


if __name__ == '__main__':
    unittest.main()
