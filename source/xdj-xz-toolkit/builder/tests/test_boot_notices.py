import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from builder import usb, firmware
from builder.jobs import Job


class BootNoticesTests(unittest.TestCase):
    def test_source_archives_stay_on_pc_and_declared_payload_stays_in_image(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);resources=root/'resources';volume=root/'volume'
            (resources/'runtime').mkdir(parents=True);(resources/'licenses').mkdir();volume.mkdir()
            manifest={}
            for field,name in [('runtime_sha256','libxz-mods.so'),('receiver_sha256','libxz-receiver.so')]:
                data=name.encode();(resources/'runtime'/name).write_bytes(data)
                manifest[field]=hashlib.sha256(data).hexdigest()
            (resources/'runtime/manifest.json').write_text(json.dumps(manifest))
            (resources/'bootstrap.sh').write_bytes(b'#!/bin/sh\n')
            for name in ('XZ-Mods-MIT.txt','xz-quad-GPL-2.0.txt','xz-quad-build.json','xz-hub-source.tar.gz','other-source.zip'):
                (resources/'licenses'/name).write_bytes(name.encode())
            observed={}
            def stage(tree,*args):
                # Explicit profile members are retained even when they are archives.
                member=tree/'licenses/xz-quad-source.zip';member.parent.mkdir(parents=True);member.write_bytes(b'paired-source')
                return {'licenses/xz-quad-source.zip':b'paired-source'}
            def author(tree,*args):
                observed.update({p.relative_to(tree).as_posix():p.read_bytes() for p in tree.rglob('*') if p.is_file()})
                return b'fixture-image'
            with patch.object(usb,'inspect_inputs'),patch.object(usb,'target_info',return_value={'direct_usb_root':False}),patch.object(firmware,'effective_key',return_value=b''),patch.object(firmware,'import_application',return_value=(b'app',True)),patch.object(firmware,'stage_payload',side_effect=stage),patch.object(firmware,'author_image',side_effect=author),patch.object(firmware,'verify_image',return_value={'verified_files':1}):
                usb.build_usb(volume,root/'firmware',root/'key',resources,Job(),True)
            self.assertEqual(set(observed),{'licenses/XZ-Mods-MIT.txt','licenses/xz-quad-GPL-2.0.txt','licenses/xz-quad-build.json','licenses/xz-quad-source.zip'})
            self.assertEqual((resources/'licenses/xz-hub-source.tar.gz').read_bytes(),b'xz-hub-source.tar.gz')
            self.assertEqual((resources/'licenses/other-source.zip').read_bytes(),b'other-source.zip')


if __name__=='__main__':unittest.main()
