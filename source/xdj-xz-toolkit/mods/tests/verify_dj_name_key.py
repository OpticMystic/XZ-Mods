"""Check the C DJ Profile name key against name_key() from XZ-Mods spec/djprofile/tools/check_djprofile.py."""
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unicodedata
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import gen_dj_name_key  # noqa: E402


def ascii_fold(text): return ''.join(chr(ord(c)+32) if 'A' <= c <= 'Z' else c for c in text)
def name_key(name): return ascii_fold(unicodedata.normalize('NFC', name))


def assigned(cp): return not 0xD800 <= cp <= 0xDFFF and unicodedata.category(chr(cp)) != 'Cn'


def pool(*ranges): return [cp for lo, hi in ranges for cp in range(lo, hi+1) if assigned(cp)]


with tempfile.TemporaryDirectory(prefix='xz-dj-name-key-') as temporary:
    binary = Path(temporary)/'test_dj_name_key'
    subprocess.run(['gcc', '-std=c11', '-O1', '-g', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I', str(ROOT),
                    str(ROOT/'tests/test_dj_name_key.c'), str(ROOT/'dj_name_key.c'), '-o', str(binary)], check=True)
    builtin = subprocess.run([str(binary)])
    # '\n' separates lines for the driver; it is ASCII and folds to itself.
    points = [chr(cp) for cp in range(0x110000) if cp != 0x0A and assigned(cp)]
    nfd = [d for c in points if (d := unicodedata.normalize('NFD', c)) != c]
    marks = pool((0x300, 0x36F), (0x483, 0x489), (0x591, 0x5C7), (0x610, 0x65F), (0x93C, 0x94D), (0x9BC, 0x9D7),
                 (0xB3C, 0xB57), (0xBBE, 0xBD7), (0xCBC, 0xCD6), (0xD3E, 0xD57), (0xDCA, 0xDDF), (0x102E, 0x103A),
                 (0x1B34, 0x1B44), (0x1DC0, 0x1DFF), (0x20D0, 0x20F0), (0x302A, 0x302F), (0x3099, 0x309A),
                 (0xFE20, 0xFE2F), (0x1D165, 0x1D16D))
    marks = sorted(set(marks) | {ord(c) for c in points if unicodedata.combining(c)})
    letters = pool((0x41, 0x5A), (0x61, 0x7A), (0xC0, 0x24F), (0x1E00, 0x1EFF), (0x370, 0x3FF), (0x1F00, 0x1FFF),
                   (0x400, 0x4FF), (0xAC00, 0xD7A3), (0x1100, 0x11FF), (0x3040, 0x30FF), (0xF900, 0xFAFF),
                   (0x2F800, 0x2FA1F), (0x0B05, 0x0B39), (0x0B95, 0x0BB9), (0x0915, 0x0939), (0x1D15E, 0x1D164),
                   (0x2126, 0x212B), (0x20, 0x40))
    jamo = pool((0x1100, 0x1112), (0x1161, 0x1175), (0x11A8, 0x11C2))
    rng = random.Random(20261019)
    alphabet = [letters, marks, jamo]
    randoms = [''.join(chr(rng.choice(rng.choices(alphabet, weights=(5, 4, 2))[0])) for _ in range(rng.randint(1, 12)))
               for _ in range(300000)]
    longs = [rng.choice(['a', 'e', chr(0x1100), chr(0x304B)])+''.join(chr(rng.choice(marks)) for _ in range(rng.randint(2000, 4000)))
             for _ in range(20)]
    groups = {'code points': points, 'NFD forms': nfd, 'random strings': randoms, 'long mark runs': longs}
    names = [name for group in groups.values() for name in group]
    feed = b''.join(name.encode('utf-8')+b'\n' for name in names)
    got = subprocess.run([str(binary), 'lines'], input=feed, capture_output=True, check=True).stdout.split(b'\n')
    if got[-1] != b'' or len(got)-1 != len(names):
        print(f'driver printed {len(got)-1} keys for {len(names)} names')
        sys.exit(1)
    mismatches, at = 0, 0
    for label, group in groups.items():
        bad = 0
        for name in group:
            want = name_key(name).encode('utf-8')
            if got[at] != want:
                bad += 1
                if bad <= 5:
                    print(f'MISMATCH {label} {[hex(ord(c)) for c in name][:16]}: want {want[:48].hex()} got {got[at][:48].hex()}')
            at += 1
        print(f'{label}: {len(group)-bad}/{len(group)} agree')
        mismatches += bad
    header = (ROOT/'dj_name_key_table.h').read_text(encoding='ascii')
    version = header.split('#define XZ_DJ_NAME_KEY_UNICODE "', 1)[1].split('"', 1)[0]
    stale = False
    if version == unicodedata.unidata_version:
        stale = gen_dj_name_key.render()[0] != header
        print('dj_name_key_table.h:', 'STALE, rerun gen_dj_name_key.py' if stale else 'matches the generator')
    else:
        print(f'dj_name_key_table.h is Unicode {version}; this Python has {unicodedata.unidata_version}, freshness check skipped')
    failed = mismatches or builtin.returncode or stale
    print('verify_dj_name_key:', 'FAIL' if failed else 'ok')
    sys.exit(1 if failed else 0)
