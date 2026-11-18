import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch

from builder import firmware, managed_usb, mod_versions as versions, usb, usb_settings
from builder.jobs import Job


class QuietJob(Job):
    def progress(self, *args, **kwargs):
        self.check()


def resource_fixture(path, label, fields=None):
    runtime = path / 'runtime'
    runtime.mkdir(parents=True)
    elf = bytearray(64)
    elf[:6] = b'\x7fELF\x01\x01'
    struct.pack_into('<H', elf, 18, 40)
    (runtime / 'libxz-mods.so').write_bytes(elf + label.encode())
    (runtime / 'libxz-receiver.so').write_bytes(elf + (label + '-receiver').encode())
    (runtime / 'xz-updater').write_bytes(elf + b'updater')
    schema = json.loads((Path(__file__).parent / 'fixtures/settings-schema.json').read_text())
    if fields:
        schema['fields'] = schema['fields'][:fields]
        schema['accepted_field_counts'] = [n for n in schema['accepted_field_counts'] if n <= fields]
    (runtime / 'settings-schema.json').write_text(json.dumps(schema))
    manifest = {'firmware': 'XDJ-XZ 1.26', 'source_commit': label,
                'runtime_sha256': versions._hash(runtime / 'libxz-mods.so'),
                'receiver_sha256': versions._hash(runtime / 'libxz-receiver.so'),
                'settings_schema_sha256': versions._hash(runtime / 'settings-schema.json'),
                'ota_updater_sha256': versions._hash(runtime / 'xz-updater')}
    (runtime / 'manifest.json').write_text(json.dumps(manifest))
    (path / 'bootstrap.sh').write_bytes(b'#!/bin/sh\n' + versions.OTA_GATE + b'\n echo apply-ota\nfi\n')
    (path / 'licenses').mkdir()
    (path / 'licenses/test.txt').write_text('synthetic test payload')
    return path


class VersionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.env = patch.dict(os.environ, {'XZ_BUILDER_DATA': str(self.root / 'data')})
        self.env.start()
        self.addCleanup(self.env.stop)
        self.a = resource_fixture(self.root / 'a', 'old', 8)
        self.b = resource_fixture(self.root / 'b', 'new')
        self.job = QuietJob()

    def choose(self, host, ident, pinned=True):
        current = versions.inventory(host)
        return versions.choose(host, {'mod_version_id': ident, 'pinned': pinned,
                                      'expected_revision': current['selected']['revision']})

    def test_choice_survives_new_app_bundle(self):
        first = versions.inventory(self.a)
        second = versions.inventory(self.b)
        self.assertEqual(first['selected']['id'], second['selected']['id'])
        self.assertNotEqual(second['bundled_id'], second['selected']['id'])
        self.assertEqual(len(second['versions']), 2)

    def test_stale_selection_refused(self):
        state = versions.inventory(self.a)
        self.choose(self.a, state['selected']['id'], False)
        with self.assertRaisesRegex(ValueError, 'saved choice changed'):
            versions.choose(self.a, {'mod_version_id': state['selected']['id'], 'pinned': True,
                                    'expected_revision': state['selected']['revision']})

    def test_unknown_choice_never_falls_back_to_latest(self):
        state = versions.inventory(self.a)
        with self.assertRaises((ValueError, FileNotFoundError)):
            self.choose(self.a, 'f' * 64)
        self.assertEqual(versions.inventory(self.a)['selected'], state['selected'])

    def test_payload_tampering_is_rejected(self):
        state = versions.inventory(self.a)
        folder = versions._root() / state['selected']['id']
        (folder / 'runtime/libxz-receiver.so').write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'changed|integrity'):
            with versions.selected_resources(self.b, {}, self.job):
                self.fail('tampered bundle was selected')

    def test_pin_changes_only_generated_boot_policy(self):
        state = versions.inventory(self.a)
        original = (self.a / 'bootstrap.sh').read_bytes()
        with versions.selected_resources(self.a, {}, self.job) as (root, version):
            self.assertTrue(version['pinned'])
            self.assertIn(versions.PINNED_GATE, (root / 'bootstrap.sh').read_bytes())
            self.assertEqual((root / 'runtime/libxz-mods.so').read_bytes(), (self.a / 'runtime/libxz-mods.so').read_bytes())
            self.assertEqual((root / 'runtime/libxz-receiver.so').read_bytes(), (self.a / 'runtime/libxz-receiver.so').read_bytes())
        self.assertEqual((self.a / 'bootstrap.sh').read_bytes(), original)
        self.choose(self.a, state['selected']['id'], False)
        with versions.selected_resources(self.a, {}, self.job) as (root, version):
            self.assertFalse(version['pinned'])
            self.assertEqual((root / 'bootstrap.sh').read_bytes(), original)

    def test_stale_build_choice_refused(self):
        a = versions.inventory(self.a)
        b = versions.inventory(self.b)
        self.choose(self.b, b['bundled_id'])
        with self.assertRaisesRegex(ValueError, 'saved mod choice changed'):
            with versions.selected_resources(self.b, {'mod_version_id': a['selected']['id']}, self.job):
                self.fail('stale choice accepted')

    def test_encrypted_loader_contains_exact_selected_pair_and_pin(self):
        state = versions.inventory(self.a)
        newer = versions.inventory(self.b)
        for ident, source in ((state['selected']['id'], self.a), (newer['bundled_id'], self.b)):
            self.choose(self.b, ident)
            target = self.root / ('usb-' + ident[:8])
            target.mkdir()
            key = self.root / 'key'
            key.write_bytes(b'test-only-key')
            with versions.selected_resources(self.b, {}, self.job) as (root, version):
                with patch.object(firmware, 'import_application', return_value=(b'synthetic-app', False)):
                    result = usb.build_usb(target, key, key, root, self.job, True)
                expected = {'tools/libxz-mods.so': (source / 'runtime/libxz-mods.so').read_bytes(),
                            'tools/libxz-directfb-hook.so': (source / 'runtime/libxz-receiver.so').read_bytes(),
                            'autoexec.sh': (root / 'bootstrap.sh').read_bytes(),
                            'xz-mod-version.json': (root / 'mod-version.json').read_bytes()}
                firmware.verify_image(Path(result['image']).read_bytes(), firmware.effective_key(key), expected)
                versions.record_loader(result, version)
                self.assertEqual(versions.lookup_loader(managed_usb.revision(result['image']))['id'], ident)

    def profile(self):
        volume = self.root / 'usb'
        (volume / 'VJ.Tools').mkdir(parents=True)
        schema = usb_settings.contract(self.b)
        data = usb_settings.serialize(usb_settings.defaults(schema), schema)
        (volume / managed_usb.SETTINGS).write_bytes(data)
        (volume / 'autoexec.bin').write_bytes(b'previous loader')
        return volume, data

    def test_older_profile_plan_preserves_common_values(self):
        volume, original = self.profile()
        plan = versions.settings_change(volume, self.a, self.b)
        self.assertTrue(plan['required'])
        self.assertEqual((volume / managed_usb.SETTINGS).read_bytes(), original)
        old = usb_settings.parse(plan['data'], usb_settings.contract(self.a))
        new = usb_settings.parse(original, usb_settings.contract(self.b))
        self.assertTrue(all(old[k] == new[k] for k in old))
        self.assertIsNone(versions.settings_change(volume, self.b, self.b))

    def test_shipped_profile_can_keep_or_switch_builds(self):
        runtime = self.b / 'runtime'
        schema_path = runtime / 'settings-schema.json'
        schema_path.write_bytes((Path(__file__).parents[2] / 'mods/settings-schema.json').read_bytes())
        manifest_path = runtime / 'manifest.json'
        manifest = json.loads(manifest_path.read_text())
        manifest['settings_schema_sha256'] = versions._hash(schema_path)
        manifest_path.write_text(json.dumps(manifest))
        volume, original = self.profile()
        self.assertGreater(len(original), 511)
        self.assertIsNone(versions.settings_change(volume, self.b, self.b))
        plan = versions.settings_change(volume, self.a, self.b)
        self.assertTrue(plan['required'])
        self.assertEqual((volume / managed_usb.SETTINGS).read_bytes(), original)
        old = usb_settings.parse(plan['data'], usb_settings.contract(self.a))
        # An older build reads the combined theme slot the interface fields derive.
        current = usb_settings.legacy_values(usb_settings.parse(original, usb_settings.contract(self.b)), usb_settings.contract(self.b))
        retired=usb_settings.contract(self.b).get('retired_fields',{})
        # The default modern interface (theme 24) does not exist in the older build: reported, then reset.
        self.assertEqual(plan['reset_fields'], ['theme'])
        self.assertTrue(all(old[key] == current[key] for key in old if key not in retired and key not in plan['reset_fields']))
        self.assertFalse(set(retired) & current.keys())

    def test_oversized_profile_is_preserved_before_switch(self):
        volume, _ = self.profile()
        original = b'x' * 1024
        (volume / managed_usb.SETTINGS).write_bytes(original)
        with self.assertRaisesRegex(ValueError, 'too large'):
            versions.settings_change(volume, self.a, self.b)
        self.assertEqual((volume / managed_usb.SETTINGS).read_bytes(), original)

    def test_failed_loader_switch_restores_exact_original_profile(self):
        volume, original = self.profile()
        plan = versions.settings_change(volume, self.a, self.b)
        observed = managed_usb.identity(volume)
        prior = managed_usb.revision(volume / 'autoexec.bin')
        candidate = self.root / 'candidate.bin'
        candidate.write_bytes(b'replacement loader')
        real = managed_usb._commit
        def fail_loader(vol, obs, target, expected, path, job):
            if target == 'autoexec.bin':
                raise OSError('injected loader failure')
            return real(vol, obs, target, expected, path, job)
        with patch.object(managed_usb, '_commit', side_effect=fail_loader):
            with self.assertRaisesRegex(OSError, 'injected'):
                managed_usb._commit_loader(volume, observed, prior, candidate, self.job, plan)
        self.assertEqual((volume / managed_usb.SETTINGS).read_bytes(), original)
        self.assertEqual((volume / 'autoexec.bin').read_bytes(), b'previous loader')

    def test_profile_change_after_review_is_rejected(self):
        volume, original = self.profile()
        plan = versions.settings_change(volume, self.a, self.b)
        candidate = self.root / 'candidate.bin'
        candidate.write_bytes(b'replacement')
        (volume / managed_usb.SETTINGS).write_bytes(original.replace(b'stems=1', b'stems=0'))
        with self.assertRaisesRegex(ValueError, 'changed since inspection'):
            managed_usb._commit_loader(volume, managed_usb.identity(volume), managed_usb.revision(volume / 'autoexec.bin'), candidate, self.job, plan)
        self.assertEqual((volume / 'autoexec.bin').read_bytes(), b'previous loader')

    def test_import_rejects_mismatched_pair(self):
        (self.a / 'runtime/libxz-receiver.so').write_bytes((self.b / 'runtime/libxz-receiver.so').read_bytes())
        with self.assertRaisesRegex(ValueError, 'integrity'):
            versions.import_version(self.b, self.a)

    def test_catalog_preferred_is_initial_only(self):
        original = versions.inventory(self.a)
        record = versions._verify(versions._root() / original['selected']['id'])
        seed = self.b / 'mod-version-seeds' / record['id']
        shutil.copytree(versions._root() / record['id'], seed)
        versions._write(seed.parent / 'catalog.json', {'schema': 'xz-mod-seeds/1', 'versions': [{'id': record['id']}], 'preferred_id': record['id']})
        state = versions.inventory(self.b)
        self.choose(self.b, state['bundled_id'])
        self.assertEqual(versions.inventory(self.b)['selected']['id'], state['bundled_id'])


