import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from xz_i18n import extract, validate, protected, translate_html, localize_output, ROOT

class LocalizationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Use the canonical generation; incomplete cases are explicit fixtures.
        raw = (ROOT / 'ui/xz-l10n-data.js').read_text(encoding='utf-8')
        cls.published = json.loads(raw.removeprefix('window.XZ_L10N_DATA=').strip().removesuffix(';'))
        cls.source = cls.published['source']
        cls.catalog = copy.deepcopy(cls.published['catalogs']['es'])

    def test_partial_catalogs_and_publish_gate(self):
        for catalog in self.published['catalogs'].values():
            self.assertEqual(validate(self.source, catalog), [])
            partial = copy.deepcopy(catalog)
            partial['entries'].pop(next(iter(partial['entries'])))
            self.assertTrue(validate(self.source, partial, complete=True))

    def test_revision_invalidates_every_translation(self):
        source = copy.deepcopy(self.source)
        source['source_revision'] = 'changed'
        self.assertIn('all translations require review', validate(source, self.catalog)[0])

    def test_controls_are_context_sensitive(self):
        self.assertEqual(protected('Go back to the master deck.'), [])
        self.assertEqual(protected('Press BACK, SHIFT + BEAT SYNC or PLAY/PAUSE.'), ['BACK', 'BEAT SYNC', 'PLAY/PAUSE', 'SHIFT'])
        self.assertEqual(protected('CUE USB E-8302'), ['CUE', 'E-8302', 'USB'])
        self.assertEqual(protected('USB-read and USB-loader'), ['USB','USB'])

    def test_changed_controls_placeholders_and_alerts_rejected(self):
        source = copy.deepcopy(self.source)
        source['entries']['test'] = {'source': 'Press BACK {deck}', 'surface': 'manual', 'source_sha256': 'hash', 'placeholders': ['{deck}'], 'protected': ['BACK'], 'immutable': False}
        catalog = copy.deepcopy(self.catalog)
        for text in ('Pulsa ATRÁS {deck}', 'Pulsa BACK {other}'):
            catalog['entries']['test'] = {'source_sha256': 'hash', 'text': text}
            self.assertTrue(validate(source, catalog))
        source['entries']['test'].update(source='AUTH CHIP ERROR', placeholders=[], protected=[], immutable=True)
        catalog['entries']['test']['text'] = 'Error de chip'
        self.assertTrue(validate(source, catalog))

    def test_html_preserves_links_and_escapes_translation(self):
        source = copy.deepcopy(self.source)
        catalog = copy.deepcopy(self.catalog)
        key = next(k for k, e in source['entries'].items() if e['surface'] == 'manual' and e['source'] == 'Goes up one level.')
        catalog['entries'][key]['text'] = '<script>bad</script>'
        result = translate_html('<a href="#same" id="same">Goes up one level.</a>', source, catalog)
        self.assertEqual(result, '<a href="#same" id="same">&lt;script&gt;bad&lt;/script&gt;</a>')

    def test_localized_manual_draft_and_blocked_publish(self):
        partial = copy.deepcopy(self.published)
        key = next(k for k,e in self.source['entries'].items() if e['source']=='Goes up one level.')
        partial['catalogs']['es']['entries'] = {key: dict(self.catalog['entries'][key], text='Sube un nivel.')}
        with tempfile.TemporaryDirectory() as temp, patch('xz_i18n.extract', return_value=self.source), patch('xz_i18n.bundle', return_value=partial):
            path = Path(temp) / 'index.html'
            original = '<html lang="en"><body><p>Goes up one level.</p></body></html>'
            path.write_text(original, encoding='utf-8')
            with self.assertRaises(ValueError): localize_output(temp, 'es')
            self.assertEqual(path.read_text(encoding='utf-8'), original)
            localize_output(temp, 'es', draft=True)
            rendered = path.read_text(encoding='utf-8')
            self.assertIn('Sube un nivel.', rendered)
            self.assertIn('Do not publish', rendered)
            self.assertIn('lang="es"', rendered)

    def test_html_entities_do_not_damage_following_markup(self):
        source = {'entries': {'test': {'surface': 'manual', 'source': 'Read & learn'}}}
        catalog = {'entries': {'test': {'text': 'Leer y aprender'}}}
        result = translate_html('<p>Read &amp; learn</p><kbd>BACK</kbd>', source, catalog)
        self.assertEqual(result, '<p>Leer y aprender</p><kbd>BACK</kbd>')

if __name__ == '__main__': unittest.main()
