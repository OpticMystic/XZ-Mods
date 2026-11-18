"""Checks prepare_av_submission.py on a fake release built from this Python's own signed executable: the PE audit
reads real version and signature data, the package records every Windows file, flags shipped debug files and
Windows executables in the player runtime, writes vendor text with the right links, and makes no network request
unless asked. Run on Windows: python tools/test_prepare_av_submission.py"""
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock
import urllib.error
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare_av_submission as av

PE = Path(sys.executable)


def no_network(*args, **kwargs):
    raise AssertionError('prepare_av_submission made a network request without being asked')


@unittest.skipUnless(os.name == 'nt', 'needs a Windows PE executable to audit')
class PackageTest(unittest.TestCase):
    def setUp(self):
        self.temp = Path(tempfile.mkdtemp(prefix='xz-av-test-'))
        self.release = self.temp / '9.9.9-Test.1'
        self.release.mkdir()
        self.installer = self.release / 'XZ.Mods_9.9.9-Test.1_x64-setup.exe'
        shutil.copyfile(PE, self.installer)
        (self.release / 'latest.json').write_text(json.dumps({'platforms': {'windows-x86_64': {
            'url': 'https://github.com/OpticMystic/XZ-Mods/releases/download/v9.9.9-test.1/XZ.Mods_9.9.9-Test.1_x64-setup.exe'}}}))
        self.app = self.temp / 'xz-mods-builder.exe'
        shutil.copyfile(PE, self.app)
        self.resources = self.temp / 'resources'
        (self.resources / 'runtime').mkdir(parents=True)
        shutil.copyfile(PE, self.resources / 'xz-audio-helper.exe')
        (self.resources / 'xz-audio-helper.pdb').write_bytes(b'Microsoft C/C++ MSF 7.00\r\n')
        shutil.copyfile(PE, self.resources / 'runtime/player-tool.exe')
        (self.resources / 'runtime/libxz-mods.so').write_bytes(b'\x7fELF' + bytes(60))
        (self.resources / 'notes.txt').write_text('not a program')

    def tearDown(self):
        shutil.rmtree(self.temp)

    def test_audit_reads_version_manifest_and_signature(self):
        pe = av.audit_pe(PE)
        self.assertEqual(pe['version'].get('CompanyName'), 'Python Software Foundation')
        self.assertTrue(pe['manifest'])
        self.assertTrue(pe['authenticode'])
        self.assertEqual(pe['machine'], 'x64')
        self.assertIsNone(av.audit_pe(self.resources / 'runtime/libxz-mods.so'))

    def test_package_records_files_and_flags_findings_offline(self):
        output = self.temp / 'package'
        with mock.patch.object(urllib.request, 'urlopen', no_network), mock.patch.dict(os.environ, {'VT_API_KEY': ''}):
            context = av.main(['--release-dir', str(self.release), '--output', str(output), '--no-extract',
                               '--app-exe', str(self.app), '--resources', str(self.resources)])
        names = {a['name'] for a in context['artifacts']}
        self.assertEqual(names, {self.installer.name, 'xz-mods-builder.exe', 'resources/xz-audio-helper.exe',
                                 'resources/runtime/player-tool.exe'})
        self.assertEqual(context['tag'], 'v9.9.9-test.1')
        findings = {(f['file'], f['finding'].split('.')[0]) for f in context['findings'] if f['level'] == 'fix'}
        self.assertIn(('resources/xz-audio-helper.pdb', 'Debug database shipped in the installer'), findings)
        self.assertIn(('resources/runtime/player-tool.exe', 'Windows executable inside the XDJ-XZ runtime folder'), findings)
        sums = (output / 'SHA256SUMS.txt').read_text()
        self.assertIn(hashlib.sha256(self.installer.read_bytes()).hexdigest() + '  ' + self.installer.name, sums)
        microsoft = (output / 'submissions/microsoft.txt').read_text(encoding='utf-8')
        self.assertIn('https://github.com/OpticMystic/XZ-Mods/releases/download/v9.9.9-test.1/' + self.installer.name, microsoft)
        self.assertEqual(len(list((output / 'submissions').iterdir())), len(av.VENDORS))
        self.assertEqual(sorted(p.name for p in (output / 'upload').iterdir()), ['xz-audio-helper.exe', 'xz-mods-builder.exe'])
        self.assertIn('Nothing in this folder has been uploaded', (output / 'CHECKLIST.md').read_text(encoding='utf-8'))
        self.assertNotIn('virustotal', context)

    def test_virustotal_needs_a_key(self):
        with mock.patch.dict(os.environ, {'VT_API_KEY': ''}), self.assertRaises(SystemExit):
            av.main(['--release-dir', str(self.release), '--output', str(self.temp / 'p'), '--virustotal-lookup',
                     '--no-extract', '--app-exe', str(self.app), '--resources', str(self.resources)])


class VirusTotalTest(unittest.TestCase):
    def test_lookup_and_small_upload_requests(self):
        sent = []

        class Response(io.BytesIO):
            status = 200

            def __enter__(self):
                return self

            def __exit__(self, *exc):
                return False

        def opener(request, timeout):
            sent.append(request)
            if request.get_method() == 'GET':
                raise urllib.error.HTTPError(request.full_url, 404, 'Not Found', {}, io.BytesIO(b'{"error":{"code":"NotFoundError"}}'))
            return Response(b'{"data":{"id":"analysis-1"}}')

        with tempfile.TemporaryDirectory() as temp:
            sample = Path(temp) / 'sample.exe'
            sample.write_bytes(b'MZ' + bytes(100))
            client = av.VirusTotal('key', opener=opener, pause=0)
            self.assertEqual(client.lookup('ab' * 32), {'known': False})
            self.assertEqual(client.upload(sample), {'uploaded': True, 'status': 200, 'analysis': 'analysis-1'})
        self.assertEqual(sent[0].full_url, 'https://www.virustotal.com/api/v3/files/' + 'ab' * 32)
        self.assertEqual(sent[1].full_url, 'https://www.virustotal.com/api/v3/files')
        self.assertEqual(sent[1].get_header('X-apikey'), 'key')
        self.assertIn(b'filename="sample.exe"', sent[1].data)


if __name__ == '__main__':
    unittest.main()
