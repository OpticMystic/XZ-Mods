import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('jog_reader', Path(__file__).resolve().parents[1] / 'read_jog_display_proof.py')
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def blob():
    value = bytearray(reader.SIZE)
    struct.pack_into('<I', value, 0, 1)
    return value


class Decode(unittest.TestCase):
    def test_idle_hook_is_distinct_from_install_success(self):
        value = blob();struct.pack_into('<I', value, 2 * 4, 1)
        result = reader.decode(value, value)
        self.assertEqual(result['counters']['install_ok'], 1)
        self.assertEqual(result['counters']['calls'], 0)
        self.assertFalse(result['atomic_snapshot'])

    def test_torn_or_odd_side_state_omitted(self):
        left, right = blob(), blob()
        sequence = 4 * (len(reader.GLOBAL) + 4)
        struct.pack_into('<I', left, sequence, 2)
        struct.pack_into('<I', right, sequence, 4)
        self.assertNotIn('state', reader.decode(left, right)['sides'][0])
        struct.pack_into('<I', left, sequence, 3)
        struct.pack_into('<I', right, sequence, 3)
        self.assertFalse(reader.decode(left, right)['sides'][0]['assembler_state_stable'])

    def test_publication_counters_are_independent(self):
        left, right = blob(), blob()
        struct.pack_into('<I', right, 4 * (len(reader.GLOBAL) + 1), 8)
        result = reader.decode(left, right)
        self.assertEqual(result['sides'][0]['counters']['publications'], 8)
        self.assertTrue(result['sides'][0]['assembler_state_stable'])

    def test_invalid_size_or_version_rejected(self):
        for value in (bytearray(), bytearray(reader.SIZE)):
            with self.assertRaises(ValueError):reader.decode(value, value)


if __name__ == '__main__':unittest.main()
