# SPDX-License-Identifier: MIT
"""Encode bounded OverCue FLAC pages in the managed audio environment.

No separation, resampling, gain or channel conversion occurs here. The frozen
backend verifies the result with the same C reader used by the device.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct

import numpy as np
import soundfile as sf

ROLES = ('vocal', 'instrumental', 'drums', 'harmonics', 'vocals-drums', 'vocals-harmonics', 'full-mix')
PAGE_BYTES = 131072


def pack(raw, target):
    size = raw.stat().st_size
    if not 0 < size <= PAGE_BYTES * 4096 or size % 4:
        raise ValueError('Invalid OverCue PCM length')
    count = -(-size // PAGE_BYTES)
    header = b'OVPGZ003' + struct.pack('>IIQ', PAGE_BYTES, count, size)
    table, pcm, offset = bytearray(), hashlib.sha256(), 24 + 48 * count
    with raw.open('rb') as source, target.open('xb') as output:
        output.seek(offset)
        for number in range(count):
            page = source.read(PAGE_BYTES)
            if len(page) != min(PAGE_BYTES, size - number * PAGE_BYTES):
                raise ValueError('PCM changed while encoding')
            samples = np.frombuffer(page, dtype='<i2').reshape(-1, 2)
            stream = io.BytesIO()
            sf.write(stream, samples, 96000, format='FLAC', subtype='PCM_16')
            payload = stream.getvalue()
            if not payload or len(payload) > len(page) + 65536:
                raise ValueError('FLAC page exceeds the format limit')
            decoded, rate = sf.read(io.BytesIO(payload), dtype='int16', always_2d=True)
            if rate != 96000 or decoded.shape != samples.shape or decoded.astype('<i2').tobytes() != page:
                raise ValueError('FLAC page did not decode to the original PCM')
            table += struct.pack('>QII', offset, len(payload), len(page)) + hashlib.sha256(page).digest()
            pcm.update(page)
            output.write(payload)
            offset += len(payload)
        if source.read(1):
            raise ValueError('PCM changed while encoding')
        output.seek(0)
        output.write(header + table)
    return {'sha256': pcm.hexdigest(), 'page_table_sha256': hashlib.sha256(header + table).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--roles', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(exist_ok=False)
    result = {role: pack(args.roles / (role + '.s16le'), args.output / f'stems-sidecar-{role}.s16le.pgz') for role in ROLES}
    (args.output / 'result.json').write_text(json.dumps(result), encoding='utf-8')


if __name__ == '__main__':
    main()
