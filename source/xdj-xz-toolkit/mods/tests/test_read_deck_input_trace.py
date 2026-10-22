"""Pure trace-decoder tests; no controller calls or imports of transport code."""
import importlib.util
import json
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('trace_reader', Path(__file__).resolve().parents[1] / 'read_deck_input_trace.py')
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def blob(next_value=1):
    value = bytearray(reader.SIZE)
    struct.pack_into('<8I', value, 0, 1, reader.SIZE, 1, reader.CAPACITY, next_value, 2, 7, 0)
    return value


def row(value, slot=0, seq=2, ordinal=0, bits=0x3f400000):
    struct.pack_into('<13I', value, reader.HEADER_BYTES + slot * reader.RECORD_BYTES,
                     seq, ordinal, 0, 2, 2, 0x4305, 4, 0xfffffff9, bits, 0, 2, 4, 14)


class Decode(unittest.TestCase):
    def test_signed_delta_and_exact_metadata(self):
        value = blob();row(value)
        result = reader.decode(value, value)
        self.assertEqual(len(result['records']), 1)
        self.assertEqual(result['records'][0]['delta'], -7)
        self.assertEqual(result['records'][0]['value'], .75)
        self.assertEqual(result['records'][0]['logical_deck'], 4)
        self.assertFalse(result['atomic_snapshot'])
        self.assertEqual(result['contention_drops'], 2)

    def test_unstable_and_in_progress_are_omitted(self):
        left = blob();right = blob();row(left);row(right, bits=0x3f800000)
        self.assertEqual(reader.decode(left, right)['unstable_records'], 1)
        row(left, seq=3);row(right, seq=3)
        self.assertFalse(reader.decode(left, right)['records'])

    def test_partial_invalid_or_changed_abi_refused(self):
        for value in (bytearray(4), blob()):
            if len(value) == reader.SIZE:
                struct.pack_into('<I', value, 0, 2)
            with self.assertRaises(ValueError):reader.decode(value, value)
        right = blob();struct.pack_into('<I', right, 8, 0)
        with self.assertRaises(ValueError):reader.decode(blob(), right)

    def test_wrap_order_and_overwritten_rows(self):
        value = blob(1)
        row(value, slot=0, ordinal=0)
        row(value, slot=reader.CAPACITY-1, ordinal=0xffffffff)
        row(value, slot=1, ordinal=1)
        rows = reader.decode(value, value)['records']
        self.assertEqual([record['ordinal'] for record in rows], [0xffffffff, 0])

    def test_nonfinite_bits_preserved_with_valid_json(self):
        value = blob();row(value, bits=0x7fc00000)
        result = reader.decode(value, value)
        self.assertEqual(result['records'][0]['value_bits'], 0x7fc00000)
        self.assertEqual(result['records'][0]['value'], 'nan')
        json.dumps(result, allow_nan=False)


if __name__ == '__main__':unittest.main()
