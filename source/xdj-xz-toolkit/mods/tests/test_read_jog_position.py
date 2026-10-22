"""Own physical commands and opaque native packets share the v1 capture ABI."""
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('position_reader', ROOT / 'read_jog_position.py')
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


def capture(physical_headers=True):
    blob = bytearray(reader.SIZE)
    struct.pack_into('<7I', blob, 0, *reader.HEADER)
    for side in range(2):
        for slot in range(3):
            at = 1000 + slot * 50
            position = 500 + slot * 50
            fields = dict.fromkeys(reader.FIELDS, 0)
            fields.update(seq=2, valid=1, ordinal=slot + 1, context=1, session=2,
                control=7, route_epoch=1, physical_side=side + 1, logical_id=side + 3,
                at_ms_lo=at, source_present=1, source_session=2, source_route_epoch=1,
                source_ordered=1, source_revision=slot + 1, publication_seq=slot + 1,
                publication_ms=at, source_loaded=1, source_elapsed_ms=position,
                source_elapsed_at_ms=at, source_total_ms=2000, source_playing=1,
                source_frame_lo=position * 48, source_rate=48000, source_at_ms=at,
                source_flags=15, source_rate_ppm=1000000, source_moving=1,
                timeline_revision=1, timeline_route_epoch=1, load_revision=1,
                transport_generation=1, snapshot_valid=1, snapshot_generation_lo=1,
                snapshot_captured_lo=at, snapshot_position_ms=position,
                snapshot_length_ms=2000, snapshot_status_known=1,
                snapshot_logical_id=side + 3, snapshot_reason=6, read_result=1,
                select_result=0, step_result=0, phase_before=2, phase=2, final_copied=1)
            packet = bytearray(64)
            packet[0] = (side + (1 if physical_headers else 3)) << 4
            packet[1] = 0x21
            packet[2] = 8
            struct.pack_into('<H', packet, 13, position)
            packet[16] = 2
            frame = bytearray(148)
            frame[0x53] = (side + 3) << 4
            struct.pack_into('<H', frame, 8, slot + 1)
            frame[12:76] = bytes(packet[i ^ 3] for i in range(64))
            struct.pack_into('<I', frame, 144, reader.crc(frame))
            start = 28 + (side * 7 + slot) * reader.RECORD_BYTES
            struct.pack_into('<55I', blob, start, *(fields[key] for key in reader.FIELDS))
            blob[start + 220:start + 368] = frame
    return blob


def latest_native(blob, *, snapshot_valid=False, step=0, phase=0):
    start = 28 + 6 * reader.RECORD_BYTES
    blob[start:start + reader.RECORD_BYTES] = blob[28:28 + reader.RECORD_BYTES]
    fields = dict(zip(reader.FIELDS, struct.unpack_from('<55I', blob, start)))
    fields.update(ordinal=4, phase_before=phase, phase=phase, step_result=step,
                  snapshot_valid=int(snapshot_valid))
    if not snapshot_valid:
        first = reader.FIELDS.index('snapshot_generation_lo')
        last = reader.FIELDS.index('snapshot_reason')
        for name in reader.FIELDS[first:last]:
            fields[name] = 0
    struct.pack_into('<55I', blob, start, *(fields[key] for key in reader.FIELDS))
    frame = bytearray(blob[start + 220:start + 368])
    # Native alternate-display INFO can use another time encoding.
    frame[12 + (11 ^ 3)] = 0x80
    struct.pack_into('<I', frame, 144, reader.crc(frame))
    blob[start + 220:start + 368] = frame
    return blob


class PositionAddressingTests(unittest.TestCase):
    def test_physical_and_prior_logical_commands_keep_advancing_banks(self):
        for physical in (True, False):
            with self.subTest(physical=physical):
                blob = capture(physical)
                result = reader.decode(blob, blob)
                self.assertEqual(len(result['series']), 2)
                self.assertEqual({s['verdict'] for s in result['series']}, {'OWN_ASSEMBLER_POSITION_ADVANCING'})
                self.assertTrue(all(r['snapshot_position_ms'] == r['encoded_position_ms'] for r in result['records']))

    def test_native_alternate_time_does_not_erase_active_bank(self):
        blob = latest_native(capture())
        result = reader.decode(blob, blob)
        latest = next(r for r in result['records'] if r['slot'] == 6)
        self.assertIsNone(latest['encoded_position_ms'])
        self.assertEqual(len(result['series']), 2)

    def test_rejected_native_frame_with_snapshot_stays_opaque(self):
        blob = latest_native(capture(), snapshot_valid=True, step=3, phase=2)
        latest = next(r for r in reader.decode(blob, blob)['records'] if r['slot'] == 6)
        self.assertIsNone(latest['encoded_position_ms'])

    def test_malformed_owned_time_is_rejected(self):
        blob = latest_native(capture(), snapshot_valid=True, phase=2)
        with self.assertRaisesRegex(ValueError, 'malformed INFO time'):
            reader.decode(blob, blob)

    def test_wrong_snapshot_deck_is_rejected(self):
        blob = capture()
        struct.pack_into('<I', blob, 28 + reader.FIELDS.index('snapshot_logical_id') * 4, 4)
        with self.assertRaisesRegex(ValueError, 'invalid computed snapshot'):
            reader.decode(blob, blob)

    def test_crc_damage_is_rejected(self):
        blob = capture()
        blob[28 + 220 + 15] ^= 0x20
        with self.assertRaisesRegex(ValueError, 'frame CRC mismatch'):
            reader.decode(blob, blob)


if __name__ == '__main__':
    unittest.main()
