"""Synthetic official-layout FLAC pages. FFmpeg is only a test fixture encoder."""
import hashlib
import json
import struct
import subprocess
from pathlib import Path


def repack(source, bundle, roles=None):
    manifest_path = bundle / 'overcue-manifest.json'
    manifest = json.loads(manifest_path.read_text())
    index_path = source.parents[2] / 'CDJMODS/index.json'
    index = json.loads(index_path.read_text())
    entry = index['tracks']['1']
    entry.update(frames=manifest['runtime']['frames'], three_part=1, page_bytes=131072,
                 page_codec='flac-96k', source_sha256=hashlib.sha256(source.read_bytes()).hexdigest())
    for role, metadata in manifest['roles'].items():
        file = bundle / f'stems-sidecar-{role}.s16le.pgz'
        data = file.read_bytes()
        count = struct.unpack_from('>I', data, 12)[0]
        raw_pages = []
        for page in range(count):
            offset, size, expanded = struct.unpack_from('>QII', data, 24 + 48 * page)
            import zlib
            raw = zlib.decompress(data[offset:offset + size])
            assert len(raw) == expanded
            raw_pages.append(raw)
        if roles is None or role in roles:
            encoded = [subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 's16le',
                       '-ar', '96000', '-ac', '2', '-i', 'pipe:0', '-c:a', 'flac', '-blocksize', '4096',
                       '-f', 'flac', 'pipe:1'], input=raw, capture_output=True, check=True).stdout for raw in raw_pages]
            # Pipe encoding leaves STREAMINFO's total unknown; the container requires the exact count.
            fixed = []
            for raw, payload in zip(raw_pages, encoded):
                payload = bytearray(payload)
                value = int.from_bytes(payload[18:26], 'big')
                payload[18:26] = ((value & ~((1 << 36) - 1)) | (len(raw) // 4)).to_bytes(8, 'big')
                fixed.append(bytes(payload))
            offset = 24 + count * 48
            table = b'OVPGZ003' + struct.pack('>IIQ', 131072, count, sum(map(len, raw_pages)))
            for raw, payload in zip(raw_pages, fixed):
                table += struct.pack('>QII', offset, len(payload), len(raw)) + hashlib.sha256(raw).digest()
                offset += len(payload)
            file.write_bytes(table + b''.join(fixed))
        else:
            table = data[:24 + count * 48]
        metadata['page_table_sha256'] = hashlib.sha256(table).hexdigest()
        key = role.replace('-', '_')
        entry[key + '_page_table_sha256'] = metadata['page_table_sha256']
        entry[key + '_sha256'] = hashlib.sha256(b''.join(raw_pages)).hexdigest()
    manifest_path.write_text(json.dumps(manifest))
    index_path.write_text(json.dumps(index))


def table_hash(source, bundle, role):
    """Re-sign a synthetic malformed table so the test reaches its structural checks."""
    file = bundle / f'stems-sidecar-{role}.s16le.pgz'
    data = file.read_bytes()
    count = struct.unpack_from('>I', data, 12)[0]
    digest = hashlib.sha256(data[:24 + count * 48]).hexdigest()
    index_path = source.parents[2] / 'CDJMODS/index.json'
    index = json.loads(index_path.read_text())
    index['tracks']['1'][role.replace('-', '_') + '_page_table_sha256'] = digest
    index_path.write_text(json.dumps(index))
