import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

from builder import service, usb
from builder.jobs import Job


class PrepareUsbTests(unittest.TestCase):
    def test_rejects_existing_loader_before_network(self):
        with tempfile.TemporaryDirectory() as temporary:
            volume=Path(temporary)
            (volume/'autoexec.bin').write_bytes(b'keep this loader')
            with patch.object(usb,'target_info',return_value={'direct_usb_root':True}), \
                 patch.object(service,'official_firmware') as download:
                with self.assertRaisesRegex(FileExistsError,'already has autoexec.bin'):
                    service.dispatch({'method':'prepare_usb','volume':str(volume),'experimental':True},Job())
                download.assert_not_called()
            self.assertEqual((volume/'autoexec.bin').read_bytes(),b'keep this loader')

    def test_requires_fat_usb_root_before_network(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(usb,'target_info',return_value={'direct_usb_root':False}), \
                 patch.object(service,'official_firmware') as download:
                with self.assertRaisesRegex(ValueError,'FAT/FAT32 USB drive'):
                    service.dispatch({'method':'prepare_usb','volume':temporary,'experimental':True},Job())
                download.assert_not_called()

    def test_one_job_downloads_then_builds(self):
        with tempfile.TemporaryDirectory() as temporary:
            volume=Path(temporary)
            with patch.object(usb,'target_info',return_value={'direct_usb_root':True}), \
                 patch.object(service,'official_firmware',return_value=volume/'firmware.zip') as download, \
                 patch('builder.boot_support.ensure_boot_key',return_value=volume/'boot.key') as support, \
                 patch.object(service,'resources',return_value=volume/'resources'), \
                 patch.object(usb,'build_usb',return_value={'image':str(volume/'autoexec.bin')}) as build, \
                 patch.object(service.games,'prepare_usb',return_value={'failed_count':0}) as games:
                result=service.dispatch({'method':'prepare_usb','volume':str(volume),'experimental':True},Job())
            download.assert_called_once()
            support.assert_called_once()
            self.assertEqual(build.call_args.args[:3],(volume,volume/'firmware.zip',volume/'boot.key'))
            self.assertTrue(result['one_step'])
            games.assert_called_once()
            self.assertEqual(games.call_args.args[0],volume)
            self.assertEqual(result['game_setup']['failed_count'],0)
            self.assertEqual(result['firmware_source'],'AlphaTheta')

    def test_update_keeps_loader_result_and_reports_game_failure(self):
        from builder import managed_usb
        with tempfile.TemporaryDirectory() as temporary:
            volume=Path(temporary)
            with patch.object(managed_usb,'check_target'),patch.object(managed_usb,'_check_revision'), \
                 patch.object(managed_usb,'update_loader',return_value={'format':'xz-mods-loader-updated/1','image':'autoexec.bin'}), \
                 patch.object(service,'resources',return_value=volume/'resources'), \
                 patch.object(usb,'target_info',return_value={'direct_usb_root':True}), \
                 patch.object(service.games,'prepare_usb',return_value={'failed_count':1}) as games:
                result=service.dispatch({'method':'update_usb','volume':temporary,'expected_identity':{},'expected_loader':{},'firmware':'firmware.zip','key':'boot.key','experimental':True},Job())
            self.assertEqual(result['format'],'xz-mods-loader-updated/1')
            self.assertEqual(result['game_setup']['failed_count'],1)
            games.assert_called_once()

    def test_staging_folder_build_does_not_try_to_install_games(self):
        with patch('builder.boot_support.ensure_boot_key',return_value='boot.key'), \
             patch.object(service,'resources',return_value='resources'), \
             patch.object(usb,'build_usb',return_value={'image':'folder/autoexec.bin','direct_usb_root':False}), \
             patch.object(service.games,'prepare_usb') as games:
            result=service.dispatch({'method':'build_usb','volume':'folder','firmware':'firmware.zip','experimental':True},Job())
        self.assertEqual(result['image'],'folder/autoexec.bin')
        games.assert_not_called()


if __name__=='__main__':unittest.main()