if __name__ == '__main__':
    unittest.main()


class CurrentBrandingTest(unittest.TestCase):
    def _branding(self, root, splash, apply=b'#!/bin/sh\n'):
        folder = Path(root) / 'branding'
        folder.mkdir(parents=True)
        (folder / 'apply.sh').write_bytes(apply)
        (folder / 'splash.rgb565').write_bytes(splash)
        (folder / 'logo.rgb565').write_bytes(splash[:4])
        (folder / 'MD5SUMS').write_bytes(b'sums ' + splash)

    def test_older_version_gets_the_apps_splash(self):
        from builder import mod_versions
        with tempfile.TemporaryDirectory() as app, tempfile.TemporaryDirectory() as view:
            self._branding(app, b'xzmods.com')
            self._branding(view, b'vj.tools/xzmods')
            self.assertTrue(mod_versions.current_branding(app, view))
            for name in ('splash.rgb565', 'logo.rgb565', 'MD5SUMS', 'apply.sh'):
                self.assertEqual((Path(view) / 'branding' / name).read_bytes(), (Path(app) / 'branding' / name).read_bytes(), name)

    def test_different_apply_script_keeps_the_versions_images(self):
        from builder import mod_versions
        with tempfile.TemporaryDirectory() as app, tempfile.TemporaryDirectory() as view:
            self._branding(app, b'xzmods.com', apply=b'#!/bin/sh\nnew offsets\n')
            self._branding(view, b'vj.tools/xzmods')
            self.assertFalse(mod_versions.current_branding(app, view))
            self.assertEqual((Path(view) / 'branding' / 'splash.rgb565').read_bytes(), b'vj.tools/xzmods')
