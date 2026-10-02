import copy
import hashlib
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

from builder import games
from builder.jobs import Cancelled,Job


class GameSetupTests(unittest.TestCase):
    def fixture(self,root):
        archive=root/'free.zip'
        data=b'IWAD'+b'free game fixture'
        with zipfile.ZipFile(archive,'w') as z:
            for name,value in {'freedoom2.wad':data,'COPYING.txt':b'license','CREDITS.txt':b'credits','CREDITS-MUSIC.txt':b'music credits'}.items():z.writestr(name,value)
        record=copy.deepcopy(games.CATALOG['freedoom']);record['sha256']=hashlib.sha256(data).hexdigest()
        return archive,data,record

    def test_verified_install_reuses_and_preserves_user_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);volume=root/'usb';volume.mkdir();(volume/'music.wav').write_bytes(b'original music')
            archive,data,record=self.fixture(root)
            with patch.dict(games.CATALOG,{'freedoom':record}),patch.object(games,'data_root',return_value=root/'data'),patch.object(games,'download',return_value=archive),patch.object(games.usb,'target_info',return_value={'direct_usb_root':True,'volume_serial':123}):
                first=games.install('freedoom',volume,Job());second=games.install('freedoom',volume,Job())
                self.assertEqual(first,second)
                self.assertEqual(Path(first['wad']).read_bytes(),data)
                self.assertEqual((Path(first['folder'])/'COPYING.txt').read_bytes(),b'license')
                Path(first['wad']).write_bytes(b'custom WAD')
                with self.assertRaises(FileExistsError):games.install('freedoom',volume,Job())
                self.assertEqual(Path(first['wad']).read_bytes(),b'custom WAD')
            self.assertEqual((volume/'music.wav').read_bytes(),b'original music')

    def test_corrupt_extracted_wad_never_published(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);archive,data,record=self.fixture(root);record['sha256']='0'*64
            with patch.dict(games.CATALOG,{'freedoom':record}),patch.object(games,'data_root',return_value=root/'data'),patch.object(games,'download',return_value=archive):
                with self.assertRaisesRegex(ValueError,'integrity'):games.obtain('freedoom',Job())
            self.assertFalse((root/'data/games/Freedoom/freedoom2.wad').exists())

    def test_invalid_destination_rejected_before_download(self):
        with tempfile.TemporaryDirectory() as temp,patch.object(games.usb,'target_info',return_value={'direct_usb_root':False}),patch.object(games,'obtain') as obtain:
            with self.assertRaisesRegex(ValueError,'FAT/FAT32'):games.install('doom',temp,Job())
            obtain.assert_not_called()

    def test_reconnected_usb_rejected_before_first_write(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);volume=root/'usb';volume.mkdir();archive,data,record=self.fixture(root)
            targets=[{'direct_usb_root':True,'volume_serial':n} for n in (1,2)]
            with patch.dict(games.CATALOG,{'freedoom':record}),patch.object(games,'data_root',return_value=root/'data'),patch.object(games,'download',return_value=archive),patch.object(games.usb,'target_info',side_effect=targets):
                with self.assertRaisesRegex(ValueError,'USB changed'):games.install('freedoom',volume,Job())
            self.assertFalse((volume/'VJTOOLS').exists())

    def test_partial_download_failure_reports_and_continues(self):
        with tempfile.TemporaryDirectory() as temp,patch.object(games.usb,'target_info',return_value={'direct_usb_root':True}):
            def install(game,volume,job):
                if game=='myhouse':raise OSError('Author host unavailable')
                return {'game':game,'name':games.CATALOG[game]['label']}
            with patch.object(games,'install',side_effect=install):result=games.prepare_usb(temp,Job())
            self.assertEqual(result['failed_count'],1)
            self.assertEqual([r['game'] for r in result['games'] if r['ok']],['doom','chex','freedoom'])
            self.assertFalse(result['commercial_doom2_downloaded'])

    def test_cancellation_propagates_instead_of_becoming_download_failure(self):
        with tempfile.TemporaryDirectory() as temp,patch.object(games.usb,'target_info',return_value={'direct_usb_root':True}),patch.object(games,'install',side_effect=Cancelled('Cancelled')):
            with self.assertRaises(Cancelled):games.prepare_usb(temp,Job())


if __name__=='__main__':unittest.main()
