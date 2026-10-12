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
        self.assertEqual(summary,{'counts':{'present':0,'missing':0,'unsupported':0,'unavailable':0,'error':0},'stems_bytes':9,'tracks':0})


if __name__=='__main__':
    unittest.main()
