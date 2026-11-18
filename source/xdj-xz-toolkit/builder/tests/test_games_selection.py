import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

from builder import games, service, usb
from builder.jobs import Job


class GameSelectionTests(unittest.TestCase):
    def test_missing_request_copies_every_game(self):
        self.assertEqual(games.selection(None),list(games.CATALOG))

    def test_empty_list_copies_nothing(self):
        self.assertEqual(games.selection([]),[])

    def test_list_keeps_catalog_order_and_rejects_unknown(self):
        self.assertEqual(games.selection(['freedoom','doom']),['doom','freedoom'])
        with self.assertRaisesRegex(ValueError,'Unknown game'):games.selection(['quake'])
        with self.assertRaisesRegex(ValueError,'list'):games.selection('doom')

    def test_prepare_usb_only_installs_the_selection(self):
        with tempfile.TemporaryDirectory() as temp:
            with patch.object(games,'_target',return_value=('same',)),patch.object(games,'install',side_effect=lambda game,volume,job:{'game':game,'name':game}) as install:
                result=games.prepare_usb(temp,Job(),['chex'])
            install.assert_called_once()
            self.assertEqual([game['game'] for game in result['games']],['chex'])
            self.assertEqual(result['skipped'],['doom','freedoom'])
            self.assertEqual(result['failed_count'],0)

    def test_installed_reads_the_usb_without_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);folder=root/'VJTOOLS/Games/ChexQuest';folder.mkdir(parents=True);(folder/'chex.wad').write_bytes(b'x')
            report={game['id']:game['installed'] for game in games.installed(root)}
            self.assertEqual(report,{'doom':False,'chex':True,'freedoom':False})

    def test_prepare_usb_request_passes_games_through(self):
        with tempfile.TemporaryDirectory() as temporary:
            volume=Path(temporary)
            with patch.object(usb,'target_info',return_value={'direct_usb_root':True}), \
                 patch.object(service,'official_firmware',return_value=volume/'firmware.zip'), \
                 patch('builder.boot_support.ensure_boot_key',return_value=volume/'boot.key'), \
                 patch.object(service,'resources',return_value=volume/'resources'), \
                 patch.object(usb,'build_usb',return_value={'image':str(volume/'autoexec.bin')}), \
                 patch.object(service.games,'prepare_usb',return_value={'failed_count':0}) as prepared:
                service._dispatch({'method':'prepare_usb','volume':str(volume),'experimental':True,'games':[]},Job())
            prepared.assert_called_once_with(volume,unittest.mock.ANY,[])


if __name__ == '__main__':
    unittest.main()
