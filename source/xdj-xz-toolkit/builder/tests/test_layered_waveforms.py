import struct
import tempfile
import unittest
from pathlib import Path
from builder import layered_waveforms as layer,service
from builder.jobs import Job
from builder.tests.test_three_band import pmai,other,section,two_ex,entry

def exported(path='/Contents/Artist/Track.flac'):
    encoded=(path+'\0').encode('utf-16-be')
    pp=b'PPTH'+struct.pack('>III',16,16+len(encoded),len(encoded))+encoded
    return pmai(pp,section(b'PWV4',6,bytes([1]*6)),section(b'PWV5',2,bytes([1,2])),other(b'PCO2',b'original cues'))

class LayeredTests(unittest.TestCase):
    def test_layout_and_real_band_bytes(self):
        path,data=layer.encode(exported(),two_ex(entry(low=200),entry(low=200,mid=100,high=50)*2))
        count,norm,n=struct.unpack_from('>III',data,8)
        self.assertEqual((count,norm),(2,800))
        self.assertEqual(data[20:20+n],path.encode())
        self.assertEqual(data[20+n:],bytes([200,100,50])*2)
        self.assertEqual(layer.path_hash(path),layer.path_hash(path.upper()))
    def test_unsafe_paths_and_absent_detail_rejected(self):
        for path in ('/Contents/../a','/outside/a','/Contents/a\\b'):
            with self.assertRaises(ValueError):layer.encode(exported(path),two_ex(entry(low=1),entry(low=1)))
        with self.assertRaisesRegex(ValueError,'PWV7'):layer.encode(exported(),two_ex(entry(low=1)))
    def test_prepare_repeat_and_originals_unchanged(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);f=root/'PIONEER/USBANLZ/P000/00000001/ANLZ0000.EXT';f.parent.mkdir(parents=True)
            original=exported();bands=two_ex(entry(low=200),entry(low=200)*3)
            f.write_bytes(original);f.with_suffix('.2EX').write_bytes(bands)
            result=service.dispatch({'method':'prepare_layered_waveforms','volume':str(root)},Job())
            self.assertEqual(result['prepared'],1);self.assertTrue(result['requires_new_runtime'])
            self.assertEqual(layer.prepare(root)['already_current'],1)
            self.assertEqual(f.read_bytes(),original);self.assertEqual(f.with_suffix('.2EX').read_bytes(),bands)
            self.assertEqual(len(list((root/layer.FOLDER).glob('*.xzw'))),1)
    def test_malformed_track_does_not_abort_good_track(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for i in range(2):
                f=root/'PIONEER/USBANLZ/P000'/str(i)/'ANLZ0000.EXT';f.parent.mkdir(parents=True)
                f.write_bytes(exported());f.with_suffix('.2EX').write_bytes(b'bad' if i==0 else two_ex(entry(low=1),entry(low=1)))
            result=layer.prepare(root);self.assertEqual(result['prepared'],1);self.assertEqual(len(result['failed']),1)
