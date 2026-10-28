import hashlib,json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
import subprocess
from types import SimpleNamespace
from builder import managed_usb as usb,usb_settings as settings
from builder.jobs import Job,Cancelled

SCHEMA=Path(__file__).parent/'fixtures/settings-schema.json'
class QuietJob(Job):
    def progress(self,*args):self.check()

class ManagedUsbTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name).resolve();self.volume=self.root/'usb';self.volume.mkdir()
        self.resources=self.root/'resources';runtime=self.resources/'runtime';runtime.mkdir(parents=True)
        data=SCHEMA.read_bytes();(runtime/'settings-schema.json').write_bytes(data)
        (runtime/'manifest.json').write_text(json.dumps({'runtime_sha256':'a'*64,'settings_schema_sha256':hashlib.sha256(data).hexdigest()}))
        self.schema=settings.contract(self.resources);self.values=settings.defaults(self.schema)
        self.firmware=self.root/'firmware';self.firmware.write_bytes(b'fixture')
        self.loader=self.volume/'autoexec.bin';self.loader.write_bytes(b'old verified loader')
        (self.volume/'Contents').mkdir();(self.volume/'Contents/song.wav').write_bytes(b'unchanged music')
        self.job=QuietJob()
    def snapshot(self):return usb.inspect(self.volume,self.resources)
    def build(self,folder,*args):
        path=Path(folder)/'autoexec.bin';path.write_bytes(b'new verified loader');return {'image':str(path)}
    def update(self,snapshot=None,job=None):
        s=snapshot or self.snapshot()
        with patch.object(usb.usb,'build_usb',self.build):
            return usb.update_loader(self.volume,s['identity'],s['loader'],self.firmware,self.firmware,self.resources,job or self.job)
    def test_inspect_reports_space_stems_and_rekordbox(self):
        (self.volume/'CDJMODS/stems/track').mkdir(parents=True);(self.volume/'CDJMODS/stems/track/a.bin').write_bytes(b'x'*7)
        report=self.snapshot()
        self.assertGreater(report['space']['total_bytes'],0);self.assertLessEqual(report['space']['free_bytes'],report['space']['total_bytes'])
        self.assertEqual(report['stems'],{'tracks':1,'bytes':7});self.assertFalse(report['rekordbox'])
        (self.volume/'PIONEER/rekordbox').mkdir(parents=True);(self.volume/'PIONEER/rekordbox/export.pdb').write_bytes(b'pdb')
        self.assertTrue(self.snapshot()['rekordbox'])
    def test_identity_keeps_large_os_ids_as_strings(self):
        observed=usb.identity(self.volume)
        self.assertIsInstance(observed["device"],str);self.assertIsInstance(observed["directory"],str)
    def test_native_settings_grammar(self):
        full=settings.serialize(self.values,self.schema)
        self.assertEqual(settings.parse(full,self.schema),self.values)
        lines=full.splitlines(keepends=True)
        for count in self.schema['accepted_field_counts']:
            self.assertEqual(settings.parse(b''.join(lines[:count+1]),self.schema),self.values)
        for count in range(13,22):
            with self.assertRaises(ValueError):settings.parse(b''.join(lines[:count+1]),self.schema)
        for broken in [full.replace(b'\n',b'\r\n'),full.replace(b'stems=1',b'stems=01'),full.replace(b'theme=0',b'theme=24'),full+b'future=1\n',full.replace(b'gate=0\n',b'gate=0\v')]:
            with self.assertRaises(ValueError):settings.parse(broken,self.schema)
        for value in [True,1.5,'1',-1,2]:
            with self.assertRaises(ValueError):settings.serialize({**self.values,'stems':value},self.schema)
    def test_update_retains_backup_media_and_settings(self):
        cfg=self.volume/usb.SETTINGS;cfg.parent.mkdir();cfg.write_bytes(settings.serialize(self.values,self.schema));before=cfg.read_bytes()
        result=self.update();self.assertEqual(self.loader.read_bytes(),b'new verified loader')
        self.assertEqual(Path(result['backup']).read_bytes(),b'old verified loader')
        self.assertEqual(cfg.read_bytes(),before);self.assertEqual((self.volume/'Contents/song.wav').read_bytes(),b'unchanged music')
        snap=result['inspection'];restored=usb.restore_loader(self.volume,snap['identity'],snap['loader'],result['transaction_id'],self.resources,self.job)
        self.assertEqual(self.loader.read_bytes(),b'old verified loader');self.assertEqual(Path(restored['backup']).read_bytes(),b'new verified loader')
    def test_conflict_and_volume_swap_do_not_build(self):
        snapshot=self.snapshot();self.loader.write_bytes(b'outside edit')
        with self.assertRaisesRegex(ValueError,'changed'):self.update(snapshot)
        self.assertEqual(self.loader.read_bytes(),b'outside edit')
        with self.assertRaisesRegex(ValueError,'USB changed'):usb.check_target(self.volume,{**snapshot['identity'],'directory':-1})
    def test_save_missing_then_update_and_conflict(self):
        snap=self.snapshot();values={**self.values,'theme':23,'stem_bank':1,'jump_8':9,'stems_overlay':0}
        result=usb.save_settings(self.volume,snap['identity'],snap['settings']['revision'],values,self.resources,self.job)
        self.assertEqual(result['settings']['values'],values);self.assertEqual(self.loader.read_bytes(),b'old verified loader')
        with self.assertRaisesRegex(ValueError,'changed'):usb.save_settings(self.volume,snap['identity'],snap['settings']['revision'],self.values,self.resources,self.job)
        again=usb.save_settings(self.volume,result['identity'],result['settings']['revision'],self.values,self.resources,self.job)
        self.assertTrue(Path(again['backup']).exists())
    def test_unsupported_settings_survive_loader_update(self):
        cfg=self.volume/usb.SETTINGS;cfg.parent.mkdir();cfg.write_bytes(b'XZ_MODS_SETTINGS 9\nfuture=7\n')
        snap=self.snapshot();self.assertFalse(snap['settings']['editable'])
        with self.assertRaises(ValueError):usb.save_settings(self.volume,snap['identity'],snap['settings']['revision'],self.values,self.resources,self.job)
        self.update();self.assertEqual(cfg.read_bytes(),b'XZ_MODS_SETTINGS 9\nfuture=7\n')
    def test_cancel_before_commit_keeps_original(self):
        class CancelAtCommit(QuietJob):
            count=0
            def check(self):
                self.count+=1
                if self.count==3:raise Cancelled()
        with self.assertRaises(Cancelled):self.update(job=CancelAtCommit())
        self.assertEqual(self.loader.read_bytes(),b'old verified loader');self.assertTrue(self.snapshot()['backups'])
    def test_replace_failure_retains_old_and_backup(self):
        original=usb.os.replace
        def fail(src,dst):
            if Path(dst)==self.loader:raise PermissionError('busy destination')
            return original(src,dst)
        with patch.object(usb.os,'replace',fail),self.assertRaises(PermissionError):self.update()
        self.assertEqual(self.loader.read_bytes(),b'old verified loader');self.assertTrue(self.snapshot()['backups'])
    def test_receipt_path_attack_and_tampered_backup(self):
        result=self.update();folder=Path(result['backup']).parent
        record=json.loads((folder/'receipt.json').read_text());record['target']='../user-data';(folder/'receipt.json').write_text(json.dumps(record))
        snap=self.snapshot()
        with self.assertRaises(ValueError):usb.restore_loader(self.volume,snap['identity'],snap['loader'],folder.name,self.resources,self.job)
        self.assertEqual(self.loader.read_bytes(),b'new verified loader')
    def test_schema_binding_low_space_and_lock_collision(self):
        with patch.object(usb.shutil,'disk_usage',return_value=type('Space',(),{'free':0,'total':0})()),self.assertRaisesRegex(ValueError,'free space'):self.update()
        (self.volume/'.xzmods-write.lock').write_bytes(b'unrelated file')
        with self.assertRaisesRegex(ValueError,'recognized lock'):self.update()
        self.assertEqual(self.loader.read_bytes(),b'old verified loader')
        (self.resources/'runtime/settings-schema.json').write_bytes(b'{}')
        with self.assertRaisesRegex(ValueError,'does not match'):settings.contract(self.resources)

