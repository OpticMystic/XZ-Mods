"""Check the C DJ Profile writer, then cross-check the sticks it writes with the TypeScript and Rust readers.

Linux (gcc 13, ASan/UBSan):  python3 tests/verify_dj_profile_write.py [--export DIR]
    Builds tests/test_dj_profile_write.c, runs every case, validates the produced sticks with the C reader,
    and with node when node is on PATH. --export copies the produced sticks to DIR.
Anywhere with node and cargo:  python tests/verify_dj_profile_write.py --cross-check DIR
    Validates every index.json / profile.json under DIR with packages/creative-graph (dist) and loads every
    listed profile with the Rust reader (native/rust-app/crates/dj-profiles/tests/external_stick.rs).
"""
from pathlib import Path
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
FIXTURES = ROOT.parents[1] / 'creative-graph' / 'fixtures' / 'dj-profile'
EXPORTED = ('created', 'fixture-edited', 'logo-removed', 'full', 'conflict')
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


def header_only(width, height):
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IEND', b'')


def prepare(work):
    images = work / 'images'
    images.mkdir()
    (images / 'green.png').write_bytes(png(40, 40, lambda x, y: (30, 220, 60)))
    (images / 'red_blue.png').write_bytes(png(320, 160, lambda x, y: (255, 0, 0) if x < 160 else (0, 0, 255)))
    (images / 'half_alpha.png').write_bytes(png(16, 16, lambda x, y: (255, 0, 0, 128), 6))
    (images / 'gray_alpha.png').write_bytes(png(8, 8, lambda x, y: (200, 255), 4))
    (images / 'tall.png').write_bytes(png(20, 40, lambda x, y: (255, 255, 255)))
    (images / 'stripes.png').write_bytes(png(480, 480, lambda x, y: (255, 255, 255) if x % 3 == 1 else (0, 0, 0)))
    (images / 'logo.jpg').write_bytes(JPEG)
    (images / 'huge.png').write_bytes(header_only(5000, 5000))
    (images / 'wide.png').write_bytes(header_only(9000, 1))
    good = png(64, 64, lambda x, y: (x * 4, y * 4, 128))
    (images / 'truncated.png').write_bytes(good[:len(good) // 2])
    (images / 'notimage.png').write_bytes(b'this is not an image, only text\n')
    (images / 'empty.png').write_bytes(b'')
    with open(images / 'big.png', 'wb') as stream:
        stream.write(good)
        stream.truncate(8 * 1024 * 1024 + 1)
    os.symlink(images / 'green.png', images / 'link.png')

    sticks = work / 'sticks'
    sticks.mkdir()
    for name in ('created', 'full', 'conflict'):
        (sticks / name).mkdir()
    for name in ('fixture-edited', 'logo-removed', 'errors'):
        shutil.copytree(FIXTURES / 'stick-two-profiles', sticks / name)

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


def sticks_under(directory):
    return sorted(path for path in Path(directory).iterdir() if (path / 'DJPROFILE').is_dir())


def node_check(directory):
    """Every index.json, profile.json and profile.previous.json through the TypeScript validator."""
    dist = REPO / 'packages' / 'creative-graph' / 'dist' / 'dj-profile.js'
    script = r'''
import fs from 'node:fs'
import path from 'node:path'
import { pathToFileURL } from 'node:url'
const [dist, root] = process.argv.slice(1)
const { parseProfileIndex, parseProfileDocument } = await import(pathToFileURL(dist).href)
let bad = 0, checked = 0
for (const stick of fs.readdirSync(root).sort()) {
  const home = path.join(root, stick, 'DJPROFILE', 'VJTOOLS')
  if (!fs.existsSync(home)) continue
  const index = parseProfileIndex(JSON.parse(fs.readFileSync(path.join(home, 'index.json'), 'utf8')))
  checked++
  if (!index.ok) { bad++; console.log(`${stick}/index.json: ${index.error}`); continue }
  for (const { dj_id } of index.value.profiles) {
    for (const file of ['profile.json', 'profile.previous.json']) {
      const where = path.join(home, 'profiles', dj_id, file)
      if (!fs.existsSync(where)) { if (file === 'profile.json') { bad++; console.log(`${stick}: missing ${dj_id}/${file}`) } continue }
      const result = parseProfileDocument(JSON.parse(fs.readFileSync(where, 'utf8')))
      checked++
      if (!result.ok) { bad++; console.log(`${stick}/${dj_id}/${file}: ${result.error}`) }
      else if (file === 'profile.json' && result.value.dj_id !== dj_id) { bad++; console.log(`${stick}/${dj_id}: dj_id mismatch`) }
    }
  }
}
console.log(`typescript: ${checked - bad}/${checked} documents valid`)
process.exit(bad ? 1 : 0)
'''
    return subprocess.run(['node', '--input-type=module', '-e', script, str(dist), str(directory)]).returncode


def cross_check(directory):
    directory = Path(directory).resolve()
    sticks = sticks_under(directory)
    if not sticks:
        print(f'no sticks under {directory}')
        return 1
    failed = node_check(directory)
    env = dict(os.environ, DJ_PROFILE_EXTERNAL_STICKS=os.pathsep.join(map(str, sticks)))
    env.pop('ELECTRON_RUN_AS_NODE', None)
    rust = subprocess.run(['cargo', 'test', '-p', 'vjtools-dj-profiles', '--test', 'external_stick', '--', '--nocapture'],
                          cwd=REPO / 'native' / 'rust-app', env=env).returncode
    print('rust:', 'FAIL' if rust else f'ok ({len(sticks)} sticks)')
    return 1 if failed or rust else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--export', help='copy the produced sticks here (replaced)')
    parser.add_argument('--cross-check', help='validate the sticks in this directory with TypeScript and Rust')
    args = parser.parse_args()
    if args.cross_check:
        sys.exit(cross_check(args.cross_check))
    with tempfile.TemporaryDirectory(prefix='xz-dj-write-') as temporary:
        temporary = Path(temporary)
        work = temporary / 'work'
        work.mkdir()
        prepare(work)
        binary = temporary / 'test_dj_profile_write'
        subprocess.run(['gcc', '-std=c11', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I', str(ROOT),
                        str(ROOT / 'tests/test_dj_profile_write.c'), str(ROOT / 'dj_profile_write.c'), str(ROOT / 'dj_profile.c'),
                        str(ROOT / 'audio/vendor/sha256/sha256.c'), '-o', str(binary)], check=True)
        failed = subprocess.run([str(binary), str(work)]).returncode
        produced = work / 'sticks'
        if shutil.which('node'):
            failed = node_check(produced) or failed
        else:
            print('typescript: node not on PATH here; run --cross-check on the exported sticks')
        if args.export:
            target = Path(args.export)
            if target.exists():
                shutil.rmtree(target)
            target.mkdir(parents=True)
            for name in EXPORTED:
                shutil.copytree(produced / name, target / name)
            print(f'exported {len(EXPORTED)} sticks to {target}')
        print('verify_dj_profile_write:', 'FAIL' if failed else 'ok')
        sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
