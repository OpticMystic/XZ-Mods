import ctypes
import tempfile
from pathlib import Path
from types import SimpleNamespace
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

    def test_refuses_a_computer_drive_root_before_network(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(usb,'target_info',return_value={'direct_usb_root':False,'volume_root':temporary}), \
                 patch.object(service,'official_firmware') as download:
                with self.assertRaisesRegex(ValueError,'not a USB'):
                    service.dispatch({'method':'prepare_usb','volume':temporary,'experimental':True},Job())
                download.assert_not_called()

    def test_staging_folder_gets_the_loader_without_games(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)/'staging';folder.mkdir()
            with patch.object(usb,'target_info',return_value={'direct_usb_root':False,'volume_root':temporary}),                  patch.object(service,'official_firmware',return_value=folder/'firmware.zip') as download,                  patch('builder.boot_support.ensure_boot_key',return_value=folder/'boot.key'),                  patch.object(usb,'build_usb',return_value={'image':str(folder/'autoexec.bin')}) as build,                  patch.object(service.games,'prepare_usb') as games:
                result=service.dispatch({'method':'prepare_usb','volume':str(folder),'experimental':True,'games':['doom']},Job())
            download.assert_called_once()
            self.assertEqual(build.call_args.args[0],folder)
            games.assert_not_called()
            self.assertIsNone(result['game_setup'])

    def test_staging_folder_keeps_an_existing_loader(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)/'staging';folder.mkdir();(folder/'autoexec.bin').write_bytes(b'keep')
            with patch.object(usb,'target_info',return_value={'direct_usb_root':False,'volume_root':temporary}),                  patch.object(service,'official_firmware') as download:
                with self.assertRaisesRegex(FileExistsError,'already has autoexec.bin'):
                    service.dispatch({'method':'prepare_usb','volume':str(folder),'experimental':True},Job())
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



class FakeKernel:
    """GetVolumePathNameW, GetVolumeInformationW and GetDriveTypeW for one volume rooted at root."""
    def __init__(self,root,filesystem,drive_type=2,read_only=False):
        kernel=self
        def path_name(path,buffer,size):buffer.value=root;return 1
        def information(name,label,size,serial,length,flags,fs_name,fs_size):
            serial._obj.value=1234;flags._obj.value=0x80000 if read_only else 0;fs_name.value=filesystem;return 1
        self.GetVolumePathNameW=path_name;self.GetVolumeInformationW=information
        self.GetDriveTypeW=lambda name:drive_type


class TargetInfoTests(unittest.TestCase):
    """Any USB drive root takes the loader, whatever its file system. Computer disks and read-only drives do not."""
    def target(self,filesystem,*,drive_type=2,read_only=False,folder=False,boot_loader=True):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary).resolve();path=root/'staging' if folder else root
            path.mkdir(exist_ok=True)
            with patch.object(usb.os,'name','nt'),patch.object(ctypes,'windll',SimpleNamespace(kernel32=FakeKernel(str(root),filesystem,drive_type,read_only)),create=True):
                return usb.target_info(path,boot_loader=boot_loader)

    def test_any_file_system_on_a_usb_drive_takes_the_loader(self):
        for filesystem in ('FAT32','FAT','exFAT','NTFS'):
            with self.subTest(filesystem=filesystem):
                target=self.target(filesystem)
                self.assertTrue(target['direct_usb_root']);self.assertFalse(target['requires_copy_to_usb_root'])
                self.assertEqual(target['filesystem'],filesystem.upper())

    def test_a_usb_disk_that_reports_fixed_counts_by_its_file_system(self):
        self.assertTrue(self.target('exFAT',drive_type=3)['direct_usb_root'])
        self.assertTrue(self.target('FAT32',drive_type=3)['direct_usb_root'])

    def test_a_computer_disk_root_is_refused_but_a_folder_on_it_is_staging(self):
        with self.assertRaisesRegex(ValueError,'not a USB'):self.target('NTFS',drive_type=3)
        self.assertFalse(self.target('NTFS',drive_type=3,boot_loader=False)['direct_usb_root'])
        folder=self.target('NTFS',drive_type=3,folder=True)
        self.assertTrue(folder['requires_copy_to_usb_root']);self.assertFalse(folder['direct_usb_root'])

    def test_a_read_only_usb_is_refused(self):
        with self.assertRaisesRegex(ValueError,'read-only'):self.target('exFAT',read_only=True)
        with self.assertRaisesRegex(ValueError,'read-only'):self.target('FAT32',read_only=True)

    def test_exfat_and_ntfs_roots_reach_the_loader_write(self):
        for filesystem in ('exFAT','NTFS'):
            with self.subTest(filesystem=filesystem),tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary).resolve()
                with patch.object(usb.os,'name','nt'),patch.object(ctypes,'windll',SimpleNamespace(kernel32=FakeKernel(str(root),filesystem)),create=True):
                    path,target=usb.require_loader_target(root)
                self.assertEqual(path,root);self.assertTrue(target['direct_usb_root'])

if __name__=='__main__':unittest.main()