class MacDiscoverTests(unittest.TestCase):
    def test_offers_only_local_disk_mounts_from_volumes(self):
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup)
        volumes=Path(temp.name).resolve()
        for name in ('KINGSTON','Folder','Share','Unreadable','Ejected','LOCKED'):(volumes/name).mkdir()
        def mac_volume(root):
            if root.name=='KINGSTON':return {'volume_root':str(root),'filesystem':'FAT32','writable':True}
            if root.name=='LOCKED':return {'volume_root':str(root),'filesystem':'EXFAT','writable':False}
            if root.name=='Folder':return {'volume_root':'/'}
            if root.name=='Share':raise ValueError('Choose a mounted USB volume or a local staging folder')
            if root.name=='Unreadable':raise subprocess.CalledProcessError(1,['/usr/sbin/diskutil'])
            raise OSError('volume went away')
        fake_os=SimpleNamespace(name='posix',stat=lambda path:SimpleNamespace(st_dev=1 if str(path)=='/' else 2))
        with patch.object(usb.sys,'platform','darwin'),patch.object(usb,'os',fake_os),\
             patch.object(usb,'Path',lambda path:volumes if path=='/Volumes' else Path(path)),\
             patch('builder.host.mac_volume',mac_volume):
            found=usb.discover(None)['usbs']
        # Every local disk is listed with what the app needs to know: whether a loader can go on it.
        self.assertEqual(sorted(drive['volume'] for drive in found),[str(volumes/'KINGSTON'),str(volumes/'LOCKED')])
        drives={Path(drive['volume']).name:drive for drive in found}
        self.assertTrue(drives['KINGSTON']['removable'])
        self.assertEqual((drives['KINGSTON']['filesystem'],drives['KINGSTON']['writable']),('FAT32',True))
        self.assertEqual((drives['LOCKED']['filesystem'],drives['LOCKED']['writable']),('EXFAT',False))

if __name__=='__main__':unittest.main()
