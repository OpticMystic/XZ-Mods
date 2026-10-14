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
        for name in writer.ROLES:(folder/writer.role_file(name)).write_bytes(b'OVPGZ001'+b'present')
        index={'schema':writer.INDEX_SCHEMA,'tracks':{'1':{'file_path':self.mapping[1],'bundle':bundle}}}
        (self.root/'CDJMODS/index.json').write_text(json.dumps(index))
        result=stem_library.browse({'volume':str(self.root),'query':'Track 1.wav'},QuietJob())
        self.assertEqual(result['tracks'][0]['stem_status'],'present')
        self.assertTrue(result['tracks'][0]['can_verify'])
        self.assertFalse(result['tracks'][0]['can_generate'])
        for name in writer.ROLES:(folder/writer.role_file(name)).write_bytes(b'OVPGZ004'+b'present')
        row=stem_library.browse({'volume':str(self.root),'query':'Track 1.wav'},QuietJob())['tracks'][0]
        self.assertEqual((row['stem_status'],row['page_codec']),('present','flac-44k1'))
        (folder/writer.role_file(writer.ROLES[0])).write_bytes(b'OVPGZ999'+b'unsupported')
        row=stem_library.browse({'volume':str(self.root),'query':'Track 1.wav'},QuietJob())['tracks'][0]
        self.assertEqual(row['stem_status'],'error')
        self.assertFalse(row['can_verify'])
        self.assertIn('not supported',row['detail'])
    def test_mp3_bundle_from_a_gapless_decode_asks_to_prepare_again(self):
        from builder import overcue_writer as writer,mp3_timing
        self.mapping[1]='/Contents/Artist/Track 1.mp3'
        write_export(self.root.joinpath(*rekordbox_pdb.EXPORT_PDB),self.mapping)
        (self.root/'Contents/Artist/Track 1.mp3').write_bytes(b'ID3'+bytes(200))
        bundle='b'*16;folder=self.root/'CDJMODS/stems'/bundle;folder.mkdir(parents=True)
        for name in writer.ROLES:(folder/writer.role_file(name)).write_bytes(b'OVPGZ003'+b'present')
        index={'schema':writer.INDEX_SCHEMA,'tracks':{'1':{'file_path':self.mapping[1],'bundle':bundle,'frames':40000}}}
        (self.root/'CDJMODS/index.json').write_text(json.dumps(index))
        old={'schema':writer.MANIFEST_SCHEMA,'writer':{'name':writer.WRITER,'version':'0.3.0-preview.1'},
             'source':{'codec':'mp3','sample_rate':44100},'runtime':{'frames':40000},'roles':{name:{} for name in writer.ROLES}}
        (folder/'overcue-manifest.json').write_text(json.dumps(old))
        tagged={'lame_tag':True,'encoder_delay':576,'encoder_padding':936,'delay_frames':1105,'padding_frames':407,'sample_rate':44100}
        with patch.object(mp3_timing,'probe',return_value=tagged):
            row=stem_library.browse({'volume':str(self.root),'query':'Track 1.mp3'},QuietJob())['tracks'][0]
        self.assertEqual((row['stem_status'],row['timeline'],row['needs_regeneration'],row['can_generate']),('present','gapless',True,True))
        self.assertIn('Prepare again',row['stem_label']);self.assertIn('25 ms',row['detail'])
        with patch.object(mp3_timing,'probe',return_value={**tagged,'lame_tag':False}):
            row=stem_library.browse({'volume':str(self.root),'query':'Track 1.mp3'},QuietJob())['tracks'][0]
        self.assertEqual((row['timeline'],row['needs_regeneration']),('player',False))
        old['source']['mp3']={**tagged,'timeline':'player'}
        (folder/'overcue-manifest.json').write_text(json.dumps(old))
        with patch.object(mp3_timing,'probe',side_effect=AssertionError('manifest answers without a probe')):
            row=stem_library.browse({'volume':str(self.root),'query':'Track 1.mp3'},QuietJob())['tracks'][0]
        self.assertEqual((row['timeline'],row['needs_regeneration']),('player',False))
    def test_paths_cannot_escape_usb(self):
        for value in ['/Contents/../../outside.wav','/Contents/C:/bad.wav','/other/track.wav','/Contents/a\\b.wav']:
            with self.assertRaises(ValueError):stem_library.source_path(self.root,value)
    def test_assignment_must_be_complete_and_unique(self):
        path=str(self.root/'Contents/Artist/Track 1.wav')
        assigned=lambda files:stem_import.assigned(*stem_import.stem_files({'files':files},None))
        with self.assertRaises(ValueError):assigned([{'path':path,'group':''}])
        with self.assertRaises(ValueError):assigned([{'path':path,'group':'vocals'}]*2)
        with self.assertRaisesRegex(ValueError,'Assign every stem'):assigned([{'path':path}])
        self.assertEqual(assigned([{'path':path,'group':'drums'}])[0]['group'],'drums')

if __name__=='__main__':unittest.main()
