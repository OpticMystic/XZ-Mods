import copy
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from builder import controller_queue as queue
from builder.jobs import Job
from builder.support_contract import CONTRACT, parse_installation_id, format_installation_id, parse_loader_id, format_loader_id
from builder.support_sources import identity

ROOT = Path(__file__).resolve().parents[4]

class ControllerQueueTests(unittest.TestCase):
    def setUp(self):
        output = Path(os.environ['XZ_SUPPORT_TEST_OUTPUT']);output.mkdir(parents=True,exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=output);self.addCleanup(self.temp.cleanup)
        self.volume = Path(self.temp.name)/'usb';self.volume.mkdir()
        self.job = Job(None)
        self.report = json.loads((ROOT/'spec/fixtures/controller-pending-report.json').read_text())
        self.loader = json.loads((ROOT/'spec/fixtures/loader-identity.json').read_text())
        self.expected = identity(self.volume)

    def file(self,report=None):
        report = report or self.report
        path = self.volume/queue.PENDING/(report['report_id']+'.json');path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(json.dumps(report,separators=(',',':')).encode())
        return path, hashlib.sha256(path.read_bytes()).hexdigest()

    def acceptance(self,digest):
        return {'schema':'xz-controller-accepted/1','report_id':self.report['report_id'],'report_reference':'R-4F7K-2M9Q','payload_sha256':digest}

    def test_shared_identity_vectors_and_firmware_fixture(self):
        queue.validate_pending(self.report)
        queue.validate_pending({**self.report,'description':'😀'*280})
        for name,parse,format in [('installation_id',parse_installation_id,format_installation_id),('loader_id',parse_loader_id,format_loader_id)]:
            for vector in CONTRACT['identifiers'][name]['vectors']:
                self.assertEqual(parse(vector['input']),vector['value'])
                if vector['value'] is not None:self.assertEqual(format(vector['value']),vector['canonical'])

    def test_identity_write_rotation_conflict_and_no_installation_key(self):
        self.assertIsNone(queue.read_identity(self.volume))
        queue.write_identity(self.volume,self.expected,self.loader,None)
        self.assertEqual(queue.read_identity(self.volume),self.loader)
        rotated = {**self.loader,'loader_key':'c'*64}
        queue.write_identity(self.volume,self.expected,rotated,self.loader)
        with self.assertRaises(ValueError):queue.write_identity(self.volume,self.expected,self.loader,self.loader)
        self.assertEqual(queue.read_identity(self.volume),rotated)
        with self.assertRaises(ValueError):queue.write_identity(self.volume,self.expected,{**rotated,'installation_key':'secret'},rotated)

    def test_loader_service_wraps_authorized_build_with_identity_write(self):
        from builder import service, mod_versions
        from contextlib import contextmanager
        @contextmanager
        def selected(*args):yield self.volume,{'label':'test'}
        request={'method':'build_usb','firmware':'synthetic','key':'synthetic','experimental':True,'volume':str(self.volume),'_loader_identity':self.loader,'_previous_loader_identity':None,'_loader_volume_identity':self.expected}
        with patch.object(service,'resources',return_value=self.volume),patch.object(mod_versions,'selected_resources',selected),patch.object(mod_versions,'settings_change',return_value=None),patch.object(mod_versions,'record_loader'),patch.object(service.usb,'build_usb',return_value={'direct_usb_root':False}):
            result=service._dispatch_request(request,self.job)
        self.assertEqual(result['loader_identity']['loader_id'],self.loader['loader_id'])
        self.assertEqual(queue.read_identity(self.volume),self.loader)
        self.assertNotIn('loader_key',json.dumps(result))

    def test_strict_scan_and_acceptance_move_exact_bytes_only(self):
        path,digest=self.file();raw=path.read_bytes()
        scan=queue.scan(self.job,[{'volume':str(self.volume)}])
        self.assertEqual(len(scan['reports']),1);self.assertEqual(scan['reports'][0]['sha256'],digest)
        self.assertTrue(path.exists(),'scan is read-only')
        for receipt in [{},self.acceptance('0'*64)]:
            with self.assertRaises(ValueError):queue.mark_sent(self.volume,self.expected,self.report['report_id'],digest,receipt,self.job)
            self.assertTrue(path.exists())
        queue.mark_sent(self.volume,self.expected,self.report['report_id'],digest,self.acceptance(digest),self.job)
        self.assertFalse(path.exists());self.assertEqual((self.volume/queue.SENT/path.name).read_bytes(),raw)

    def test_changed_file_and_changed_root_cannot_be_moved(self):
        path,digest=self.file()
        path.write_text('{}')
        with self.assertRaises(ValueError):queue.mark_sent(self.volume,self.expected,self.report['report_id'],digest,self.acceptance(digest),self.job)
        self.assertTrue(path.exists())
        self.file()
        with self.assertRaises(ValueError):queue.read_pending(self.volume,[0,0],self.report['report_id'],digest,self.job)

    def test_unknown_fields_enums_duplicate_json_and_filename_rejected(self):
        for update in [{'screenshot':'no'},{'screen_state':{**self.report['screen_state'],'stock_page':'TITLE'}},{'description':'a'*281},{'installation_id':None}]:
            with self.assertRaises(ValueError):queue.validate_pending({**self.report,**update})
        with self.assertRaises(ValueError):queue.decode(b'{"a":1,"a":2}')
        path,_=self.file();path.rename(path.with_name('f'*32+'.json'))
        self.assertEqual(queue.scan(self.job,[{'volume':str(self.volume)}])['reports'],[])

    def test_scan_does_not_read_symlinks_or_oversized_reports(self):
        path,_=self.file();path.write_bytes(b' '* (CONTRACT['limits']['report_bytes']+1))
        self.assertEqual(queue.scan(self.job,[{'volume':str(self.volume)}])['reports'],[])
        path.unlink()
        outside=Path(self.temp.name)/'outside.json';outside.write_text(json.dumps(self.report))
        try:path.symlink_to(outside)
        except OSError:self.skipTest('Symlink privilege unavailable')
        self.assertEqual(queue.scan(self.job,[{'volume':str(self.volume)}])['reports'],[])

    def test_existing_sent_file_is_not_overwritten(self):
        path,digest=self.file();sent=self.volume/queue.SENT/path.name;sent.parent.mkdir(parents=True);sent.write_text('other')
        with self.assertRaises(ValueError):queue.mark_sent(self.volume,self.expected,self.report['report_id'],digest,self.acceptance(digest),self.job)
        self.assertEqual(sent.read_text(),'other');self.assertTrue(path.exists())

    def test_scan_recovers_loader_identity_without_reports_and_skips_unrelated_disks(self):
        queue.write_identity(self.volume,self.expected,self.loader,None)
        roots=[]
        for number in range(5):
            root=Path(self.temp.name)/('empty-'+str(number));root.mkdir();roots.append({'volume':str(root)})
        result=queue.scan(self.job,roots+[{'volume':str(self.volume)}])
        self.assertEqual(result['reports'],[])
        self.assertEqual(result['loaders'][0]['loader_identity'],self.loader)

if __name__=='__main__':unittest.main()
