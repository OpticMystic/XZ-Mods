import json
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from contextlib import redirect_stdout
import io
from unittest.mock import patch
from xz_translate import atomic, pieces, LANG, run
from xz_i18n import extract, validate, LOCALES

class PipelineTests(unittest.TestCase):
    def test_windows_sharing_violation_retries_atomic_replace(self):
        import os
        with tempfile.TemporaryDirectory() as temp:
            target=Path(temp)/'bundle.js'; target.write_text('prior')
            error=PermissionError('reader holds file'); error.winerror=32
            original=os.replace; calls=[]
            def replacement(source,destination):
                calls.append(source)
                if len(calls)<3: raise error
                original(source,destination)
            with patch('xz_translate.os.replace',side_effect=replacement), patch('xz_translate.time.sleep') as sleep:
                atomic(target,'complete')
                self.assertEqual(sleep.call_count,2)
            self.assertEqual(target.read_text(),'complete')

    def test_persistent_sharing_violation_is_bounded_and_keeps_prior_file(self):
        with tempfile.TemporaryDirectory() as temp:
            target=Path(temp)/'bundle.js'; target.write_text('prior')
            error=PermissionError('reader holds file'); error.winerror=5
            with patch('xz_translate.os.replace',side_effect=error), patch('xz_translate.time.monotonic',side_effect=[0,6]):
                with self.assertRaises(PermissionError): atomic(target,'unpublished')
            self.assertEqual(target.read_text(),'prior')

    def test_protected_tokens_are_never_submitted(self):
        parts=pieces('Press SHIFT + BEAT SYNC, then BACK {deck}. E-8307: UNSUPPORTED DEVICE')
        self.assertEqual([text for keep,text in parts if keep and any(c.isalnum() for c in text)],['SHIFT','BEAT SYNC','BACK','{deck}','E-8307: UNSUPPORTED DEVICE'])
    def test_failed_replace_keeps_previous_generation(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'bundle.js';path.write_text('previous')
            with patch('xz_translate.os.replace',side_effect=OSError('locked')):
                with self.assertRaises(OSError): atomic(path,'next')
            self.assertEqual(path.read_text(),'previous')
    def test_reserved_publication_preserves_prior_bundle_until_replace(self):
        from xz_translate import publish_reserved
        with tempfile.TemporaryDirectory() as tmp:
            target=Path(tmp)/'bundle.js'; reserve=Path(tmp)/'reserve'
            target.write_text('prior'); reserve.write_bytes(bytes(64))
            with patch('xz_translate.os.replace',side_effect=OSError('rename failed')):
                with self.assertRaises(OSError): publish_reserved(target,'complete',reserve)
            self.assertEqual(target.read_text(),'prior')
            self.assertEqual(reserve.read_text(),'complete')
            publish_reserved(target,'complete',reserve)
            self.assertEqual(target.read_text(),'complete')
            self.assertFalse(reserve.exists())

    def test_undersized_reserve_cannot_touch_previous_publication(self):
        from xz_translate import publish_reserved
        with tempfile.TemporaryDirectory() as tmp:
            target=Path(tmp)/'bundle.js'; reserve=Path(tmp)/'reserve'
            target.write_text('prior'); reserve.write_bytes(bytes(2))
            with self.assertRaises(ValueError): publish_reserved(target,'complete',reserve)
            self.assertEqual(target.read_text(),'prior')
            self.assertEqual(reserve.read_bytes(),bytes(2))

    def test_all_locale_base_tokens_explicit(self):
        self.assertEqual(set(LANG),set(LOCALES))
        self.assertEqual(LANG['zh-Hant'],'zh'); self.assertEqual(LANG['pt'],'pt')
    def test_dynamic_wizard_and_search_extracted(self):
        source=extract(); entries=list(source['entries'].values())
        self.assertTrue(any(e['source']=='XZ language setup' for e in entries))
        self.assertTrue(any(e['source']=='Cancelling safely…' for e in entries))
        self.assertTrue(any(any('manual/search.js' in c for c in e['contexts']) for e in entries))
        self.assertGreater(sum(e['surface']=='desktop' for e in entries),1000)
    def test_missing_locale_and_changed_source_rejected(self):
        source={'source_revision':'now','entries':{'a':{'surface':'manual'}}}
        self.assertTrue(validate(source,{},True))
        self.assertTrue(validate(source,{'schema':1,'product':'xz-mods','locale':'es','source_revision':'then','entries':{}},True))
    def test_manual_attributes_translate_without_changing_links(self):
        from xz_i18n import translate_html
        source={'entries':{'a':{'surface':'manual','source':'Choose language'}}}
        catalog={'entries':{'a':{'text':'??'}}}
        self.assertEqual(translate_html('<a href="#safe" title="Choose language">Choose language</a>',source,catalog), '<a href="#safe" title="??">??</a>')

    def test_authored_wrapper_preserves_conditions_and_metadata(self):
        import subprocess
        from xz_i18n import ROOT
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'sample.js'
            original="status.textContent=mode==='Ready'?'Choose language '+userName:'Working'; (function(){view.textContent=`Ready for ${userName}`;})();"
            path.write_text(original)
            subprocess.check_call(['node',str(ROOT/'tools/xz_wrap_display.cjs'),str(path)],stdout=subprocess.DEVNULL)
            output=path.read_text()
            self.assertIn("mode==='Ready'",output)
            self.assertIn("XZTemplate`Ready for ${userName}`",output)
            self.assertIn("XZText('Choose language ')+userName",output)
            subprocess.check_call(['node',str(ROOT/'tools/xz_wrap_display.cjs'),str(path)],stdout=subprocess.DEVNULL)
            self.assertEqual(path.read_text(),output)

    def test_brands_are_protected_but_library_prose_is_not(self):
        from xz_i18n import protected
        self.assertEqual(protected('Open the library in XZ Mods using BACK.'),['BACK','XZ Mods'])
        self.assertEqual(protected('The library goes back to the master deck.'),[])

    def test_context_markers_restore_controls_next_to_korean_suffix(self):
        from xz_translate import mask_text, restore_text
        from xz_i18n import protected_valid
        masked,replacements=mask_text('Press BACK to go up one level.')
        restored=restore_text(replacements[0][0]+'\uB97C',replacements)
        self.assertTrue(protected_valid(['BACK'],restored))
        with self.assertRaises(ValueError): restore_text('marker missing',replacements)

    def test_restored_controls_must_keep_their_word_boundaries(self):
        from xz_translate import restore_checked, mask_text
        entry={'source':'AUTO CUE','placeholders':[], 'protected':['CUE']}
        with self.assertRaises(ValueError): restore_checked('AUTO9001',[('9001','CUE')],entry)
        self.assertEqual(restore_checked('AUTO 9001',[('9001','CUE')],entry),'AUTO CUE')
        masked,replacements=mask_text('Find USBs, then choose the USB.')
        self.assertIn(('9001','USBs'),replacements)
        self.assertNotIn('USBs',masked)
        with self.assertRaisesRegex(ValueError,'dropped all prose'):
            restore_checked('9001',[('9001','USB')],{'source':'Writing pauses during heavy USB reads.','placeholders':[],'protected':['USB']})

    def test_brand_case_is_preserved_without_changing_source_contract(self):
        from xz_translate import mask_text, restore_checked
        entry={'source':'Wait for rekordbox to close.','placeholders':[],'protected':[]}
        masked,replacements=mask_text(entry['source'])
        self.assertIn(('9001','rekordbox'),replacements)
        self.assertNotIn('rekordbox',masked)
        with self.assertRaises(ValueError): restore_checked('Warten Sie auf Rekordbox.',[],entry)
        self.assertEqual(restore_checked('Warten Sie auf 9001.',replacements,entry),'Warten Sie auf rekordbox.')

    def test_search_uses_translated_manual_and_rejects_missing_fragments(self):
        from xz_translate import derive_search_entries
        from xz_i18n import digest, protected
        values={'verb':'Choose','control':'USB','suffix':'now.','search':'Choose USB now.'}
        source={'source_revision':'fixture','entries':{key:{'source':value,'source_sha256':digest(value),'surface':'manual','placeholders':[],'protected':protected(value),'immutable':False} for key,value in values.items()},
                'manual_documents':{'guide':{'title':'Guide','html':'<p>Choose <kbd>USB</kbd> now.</p>'}},
                'manual_search_sections':[{'id':'search','chapter':'guide','anchor':''}]}
        catalog={'schema':1,'product':'xz-mods','locale':'es','source_revision':'fixture','entries':{key:{'source_sha256':source['entries'][key]['source_sha256'],'text':value} for key,value in {'verb':'Elija','control':'USB','suffix':'ahora.'}.items()}}
        derived=derive_search_entries(source,catalog)
        self.assertEqual(derived['search']['text'],'Elija USB ahora.')
        self.assertEqual(derived['search']['provenance']['context'],'derived-manual-search-v1')
        self.assertEqual(validate(source,dict(catalog,entries=dict(catalog['entries'],**derived)),complete=True),[])
        catalog['entries'].pop('verb')
        with self.assertRaisesRegex(ValueError,'incomplete translated manual'): derive_search_entries(source,catalog)

    def test_search_derivation_reproduces_every_original_section(self):
        from xz_translate import derive_search_entries
        source=extract(); derived={row['id'] for row in source['manual_search_sections']}
        catalog={'entries':{key:{'text':entry['source']} for key,entry in source['entries'].items() if key not in derived}}
        result=derive_search_entries(source,catalog)
        self.assertGreater(len(result),100)
        for key,entry in result.items(): self.assertEqual(entry['text'],source['entries'][key]['source'])

    def test_whole_set_publication_and_resume(self):
        from xz_i18n import digest
        source={'source_revision':'r1','entries':{'desktop:a':{
            'source':'Choose language','source_sha256':digest('Choose language'),
            'surface':'desktop','placeholders':[],'protected':[],'immutable':False}}}
        calls=[]
        class Fake:
            identity={'provider':'test'}
            def __init__(self,*args): pass
            def translate(self,texts,locale):
                calls.append(locale); return [locale+' translation' for text in texts]
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); target=root/'ui/xz-l10n-data.js'
            args=SimpleNamespace(model=root,state=root/'state',threads=1,batch=2,seconds=0,round_size=0)
            with patch('xz_translate.ROOT',root), patch('xz_translate.extract',return_value=source), patch('xz_translate.Provider',Fake), redirect_stdout(io.StringIO()):
                self.assertEqual(run(args),0)
                raw=target.read_text(encoding='utf-8')
                value=json.loads(raw.removeprefix('window.XZ_L10N_DATA=').strip().removesuffix(';'))
                self.assertEqual(set(value['catalogs']),set(LOCALES))
                self.assertTrue(all(not validate(source,c,True) for c in value['catalogs'].values()))
                self.assertEqual(run(args),0)
                self.assertEqual(len(calls),14)
                self.assertEqual(target.read_text(encoding='utf-8'),raw)

    def test_dropped_marker_fallback_is_explicit_and_structural(self):
        from xz_i18n import digest
        source={'source_revision':'r1','entries':{'desktop:a':{
            'source':'Press BACK.','source_sha256':digest('Press BACK.'),
            'surface':'desktop','placeholders':[],'protected':['BACK'],'immutable':False}}}
        class Fake:
            identity={'provider':'test','review':'machine-unreviewed'}
            def __init__(self,*args): pass
            def translate(self,texts,locale,beam=1,bounded=False):
                return ['lost marker' if any(c.isdigit() for c in text) else 'Pulsa' for text in texts]
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            args=SimpleNamespace(model=root,state=root/'state',threads=1,batch=2,seconds=0,round_size=0)
            with patch('xz_translate.ROOT',root), patch('xz_translate.extract',return_value=source), patch('xz_translate.Provider',Fake), redirect_stdout(io.StringIO()):
                self.assertEqual(run(args),0)
            catalog=json.loads((root/'state/es.staged.json').read_text(encoding='utf-8'))
            self.assertEqual(catalog['entries']['desktop:a']['text'],'Pulsa BACK.')
            self.assertEqual(catalog['entries']['desktop:a']['provenance']['protection'],'split-fallback-unreviewed')

    def test_provider_failure_and_source_race_preserve_published_set(self):
        from xz_i18n import digest
        source={'source_revision':'r1','entries':{'desktop:a':{
            'source':'Choose language','source_sha256':digest('Choose language'),
            'surface':'desktop','placeholders':[],'protected':[],'immutable':False}}}
        class Fake:
            identity={'provider':'test'}
            fail=True
            def __init__(self,*args): pass
            def translate(self,texts,locale):
                if self.fail and locale=='fr': raise ValueError('provider failed')
                return ['translated' for text in texts]
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); target=root/'ui/xz-l10n-data.js';target.parent.mkdir();target.write_text('previous')
            args=SimpleNamespace(model=root,state=root/'state',threads=1,batch=2,seconds=0,round_size=0)
            with patch('xz_translate.ROOT',root), patch('xz_translate.extract',return_value=source), patch('xz_translate.Provider',Fake), redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(ValueError,'provider failed'): run(args)
                self.assertEqual(target.read_text(),'previous')
            Fake.fail=False
            with patch('xz_translate.ROOT',root), patch('xz_translate.extract',side_effect=[source,{'source_revision':'changed'}]), patch('xz_translate.Provider',Fake), redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(ValueError,'Sources changed'): run(args)
                self.assertEqual(target.read_text(),'previous')
if __name__=='__main__': unittest.main()
