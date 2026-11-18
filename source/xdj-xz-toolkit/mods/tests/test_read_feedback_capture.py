"""Pure decoder tests for read_feedback_capture.py; no controller calls."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('feedback_reader', Path(__file__).resolve().parents[1] / 'read_feedback_capture.py')
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def codes(used=1):
    value = bytearray(reader.CODES_SIZE)
    struct.pack_into('<8I', value, 0, 1, reader.CODES_SIZE, 1, reader.CODES, used, 3, 1, 0)
    return value


class Codes(unittest.TestCase):
    def test_entry_fields(self):
        value = codes()
        struct.pack_into('<12I', value, 32, 4, 0x410b | 1 << 16 | 1 << 17, 9, 1 << 5, 1 << 3 | 1 << 1,
                         10, 90, 5, 0, struct.unpack('<I', struct.pack('<f', -0.5))[0], 0, 6)
        result = reader.decode_codes(value, value)
        self.assertEqual(result['overflow'], 3)
        row = result['codes'][0]
        self.assertEqual((row['code_hex'], row['side'], row['count']), ('0x410b', 'right', 9))
        self.assertEqual(row['operations'], [5])
        self.assertEqual(row['paths'], ['fallback', 'global_key'])
        self.assertEqual(row['last_value'], -0.5)
        self.assertEqual(row['decks_3_4_selected'], {'left': False, 'right': True})

    def test_unstable_and_odd_entries_are_skipped(self):
        first, second = codes(2), codes(2)
        for value in (first, second):
            struct.pack_into('<2I', value, 32, 4, 0x4101 | 1 << 17)
            struct.pack_into('<2I', value, 80, 3, 0x4102 | 1 << 17)
        struct.pack_into('<I', second, 32 + 8, 2)
        result = reader.decode_codes(first, second)
        self.assertEqual((len(result['codes']), result['unstable_entries']), (0, 2))

    def test_abi_is_checked(self):
        value = codes()
        struct.pack_into('<I', value, 12, 64)
        with self.assertRaises(ValueError):
            reader.decode_codes(value, value)
        with self.assertRaises(ValueError):
            reader.decode_codes(value[:-1], value[:-1])


class Led(unittest.TestCase):
    def test_status_and_pad_records(self):
        led = bytearray(reader.LED_SIZE)
        struct.pack_into('<3I', led, 0, 1, 0, 0)
        struct.pack_into('<12I', led, 12 + reader.LED_IDS * 48, 2, 7, 2, 1, 0, 0, 2, 1, 0, 0, 0, 0)
        result = reader.decode_led(led, led)
        self.assertEqual(result['channels']['1'], [])
        self.assertEqual(result['channels']['2'][0]['id'], 2)
        self.assertEqual(result['channels']['2'][0]['state_word_0x10'], 2)
        pads = bytearray(reader.PAD_SIZE)
        struct.pack_into('<I', pads, 0, 1)
        struct.pack_into('<11I', pads, 4 + ((1 * 4 + 2) * 8 + 3) * 44, 2, 5, 0, 0, 1, 0, 0, 0, 0, 0, 0x563412)
        rows = reader.decode_pads(pads, pads)['pads']
        self.assertEqual(rows, [{'channel': 2, 'native_page': 'SLIP LOOP', 'pad': 'D', 'seen': 5,
                                 'words': ['0x0', '0x0', '0x1', '0x0', '0x0', '0x0', '0x0', '0x0'],
                                 'rgb': '#123456'}])


if __name__ == '__main__':
    unittest.main()
