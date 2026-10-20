#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Regenerate spec/djprofile/examples: one known-good stick per conformance level.

    python make_examples.py

Output is deterministic, so a diff after running it means the examples drifted.
"""
from __future__ import annotations

import hashlib
import io
import json
import shutil
import struct
import zlib
from pathlib import Path

EXAMPLES = Path(__file__).resolve().parent.parent / 'examples'
TRACK_SECONDS = 10
SILENT_FRAME = bytes((0xFF, 0xFB, 0x90, 0x40)) + bytes(417 - 4)  # MPEG-1 Layer III, 128 kbit/s, 44.1 kHz
TRACK_FRAMES = -(-TRACK_SECONDS * 44100 // 1152)


def png(width: int, height: int, rgb: tuple[int, int, int]) -> bytes:
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    rows = b''.join(b'\0' + bytes(rgb) * width for _ in range(height))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def jpeg(width: int, height: int, rgb: tuple[int, int, int]) -> bytes:
    from PIL import Image  # only the generator needs Pillow; the checker and readers do not
    out = io.BytesIO()
    Image.new('RGB', (width, height), rgb).save(out, 'JPEG', quality=90)
    return out.getvalue()


def synchsafe(value: int) -> bytes:
    return bytes((value >> 21 & 0x7F, value >> 14 & 0x7F, value >> 7 & 0x7F, value & 0x7F))


def frame(frame_id: str, payload: bytes) -> bytes:
    return frame_id.encode('ascii') + synchsafe(len(payload)) + b'\0\0' + payload


def text(value: str) -> bytes:
    return b'\x03' + value.encode('utf-8')


def compact(document: dict) -> bytes:
    return json.dumps(document, ensure_ascii=False, separators=(',', ':'), sort_keys=True).encode('utf-8')


def entry(folder: Path, name: str, data: bytes, **extra) -> dict:
    (folder / name).write_bytes(data)
    return {'file': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(), **extra}


def profile_track(document: dict, cover: bytes) -> tuple[bytes, str]:
    payload = compact({k: v for k, v in document.items() if k != 'track'})
    marker = f'djprofile:1:{document["id"]}:{hashlib.sha256(payload).hexdigest()[:12]}'
    website = next((link['url'] for link in document.get('links', []) if link['kind'] == 'website'), None)
    name = document['name']
    frames = [frame('TIT2', text(name)), frame('TPE1', text(name)), frame('TPE2', text(name)),
              frame('TALB', text('DJ PROFILE')), frame('TCON', text('DJ Profile')),
              frame('COMM', b'\x03eng\0' + (marker + (' ' + website if website else '')).encode('utf-8')),
              frame('TPUB', text(document.get('tagline', ''))),
              frame('APIC', b'\x03image/jpeg\0\x03\0' + cover),
              frame('TXXX', b'\x03DJPROFILE\0' + payload)]
    if website:
        frames.append(frame('WOAR', website.encode('ascii')))
    body = b''.join(frames)
    return b'ID3\x04\x00\x00' + synchsafe(len(body)) + body + SILENT_FRAME * TRACK_FRAMES, marker


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


def main() -> None:
    shutil.rmtree(EXAMPLES, ignore_errors=True)

    level0 = EXAMPLES / 'level0' / 'DJPROFILE' / 'DJ Kai'
    level0.mkdir(parents=True)
    (level0 / 'logo.png').write_bytes(png(64, 64, (255, 160, 100)))

    for name in ('level1', 'level1-track'):
        top = EXAMPLES / name / 'DJPROFILE'
        kai, lena = top / 'DJ Kai', top / 'Lena B'
        kai.mkdir(parents=True)
        lena.mkdir()
        (lena / 'logo.jpg').write_bytes(jpeg(64, 64, (152, 40, 240)))
        document = {
            'format': 'djprofile/1',
            'id': '5f1d6f0e-2b7a-4c55-9a43-1d1f0b7c9e21',
            'revision': '2026-10-19T15-02-11Z-3f1c',
            'name': 'DJ Kai',
            'tagline': 'House and garage, Leeds',
            'accent': '#ffa064',
            'aliases': ['Kai'],
            'logos': [entry(kai, 'logo.png', png(64, 64, (255, 160, 100)), role='primary', background='dark'),
                      entry(kai, 'logo-light.png', png(64, 64, (30, 80, 240)), role='alternate', background='light')],
            'links': [{'kind': 'website', 'url': 'https://djkai.example'},
                      {'kind': 'instagram', 'url': 'https://instagram.com/djkai.example'}],
            'ext': {'example.org': {'note': 'readers ignore this, writers keep it'}},
        }
        if name == 'level1-track':
            track, marker = profile_track(document, jpeg(240, 240, (255, 160, 100)))
            document['track'] = {**entry(kai, 'DJ PROFILE - DJ Kai.mp3', track), 'marker': marker}
        write_json(kai / 'profile.json', document)
        write_json(top / 'DJPROFILE.json', {'format': 'djprofile-stick/1', 'revision': '2026-10-19T15-02-11Z-3f1c',
                                            'active': document['id'], 'order': [document['id'], 'Lena B']})


if __name__ == '__main__':
    main()
