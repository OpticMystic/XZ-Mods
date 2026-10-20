"""Check the C DJ Profile reader: the pre-1.0 store against the shared fixtures, and DJ Profile 1.0 against
the reference reader spec/djprofile/tools/check_djprofile.py on every example stick and a set of edge-case
sticks. Then the folder/colour test and the MODS worker test.

Linux (gcc, ASan/UBSan):  python3 tests/verify_dj_profile.py
Finds the fixtures in the monorepo (packages/creative-graph/fixtures/dj-profile) or in XZ-Mods
(source/xdj-xz-toolkit/builder/tests/fixtures/dj-profile), and the spec in XZ-Mods (spec/djprofile), a
sibling XZ-Mods checkout, $XZ_MODS, or the monorepo's copy (fixtures/dj-profile/v1).
"""
from pathlib import Path
import importlib.util
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unicodedata
import zlib

ROOT = Path(__file__).resolve().parents[1]
READER = ['dj_profile.c', 'dj_json.c', 'dj_folders.c', 'dj_name_key.c', 'audio/vendor/sha256/sha256.c']
WRITER = READER + ['dj_profile_write.c']
FLAGS = ['gcc', '-std=c11', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I', str(ROOT)]


def first(paths, need):
    for path in paths:
        if path and all((Path(path) / item).exists() for item in need):
            return Path(path)
    return None


OLD_FIXTURES = first([ROOT.parents[1] / 'creative-graph' / 'fixtures' / 'dj-profile',
                      ROOT.parent / 'builder' / 'tests' / 'fixtures' / 'dj-profile'], ['cases.json', 'stick-two-profiles'])
SPEC = first([ROOT.parents[2] / 'spec' / 'djprofile', os.environ.get('XZ_MODS') and Path(os.environ['XZ_MODS']) / 'spec' / 'djprofile',
              ROOT.parents[3] / 'XZ-Mods' / 'spec' / 'djprofile'], ['tools/check_djprofile.py', 'examples'])
MONOREPO_CASES = ROOT.parents[1] / 'creative-graph' / 'fixtures' / 'dj-profile' / 'v1' / 'cases'


def build(name, sources, temporary, extra=()):
    binary = temporary / name
    subprocess.run([*FLAGS, *(str(ROOT / s) for s in sources), *extra, '-o', str(binary)], check=True)
    return binary


def checker():
    spec = importlib.util.spec_from_file_location('check_djprofile', SPEC / 'tools' / 'check_djprofile.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def png(width, height, rgba=(10, 200, 90, 255), interlace=0):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    row = b'\x00' + bytes(rgba) * width
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, interlace))
            + chunk(b'IDAT', zlib.compress(row * height)) + chunk(b'IEND', b''))


def header_png(width, height):
    return b'\x89PNG\r\n\x1a\n' + struct.pack('>I', 13) + b'IHDR' + struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0) + b'\0' * 8


def jpeg(width, height, sof=0xC0):
    return b'\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00' + bytes([0xFF, sof]) + \
        struct.pack('>HBHHB', 11, 8, height, width, 1) + b'\x01\x11\x00' + b'\xff\xd9'


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode('utf-8'))


def sha(data):
    import hashlib
    return hashlib.sha256(data).hexdigest()


def document(folder, id_, name, **extra):
    logo = png(32, 32)
    write(folder / 'logo.png', logo)
    doc = {'format': 'djprofile/1', 'id': id_, 'revision': '2026-10-19T00-00-00Z-0001', 'name': name,
           'logos': [{'file': 'logo.png', 'bytes': len(logo), 'sha256': sha(logo), 'role': 'primary'}]}
    doc.update(extra)
    write(folder / 'profile.json', json.dumps(doc, indent=2, ensure_ascii=False))
    return doc


