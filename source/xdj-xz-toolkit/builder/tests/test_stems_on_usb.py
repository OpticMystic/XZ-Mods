import os
import tempfile
from pathlib import Path
import unittest

from builder import stem_library


class StemsOnUsbTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)

    def test_empty_usb_reports_zeros(self):
        self.assertEqual(stem_library.stems_on_usb(self.root),{'tracks':0,'bytes':0})
        (self.root/'CDJMODS/stems').mkdir(parents=True)
        self.assertEqual(stem_library.stems_on_usb(str(self.root)),{'tracks':0,'bytes':0})

    def test_counts_folders_and_their_file_bytes(self):
        stems=self.root/'CDJMODS/stems'
        (stems/'one').mkdir(parents=True);(stems/'two').mkdir()
        (stems/'one/a.bin').write_bytes(b'a'*10);(stems/'one/b.bin').write_bytes(b'b'*5)
        (stems/'two/c.bin').write_bytes(b'c'*100)
        (stems/'loose.bin').write_bytes(b'z'*1000)
        self.assertEqual(stem_library.stems_on_usb(self.root),{'tracks':2,'bytes':115})

    def test_symlinked_folder_is_ignored(self):
        stems=self.root/'CDJMODS/stems';(stems/'real').mkdir(parents=True)
        (stems/'real/a.bin').write_bytes(b'a'*3)
        outside=self.root/'outside';outside.mkdir();(outside/'big.bin').write_bytes(b'x'*50)
        try:os.symlink(outside,stems/'linked',target_is_directory=True)
        except (OSError,NotImplementedError) as error:self.skipTest(f'symlinks unavailable: {error}')
        self.assertEqual(stem_library.stems_on_usb(self.root),{'tracks':1,'bytes':3})

    def test_library_summary_reports_the_same_bytes(self):
        stems=self.root/'CDJMODS/stems';(stems/'one').mkdir(parents=True);(stems/'one/a.bin').write_bytes(b'a'*9)
        summary=stem_library.library_summary(self.root,[],{},None)
        self.assertEqual(summary,{'counts':{'present':0,'missing':0,'unsupported':0,'unavailable':0,'error':0},
                                  'stem_sample_rates':{'44100':0,'96000':0},'stems_bytes':9,'tracks':0,'overcue_index_v2':False})

    def test_library_summary_counts_stems_by_sample_rate_and_spots_overcue_index_v2(self):
        music=self.root/'Contents';music.mkdir()
        tracks=[]
        for number in range(1,5):
            (music/f'{number}.wav').write_bytes(b'RIFF');tracks.append({'id':number,'file_path':f'/Contents/{number}.wav'})
        index={'tracks':{'1':{'bundle':'a'*16,'file_path':'/Contents/1.wav','sample_rate':44100},
                         '2':{'bundle':'b'*16,'file_path':'/Contents/2.wav'}},
               'tracks_onelibrary':{'9':{'bundle':'c'*16,'file_path':'/Contents/3.wav','sample_rate':44100}}}
        summary=stem_library.library_summary(self.root,tracks,index,None)
        # An entry without sample_rate is the 96 kHz profile; track 4 has no stems.
        self.assertEqual((summary['counts']['present'],summary['counts']['missing']),(3,1))
        self.assertEqual(summary['stem_sample_rates'],{'44100':2,'96000':1})
        self.assertFalse(summary['overcue_index_v2'])
        (self.root/'CDJMODS/index-v2').mkdir(parents=True)
        self.assertTrue(stem_library.library_summary(self.root,tracks,index,None)['overcue_index_v2'])


if __name__=='__main__':
    unittest.main()
