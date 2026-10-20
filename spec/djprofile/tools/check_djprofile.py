#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Check a stick (or a folder laid out like one) against DJ Profile 1.0.

    python check_djprofile.py E:\\
    python check_djprofile.py path/to/stick --json

Prints each profile with its level, the active profile, every broken rule
(with the section of DJPROFILE.md that states it) and warnings for SHOULD
rules. Exit status is 0 when no rule is broken, 1 when one is, 2 when the path
is not a folder. Standard library only; Python 3.9+.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import unicodedata
from pathlib import Path

TOP_NAMES = ('djprofile', 'djprofiles')
MAX_PROFILES = 16
MAX_JSON = 64 * 1024
MAX_STICK_JSON = 16 * 1024
MAX_DEPTH = 8
MAX_LOGOS = 8
MIN_LOGO = 8
MAX_LOGO = 8 * 1024 * 1024
MAX_ALL_LOGOS = 24 * 1024 * 1024
MAX_SIDE = 4096
MAX_PIXELS = 4 * 1024 * 1024
MAX_RENDITION = 4 * 1024 * 1024
RAW_BYTES_PER_PIXEL = {'rgb565le': 2, 'rgb565be': 2, 'rgb888': 3, 'rgba8888': 4, 'bgra8888': 4}
LOGO_FIRST = ('logo.png', 'logo.jpg', 'logo.jpeg')
IMAGE_SUFFIXES = ('.png', '.jpg', '.jpeg')
FAT_FORBIDDEN = set('<>:"/\\|?*')
ID = re.compile(r'[A-Za-z0-9_-]{1,128}')
REVISION = re.compile(r'[\x21-\x7e]{1,64}')
COLOUR = re.compile(r'#[0-9a-fA-F]{6}')
SHA256 = re.compile(r'[0-9a-fA-F]{64}')
MARKER = re.compile(r'(?:^|(?<=\s))djprofile:1:([A-Za-z0-9_-]{1,128}):([0-9a-f]{12})(?=\s|$)')
SOF_BASELINE = (0xC0, 0xC1)
SOF_OTHER = (0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF)


class Report:
    def __init__(self):
        self.problems: list[dict] = []
        self.warnings: list[dict] = []
        self.profiles: list[dict] = []
        self.active: str | None = None

    def fail(self, where: str, section: str, text: str) -> None:
        self.problems.append({'where': where, 'section': section, 'problem': text})

    def warn(self, where: str, section: str, text: str) -> None:
        self.warnings.append({'where': where, 'section': section, 'problem': text})


def ascii_fold(text: str) -> str:
    return ''.join(chr(ord(c) + 32) if 'A' <= c <= 'Z' else c for c in text)


def name_key(name: str) -> str:
    return ascii_fold(unicodedata.normalize('NFC', name))


def sort_key(name: str) -> bytes:
    return name_key(name).encode('utf-8')


