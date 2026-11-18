import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch
from xz_i18n import digest, LOCALES
from xz_translate import run, mask_text, case_variants
from xz_translation_quality import LiteralPolicy, require_quality


class QualityTests(unittest.TestCase):
    def test_unknown_markers_are_source_aware(self):
        policy=LiteralPolicy()
        for marker in ('\u2047','\ufffd'):
            self.assertTrue(policy.errors('Choose language','Choisir '+marker))
            self.assertEqual(policy.errors('Example '+marker,'Exemple '+marker),[])
            self.assertTrue(policy.errors('Example '+marker,'Exemple '+marker+marker))

    def test_brand_control_and_url_bytes_are_protected(self):
        policy=LiteralPolicy({'manual_documents':{'x':{'html':'<kbd>TRACK SEARCH</kbd>'}}})
        for original,bad in [('Open REKORDBOX','Open Rekordbox'),('Press TRACK SEARCH','Press Suche'),
                             ('Open https://example.com/manual','Open https://example.net/manual')]:
            self.assertTrue(policy.errors(original,bad))
        self.assertEqual(policy.errors('Press BACK in REKORDBOX.','Pulse BACK en REKORDBOX.'),[])
        self.assertEqual(policy.errors('No drive found. Connect again.','No encontrado.Conecte de nuevo.'),[])
        self.assertTrue(policy.errors('Open example.com/manual','Open ejemplo.com/manual'))
        self.assertEqual(policy.errors('Use https://, not http://.','Utilize https://, nao http://.'),[])

    def test_case_retry_keeps_masked_terms(self):
        masked,replacements=mask_text('SET UP REKORDBOX AND BACK')
        variants=case_variants(masked)
        self.assertTrue(variants)
        for variant in variants:
            for marker,_ in replacements: self.assertEqual(variant.count(marker),1)
        self.assertIn(('9001','REKORDBOX'),replacements)

    def test_fragment_repair_retains_authored_terminology(self):
        from xz_translate import pieces
        terms=[{'sources':['firmware'],'target':'펌웨어'}]
        parts=pieces('Choose firmware for USB.',terms)
        self.assertIn((True,'펌웨어'),parts)
        self.assertIn((True,'USB'),parts)
        self.assertFalse(any('firmware' in value for _,value in parts))

    def test_short_repair_decoding_is_bounded_and_never_accepts_truncation(self):
        from xz_translate import Provider
        class Tokenizer:
            def encode(self,text,out_type=None): return ['word']*3
        calls=[]
        class Decoder:
            def translate_batch(self,texts,**options):
                calls.append(options)
                return [SimpleNamespace(hypotheses=[['word']*options['max_decoding_length']])]
        provider=Provider.__new__(Provider)
        provider.sp=Tokenizer(); provider.translator=Decoder(); provider.inference_calls=0
        with self.assertRaisesRegex(ValueError,'decoding limit'):
            provider.translate(['Short text'],'ko',beam=4,bounded=True)
        self.assertEqual(len(calls),1)
        self.assertEqual(calls[0]['max_decoding_length'],64)
        self.assertNotIn('disable_unk',calls[0])

    def test_provenance_cannot_override_bad_text(self):
        source={'source_revision':'r','entries':{'a':{'source':'Choose language','source_sha256':'h','surface':'desktop','protected':[],'placeholders':[],'immutable':False}}}
        catalog={'schema':1,'product':'xz-mods','locale':'es','source_revision':'r','entries':{'a':{'source_sha256':'h','text':'\u2047','provenance':{'review':'machine-validated','validated':True}}}}
        with self.assertRaisesRegex(ValueError,r'U\+2047'):
            require_quality({'source':source,'catalogs':{'es':catalog}})

    def test_cached_unknowns_retry_and_cannot_publish_english_fallback(self):
        source={'source_revision':'r','entries':{'a':{'source':'Choose language','source_sha256':digest('Choose language'),'surface':'desktop','protected':[],'placeholders':[],'immutable':False}}}
        class Fake:
            identity={'provider':'quality-test','review':'machine-validated'}
            english=False
            def __init__(self,*args): self.inference_calls=0
            def translate(self,texts,locale,beam=1,bounded=False):
                self.inference_calls+=1
                return ['Choose language' if self.english else locale+' traduit' for _ in texts]
        identity=digest(json.dumps(Fake.identity,sort_keys=True))
        for english in (False,True):
            with tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp); state=root/'state'; state.mkdir(); (root/'ui').mkdir()
                target=root/'ui/xz-l10n-data.js'; target.write_text('prior')
                for locale in LOCALES:
                    (state/(locale+'-'+identity[:12]+'.json')).write_text(json.dumps({'identity':identity,'segments':{digest('Choose language'):'\u2047'}}))
                Fake.english=english
                args=SimpleNamespace(model=root,state=state,threads=1,batch=32,round_size=0,seconds=0,cached_only=False)
                with patch('xz_translate.extract',return_value=source),patch('xz_translate.Provider',Fake),patch('xz_translate.ROOT',root),redirect_stdout(io.StringIO()):
                    if english:
                        with self.assertRaisesRegex(ValueError,'unchanged English'): run(args)
                        self.assertEqual(target.read_text(),'prior')
                        failures=json.loads((state/'failures.json').read_text())
                        self.assertEqual({failure['locale'] for failure in failures},set(LOCALES))
                    else:
                        self.assertEqual(run(args),0)
                        self.assertNotIn('\u2047',target.read_text(encoding='utf-8'))
                        self.assertGreater(json.loads((state/'progress.json').read_text())['inference_calls'],0)


if __name__=='__main__': unittest.main()
