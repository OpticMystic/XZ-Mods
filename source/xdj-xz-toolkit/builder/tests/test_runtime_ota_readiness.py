import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from builder import runtime_ota


class OtaReadinessTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        (self.root/'runtime').mkdir()
        (self.root/'ota-release').mkdir()
        (self.root/'runtime/manifest.json').write_text('{}')
        manifest=bytearray(576)
        manifest[:8]=b'XZOTA1\0\0'
        for n,name in enumerate(('libxz-mods.so','libxz-receiver.so','xz-doom','source.zip')):
            data=(name+' current build').encode()
            (self.root/'runtime'/name).write_bytes(data)
            struct.pack_into('<32sI32s28s',manifest,116+n*96,name.encode(),len(data),hashlib.sha256(data).digest(),bytes(28))
        (self.root/'ota-release/manifest.bin').write_bytes(manifest)
        (self.root/'ota-release/latest.xzu').write_bytes(b'fixture payload')

    def test_matching_pair_is_ready(self):
        self.assertTrue(runtime_ota.readiness(self.root)['ready'])

    def test_stale_receiver_or_runtime_is_never_served(self):
        for name in ('libxz-mods.so','libxz-receiver.so'):
            with self.subTest(name=name):
                path=self.root/'runtime'/name
                original=path.read_bytes()
                path.write_bytes(original+b' newer')
                self.assertFalse(runtime_ota.readiness(self.root)['ready'])
                with self.assertRaisesRegex(ValueError,'Set up USB'):
                    runtime_ota.serve(self.root,'127.0.0.1',None)
                path.write_bytes(original)

    def test_explicit_usb_update_reason_wins_over_old_package(self):
        reason='Runtime exceeds the network updater limit. Use Set up USB.'
        (self.root/'runtime/manifest.json').write_text(json.dumps({'ota_unavailable_reason':reason}))
        self.assertEqual(runtime_ota.readiness(self.root),{'ready':False,'reason':reason})


if __name__=='__main__':
    unittest.main()
