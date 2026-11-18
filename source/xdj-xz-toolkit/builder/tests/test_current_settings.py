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
                # Records from rekordbox_menu on but before the REKORDBOX buttons migrate from its value
                # (written 0 here, as the default press is INTERFACE): the stock action.
                expected=values if count<=35 or count>=43 else {**values,'rekordbox_press':1}
                self.assertEqual(usb_settings.parse(b''.join(lines[:count+1]),SCHEMA),expected)

    def test_rekordbox_buttons(self):
        values=usb_settings.defaults(SCHEMA)
        self.assertEqual((values['rekordbox_press'],values['rekordbox_shift']),(2,1))
        self.assertNotIn('rekordbox_menu',values)
        for press in range(6):
            record=usb_settings.serialize({**values,'rekordbox_press':press,'rekordbox_shift':5-press},SCHEMA)
            self.assertIn(('rekordbox_menu=%d\n'%(press==0)).encode(),record)
            self.assertEqual(usb_settings.parse(record,SCHEMA),{**values,'rekordbox_press':press,'rekordbox_shift':5-press})
        lines=usb_settings.serialize(values,SCHEMA).splitlines(keepends=True)
        old=b''.join(lines[:42])
        for menu,takeover,press,assign in ((1,0,2,0),(0,0,1,0),(0,1,3,0),(1,1,2,0),(0,2,1,2)):
            record=old.replace(b'rekordbox_menu=0\n',b'rekordbox_menu=%d\n'%menu).replace(b'takeover_assign=0\n',b'takeover_assign=%d\n'%takeover)
            with self.subTest(menu=menu,takeover=takeover):
                parsed=usb_settings.parse(record,SCHEMA)
                self.assertEqual((parsed['rekordbox_press'],parsed['rekordbox_shift'],parsed['takeover_assign']),(press,1,assign))
        # The retired REKORDBOX screen switch choice saves as LINK.
        self.assertIn(b'takeover_assign=0\n',usb_settings.serialize({**values,'takeover_assign':1},SCHEMA))

    def test_shortcut_options_remain_editable(self):
        values=usb_settings.defaults(SCHEMA)
        values.update(stem_order=5,stem_page=2,stem_bank=1,jump_5=9)
        self.assertEqual(usb_settings.parse(usb_settings.serialize(values,SCHEMA),SCHEMA),values)

    def test_browse_preview(self):
        values=usb_settings.defaults(SCHEMA)
        self.assertEqual(values['browse_preview'],0)
        self.assertEqual(SCHEMA['wire_order'][46],'browse_preview')
        for shown in (0,1):
            chosen={**values,'browse_preview':shown}
            wire=usb_settings.serialize(chosen,SCHEMA)
            self.assertEqual(usb_settings.parse(wire,SCHEMA),chosen)
            self.assertLessEqual(len(wire),usb_settings.MAX_RECORD_BYTES)
            for count in SCHEMA['accepted_field_counts']:
                if count<47:
                    old=b''.join(wire.splitlines(keepends=True)[:count+1])
                    self.assertEqual(usb_settings.parse(old,SCHEMA)['browse_preview'],0)
        for invalid in (-1,2):
            with self.assertRaises(ValueError):
                usb_settings.serialize({**values,'browse_preview':invalid},SCHEMA)

    def test_band_and_modern_display_blocks(self):
        defaults=usb_settings.defaults(SCHEMA)
        keys=['band_low_color','band_mid_color','band_high_color','modern_wave_color','wave_position',
              'key_display','wave_zoom','usb_colors','hidden_parts','modern_browse_columns']
        self.assertEqual([defaults[k] for k in keys],[0,0,0,2,0,0,3,0,0,0])
        fields={f['key']:f for f in SCHEMA['fields']}
        expected=['STANDARD','BLUE','RED','GREEN','AMBER','PURPLE','CYAN','PINK','WHITE']
        for k in keys[:3]:
            self.assertEqual([c['label'] for c in fields[k]['choices']],expected)
            self.assertEqual(fields[k]['group'],fields['stem_drums_color']['group'])
        maximum={k:f['max'] for k,f in fields.items()}
        maximum.update(browse_1=3,browse_2=2,browse_3=1)
        wire=usb_settings.serialize(maximum,SCHEMA)
        self.assertLessEqual(len(wire),1023)
        self.assertEqual(usb_settings.parse(wire,SCHEMA),maximum)
        lines=wire.splitlines(keepends=True)
        for count in range(47,58):
            with self.subTest(fields=count):
                if count not in (47,50,56,57):
                    with self.assertRaises(ValueError):usb_settings.parse(b''.join(lines[:count+1]),SCHEMA)
                else:
                    got=usb_settings.parse(b''.join(lines[:count+1]),SCHEMA)
                    for i,k in enumerate(keys):
                        self.assertEqual(got[k],maximum[k] if 47+i<count else defaults[k])
        for k in keys:
            for invalid in (-1,fields[k]['max']+1):
                with self.subTest(key=k,value=invalid),self.assertRaises(ValueError):
                    usb_settings.serialize({**defaults,k:invalid},SCHEMA)
        # A runtime predating the append-only extension rejects it, preserving the original file.
        import copy
        old=copy.deepcopy(SCHEMA)
        old['wire_order']=old['wire_order'][:47]
        old['fields']=[f for f in old['fields'] if f['key'] in old['wire_order']]
        old['accepted_field_counts']=[n for n in old['accepted_field_counts'] if n<=47]
        with self.assertRaises(ValueError):usb_settings.parse(wire,old)