def edge_sticks(base):
    """Sticks the reference reader and the C reader must judge alike."""
    sticks = {}

    def stick(name):
        path = base / name
        sticks[name] = path
        return path

    def from_example(name, example):
        path = stick(name)
        shutil.copytree(SPEC / 'examples' / example, path)
        return path / 'DJPROFILE'

    top = from_example('dot-files', 'level1')
    write(top / '.Hidden' / 'logo.png', png(8, 8))
    write(top / 'DJ Kai' / '._logo.png', b'\0' * 4096)
    write(top / '.tmp-profile', b'{}')
    top = from_example('both-tops', 'level1')
    write(top.parent / 'DJPROFILES' / 'dj kai' / 'logo.png', png(8, 8))
    write(top.parent / 'DJPROFILES' / 'Mia' / 'logo.png', png(8, 8))
    write(top.parent / 'DJPROFILES' / 'Abe' / 'logo.png', png(8, 8))
    top = from_example('previous-fallback', 'level1')
    (top / 'DJ Kai' / 'profile.json').rename(top / 'DJ Kai' / 'profile.previous.json')
    write(top / 'DJ Kai' / 'profile.json', '{"format": "djprofile/1",')
    top = from_example('broken-no-previous', 'level1')
    write(top / 'DJ Kai' / 'profile.json', '{"format": "djprofile/1",')
    good = json.loads((SPEC / 'examples' / 'level1' / 'DJPROFILE' / 'DJ Kai' / 'profile.json').read_text(encoding='utf-8'))
    raw = json.dumps(good, indent=2, ensure_ascii=False)
    variants = {
        'bom': '﻿' + raw,
        'duplicate-member': raw.replace('"name": "DJ Kai"', '"name": "DJ Kai", "name": "DJ Kai"'),
        'float': raw.replace('"name": "DJ Kai"', '"name": "DJ Kai", "x": 1.5'),
        'too-deep': raw.replace('"name": "DJ Kai"', '"name": "DJ Kai", "x": [[[[[[[[1]]]]]]]]'),
        'deep-ok': raw.replace('"name": "DJ Kai"', '"name": "DJ Kai", "x": [[[[[[[1]]]]]]]'),
        'non-ascii-member': raw.replace('"name": "DJ Kai"', '"name": "DJ Kai", "né": 1'),
        'big-int': raw.replace('"name": "DJ Kai"', '"name": "DJ Kai", "x": 9007199254740993'),
        'max-int': raw.replace('"name": "DJ Kai"', '"name": "DJ Kai", "x": -9007199254740992'),
        'wrong-format': raw.replace('djprofile/1', 'djprofile/2'),
        'bad-id': raw.replace('5f1d6f0e-2b7a-4c55-9a43-1d1f0b7c9e21', 'has space'),
        'long-tagline': raw.replace('House and garage, Leeds', 'é' * 121),
        'tagline-240': raw.replace('House and garage, Leeds', 'é' * 120),
        'bad-accent': raw.replace('#ffa064', '#ffa06'),
        'http-link': raw.replace('https://djkai.example', 'http://djkai.example'),
        'logo-sha': raw.replace(good['logos'][0]['sha256'], '0' * 64),
        'logo-bytes': raw.replace('"bytes": 136', '"bytes": 137'),
        'two-primaries': raw.replace('"alternate"', '"primary"'),
        'primary-not-logo': raw.replace('"file": "logo.png"', '"file": "logo-light.png"', 1),
        'unsafe-path': raw.replace('"file": "logo-light.png"', '"file": "../Lena B/logo.jpg"'),
        'ext-not-object': raw.replace('"readers ignore this, writers keep it"', '"x"').replace('"example.org": {\n      "note": "x"\n    }', '"example.org": 1'),
        'unknown-members': raw.replace('"name": "DJ Kai"', '"name": "DJ Kai", "future": {"a": [1, true, null, "x"]}'),
    }
    for name, text in variants.items():
        top = from_example('doc-' + name, 'level1')
        write(top / 'DJ Kai' / 'profile.json', text)
    top = from_example('duplicate-id', 'level1')
    shutil.copytree(top / 'DJ Kai', top / 'Ann')
    top = from_example('order-and-active', 'level1')
    write(top / 'Zed' / 'logo.jpg', jpeg(10, 10))
    write(top / 'DJPROFILE.json', json.dumps({'format': 'djprofile-stick/1', 'active': 'lena b',
                                              'profiles': [{'folder': 'Zed'}, {'folder': 'Lena B'}, {'folder': 'Nobody'},
                                                           {'folder': 'DJ Kai', 'id': good['id']}]}))
    top = from_example('active-unresolved', 'level1')
    write(top / 'DJPROFILE.json', json.dumps({'format': 'djprofile-stick/1', 'active': 'nobody'}))
    top = from_example('renamed-by-hand', 'level1')
    (top / 'DJ Kai').rename(top / 'Kai Renamed')
    top = from_example('bad-stick-file', 'level1')
    write(top / 'DJPROFILE.json', json.dumps({'format': 'djprofile-stick/2', 'active': 'Lena B'}))
    top = from_example('stick-file-not-json', 'level1')
    write(top / 'DJPROFILE.json', '{')
    nfd = unicodedata.normalize('NFD', 'Beyoncé')
    top = stick('name-keys') / 'DJPROFILE'
    write(top / nfd / 'logo.png', png(8, 8))
    write(top / 'zoe' / 'logo.png', png(8, 8))
    write(top / 'Anna' / 'logo.png', png(8, 8))
    write(top / 'Åse' / 'logo.png', png(8, 8))
    write(top / 'DJPROFILE.json', json.dumps({'format': 'djprofile-stick/1', 'active': 'BEYONCÉ'.replace('É', 'é'),
                                              'profiles': [{'folder': 'ANNA'}]}, ensure_ascii=False))
    top = stick('level0-images') / 'DJPROFILE'
    write(top / 'Fold' / 'flyer.png', png(8, 8))
    write(top / 'Fold' / 'Logo.JPG', jpeg(10, 10))
    write(top / 'Fold' / 'logo.jpeg', jpeg(10, 10))
    write(top / 'Order' / 'b.png', png(8, 8))
    write(top / 'Order' / 'A.jpg', jpeg(10, 10))
    write(top / 'Big' / 'logo.png', header_png(4097, 10))
    write(top / 'Big' / 'z.png', png(8, 8))
    write(top / 'Pixels' / 'logo.png', header_png(2049, 2048))
    write(top / 'Edge' / 'logo.png', header_png(4096, 1024))
    write(top / 'Progressive' / 'logo.jpg', jpeg(10, 10, 0xC2))
    write(top / 'Interlaced' / 'logo.png', png(8, 8, interlace=1))
    write(top / 'Tiny' / 'logo.png', b'\x89PNG\r\n')
    write(top / 'Text' / 'logo.png', b'not an image at all, just words')
    write(top / 'NoLogo' / 'readme.txt', b'hello')
    write(top / 'Deep' / 'sub' / 'logo.png', png(8, 8))
    write(top / 'Upper' / 'LOGO.PNG', png(8, 8))
    top = stick('many') / 'DJPROFILE'
    for i in range(20):
        write(top / f'DJ {i:02d}' / 'logo.png', png(4, 4))
    top = stick('documents') / 'DJPROFILE'
    document(top / 'No Logos', 'nologos', 'No Logos').pop('logos')
    write(top / 'No Logos' / 'profile.json', json.dumps({'format': 'djprofile/1', 'id': 'nologos', 'revision': 'r', 'name': 'Shown Name'}))
    write(top / 'No Logos' / 'pic.jpg', jpeg(5, 5))
    write(top / 'Bare' / 'profile.json', json.dumps({'format': 'djprofile/1', 'id': 'bare', 'revision': 'r', 'name': 'Bare'}))
    tile = bytes(range(256)) * 200
    folder = top / 'Tiled'
    doc = document(folder, 'tiled', 'Tiled')
    write(folder / 'tile-160.rgb565', tile)
    doc['renditions'] = [{'file': 'tile-160.rgb565', 'bytes': len(tile), 'sha256': sha(tile), 'width': 160, 'height': 160,
                          'format': 'rgb565le', 'for': 'xdj-xz'}]
    write(folder / 'profile.json', json.dumps(doc))
    folder = top / 'Bad Rendition'
    doc = document(folder, 'badrendition', 'Bad Rendition')
    write(folder / 'tile-160.rgb565', tile)
    doc['renditions'] = [{'file': 'tile-160.rgb565', 'bytes': len(tile), 'sha256': '0' * 64, 'width': 160, 'height': 160,
                          'format': 'rgb565le', 'for': 'xdj-xz'}]
    write(folder / 'profile.json', json.dumps(doc))
    folder = top / 'Wrong Raw Size'
    doc = document(folder, 'rawsize', 'Wrong Raw Size')
    write(folder / 'tile.raw', tile)
    doc['renditions'] = [{'file': 'tile.raw', 'bytes': len(tile), 'sha256': sha(tile), 'width': 100, 'height': 100,
                          'format': 'rgb565le', 'for': 'xdj-xz'}]
    write(folder / 'profile.json', json.dumps(doc))
    folder = top / 'Track Marker'
    document(folder, 'marker', 'Track Marker', track={'marker': 'djprofile:1:marker:0123456789ab'})
    folder = top / 'Track Other Id'
    document(folder, 'otherid', 'Track Other Id', track={'marker': 'djprofile:1:marker:0123456789ab'})
    folder = top / 'Track Missing File'
    document(folder, 'missing', 'Track Missing File', track={'marker': 'djprofile:1:missing:0123456789ab', 'file': 'x.mp3',
                                                            'bytes': 3, 'sha256': sha(b'abc')})
    folder = top / 'Sub Logo'
    logo = png(16, 16)
    write(folder / 'art' / 'alt.png', logo)
    document(folder, 'sublogo', 'Sub Logo')
    doc = json.loads((folder / 'profile.json').read_text())
    doc['logos'].append({'file': 'art/alt.png', 'bytes': len(logo), 'sha256': sha(logo), 'role': 'alternate', 'background': 'light', 'label': 'L'})
    write(folder / 'profile.json', json.dumps(doc))
    return sticks