def canonical(document: dict) -> bytes:
    """RFC 8785, exact for documents that keep section 5's rules (ASCII member names, whole numbers)."""
    return json.dumps(document, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode('utf-8')


def canonical_hash12(document: dict) -> str:
    return hashlib.sha256(canonical({k: v for k, v in document.items() if k != 'track'})).hexdigest()[:12]


def _visible(folder: Path) -> list[Path]:
    return [p for p in folder.iterdir() if not p.name.startswith('.') and not p.is_symlink()]


def _fixed(folder: Path, name: str) -> Path | None:
    return next((p for p in _visible(folder) if p.is_file() and ascii_fold(p.name) == name), None)


def _control(text: str) -> bool:
    return any(ord(c) < 32 or 127 <= ord(c) <= 159 for c in text)


def _text_ok(value: object, max_bytes: int, min_bytes: int = 0) -> bool:
    return isinstance(value, str) and min_bytes <= len(value.encode('utf-8')) <= max_bytes


def _valid_name(name: object) -> bool:
    return _text_ok(name, 160, 1) and bool(name.strip()) and not _control(name)


def _valid_folder_name(name: str) -> bool:
    utf16_units = len(name.encode('utf-16-le')) // 2
    return (_valid_name(name) and utf16_units <= 100 and not (set(name) & FAT_FORBIDDEN)
            and not name.endswith((' ', '.')))


def _image_info(data: bytes) -> tuple[str, int, int, bool] | None:
    """(kind, width, height, plain) from the header; plain means baseline JPEG or 8-bit non-interlaced PNG."""
    if data[:8] == b'\x89PNG\r\n\x1a\n' and len(data) >= 29 and data[12:16] == b'IHDR':
        width, height, depth, _, _, _, interlace = struct.unpack('>IIBBBBB', data[16:29])
        return 'png', width, height, depth == 8 and interlace == 0
    if data[:2] == b'\xff\xd8':
        at = 2
        while at + 9 < len(data):
            if data[at] != 0xFF:
                return None
            marker = data[at + 1]
            if marker in (0xD8, 0x01, 0xFF) or 0xD0 <= marker <= 0xD7:
                at += 1 if marker == 0xFF else 2
                continue
            length = struct.unpack('>H', data[at + 2:at + 4])[0]
            if marker in SOF_BASELINE + SOF_OTHER:
                height, width = struct.unpack('>HH', data[at + 5:at + 9])
                return 'jpeg', width, height, marker in SOF_BASELINE
            at += 2 + length
    return None


def _check_image(report: Report, where: str, data: bytes) -> tuple[str, int, int] | None:
    if not MIN_LOGO <= len(data) <= MAX_LOGO:
        report.fail(where, '5.2', f'{len(data)} bytes; a logo is 8 bytes to 8 MiB')
        return None
    info = _image_info(data)
    if not info:
        report.fail(where, '5.2', 'not a PNG or JPEG')
        return None
    kind, width, height, plain = info
    if not (0 < width <= MAX_SIDE and 0 < height <= MAX_SIDE and width * height <= MAX_PIXELS):
        report.fail(where, '5.2', f'{width}x{height} is over 4096 a side or 4 megapixels')
        return None
    if not plain:
        report.warn(where, '5.2', 'progressive JPEG or interlaced/16-bit PNG; some readers will skip it')
    return kind, width, height


def _safe_relative(path: object) -> bool:
    if not isinstance(path, str) or not 0 < len(path.encode('utf-8')) <= 512:
        return False
    if path.startswith('/') or '\\' in path or re.match(r'[A-Za-z]:', path):
        return False
    parts = path.split('/')
    return len(parts) <= 2 and all(part and not part.startswith('.') for part in parts)


def _parse_json(report: Report, where: str, path: Path, limit: int) -> object | None:
    size = path.stat().st_size
    if size > limit:
        report.fail(where, '5', f'{size} bytes; the limit is {limit // 1024} KiB')
        return None
    data = path.read_bytes()
    if data.startswith(b'\xef\xbb\xbf'):
        report.fail(where, '5', 'starts with a byte-order mark')
        return None

    def pairs(items):
        keys = [key for key, _ in items]
        if len(set(keys)) != len(keys):
            raise ValueError('duplicate member name')
        if any(not key.isascii() for key in keys):
            raise ValueError('member name is not ASCII')
        return dict(items)

    def no_float(text):
        raise ValueError(f'{text} is not a whole number')

    try:
        value = json.loads(data.decode('utf-8'), object_pairs_hook=pairs, parse_float=no_float,
                           parse_constant=no_float)
    except (UnicodeDecodeError, ValueError, RecursionError) as error:
        report.fail(where, '5', f'not valid for DJ Profile: {error}')
        return None

    def depth(node, level=1):
        if isinstance(node, dict):
            return max([level] + [depth(v, level + 1) for v in node.values()])
        if isinstance(node, list):
            return max([level] + [depth(v, level + 1) for v in node])
        if isinstance(node, int) and not isinstance(node, bool) and not -2**53 <= node <= 2**53:
            raise ValueError(f'{node} is outside -2^53..2^53')
        return level - 1

    try:
        if depth(value) > MAX_DEPTH:
            report.fail(where, '5', 'nested more than 8 levels')
            return None
    except ValueError as error:
        report.fail(where, '5', str(error))
        return None
    return value


def _read_entry(report: Report, where: str, folder: Path, entry: object, section: str,
                limit: int) -> bytes | None:
    if not isinstance(entry, dict):
        report.fail(where, section, 'entry is not an object')
        return None
    path, size, digest = entry.get('file'), entry.get('bytes'), entry.get('sha256')
    if not _safe_relative(path):
        report.fail(where, '5.1', f'file {path!r} is not a safe path inside the profile folder')
        return None
    if not isinstance(size, int) or isinstance(size, bool) or not 0 <= size <= limit:
        report.fail(where, section, f'bytes must be a whole number up to {limit}')
        return None
    if not isinstance(digest, str) or not SHA256.fullmatch(digest):
        report.fail(where, section, 'sha256 must be 64 hex digits')
        return None
    target = folder / path
    if not target.is_file() or target.is_symlink():
        report.fail(where, section, f'{path} is missing')
        return None
    actual = target.stat().st_size
    if actual != size:
        report.fail(where, section, f'{path} is {actual} bytes, the document says {size}')
        return None
    data = target.read_bytes()
    if hashlib.sha256(data).hexdigest() != digest.lower():
        report.fail(where, section, f'{path} does not match its sha256')
        return None
    return data


def _id3(data: bytes) -> tuple[int, dict[str, list[bytes]]] | None:
    if len(data) < 10 or data[:3] != b'ID3' or data[3] not in (3, 4):
        return None
    version = data[3]
    size = (data[6] << 21) | (data[7] << 14) | (data[8] << 7) | data[9]
    body, frames, at = data[10:10 + size], {}, 0
    while at + 10 <= len(body) and body[at:at + 4] != b'\0\0\0\0':
        raw = body[at + 4:at + 8]
        length = ((raw[0] << 21) | (raw[1] << 14) | (raw[2] << 7) | raw[3]) if version == 4 else struct.unpack('>I', raw)[0]
        frames.setdefault(body[at:at + 4].decode('latin-1'), []).append(body[at + 10:at + 10 + length])
        at += 10 + length
    return version, frames


def _utf8_text(payload: bytes) -> str | None:
    if not payload or payload[0] != 3 or payload.endswith(b'\0'):
        return None
    return payload[1:].decode('utf-8', 'replace')


def _check_track_file(report: Report, where: str, data: bytes, document: dict, marker: str) -> None:
    parsed = _id3(data)
    if parsed is None or parsed[0] != 4:
        report.fail(where, '9', 'no ID3v2.4 tag at the start of the file')
        return
    frames = parsed[1]
    for frame_id, want in (('TIT2', document.get('name')), ('TPE1', document.get('name')),
                           ('TALB', 'DJ PROFILE'), ('TCON', 'DJ Profile')):
        values = [_utf8_text(p) for p in frames.get(frame_id, [])]
        if values != [want]:
            report.fail(where, '9', f'{frame_id} must be one UTF-8 frame reading {want!r}')
    comments = frames.get('COMM', [])
    if len(comments) != 1 or comments[0][:5] != b'\x03eng\0' or comments[0].endswith(b'\0'):
        report.fail(where, '9', 'needs exactly one UTF-8 COMM frame, language eng, empty description')
    elif not any(m.group(0) == marker for m in MARKER.finditer(comments[0][5:].decode('utf-8', 'replace'))):
        report.fail(where, '9', 'COMM does not carry the marker')
    txxx = [p for p in frames.get('TXXX', []) if p[:11] == b'\x03DJPROFILE\0']
    if len(txxx) != 1 or txxx[0].endswith(b'\0'):
        report.fail(where, '9', 'needs exactly one UTF-8 TXXX:DJPROFILE frame')
    else:
        try:
            carried = json.loads(txxx[0][11:].decode('utf-8'))
        except (UnicodeDecodeError, ValueError):
            report.fail(where, '9', 'TXXX:DJPROFILE is not JSON')
        else:
            if not isinstance(carried, dict) or canonical_hash12(carried) != marker.rsplit(':', 1)[1]:
                report.fail(where, '9', 'the marker hash does not match the document in TXXX:DJPROFILE')
    covers = [p for p in frames.get('APIC', []) if b'\0' in p[1:] and p[1:].split(b'\0', 1)[1][:1] == b'\x03']
    if len(covers) != 1:
        report.fail(where, '9', 'needs exactly one front cover (APIC type 3)')
        return
    mime, rest = covers[0][1:].split(b'\0', 1)
    picture = rest[1:].split(b'\0', 1)[1] if covers[0][0] in (0, 3) else b''
    info = _image_info(picture)
    if mime != b'image/jpeg' or not info or info[0] != 'jpeg' or not info[3] or info[1] != info[2] \
            or not 240 <= info[1] <= 800:
        report.fail(where, '9', 'the front cover must be a square baseline JPEG, 240 to 800 pixels a side')


def _in_library(stick: Path, marker: str) -> str | None:
    pdb = stick / 'PIONEER' / 'rekordbox' / 'export.pdb'
    if pdb.is_file():
        data = pdb.read_bytes()
        if marker.encode('ascii') in data or marker.encode('utf-16-le') in data:
            return 'export.pdb'
    return None


def _check_document(report: Report, where: str, folder: Path, document: object) -> dict | None:
    if not isinstance(document, dict):
        report.fail(where, '5', 'profile.json is not an object')
        return None
    before = len(report.problems)
    if document.get('format') != 'djprofile/1':
        report.fail(where, '5', 'format must be "djprofile/1"')
        return None
    if not isinstance(document.get('id'), str) or not ID.fullmatch(document['id']):
        report.fail(where, '5', 'id must be 1 to 128 of A-Z a-z 0-9 _ -')
    if not isinstance(document.get('revision'), str) or not REVISION.fullmatch(document['revision']):
        report.fail(where, '5', 'revision must be 1 to 64 printable ASCII characters')
    if not _valid_name(document.get('name')):
        report.fail(where, '5', 'name must be 1 to 160 bytes, not blank, no control characters')
    if 'tagline' in document and not _text_ok(document['tagline'], 240):
        report.fail(where, '5', 'tagline must be text of at most 240 bytes')
    if 'accent' in document and not (isinstance(document['accent'], str) and COLOUR.fullmatch(document['accent'])):
        report.fail(where, '5', 'accent must be #rrggbb')
    aliases = document.get('aliases', [])
    if not (isinstance(aliases, list) and len(aliases) <= 8 and all(_valid_name(a) for a in aliases)):
        report.fail(where, '5', 'aliases must be up to 8 names of 1 to 160 bytes')
    links = document.get('links', [])
    if not isinstance(links, list) or len(links) > 12:
        report.fail(where, '5.3', 'links must be a list of at most 12')
        links = []
    for i, link in enumerate(links):
        if not (isinstance(link, dict) and _text_ok(link.get('kind'), 40, 1) and _text_ok(link.get('url'), 2048)
                and link['url'].startswith('https://')):
            report.fail(f'{where} links[{i}]', '5.3', 'a link is {kind, url} with an https:// url')
    if 'note' in document and not _text_ok(document['note'], 8192):
        report.fail(where, '5', 'note must be text of at most 8192 bytes')
    ext = document.get('ext', {})
    if not (isinstance(ext, dict) and all(isinstance(v, dict) for v in ext.values())):
        report.fail(where, '10', 'ext maps vendor names to objects')
    logo = None
    if 'logos' in document:
        logos = document['logos']
        if not isinstance(logos, list) or not 1 <= len(logos) <= MAX_LOGOS:
            report.fail(where, '5.1', 'logos must hold 1 to 8 entries')
            logos = []
        primaries = [e for e in logos if isinstance(e, dict) and e.get('role') == 'primary']
        if len(primaries) != 1:
            report.fail(where, '5.1', 'exactly one logo has role "primary"')
        elif ascii_fold(str(primaries[0].get('file'))) not in ('logo.png', 'logo.jpg'):
            report.fail(where, '5.1', 'the primary logo file is logo.png or logo.jpg at the top of the folder')
        total = 0
        for i, entry in enumerate(logos):
            label = f'{where} logos[{i}]'
            if isinstance(entry, dict):
                if entry.get('role') not in ('primary', 'alternate'):
                    report.fail(label, '5.1', 'role must be "primary" or "alternate"')
                if entry.get('background', 'any') not in ('dark', 'light', 'any'):
                    report.fail(label, '5.1', 'background must be "dark", "light" or "any"')
                if 'label' in entry and not _text_ok(entry['label'], 160):
                    report.fail(label, '5.1', 'label must be at most 160 bytes')
                if 'note' in entry and not _text_ok(entry['note'], 1024):
                    report.fail(label, '5.1', 'note must be at most 1024 bytes')
            data = _read_entry(report, label, folder, entry, '5.1', MAX_LOGO)
            if data is not None:
                total += len(data)
                if _check_image(report, label, data) and primaries and entry is primaries[0]:
                    logo = entry['file']
        if total > MAX_ALL_LOGOS:
            report.fail(where, '5.1', 'logos add up to more than 24 MiB')
    renditions = document.get('renditions', [])
    if not isinstance(renditions, list) or len(renditions) > 8:
        report.fail(where, '5.4', 'renditions must be a list of at most 8')
        renditions = []
    for i, rendition in enumerate(renditions):
        # Section 5.4: readers ignore a broken rendition, so it is a warning and the profile still counts.
        label, own = f'{where} renditions[{i}]', Report()
        data = _read_entry(own, label, folder, rendition, '5.4', MAX_RENDITION)
        if data is not None:
            w, h, fmt = rendition.get('width'), rendition.get('height'), rendition.get('format')
            if not (isinstance(w, int) and isinstance(h, int) and 0 < w <= MAX_SIDE and 0 < h <= MAX_SIDE
                    and _text_ok(fmt, 40, 1) and _text_ok(rendition.get('for'), 80, 1)):
                own.fail(label, '5.4', 'needs width, height (1 to 4096), format and for')
            elif fmt in RAW_BYTES_PER_PIXEL and len(data) != w * h * RAW_BYTES_PER_PIXEL[fmt]:
                own.fail(label, '5.4', f'{fmt} {w}x{h} must be {w * h * RAW_BYTES_PER_PIXEL[fmt]} bytes')
        report.warnings += [{**item, 'problem': 'ignored: ' + item['problem']} for item in own.problems]
    track = None
    if 'track' in document:
        track = document['track']
        marker = track.get('marker') if isinstance(track, dict) else None
        found = MARKER.fullmatch(marker) if isinstance(marker, str) else None
        if not found:
            report.fail(where, '9', 'track.marker must be djprofile:1:<id>:<12 hex>')
            track = None
        else:
            if found.group(1) != document.get('id'):
                report.fail(where, '9', 'track.marker names a different id')
            if canonical_hash12(document) != found.group(2):
                report.warn(where, '9', 'the profile track was made before the last edit; make a new one')
            if 'file' in track:
                data = _read_entry(report, where + ' track', folder, track, '9', 64 * 1024 * 1024)
                if data is not None:
                    _check_track_file(report, where + ' track', data, document, marker)
            track = marker
    if len(report.problems) > before:
        return None
    return {'logo': logo, 'marker': track}


def _level0_logo(report: Report, where: str, folder: Path) -> str | None:
    images = [p for p in _visible(folder) if p.is_file() and ascii_fold(p.suffix) in IMAGE_SUFFIXES]
    images.sort(key=lambda p: (LOGO_FIRST.index(ascii_fold(p.name)) if ascii_fold(p.name) in LOGO_FIRST else 3,
                               sort_key(p.name)))
    for image in images:
        if image.stat().st_size <= MAX_LOGO and _check_image(Report(), '', image.read_bytes()):
            return image.name
        report.warn(f'{where}/{image.name}', '4', 'skipped: not a usable PNG or JPEG')
    return None


def check(stick: Path) -> Report:
    report = Report()
    tops = sorted((p for p in _visible(stick) if p.is_dir() and ascii_fold(p.name) in TOP_NAMES),
                  key=lambda p: TOP_NAMES.index(ascii_fold(p.name)))
    if tops and tops[0].name != 'DJPROFILE':
        report.fail(tops[0].name, '3', 'writers name the folder exactly DJPROFILE')
    folders: dict[str, Path] = {}
    for top in tops:
        for entry in sorted(_visible(top), key=lambda p: sort_key(p.name)):
            if entry.is_dir():
                folders.setdefault(name_key(entry.name), entry)
    stick_file = None
    if tops and (path := _fixed(tops[0], 'djprofile.json')):
        stick_file = _parse_json(report, 'DJPROFILE.json', path, MAX_STICK_JSON)
        if not isinstance(stick_file, dict) or stick_file.get('format') != 'djprofile-stick/1':
            report.fail('DJPROFILE.json', '6', 'format must be "djprofile-stick/1"')
            stick_file = None

    candidates = []
    for folder in folders.values():
        where = f'{folder.parent.name}/{folder.name}'
        if not _valid_folder_name(folder.name):
            report.fail(where, '3', 'folder name breaks the FAT32 rules (length, forbidden or control character, trailing space or dot)')
        profile = None
        for name in ('profile.json', 'profile.previous.json'):
            path = _fixed(folder, name)
            if path is None:
                continue
            document = _parse_json(report, f'{where}/{path.name}', path, MAX_JSON)
            result = _check_document(report, f'{where}/{path.name}', folder, document)
            if result is not None:
                logo = result['logo'] or (None if 'logos' in document else _level0_logo(report, where, folder))
                if logo:
                    profile = {'folder': folder.name, 'id': document['id'], 'name': document['name'], 'level': 1,
                               'logo': logo, 'marker': result['marker']}
                    if name != 'profile.json':
                        report.warn(where, '5', 'profile.json is unusable; read profile.previous.json')
                break
        if profile is None:
            logo = _level0_logo(report, where, folder)
            if logo is None:
                report.warn(where, '4', 'no usable PNG or JPEG, so this folder is not a profile')
                continue
            profile = {'folder': folder.name, 'id': None, 'name': folder.name, 'level': 0, 'logo': logo, 'marker': None}
        if profile['marker']:
            library = _in_library(stick, profile['marker'])
            profile['track_library'] = library
            if library:
                profile['level'] = 2
        candidates.append(profile)

    def resolve(reference, pool):
        if not isinstance(reference, str):
            return None
        by_id = next((p for p in pool if p['id'] == reference), None)
        key = name_key(reference)
        return by_id or next((p for p in pool if p['folder'] and name_key(p['folder']) == key), None)

    entries = (stick_file or {}).get('profiles', [])
    if not isinstance(entries, list) or len(entries) > MAX_PROFILES:
        report.fail('DJPROFILE.json', '6', 'profiles must be a list of at most 16')
        entries = []
    ordered, seen = [], set()
    for i, entry in enumerate(entries):
        if not (isinstance(entry, dict) and isinstance(entry.get('folder'), str) and _valid_folder_name(entry['folder'])):
            report.fail(f'DJPROFILE.json profiles[{i}]', '6', 'an entry is {folder, id} with a valid folder name')
            continue
        if 'id' in entry:
            profile = next((p for p in candidates if p['id'] == entry['id']), None)
        else:
            profile = next((p for p in candidates if name_key(p['folder']) == name_key(entry['folder'])), None)
        if profile is None:
            report.warn(f'DJPROFILE.json profiles[{i}]', '6', f'{entry["folder"]!r} is not a profile here; skipped')
        elif id(profile) not in seen:
            if name_key(profile['folder']) != name_key(entry['folder']):
                report.warn(f'DJPROFILE.json profiles[{i}]', '6', f'lists {entry["folder"]!r}, found by id as {profile["folder"]!r}')
            ordered.append(profile)
            seen.add(id(profile))
    ordered += [p for p in candidates if id(p) not in seen]

    ids = set()
    for profile in ordered:
        if profile['id'] is not None and profile['id'] in ids:
            report.warn(profile['folder'], '5', 'same id as an earlier profile; skipped')
            continue
        if profile['id'] is not None:
            ids.add(profile['id'])
        report.profiles.append(profile)
    report.profiles = report.profiles[:MAX_PROFILES]

    active = resolve((stick_file or {}).get('active'), report.profiles)
    if stick_file and stick_file.get('active') is not None and active is None:
        report.fail('DJPROFILE.json', '6', f'active {stick_file["active"]!r} names no profile here')
    active = active or (report.profiles[0] if report.profiles else None)
    report.active = active['folder'] if active else None
    if not tops:
        report.fail(str(stick), '3', 'no DJPROFILE folder at the root')
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description='Check a stick against DJ Profile 1.0.')
    parser.add_argument('stick', type=Path)
    parser.add_argument('--json', action='store_true', help='print the report as JSON')
    args = parser.parse_args(argv)
    if not args.stick.is_dir():
        print(f'{args.stick} is not a folder', file=sys.stderr)
        return 2
    report = check(args.stick)
    if args.json:
        print(json.dumps({'active': report.active, 'profiles': report.profiles, 'problems': report.problems,
                          'warnings': report.warnings}, indent=2, ensure_ascii=False))
    else:
        for p in report.profiles:
            mark = ' (active)' if p['folder'] == report.active else ''
            extra = ''
            if p.get('marker'):
                extra = f'  track in {p["track_library"]}' if p.get('track_library') else \
                    '  track made, not found in export.pdb (OneLibrary cannot be checked here)'
            print(f'level {p["level"]}  {p["folder"]}{mark}{extra}')
        for kind, items in (('problem', report.problems), ('warning', report.warnings)):
            for item in items:
                print(f'{kind}  section {item["section"]}  {item["where"]}: {item["problem"]}')
        if not report.profiles:
            print('no profiles')
        elif not report.problems:
            print('conforms')
    return 1 if report.problems else 0


if __name__ == '__main__':
    sys.exit(main())
