"""Check the C DJ Profile writer, then check every stick it wrote with the reference reader
spec/djprofile/tools/check_djprofile.py: no problems, every profile found.

Linux (gcc, ASan/UBSan):  python3 tests/verify_dj_profile_write.py [--export DIR]
    Builds tests/test_dj_profile_write.c, runs every case and checks the sticks it produced.
    --export copies the produced sticks to DIR for other readers (the Rust and TypeScript 1.0 readers).
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_dj_profile import OLD_FIXTURES, SPEC, WRITER, build, checker  # noqa: E402

EXPORTED = ('created', 'migrated', 'adopted', 'unknown', 'full', 'errors', 'large', 'heavy')
# 16x16 JPEG of rgb(40,200,90), quality 95, 4:4:4 (Pillow 12.3).
JPEG = bytes.fromhex(
    'ffd8ffe000104a46494600010100000100010000ffdb0043000201010101010201010102020202020403020202020504040304060506060605060606'
    '070908060709070606080b08090a0a0a0a0a06080b0c0b0a0c090a0a0affdb004301020202020202050303050a0706070a0a0a0a0a0a0a0a0a0a0a0a'
    '0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0affc00011080010001003011100021101031101ffc400'
    '1f0000010501010101010100000000000000000102030405060708090a0bffc400b5100002010303020403050504040000017d010203000411051221'
    '31410613516107227114328191a1082342b1c11552d1f02433627282090a161718191a25262728292a3435363738393a434445464748494a53545556'
    '5758595a636465666768696a737475767778797a838485868788898a92939495969798999aa2a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6'
    'c7c8c9cad2d3d4d5d6d7d8d9dae1e2e3e4e5e6e7e8e9eaf1f2f3f4f5f6f7f8f9faffc4001f0100030101010101010101010000000000000102030405'
    '060708090a0bffc400b51100020102040403040705040400010277000102031104052131061241510761711322328108144291a1b1c109233352f015'
    '6272d10a162434e125f11718191a262728292a35363738393a434445464748494a535455565758595a636465666768696a737475767778797a828384'
    '85868788898a92939495969798999aa2a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae2e3e4e5e6e7e8e9ea'
    'f2f3f4f5f6f7f8f9faffda000c03010002110311003f00ec2bf0f3fce30a002800a00fffd9')


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)


def png(width, height, pixel, color_type=2):
    """pixel(x, y) -> tuple of channel bytes for the colour type (2 RGB, 4 gray+alpha, 6 RGBA)."""
    rows = b''.join(b'\0' + b''.join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))
    header = struct.pack('>IIBBBBB', width, height, 8, color_type, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')


def solid_png(width, height, rgb):
    row = b'\0' + bytes(rgb) * width
    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(row * height)) + chunk(b'IEND', b'')


def header_only(width, height):
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IEND', b'')


OLD_MARKER = 'djprofile:1:djp_lena:0123456789ab'
LONG_TAGLINE = '\U0001F3B5' * 80


def id3_track(comment):
    """An ID3v2.4 tag with a UTF-8 COMM frame, then one silent MPEG frame."""
    def synchsafe(n):
        return bytes((n >> 21 & 127, n >> 14 & 127, n >> 7 & 127, n & 127))
    frames = b''
    for frame_id, payload in ((b'TIT2', b'\x03Lena B'), (b'COMM', b'\x03eng\0' + comment.encode())):
        frames += frame_id + synchsafe(len(payload)) + b'\0\0' + payload
    return b'ID3\x04\0\0' + synchsafe(len(frames)) + frames + b'\xff\xfb\x90\x40' + bytes(413)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding='utf-8')


def prepare(work):
    images = work / 'images'
    images.mkdir()
    green = png(40, 40, lambda x, y: (30, 220, 60))
    (images / 'green.png').write_bytes(green)
    (images / 'red_blue.png').write_bytes(png(320, 160, lambda x, y: (255, 0, 0) if x < 160 else (0, 0, 255)))
    (images / 'half_alpha.png').write_bytes(png(16, 16, lambda x, y: (255, 0, 0, 128), 6))
    (images / 'gray_alpha.png').write_bytes(png(8, 8, lambda x, y: (200, 255), 4))
    (images / 'tall.png').write_bytes(png(20, 40, lambda x, y: (255, 255, 255)))
    (images / 'stripes.png').write_bytes(png(480, 480, lambda x, y: (255, 255, 255) if x % 3 == 1 else (0, 0, 0)))
    (images / 'logo.jpg').write_bytes(JPEG)
    (images / 'huge.png').write_bytes(header_only(5000, 5000))
    (images / 'wide.png').write_bytes(header_only(9000, 1))
    (images / 'wide4100.png').write_bytes(solid_png(4100, 100, (250, 160, 100)))
    (images / 'square2100.png').write_bytes(solid_png(2100, 2100, (16, 228, 220)))
    good = png(64, 64, lambda x, y: (x * 4, y * 4, 128))
    # 1600x1600 of noise, stored: a logo just under 8 MiB.
    row = 1 + 1600 * 3
    noise = bytearray(os.urandom(row * 1600))
    noise[::row] = bytes(1600)
    (images / 'noise.png').write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 1600, 1600, 8, 2, 0, 0, 0))
                                       + chunk(b'IDAT', zlib.compress(bytes(noise), 0)) + chunk(b'IEND', b''))
    (images / 'truncated.png').write_bytes(good[:len(good) // 2])
    (images / 'notimage.png').write_bytes(b'this is not an image, only text\n')
    (images / 'empty.png').write_bytes(b'')
    with open(images / 'big.png', 'wb') as stream:
        stream.write(good)
        stream.truncate(8 * 1024 * 1024 + 1)
    os.symlink(images / 'green.png', images / 'link.png')

    sticks = work / 'sticks'
    sticks.mkdir()
    for name in ('created', 'full', 'large'):
        (sticks / name).mkdir()
    for name in ('migrated', 'adopted', 'errors'):
        shutil.copytree(OLD_FIXTURES / 'stick-two-profiles', sticks / name)
    kai = sticks / 'adopted' / 'DJPROFILE' / 'DJ Kai'
    kai.mkdir()
    (kai / 'Logo.PNG').write_bytes(green)
    (kai / 'logo.jpeg').write_bytes(JPEG)
    (kai / 'flyer.png').write_bytes(png(8, 8, lambda x, y: (1, 2, 3)))
    # A pre-1.0 document with a profile track, a member 1.0 does not know and a tagline over 240 bytes.
    old = sticks / 'adopted' / 'DJPROFILE' / 'VJTOOLS' / 'profiles' / 'djp_lena'
    track = id3_track(OLD_MARKER + ' https://vj.tools/dj')
    (old / 'track').mkdir()
    (old / 'track' / 'DJ PROFILE - Lena B.mp3').write_bytes(track)
    value = json.loads((old / 'profile.json').read_text(encoding='utf-8'))
    value['profile_track'] = {'path': 'DJPROFILE/VJTOOLS/profiles/djp_lena/track/DJ PROFILE - Lena B.mp3', 'bytes': len(track),
                              'sha256': hashlib.sha256(track).hexdigest()}
    value['x_future'] = {'n': 1.5, 'list': [1, 'two']}
    value['tagline'] = LONG_TAGLINE
    write_json(old / 'profile.json', value)
    (sticks / 'adopted' / 'DJPROFILE' / 'Zed').mkdir()
    (sticks / 'adopted' / 'DJPROFILE' / 'Zed' / 'flyer.png').write_bytes(png(12, 12, lambda x, y: (200, 20, 20)))
    # Three 6 MiB alternates: a near-8 MiB primary would pass the 24 MiB of section 5.1.
    heavy = sticks / 'heavy' / 'DJPROFILE' / 'Heavy'
    heavy.mkdir(parents=True)
    small = png(16, 16, lambda x, y: (9, 9, 9))
    (heavy / 'logo.png').write_bytes(small)
    logos = [{'file': 'logo.png', 'bytes': len(small), 'sha256': hashlib.sha256(small).hexdigest(), 'role': 'primary'}]
    for k in range(3):
        data = header_only(64, 64) + bytes(6 * 1024 * 1024)
        (heavy / f'big-{k}.png').write_bytes(data)
        logos.append({'file': f'big-{k}.png', 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'role': 'alternate'})
    write_json(heavy / 'profile.json', {'format': 'djprofile/1', 'id': 'heavy', 'revision': 'r1', 'name': 'Heavy', 'logos': logos})
    # A pre-1.0 member 1.0 cannot hold (a non-ASCII member name): that profile cannot move, the others still do.
    stuck = sticks / 'errors' / 'DJPROFILE' / 'VJTOOLS' / 'profiles' / 'djp_kai' / 'profile.json'
    value = json.loads(stuck.read_text(encoding='utf-8'))
    value['\u00e9tiquette'] = 1
    write_json(stuck, value)
    unknown = sticks / 'unknown'
    shutil.copytree(SPEC / 'examples' / 'level1-track', unknown)
    document = unknown / 'DJPROFILE' / 'DJ Kai' / 'profile.json'
    value = json.loads(document.read_text(encoding='utf-8'))
    value['logos'][1]['x-entry'] = [1, 2]
    value['links'][0]['x-link'] = True
    value['x-root'] = {'a': 1, 'b': ['c']}
    # An alternate stored as logo.jpg, the name a JPEG primary takes.
    (unknown / 'DJPROFILE' / 'DJ Kai' / 'logo.jpg').write_bytes(JPEG)
    value['logos'].append({'file': 'logo.jpg', 'bytes': len(JPEG), 'sha256': hashlib.sha256(JPEG).hexdigest(), 'role': 'alternate'})
    write_json(document, value)
    stick_file = unknown / 'DJPROFILE' / 'DJPROFILE.json'
    value = json.loads(stick_file.read_text(encoding='utf-8'))
    value['x-stick'] = 'kept'
    write_json(stick_file, value)

    tree = work / 'list'
    files = ['a.jpg', 'Alpha.png', 'b.PNG', 'c.JpEg', 'Zed.png', 'x.gif', '.hidden.png', 'pioneer/p.png', 'DJPROFILE/d.png',
             'VJ.Tools/v.png', 'CDJMODS/c.png', 'System Volume Information/s.png', '$RECYCLE.BIN/r.png', '.Trashes/t.png',
             'l1/l2/l3/deep.png', 'l1/l2/l3/l4/deeper.png', 'l1/one.jpeg', 'l1/notes.txt', ('d' * 200) + '/' + ('f' * 60) + '.png',
             'DJPROFILE/DJ Kai/logo.png', 'DJPROFILES/Mia/m.jpg', 'DJPROFILE/VJTOOLS/v.png', 'DJPROFILE/DJ Kai/sub/deep.png',
             'DJPROFILE/DJ Kai/.hidden.png', 'DJPROFILE/DJ Kai/notes.txt']
    for name in files:
        path = tree / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'0123456789')
    (tree / 'tiny.png').write_bytes(b'1234')
    with open(tree / 'big.jpg', 'wb') as stream:
        stream.truncate(8 * 1024 * 1024 + 1)
    os.symlink(tree / 'a.jpg', tree / 'link.png')
    os.symlink(tree / 'l1', tree / 'linkdir')


def check_sticks(directory):
    """check_djprofile.py on every stick: no problem, and the profiles it finds."""
    check = checker()
    failed = 0
    for name in EXPORTED:
        report = check.check(directory / name)
        profiles = ', '.join(f'{p["folder"]} (level {p["level"]})' for p in report.profiles)
        print(f'check_djprofile {name}: {len(report.problems)} problems, {len(report.warnings)} warnings; {profiles or "no profiles"}; active {report.active}')
        for kind, items in (('problem', report.problems), ('warning', report.warnings)):
            for item in items:
                print(f'  {kind} section {item["section"]} {item["where"]}: {item["problem"]}')
        if report.problems:
            failed = 1
    return failed


def check_migrated(sticks):
    """Contract 0.2 on the adopted stick: track is {marker} from the old MP3, nothing the limits cut is lost."""
    document = json.loads((sticks / 'adopted' / 'DJPROFILE' / 'Lena B' / 'profile.json').read_text(encoding='utf-8'))
    vendor = document.get('ext', {}).get('vj.tools', {})
    checks = {
        'track is the old marker only': document.get('track') == {'marker': OLD_MARKER},
        'old MP3 not copied': not any(p.suffix == '.mp3' for p in (sticks / 'adopted' / 'DJPROFILE' / 'Lena B').iterdir()),
        'unknown member kept, numbers whole': vendor.get('x_future') == {'n': '1.5', 'list': [1, 'two']},
        'cut tagline kept whole': vendor.get('tagline') == LONG_TAGLINE and len(document['tagline'].encode()) <= 240,
        'tile rendition for the XZ': any(r.get('for') == 'xdj-xz' for r in document.get('renditions', [])),
    }
    for name, ok in checks.items():
        print(f'migration {name}: {"ok" if ok else "FAIL"}')
    return 0 if all(checks.values()) else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--export', help='copy the produced sticks here (replaced)')
    args = parser.parse_args()
    if not OLD_FIXTURES or not SPEC:
        print(f'fixtures not found (pre-1.0: {OLD_FIXTURES}, spec: {SPEC})')
        return 1
    with tempfile.TemporaryDirectory(prefix='xz-dj-write-') as temporary:
        temporary = Path(temporary)
        work = temporary / 'work'
        work.mkdir()
        prepare(work)
        binary = build('test_dj_profile_write', ['tests/test_dj_profile_write.c', *WRITER], temporary, ['-lm'])
        failed = subprocess.run([str(binary), str(work)]).returncode
        failed = check_sticks(work / 'sticks') or failed
        failed = check_migrated(work / 'sticks') or failed
        if args.export:
            target = Path(args.export)
            if target.exists():
                shutil.rmtree(target)
            target.mkdir(parents=True)
            for name in EXPORTED:
                shutil.copytree(work / 'sticks' / name, target / name, symlinks=True)
            print(f'exported {len(EXPORTED)} sticks to {target}')
    print('verify_dj_profile_write:', 'FAIL' if failed else 'ok')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
