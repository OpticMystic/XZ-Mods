"""Capture screen frames and stem readiness through the read-only XZ observer.

Requires the screen-capable USB loader. Sends no control or filesystem commands.
"""
import argparse
import ipaddress
import json
from pathlib import Path
import socket
import struct
import time


def exact(sock, count):
    data = bytearray()
    while len(data) < count:
        part = sock.recv(count - len(data))
        if not part:
            raise ValueError('XZ closed an incomplete observer response')
        data.extend(part)
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, type=ipaddress.IPv4Address)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--frames', type=int, choices=range(1, 11), default=1)
    parser.add_argument('--expect-prepared-deck', type=int, choices=(1, 2))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    report = {'host': str(args.host), 'observation_only': True, 'frames': []}
    with socket.create_connection((str(args.host), 50010), timeout=3) as sock:
        sock.settimeout(5)
        for i in range(args.frames):
            start = time.monotonic()
            sock.sendall(b'XZSC\x01\0\0\0')
            header = exact(sock, 20)
            if header[:16] != b'XZSF\x01\0\x20\x03\xe0\x01\0\0\0\xb8\x0b\0':
                raise ValueError('Unsupported XZ screen response')
            pixels = exact(sock, 768000)
            bmp = bytearray(66)
            bmp[:2] = b'BM'
            for offset, value in ((2, 768066), (10, 66), (14, 40), (18, 800), (22, (-480) & 0xffffffff),
                                  (30, 3), (34, 768000), (54, 0xf800), (58, 0x7e0), (62, 0x1f)):
                struct.pack_into('<I', bmp, offset, value)
            struct.pack_into('<HH', bmp, 26, 1, 16)
            filename = f'frame-{i:02d}.bmp'
            (args.output / filename).write_bytes(bmp + pixels)
            report['frames'].append({'file': filename, 'sequence': struct.unpack_from('<I', header, 16)[0],
                                     'capture_ms': round((time.monotonic() - start) * 1000, 1)})
            if i + 1 < args.frames:
                time.sleep(max(0, 1 - (time.monotonic() - start)))
        sock.sendall(b'XZSD\x01\0\0\0')
        header = exact(sock, 8)
        size = struct.unpack_from('<I', header, 4)[0]
        if header[:4] != b'XZSD' or not 0 < size <= 16384:
            raise ValueError('Invalid XZ status response')
        report['stems'] = json.loads(exact(sock, size))
    (args.output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    if args.expect_prepared_deck:
        state = report['stems']
        deck = next((d for d in state.get('decks', []) if d['deck'] == args.expect_prepared_deck), {})
        if not state.get('engine') or not state.get('enabled') or not deck.get('prepared'):
            raise SystemExit('FAIL: loaded deck has no active prepared stem set; inspect report.json')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
