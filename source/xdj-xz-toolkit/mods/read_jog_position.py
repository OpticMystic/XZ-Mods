"""Pure Stage1 paired-byte decoder. No live reader, ELF, process or network IO.

Records are HISTORICAL and session-bound. A stable record needs equal bytes in
both supplied captures and an even guard. Separate slots are never one atomic
snapshot. A sequence/CRC/digest change alone does not prove position movement.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SYMBOL = 'xz_mods_jog_position_v1'
SIZE = 5180
RECORD_BYTES = 368
HEADER = (1, SIZE, 2, 7, RECORD_BYTES, 148, 50)
FIELDS = '''seq valid ordinal context session control route_epoch physical_side logical_id
at_ms_lo at_ms_hi source_present source_session source_route_epoch source_ordered source_revision
publication_seq publication_ms source_known source_loaded source_device source_elapsed_ms
source_elapsed_at_ms source_total_ms source_playing source_frame_lo source_frame_hi source_rate
source_at_ms source_flags source_rate_ppm source_moving timeline_revision timeline_route_epoch
load_revision media_lo media_hi transport_generation snapshot_valid snapshot_generation_lo
snapshot_generation_hi snapshot_captured_lo snapshot_captured_hi snapshot_position_ms
snapshot_length_ms snapshot_paused snapshot_status_known snapshot_logical_id snapshot_reason
read_result select_result step_result phase_before phase final_copied'''.split()
assert len(FIELDS) == 55
KEY = '''physical_side session context control route_epoch source_session source_route_epoch
timeline_route_epoch load_revision media_lo media_hi transport_generation source_device'''.split()


def crc(frame):
    value = 0xffff
    for i in range(144):
        value ^= frame[i ^ 3]
        for _ in range(8):
            value = (value >> 1) ^ (0x8408 if value & 1 else 0)
    return ((~value) & 0xffff) << 16


def time_ms(raw):
    if raw[0] >= 128 or raw[1] >= 60 or int.from_bytes(raw[2:], 'little') >= 1000:
        raise ValueError('malformed INFO time')
    return raw[0] * 60000 + raw[1] * 1000 + int.from_bytes(raw[2:], 'little')


def record(blob, side, slot):
    words = struct.unpack('<92I', blob)
    r = dict(zip(FIELDS, words))
    if not r['valid']:
        if any(words[1:]):
            raise ValueError('nonempty invalid record')
        return None
    if r['valid'] != 1 or not all(r[k] for k in ('context', 'session', 'ordinal')):
        raise ValueError('invalid record identity')
    booleans = '''source_present source_ordered source_loaded source_playing source_moving
snapshot_valid snapshot_paused snapshot_status_known final_copied'''.split()
    if any(r[k] not in (0, 1) for k in booleans):
        raise ValueError('invalid boolean')
    desired = side + (3 if r['control'] & 4 and r['control'] & (1 << side) else 1)
    if r['physical_side'] != side + 1 or r['logical_id'] != desired or r['control'] > 15:
        raise ValueError('invalid physical/logical identity')
    if r['snapshot_reason'] >= 7 or r['select_result'] >= 6 or r['step_result'] >= 6 or r['phase_before'] >= 5 or r['phase'] >= 5:
        raise ValueError('invalid result or phase')
    if r['read_result'] not in (0, 1, 0xffffffff) or r['publication_seq'] > 0x3fffffff:
        raise ValueError('invalid read/publication')
    if r['source_present']:
        if r['source_session'] != r['session'] or r['source_route_epoch'] != r['route_epoch'] or not r['publication_seq']:
            raise ValueError('source identity mismatch')
        if r['timeline_route_epoch'] not in (0, r['route_epoch']):
            raise ValueError('timeline route mismatch')
    elif any(r[k] for k in FIELDS[FIELDS.index('source_session'):FIELDS.index('snapshot_valid')]):
        raise ValueError('source data without sample')
    if not r['snapshot_valid'] and any(r[k] for k in FIELDS[FIELDS.index('snapshot_generation_lo'):FIELDS.index('snapshot_reason')]):
        raise ValueError('snapshot data without result')
    if r['snapshot_valid'] and (not r['source_present'] or r['snapshot_logical_id'] != side + 3 or
            r['snapshot_position_ms'] > r['snapshot_length_ms'] or r['snapshot_captured_hi'] > r['at_ms_hi'] or
            ((r['snapshot_captured_hi'] << 32) | r['snapshot_captured_lo']) > ((r['at_ms_hi'] << 32) | r['at_ms_lo'])):
        raise ValueError('invalid computed snapshot')
    frame = blob[len(FIELDS) * 4:]
    if crc(frame) != int.from_bytes(frame[144:], 'little'):
        raise ValueError('frame CRC mismatch')
    packet = bytes(frame[12 + (i ^ 3)] for i in range(64))
    if frame[0x53] >> 4 not in (side + 1, side + 3) or packet[0] >> 4 not in (side + 1, side + 3):
        raise ValueError('frame side mismatch')
    r.update(slot=slot, evidence='HISTORICAL', current_context_claim=False,
             copy_scope='assembler copy' if r['final_copied'] else 'local candidate, copy guard skipped',
             at_ms=(r['at_ms_hi'] << 32) | r['at_ms_lo'],
             source_frame=(r['source_frame_hi'] << 32) | r['source_frame_lo'],
             source_elapsed_ms_signed=struct.unpack('<i', struct.pack('<I', r['source_elapsed_ms']))[0],
             source_total_ms_signed=struct.unpack('<i', struct.pack('<I', r['source_total_ms']))[0],
             source_rate_ppm_signed=struct.unpack('<i', struct.pack('<I', r['source_rate_ppm']))[0],
             frame_hex=frame.hex(), frame_sha256=hashlib.sha256(frame).hexdigest(),
             output_seq=int.from_bytes(frame[8:10], 'little'), output_crc=int.from_bytes(frame[144:], 'little'),
             frame_53=frame[0x53], info_00=packet[0], command=packet[1])
    if packet[1] == 0x21:
        r.update(info_time_hex=packet[11:15].hex(), info_length_hex=packet[15:19].hex(),
                 info_status=packet[2], info_validity=packet[9], info_track_hex=packet[19:21].hex())
        # Own INFO uses our millisecond encoding. Native INFO is opaque here;
        # its time layout must not prevent recovery of stable own active banks.
        if (r['snapshot_valid'] and r['phase_before'] == r['phase'] == 2 and
                r['step_result'] == 0 and r['snapshot_reason'] == 6 and
                r['snapshot_logical_id'] == r['logical_id'] == frame[0x53] >> 4 == side + 3 and
                packet[0] >> 4 in (side + 1, side + 3)):
            r.update(encoded_position_ms=time_ms(packet[11:15]),
                     encoded_length_ms=time_ms(packet[15:19]), info_time_scope='own INFO milliseconds')
        else:
            r.update(encoded_position_ms=None, encoded_length_ms=None,
                     info_time_scope='opaque unqualified INFO bytes')
    if slot < 6 and not (r['snapshot_valid'] and r['snapshot_reason'] == 6 and r['source_loaded'] and
            r['final_copied'] and r['step_result'] == 0 and r['phase_before'] == r['phase'] == 2 and
            (r['source_playing'] or r['source_moving']) and packet[1] == 0x21 and
            r['snapshot_logical_id'] == r['logical_id'] == frame[0x53] >> 4 == side + 3 and
            packet[0] >> 4 in (side + 1, side + 3)):
        raise ValueError('non-active record in active bank')
    return r


def classifications(records):
    groups = {}
    for r in records:
        if r['slot'] < 6:
            groups.setdefault(tuple(r[k] for k in KEY), {})[r['ordinal']] = r
    result = []
    for rows in groups.values():
        rows = sorted(rows.values(), key=lambda r: (r['at_ms'], r['ordinal']))
        verdict = 'INSUFFICIENT_SERIES'
        if len(rows) >= 3:
            def changes(k):
                return all(a[k] != b[k] for a, b in zip(rows, rows[1:]))
            def stable(k):
                return len({r[k] for r in rows}) == 1
            qualified = all(r['source_ordered'] and r['source_flags'] & 1 and r['source_rate'] and
                            r['source_playing'] and r['source_moving'] for r in rows)
            # Qualify a progressing publication and source observation before
            # assigning a stage. No slope, audible movement or MCU inference.
            advancing = changes('publication_seq') and changes('publication_ms') and changes('source_revision')
            if qualified and advancing:
                if stable('source_frame') and stable('source_elapsed_ms'):
                    verdict = 'SOURCE_POSITION_STALE'
                elif changes('source_frame') and changes('source_elapsed_ms'):
                    if stable('snapshot_position_ms'):
                        verdict = 'SNAPSHOT_POSITION_STALE'
                    elif changes('snapshot_position_ms'):
                        if stable('encoded_position_ms'):
                            verdict = 'ENCODER_POSITION_STALE'
                        elif (all(r['encoded_position_ms'] == r['snapshot_position_ms'] for r in rows) and
                              changes('output_seq') and changes('output_crc') and changes('frame_sha256')):
                            verdict = 'OWN_ASSEMBLER_POSITION_ADVANCING'
                        else:
                            verdict = 'POSITION_OR_FRAME_MISMATCH'
                    else:
                        verdict = 'MIXED_POSITION_OBSERVATIONS'
                else:
                    verdict = 'MIXED_SOURCE_OBSERVATIONS'
            else:
                verdict = 'INSUFFICIENT_PROGRESS_QUALIFICATION'
        result.append(dict(physical_side=rows[0]['physical_side'], session=rows[0]['session'],
                           context=rows[0]['context'], evidence='HISTORICAL', count=len(rows),
                           ordinals=[r['ordinal'] for r in rows], verdict=verdict))
    return result


def decode(first, second):
    if len(first) != SIZE or len(second) != SIZE:
        raise ValueError('truncated or oversized position export')
    if struct.unpack('<7I', first[:28]) != HEADER or struct.unpack('<7I', second[:28]) != HEADER:
        raise ValueError('unsupported position metadata/version')
    records, rejected = [], []
    for side in range(2):
        for slot in range(7):
            at = 28 + (side * 7 + slot) * RECORD_BYTES
            a, b = first[at:at + RECORD_BYTES], second[at:at + RECORD_BYTES]
            if a != b or struct.unpack_from('<I', a)[0] & 1:
                rejected.append(dict(physical_side=side + 1, slot=slot, reason='unstable paired record'))
                continue
            r = record(a, side, slot)
            if r:
                records.append(r)
    if rejected and not records:
        raise ValueError('no stable paired records')
    return dict(version=1, size=SIZE, symbol=SYMBOL, evidence='HISTORICAL', atomic_snapshot=False,
                transmission_observed=False, mcu_acceptance_observed=False,
                pair_sha256=[hashlib.sha256(b).hexdigest() for b in (first, second)],
                records=records, rejected=rejected, series=classifications(records))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('first', type=Path)
    p.add_argument('second', type=Path)
    a = p.parse_args()
    print(json.dumps(decode(a.first.read_bytes(), a.second.read_bytes()), indent=2))


if __name__ == '__main__':
    main()
