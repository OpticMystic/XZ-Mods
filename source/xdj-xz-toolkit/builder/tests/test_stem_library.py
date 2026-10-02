import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from builder import stem_library,rekordbox_pdb,stem_import
from builder.tests.pdb_fixture import write_export
from builder.tests.test_overcue_writer import QuietJob

class LibraryTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.mapping={i:'/Contents/Artist/Track '+str(i)+'.wav' for i in range(1,85)}
        write_export(self.root.joinpath(*rekordbox_pdb.EXPORT_PDB),self.mapping)
        for path in self.mapping.values():
            target=self.root/path.lstrip('/');target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(b'audio fixture')
    def tearDown(self):self.temp.cleanup()
    def test_reads_database_with_pagination_and_search(self):
        result=stem_library.browse({'volume':str(self.root)},QuietJob())
        self.assertEqual(result['total'],84);self.assertEqual(len(result['tracks']),40)
        self.assertTrue(result['tracks'][0]['can_generate']);self.assertGreater(result['free_bytes'],0)
        result=stem_library.browse({'volume':str(self.root),'query':'Track 84'},QuietJob())
        self.assertEqual([t['id'] for t in result['tracks']],[84])
    def test_missing_audio_is_not_generatable(self):
        (self.root/'Contents/Artist/Track 1.wav').unlink()
        result=stem_library.browse({'volume':str(self.root),'query':'Track 1.wav'},QuietJob())
        self.assertEqual(result['tracks'][0]['stem_status'],'unavailable')
        self.assertFalse(result['tracks'][0]['can_generate'])
    def test_bad_index_disables_writes_but_library_stays_visible(self):
        (self.root/'CDJMODS').mkdir();(self.root/'CDJMODS/index.json').write_text('{broken')
        result=stem_library.browse({'volume':str(self.root)},QuietJob())
        self.assertEqual(len(result['tracks']),40);self.assertTrue(result['index_error'])
        self.assertTrue(all(not t['can_generate'] for t in result['tracks']))
    def test_overcue_manifests_use_fixed_role_names_without_file_fields(self):
        from builder import overcue_writer as writer
        bundle='a'*16;folder=self.root/'CDJMODS/stems'/bundle;folder.mkdir(parents=True)
        manifest={'schema':writer.MANIFEST_SCHEMA,'roles':{name:{} for name in writer.ROLES}}
        (folder/'overcue-manifest.json').write_text(json.dumps(manifest))
        for name in writer.ROLES:(folder/writer.role_file(name)).write_bytes(b'present')
        index={'schema':writer.INDEX_SCHEMA,'tracks':{'1':{'file_path':self.mapping[1],'bundle':bundle}}}
        (self.root/'CDJMODS/index.json').write_text(json.dumps(index))
        result=stem_library.browse({'volume':str(self.root),'query':'Track 1.wav'},QuietJob())
        self.assertEqual(result['tracks'][0]['stem_status'],'present')
        self.assertTrue(result['tracks'][0]['can_verify'])
        self.assertFalse(result['tracks'][0]['can_generate'])
    def test_paths_cannot_escape_usb(self):
        for value in ['/Contents/../../outside.wav','/Contents/C:/bad.wav','/other/track.wav','/Contents/a\\b.wav']:
            with self.assertRaises(ValueError):stem_library.source_path(self.root,value)
    def test_assignment_must_be_complete_and_unique(self):
        path=str(self.root/'Contents/Artist/Track 1.wav')
        with self.assertRaises(ValueError):stem_import.validated_files({'files':[{'path':path,'group':''}]})
        with self.assertRaises(ValueError):stem_import.validated_files({'files':[{'path':path,'group':'vocals'}]*2})
        self.assertEqual(stem_import.validated_files({'files':[{'path':path,'group':'drums'}]})[0]['group'],'drums')

if __name__=='__main__':unittest.main()