def compare(binary, sticks, check):
    total = bad = 0
    for name, path in sorted(sticks.items()):
        got = json.loads(subprocess.run([str(binary), 'scan', str(path)], check=True, capture_output=True, text=True).stdout)
        report = check.check(path)
        want = {'active': report.active, 'profiles': [{'folder': p['folder'], 'id': p['id'], 'name': p['name'],
                                                         'level': min(p['level'], 1), 'logo': p['logo']} for p in report.profiles]}
        mine = {'active': got['active'], 'profiles': [{k: p[k] for k in ('folder', 'id', 'name', 'level', 'logo')} for p in got['profiles']]}
        total += 1
        if mine != want:
            bad += 1
            print(f'DISAGREE {name}:\n  checker {json.dumps(want, ensure_ascii=False)}\n  C       {json.dumps(mine, ensure_ascii=False)}')
    print(f'check_djprofile.py agreement: {total - bad}/{total} sticks')
    return bad


def main():
    if not OLD_FIXTURES or not SPEC:
        print(f'fixtures not found (pre-1.0: {OLD_FIXTURES}, spec: {SPEC})')
        return 1
    with tempfile.TemporaryDirectory(prefix='xz-dj-profile-') as temporary:
        temporary = Path(temporary)
        binary = build('test_dj_profile', ['tests/test_dj_profile.c', *READER], temporary)
        cases = json.loads((OLD_FIXTURES / 'cases.json').read_text(encoding='utf-8'))
        total = mismatches = 0
        for kind in ('index', 'profile'):
            for case in cases[kind]:
                document_path = temporary / 'case.json'
                document_path.write_text(json.dumps(case['document']), encoding='utf-8')
                got = subprocess.run([str(binary), 'case', kind, str(document_path)], check=True, capture_output=True, text=True).stdout.strip()
                want = 'valid' if case['valid'] else 'invalid'
                total += 1
                if got != want:
                    mismatches += 1
                    print(f'MISMATCH {kind} "{case["name"]}": expected {want}, got {got}')
        print(f'pre-1.0 cases: {total - mismatches}/{total} agree with cases.json')
        failed = mismatches
        failed |= subprocess.run([str(binary), 'syntax']).returncode
        stick = temporary / 'stick'
        shutil.copytree(OLD_FIXTURES / 'stick-two-profiles', stick)
        failed |= subprocess.run([str(binary), 'stick', str(stick)]).returncode
        sticks = {f'example {p.name}': p for p in sorted((SPEC / 'examples').iterdir()) if p.is_dir()}
        if MONOREPO_CASES.is_dir():
            sticks.update({f'monorepo case {p.name}': p for p in sorted(MONOREPO_CASES.iterdir()) if p.is_dir()})
        (temporary / 'edge').mkdir()
        sticks.update(edge_sticks(temporary / 'edge'))
        failed |= compare(binary, sticks, checker())
        folders = build('test_dj_folders', ['tests/test_dj_folders.c', *READER], temporary)
        (temporary / 'folders').mkdir()
        shutil.copytree(OLD_FIXTURES / 'stick-two-profiles', temporary / 'folders-stick')
        failed |= subprocess.run([str(folders), str(temporary / 'folders'), str(temporary / 'folders-stick')]).returncode
        runtime = build('test_dj_runtime', ['tests/test_dj_runtime.c', *WRITER], temporary, ['-pthread', '-lm'])
        usb = [temporary / 'usb1', temporary / 'usb2']
        for copy in usb:
            shutil.copytree(OLD_FIXTURES / 'stick-two-profiles', copy)
        (usb[0] / 'Logos').mkdir()
        (usb[0] / 'Logos' / 'me.png').write_bytes(png(64, 64))
        (usb[0] / 'not-a-logo.jpg').write_bytes(b'this is not a jpeg at all')
        failed |= subprocess.run([str(runtime), *map(str, usb)]).returncode
    print('verify_dj_profile:', 'FAIL' if failed else 'ok')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
