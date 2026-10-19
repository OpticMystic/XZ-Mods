"""The active desktop contract accepts settings saved before toggle retirement."""
import json
from pathlib import Path
import unittest
from builder import usb_settings

SCHEMA=json.loads((Path(__file__).resolve().parents[2]/'mods/settings-schema.json').read_text())

class CurrentSettingsTests(unittest.TestCase):
    def test_old_disabled_values_are_ignored(self):
        values=usb_settings.defaults(SCHEMA)
        record=usb_settings.serialize(values,SCHEMA)
        for key in SCHEMA['retired_fields']:
            with self.subTest(key=key):
                old=record.replace((key+'=1\n').encode(),(key+'=0\n').encode())
                self.assertEqual(usb_settings.parse(old,SCHEMA),values)
                self.assertNotIn(key,values)

    def test_old_complete_forms_still_save(self):
        values=usb_settings.defaults(SCHEMA)
        old={**values,**{key:0 for key in SCHEMA['retired_fields']}}
        self.assertEqual(usb_settings.serialize(old,SCHEMA),usb_settings.serialize(values,SCHEMA))

    def test_all_historical_record_lengths_load(self):
        values=usb_settings.defaults(SCHEMA)
        lines=usb_settings.serialize(values,SCHEMA).splitlines(keepends=True)
        for count in SCHEMA['accepted_field_counts']:
            with self.subTest(count=count):
                self.assertEqual(usb_settings.parse(b''.join(lines[:count+1]),SCHEMA),values)
