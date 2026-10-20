# SPDX-License-Identifier: MIT
"""DJ profiles on a USB stick and the silent profile track.

Reads and writes DJ Profile 1.0 (spec/djprofile/DJPROFILE.md in this repository): one folder per DJ inside
DJPROFILE/, the optional stick file DJPROFILE/DJPROFILE.json, and the profile track of section 9.

Profiles stored before 1.0 (DJPROFILE/VJTOOLS and the older VJ.Tools/Branding card) are still read. Any write
first moves the whole stick to 1.0, following the migration contract in VJToolsLibrary
docs/architecture/dj-profile-standard.md section 0: every old profile gets a profile folder, then the old
VJTOOLS folder is copied to this computer, checked file by file and only then deleted from the stick.
The Branding card is moved and never deleted. Nothing here writes outside DJPROFILE/ on the stick.
"""
from __future__ import annotations

import base64
from contextlib import contextmanager
import copy
from dataclasses import dataclass, field
from datetime import datetime, timezone
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import secrets
import shutil
import struct
import subprocess
import unicodedata
from urllib.parse import quote
import uuid

from . import onelibrary, rekordbox_pdb
from .cache import _regular, _safe_path
from .jobs import exclusive_lock

TOP = 'DJPROFILE'
TOP_NAMES = ('djprofile', 'djprofiles')
STICK_FILE = 'DJPROFILE.json'
PROFILE_FILE = 'profile.json'
PREVIOUS_FILE = 'profile.previous.json'
FORMAT = 'djprofile/1'
STICK_FORMAT = 'djprofile-stick/1'
VENDOR = 'vj.tools'
FOLDER_ID = 'folder:'
LOCK = '.xzmods-write.lock'
BACKUPS = 'dj-profile-backups'
BACKUP_FORMAT = 'xz-dj-profile-backup/1'

OLD_HOME = 'VJTOOLS'
OLD_INDEX_PATH = 'DJPROFILE/VJTOOLS/index.json'
OLD_CARD_PATH = 'VJ.Tools/Branding/profile.json'
OLD_INDEX_SCHEMA = 'vjtools.dj-profile-index'
OLD_PROFILE_SCHEMA = 'vj.tools.dj-profile'
OLD_KNOWN = {'schema', 'version', 'dj_id', 'revision', 'dj_name', 'note', 'bundle', 'preferred_logo', 'secondary_logos',
             'xz_tile', 'tagline', 'accent', 'aliases', 'links', 'page_url', 'profile_track'}

MAX_PROFILES = 16
MAX_JSON = 64 * 1024
MAX_STICK_JSON = 16 * 1024
MAX_DEPTH = 8
MAX_NAME = 160
MAX_TAGLINE = 240
MAX_ALIASES = 8
MAX_LINKS = 12
MAX_LINK_KIND = 40
MAX_URL = 2048
MAX_NOTE_BYTES = 8 * 1024
MAX_LOGO_NOTE_BYTES = 1024
MAX_LOGOS = 8
MIN_LOGO_BYTES = 8
MAX_LOGO_BYTES = 8 * 1024 * 1024
MAX_TOTAL_LOGO_BYTES = 24 * 1024 * 1024
MAX_RENDITIONS = 8
MAX_RENDITION_BYTES = 4 * 1024 * 1024
MAX_TRACK_BYTES = 64 * 1024 * 1024
MAX_SIDE = 4096
MAX_PIXELS = 4 * 1024 * 1024
# Logos the DJ picks on the computer may be larger; they are scaled down to MAX_SIDE / MAX_PIXELS on the stick.
MAX_INPUT_SIDE = 8192
MAX_INPUT_PIXELS = 16 * 1024 * 1024
MAX_FOLDER_UNITS = 100
OLD_MAX_TAGLINE = 80
OLD_MAX_ALIAS = 80

TILE_SIDE = 160
TILE_FORMAT = 'rgb565le'
TILE_FOR = 'xdj-xz'
TILE_BYTES = TILE_SIDE * TILE_SIDE * 2
TILE_FILE = 'tile-160.rgb565'
RAW_BYTES_PER_PIXEL = {'rgb565le': 2, 'rgb565be': 2, 'rgb888': 3, 'rgba8888': 4, 'bgra8888': 4}
LOGO_NAMES = ('logo.png', 'logo.jpg', 'logo.jpeg')
IMAGE_SUFFIXES = ('.png', '.jpg', '.jpeg')
ORIGINAL_STEM = 'logo-original'
PREVIEW_SIDES = (256, 128, 64)
# The desktop host drops backend result lines over 64 KiB, so all previews share this budget.
PREVIEW_BUDGET = 40 * 1024
COVER_MIN, COVER_MAX = 240, 800
# Spec section 11: values 1 to 8 of the stick's DJ software colour.
STICK_COLOURS = {1: '#f870f8', 2: '#f87070', 3: '#ffa064', 4: '#f8e550', 5: '#1ee12b', 6: '#10e4dc', 7: '#1e50f0', 8: '#9828f0'}
PNG_MAGIC = b'\x89PNG\r\n\x1a\n'
JPEG_MAGIC = b'\xff\xd8\xff'
FORMATS = {'png': ('png', 'image/png'), 'jpeg': ('jpg', 'image/jpeg')}
EPK_KINDS = ('media-kit', 'epk', 'files')
FAT_FORBIDDEN = '<>:"/\\|?*'
CONFLICT = 'The DJ profiles on this USB changed since they were loaded (maybe on the XZ). Reload the USB and try again.'


class ProfileError(ValueError):
    pass


def _need(condition, message):
    if not condition:
        raise ProfileError(message)


# --- names (spec section 3) ---

def _fold(text):
    return ''.join(chr(ord(c) + 32) if 'A' <= c <= 'Z' else c for c in text)


def name_key(name):
    return _fold(unicodedata.normalize('NFC', name))


def _sort_key(name):
    return name_key(name).encode('utf-8')


def _utf8_len(text):
    return len(text.encode('utf-8'))


def _control(text):
    return any(ord(c) < 32 or 127 <= ord(c) <= 159 for c in text)


def _cut(text, max_bytes):
    """text cut at a character boundary to at most max_bytes of UTF-8."""
    data = text.encode('utf-8')[:max_bytes]
    return data.decode('utf-8', 'ignore')


def _utf16_units(text):
    return len(text.encode('utf-16-le')) // 2


def _spec_text(value, max_bytes, min_bytes=0):
    return isinstance(value, str) and min_bytes <= _utf8_len(value) <= max_bytes


def _spec_name(value):
    return _spec_text(value, MAX_NAME, 1) and bool(value.strip()) and not _control(value)


def _valid_folder_name(name):
    return (_spec_name(name) and _utf16_units(name) <= MAX_FOLDER_UNITS and not set(name) & set(FAT_FORBIDDEN)
            and not name.endswith((' ', '.')))


def folder_name(dj_name):
    """A profile folder name for dj_name. FAT-invalid and control characters are removed, with no leading dot or space,
    no trailing space or dot, at most 100 UTF-16 units and 160 UTF-8 bytes (contract 0.3 step 1)."""
    text = ''.join(c for c in unicodedata.normalize('NFC', dj_name) if c not in FAT_FORBIDDEN and not _control(c))
    text = text.lstrip(' .').rstrip(' .')
    while _utf16_units(text) > MAX_FOLDER_UNITS or _utf8_len(text) > MAX_NAME:
        text = text[:-1]
    return text.rstrip(' .') or 'DJ'


def _unique_folder(name, taken):
    """name, or name with ' 2', ' 3' ... so its name key is not in taken."""
    candidate, n = name, 2
    while name_key(candidate) in taken or name_key(candidate) == _fold(OLD_HOME):
        suffix = f' {n}'
        base = name
        while _utf16_units(base + suffix) > MAX_FOLDER_UNITS or _utf8_len(base + suffix) > MAX_NAME:
            base = base[:-1]
        candidate, n = base.rstrip(' .') + suffix, n + 1
    return candidate


def _unique_file(name, used):
    """name, or stem-2.ext, stem-3.ext ... so its folded form is not in used."""
    stem, dot, ext = name.rpartition('.')
    if not dot:
        stem, ext = name, ''
    candidate, n = name, 2
    while _fold(candidate) in used:
        candidate, n = f'{stem}-{n}{dot}{ext}', n + 1
    return candidate


# --- validation of what the DJ types (the editor) ---

_ID = re.compile(r'[A-Za-z0-9_-]{1,128}')
_REVISION = re.compile(r'[\x21-\x7e]{1,64}')
_DIGEST = re.compile(r'[0-9a-fA-F]{64}')
_ACCENT = re.compile(r'#[0-9a-fA-F]{6}')
MARKER = re.compile(r'djprofile:1:([A-Za-z0-9_-]{1,128}):([0-9a-f]{12})')


def _string(value, label):
    if not isinstance(value, str):
        raise ProfileError(f'{label} must be text')
    return value


def _id(value, label='DJ ID'):
    if not isinstance(value, str) or not _ID.fullmatch(value):
        raise ProfileError(f'{label} must contain 1 to 128 letters, numbers, hyphens, or underscores')
    return value


def _label(value, label, max_bytes):
    value = _string(value, label)
    if not value.replace('\ufeff', '').strip() or _utf8_len(value) > max_bytes or _control(value):
        raise ProfileError(f'{label} must be 1 to {max_bytes} bytes of printable characters')
    return value


def _text(value, label, max_bytes):
    value = _string(value, label)
    if _utf8_len(value) > max_bytes or _control(value):
        raise ProfileError(f'{label} must be at most {max_bytes} bytes of printable characters')
    return value


def _note(value, label, max_bytes):
    value = _string(value, label)
    if _utf8_len(value) > max_bytes or any(unicodedata.category(c) == 'Cc' and c not in '\n\r\t' for c in value):
        raise ProfileError(f'{label} must be at most {max_bytes} bytes of plain text')
    return value


def _https(value, label):
    value = _string(value, label)
    host = re.split(r'[/?#]', value[len('https://'):], maxsplit=1)[0] if value.startswith('https://') else ''
    if not host or _utf8_len(value) > MAX_URL or any(c.isspace() for c in value) or _control(value):
        raise ProfileError(f'{label} must be an https:// address of at most {MAX_URL} characters')
    return value


def _accent(value):
    if not isinstance(value, str) or not _ACCENT.fullmatch(value):
        raise ProfileError('Accent must be a #rrggbb colour')
    return value


def _list(value, label, max_items):
    if not isinstance(value, list):
        raise ProfileError(f'{label} must be a list')
    if len(value) > max_items:
        raise ProfileError(f'A profile may list at most {max_items} {label.lower()}')
    return value


def _count(value, label):
    if type(value) is not int or value < 0:
        raise ProfileError(f'{label} must be a whole number')
    return value


# --- JSON under the rules of spec section 5 ---

def _json_rules(value, level=1):
    """Raises unless value nests at most 8 levels, has ASCII member names and only whole numbers within 2^53."""
    if isinstance(value, dict):
        _need(level <= MAX_DEPTH, 'JSON is nested more than 8 levels')
        for key, item in value.items():
            _need(isinstance(key, str) and key.isascii(), 'JSON member names must be ASCII')
            _json_rules(item, level + 1)
    elif isinstance(value, list):
        _need(level <= MAX_DEPTH, 'JSON is nested more than 8 levels')
        for item in value:
            _json_rules(item, level + 1)
    elif isinstance(value, float):
        raise ProfileError(f'{value} is not a whole number')
    elif isinstance(value, int) and not isinstance(value, bool):
        _need(-2 ** 53 <= value <= 2 ** 53, f'{value} is outside -2^53..2^53')


def parse_strict(data, limit, label):
    """A profile.json or DJPROFILE.json read under spec section 5: size, no BOM, no duplicate or non-ASCII
    member names, whole numbers only, at most 8 levels."""
    _need(len(data) <= limit, f'{label} is larger than {limit // 1024} KiB')
    _need(not data.startswith(b'\xef\xbb\xbf'), f'{label} starts with a byte-order mark')

    def pairs(items):
        keys = [key for key, _ in items]
        if len(set(keys)) != len(keys):
            raise ValueError('duplicate member name')
        if not all(key.isascii() for key in keys):
            raise ValueError('member name is not ASCII')
        return dict(items)

    def no_float(text):
        raise ValueError(f'{text} is not a whole number')

    try:
        value = json.loads(data.decode('utf-8'), object_pairs_hook=pairs, parse_float=no_float, parse_constant=no_float)
    except (UnicodeDecodeError, ValueError, RecursionError) as exc:
        raise ProfileError(f'{label} is not valid JSON for DJ Profile: {exc}') from None
    _json_rules(value)
    return value


def pretty(value):
    return json.dumps(value, indent=2, ensure_ascii=False).encode()


def compact(value):
    return json.dumps(value, separators=(',', ':'), ensure_ascii=False).encode()


def canonical(document):
    """RFC 8785 for documents under spec section 5's rules (ASCII member names, whole numbers)."""
    return json.dumps(document, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode('utf-8')


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def hash12(document):
    return _sha(canonical({key: value for key, value in document.items() if key != 'track'}))[:12]


def track_marker(document):
    """djprofile:1:<id>:<hash12> (spec section 9)."""
    return f'djprofile:1:{document["id"]}:{hash12(document)}'


def _safe_file(path):
    """Whether path stays inside the profile folder, at most one subfolder deep, with no dot segments (spec 5.1)."""
    if not isinstance(path, str) or not 0 < _utf8_len(path) <= 512:
        return False
    if path.startswith('/') or '\\' in path or re.match(r'[A-Za-z]:', path) or _control(path):
        return False
    parts = path.split('/')
    return len(parts) <= 2 and all(part and not part.startswith('.') for part in parts)


def _file_entry(entry, label, limit):
    _need(isinstance(entry, dict), f'{label} must be an object')
    _need(_safe_file(entry.get('file')), f'{label} file must be a path inside the profile folder')
    size = entry.get('bytes')
    _need(type(size) is int and 0 <= size <= limit, f'{label} bytes must be a whole number up to {limit}')
    _need(isinstance(entry.get('sha256'), str) and _DIGEST.fullmatch(entry['sha256']), f'{label} sha256 must be 64 hex digits')


def validate_profile(document):
    """A 1.0 profile document checked against every MUST of spec section 5. Returns it unchanged."""
    _need(isinstance(document, dict), 'A profile must be a JSON object')
    _need(document.get('format') == FORMAT, 'format must be "djprofile/1"')
    _need(isinstance(document.get('id'), str) and _ID.fullmatch(document['id']), 'id must be 1 to 128 of A-Z a-z 0-9 _ -')
    _need(isinstance(document.get('revision'), str) and _REVISION.fullmatch(document['revision']),
          'revision must be 1 to 64 printable ASCII characters')
    _need(_spec_name(document.get('name')), 'name must be 1 to 160 bytes, not blank, no control characters')
    if 'tagline' in document:
        _need(_spec_text(document['tagline'], MAX_TAGLINE), 'tagline must be text of at most 240 bytes')
    if 'accent' in document:
        _need(isinstance(document['accent'], str) and _ACCENT.fullmatch(document['accent']), 'accent must be #rrggbb')
    aliases = document.get('aliases', [])
    _need(isinstance(aliases, list) and len(aliases) <= MAX_ALIASES and all(_spec_name(a) for a in aliases),
          'aliases must be up to 8 names of 1 to 160 bytes')
    links = document.get('links', [])
    _need(isinstance(links, list) and len(links) <= MAX_LINKS, 'links must be a list of at most 12')
    for link in links:
        _need(isinstance(link, dict) and _spec_text(link.get('kind'), MAX_LINK_KIND, 1) and _spec_text(link.get('url'), MAX_URL)
              and link['url'].startswith('https://'), 'a link is {kind, url} with an https:// url')
    if 'note' in document:
        _need(_spec_text(document['note'], MAX_NOTE_BYTES), 'note must be text of at most 8192 bytes')
    ext = document.get('ext')
    _need(ext is None or (isinstance(ext, dict) and all(isinstance(v, dict) for v in ext.values())), 'ext maps vendor names to objects')
    if 'logos' in document:
        logos = document['logos']
        _need(isinstance(logos, list) and 1 <= len(logos) <= MAX_LOGOS, 'logos must hold 1 to 8 entries')
        primaries = [entry for entry in logos if isinstance(entry, dict) and entry.get('role') == 'primary']
        _need(len(primaries) == 1, 'exactly one logo has role "primary"')
        _need(isinstance(primaries[0].get('file'), str) and _fold(primaries[0]['file']) in ('logo.png', 'logo.jpg'),
              'the primary logo file is logo.png or logo.jpg at the top of the folder')
        for entry in logos:
            _file_entry(entry, 'Each logo', MAX_LOGO_BYTES)
            _need(entry.get('role') in ('primary', 'alternate'), 'logo role must be "primary" or "alternate"')
            _need(entry.get('background', 'any') in ('dark', 'light', 'any'), 'logo background must be "dark", "light" or "any"')
            _need('label' not in entry or _spec_text(entry['label'], MAX_NAME), 'logo label must be at most 160 bytes')
            _need('note' not in entry or _spec_text(entry['note'], MAX_LOGO_NOTE_BYTES), 'logo note must be at most 1024 bytes')
        _need(sum(entry['bytes'] for entry in logos) <= MAX_TOTAL_LOGO_BYTES, 'logos add up to more than 24 MiB')
    renditions = document.get('renditions', [])
    _need(isinstance(renditions, list) and len(renditions) <= MAX_RENDITIONS, 'renditions must be a list of at most 8')
    for rendition in renditions:
        _file_entry(rendition, 'Each rendition', MAX_RENDITION_BYTES)
        width, height, kind = rendition.get('width'), rendition.get('height'), rendition.get('format')
        _need(type(width) is int and type(height) is int and 0 < width <= MAX_SIDE and 0 < height <= MAX_SIDE
              and _spec_text(kind, 40, 1) and _spec_text(rendition.get('for'), 80, 1),
              'a rendition needs width, height (1 to 4096), format and for')
        _need(kind not in RAW_BYTES_PER_PIXEL or rendition['bytes'] == width * height * RAW_BYTES_PER_PIXEL[kind],
              f'a {kind} rendition must be width * height * bytes per pixel')
    if 'track' in document:
        track = document['track']
        found = MARKER.fullmatch(track.get('marker', '')) if isinstance(track, dict) and isinstance(track.get('marker'), str) else None
        _need(found is not None, 'track.marker must be djprofile:1:<id>:<12 hex>')
        _need(found.group(1) == document['id'], 'track.marker names a different id')
        if 'file' in track:
            _file_entry(track, 'The track', MAX_TRACK_BYTES)
    return document


def validate_stick_file(document):
    _need(isinstance(document, dict) and document.get('format') == STICK_FORMAT, 'format must be "djprofile-stick/1"')
    if 'revision' in document:
        _need(isinstance(document['revision'], str) and _REVISION.fullmatch(document['revision']), 'revision must be printable ASCII')
    entries = document.get('profiles', [])
    _need(isinstance(entries, list) and len(entries) <= MAX_PROFILES, 'profiles must be a list of at most 16')
    for entry in entries:
        _need(isinstance(entry, dict) and isinstance(entry.get('folder'), str) and _valid_folder_name(entry['folder']),
              'a profiles entry is {folder, id} with a valid folder name')
        _need('id' not in entry or (isinstance(entry['id'], str) and _ID.fullmatch(entry['id'])), 'a profiles entry id must be a profile id')
    _need(document.get('active') is None or isinstance(document['active'], str), 'active must be a profile reference')
    return document


# --- the store before 1.0 (contract section 0.1), validated as the shipped readers did ---

def _old_parts(path):
    if (not isinstance(path, str) or not path or len(path) > 1024 or path.startswith('/')
            or any(c in path for c in '\\:\0')):
        raise ProfileError('Profile paths must be relative paths with forward slashes')
    parts = path.split('/')
    for part in parts:
        if (not part or part in ('.', '..') or len(part) > 255 or part.endswith((' ', '.'))
                or _control(part) or any(c in part for c in '*?"<>|')):
            raise ProfileError('Unsafe profile path component')
    return parts


def _old_bundle(bundle, dj_id):
    parts = _old_parts(bundle)
    if len(parts) == 4 and parts[:3] == ['VJ.Tools', 'Branding', 'bundles']:
        bundle_id = parts[3]
    elif len(parts) == 6 and parts[:3] == ['DJPROFILE', 'VJTOOLS', 'profiles'] and parts[3] == dj_id and parts[4] == 'bundles':
        bundle_id = parts[5]
    else:
        raise ProfileError("Bundle must be an immutable directory in VJ.Tools/Branding/bundles or in this DJ's DJPROFILE/VJTOOLS/profiles/<dj_id>/bundles")
    _id(bundle_id, 'Branding bundle ID')
    return bundle


def _old_member(path, directory, dj_id, problem):
    parts = _old_parts(path)
    if len(parts) < 3 or parts[-2] != directory:
        raise ProfileError(problem)
    bundle = '/'.join(parts[:-2])
    try:
        _old_bundle(bundle, dj_id)
    except ProfileError:
        raise ProfileError(problem) from None
    return bundle


LOGO_PLACEMENT = "Logos must be regular files in an immutable branding bundle's assets directory"
TILE_PLACEMENT = "The XZ tile must be a file in its branding bundle's renditions directory"


def _old_label(value, label, max_chars):
    value = _string(value, label)
    if not value.replace('\ufeff', '').strip() or len(value) > max_chars or _control(value):
        raise ProfileError(f'{label} must contain 1 to {max_chars} printable characters')
    return value


def _old_logo(value):
    if not isinstance(value, dict):
        raise ProfileError('Each logo must be an object')
    _string(value.get('path'), 'Logo path')
    size = _count(value.get('bytes'), 'Logo size')
    digest = value.get('sha256')
    if not 0 < size <= MAX_LOGO_BYTES or not isinstance(digest, str) or not _DIGEST.fullmatch(digest):
        raise ProfileError('Each logo needs a byte count of at most 8 MiB and a SHA-256 digest')
    label = _string(value.get('label', ''), 'Logo label')
    if len(label) > MAX_NAME or _control(label):
        raise ProfileError('Logo labels must be at most 160 printable characters')
    _note(value.get('note', ''), 'Logo note', MAX_LOGO_NOTE_BYTES)
    return value


def _old_logos(document):
    return ([document['preferred_logo']] if document.get('preferred_logo') else []) + document.get('secondary_logos', [])


def validate_old_document(document):
    """An old-store profile.json, validated the way every shipped pre-1.0 reader does. Returns it unchanged."""
    if not isinstance(document, dict):
        raise ProfileError('A profile must be a JSON object')
    if document.get('schema') != OLD_PROFILE_SCHEMA or document.get('version') != 1 or type(document.get('version')) is not int:
        raise ProfileError('Unsupported USB DJ profile schema or version')
    dj_id = _id(document.get('dj_id'))
    _id(document.get('revision'), 'Profile revision')
    _old_label(document.get('dj_name'), 'DJ name', MAX_NAME)
    _note(document.get('note', ''), 'Note', MAX_NOTE_BYTES)
    tagline = _string(document.get('tagline', ''), 'Tagline')
    if len(tagline) > OLD_MAX_TAGLINE or _control(tagline):
        raise ProfileError(f'Tagline must be at most {OLD_MAX_TAGLINE} printable characters')
    if document.get('accent') is not None:
        _accent(document['accent'])
    for alias in _list(document.get('aliases', []), 'Aliases', MAX_ALIASES):
        _old_label(alias, 'Each alias', OLD_MAX_ALIAS)
    for link in _list(document.get('links', []), 'Links', MAX_LINKS):
        if not isinstance(link, dict):
            raise ProfileError('Each link must be an object')
        _old_label(link.get('kind'), 'Each link kind', MAX_LINK_KIND)
        _https(link.get('url'), 'Each link')
    if document.get('page_url') is not None:
        _https(document['page_url'], 'The profile page')
    preferred = document.get('preferred_logo')
    secondary = _list(document.get('secondary_logos', []), 'Logos', MAX_LOGOS)
    if preferred is None and secondary:
        raise ProfileError('Choose a preferred logo before adding alternates')
    references = [_old_logo(logo) for logo in ([preferred] if preferred is not None else []) + secondary]
    if len(references) > MAX_LOGOS:
        raise ProfileError('A profile may contain at most eight logos')
    bundle = _string(document.get('bundle', ''), 'Bundle')
    if bundle:
        _old_bundle(bundle, dj_id)
    elif references:
        bundle = _old_member(references[0]['path'], 'assets', dj_id, LOGO_PLACEMENT)
    seen = set()
    for reference in references:
        if _old_member(reference['path'], 'assets', dj_id, LOGO_PLACEMENT) != bundle:
            raise ProfileError("Every logo must belong to the profile's branding bundle")
        if reference['path'] in seen:
            raise ProfileError('A logo cannot appear twice')
        seen.add(reference['path'])
    if sum(reference['bytes'] for reference in references) > MAX_TOTAL_LOGO_BYTES:
        raise ProfileError('Profile logos exceed 24 MiB total')
    tile = document.get('xz_tile')
    if tile is not None:
        if not isinstance(tile, dict):
            raise ProfileError('The XZ tile must be an object')
        if not bundle:
            raise ProfileError('The XZ tile needs a branding bundle')
        if _old_member(_string(tile.get('path'), 'Tile path'), 'renditions', dj_id, TILE_PLACEMENT) != bundle:
            raise ProfileError("The XZ tile must belong to the profile's branding bundle")
        digest = tile.get('sha256')
        if (tile.get('width') != TILE_SIDE or tile.get('height') != TILE_SIDE or tile.get('format') != TILE_FORMAT
                or tile.get('bytes') != TILE_BYTES or not isinstance(digest, str) or not _DIGEST.fullmatch(digest)):
            raise ProfileError(f'The XZ tile must be {TILE_SIDE}x{TILE_SIDE} {TILE_FORMAT}, {TILE_BYTES} bytes, with a SHA-256 digest')
    return document


def validate_old_index(index):
    if not isinstance(index, dict):
        raise ProfileError('The profile index must be a JSON object')
    if index.get('schema') != OLD_INDEX_SCHEMA or index.get('version') != 1 or type(index.get('version')) is not int:
        raise ProfileError('Unsupported DJ profile index schema or version')
    _id(index.get('revision'), 'Index revision')
    entries = index.get('profiles')
    if not isinstance(entries, list) or not 1 <= len(entries) <= MAX_PROFILES:
        raise ProfileError(f'The profile index must list 1 to {MAX_PROFILES} profiles')
    seen = set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise ProfileError('Each index entry must be an object')
        dj_id = _id(entry.get('dj_id'))
        _old_label(entry.get('dj_name'), 'DJ name', MAX_NAME)
        if dj_id in seen:
            raise ProfileError('The profile index lists a DJ ID twice')
        seen.add(dj_id)
    if index.get('active') not in seen:
        raise ProfileError('The active profile is not listed in the profile index')
    return index


def parse_json(data, label='Profile'):
    if len(data) > MAX_JSON:
        raise ProfileError(f'{label} JSON exceeds 64 KiB')
    try:
        return json.loads(data.decode('utf-8'))
    except (UnicodeDecodeError, ValueError) as exc:
        raise ProfileError(f'{label} JSON is not valid: {exc}') from None


def from_old(document):
    """An old-store document in the 1.0 model (contract 0.2), and where each of its files sits on the stick now.
    File names are the ones a migration gives them in a fresh folder."""
    files, logos = {}, []
    used = {_fold(name) for name in LOGO_NAMES + (PROFILE_FILE, PREVIOUS_FILE, TILE_FILE)}
    for i, reference in enumerate(_old_logos(document)):
        base = reference['path'].rsplit('/', 1)[1]
        if i == 0:
            name = 'logo.jpg' if _fold(base).endswith(('.jpg', '.jpeg')) else 'logo.png'
        else:
            name = _unique_file(base, used)
            used.add(_fold(name))
        files[name] = reference['path']
        entry = {'file': name, 'bytes': reference['bytes'], 'sha256': reference['sha256'].lower(),
                 'role': 'primary' if i == 0 else 'alternate', 'background': 'any'}
        if reference.get('label'):
            entry['label'] = _cut(reference['label'], MAX_NAME)
        if reference.get('note'):
            entry['note'] = reference['note']
        entry.update({key: value for key, value in reference.items() if key not in ('path', 'bytes', 'sha256', 'label', 'note')})
        logos.append(entry)
    mapped = {'format': FORMAT, 'id': document['dj_id'], 'revision': document['revision'],
              'name': _cut(document['dj_name'], MAX_NAME)}
    if document.get('tagline'):
        mapped['tagline'] = _cut(document['tagline'], MAX_TAGLINE)
    if document.get('accent'):
        mapped['accent'] = document['accent']
    if document.get('aliases'):
        mapped['aliases'] = [_cut(alias, MAX_NAME) for alias in document['aliases']]
    links = [copy.deepcopy(link) for link in document.get('links', [])]
    if document.get('page_url'):
        links = [{'kind': 'page', 'url': document['page_url']}] + links
    if links:
        mapped['links'] = links[:MAX_LINKS]
    if document.get('note'):
        mapped['note'] = document['note']
    if logos:
        mapped['logos'] = logos
    tile = document.get('xz_tile')
    if tile:
        files[TILE_FILE] = tile['path']
        mapped['renditions'] = [{'file': TILE_FILE, 'bytes': tile['bytes'], 'sha256': tile['sha256'].lower(),
                                 'width': TILE_SIDE, 'height': TILE_SIDE, 'format': TILE_FORMAT, 'for': TILE_FOR}]
    unknown = {key: copy.deepcopy(value) for key, value in document.items() if key not in OLD_KNOWN}
    if unknown:
        mapped['ext'] = {VENDOR: unknown}
    if 'profile_track' in document:
        mapped['track'] = {'marker': track_marker(mapped)}
    return mapped, files


# --- stick files ---

def _volume(volume):
    root = _safe_path(volume)
    if not root.is_dir():
        raise ProfileError('Choose the USB drive or folder that holds the DJ profiles')
    return root


def _confined(root, relative):
    return _safe_path(root.joinpath(*_old_parts(relative)))


def _read_file(path, max_bytes):
    path = _regular(path)
    size = path.stat().st_size
    if size > max_bytes:
        raise ProfileError(f'{path.name} is larger than {max_bytes} bytes')
    data = path.read_bytes()
    if len(data) != size:
        raise ProfileError(f'{path.name} changed while it was read')
    return data


def _read(root, relative, max_bytes):
    return _read_file(_confined(root, relative), max_bytes)


def _exists(root, relative):
    try:
        return _confined(root, relative).is_file()
    except (OSError, ValueError):
        return False


def _visible(folder):
    """The entries a reader looks at. Dot names and symbolic links are skipped (spec section 3)."""
    try:
        return [entry for entry in os.scandir(folder) if not entry.name.startswith('.') and not entry.is_symlink()]
    except OSError:
        return []


def _fixed(folder, name):
    """The file in folder whose name equals name under ASCII case folding, or None."""
    return next((Path(entry.path) for entry in _visible(folder) if entry.is_file() and _fold(entry.name) == name), None)


def _write_new(path, data):
    with _safe_path(path).open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def _flush_folder(folder):
    if os.name == 'nt':
        return
    try:
        handle = os.open(folder, os.O_RDONLY)
    except OSError:
        return
    try:
        os.fsync(handle)
    except OSError:
        pass
    finally:
        os.close(handle)


def _replace(destination, data):
    """A dot-named temporary file in the same folder, flushed, renamed over the destination (spec section 7)."""
    temporary = destination.parent / f'.profile-{secrets.token_hex(8)}'
    _write_new(temporary, data)
    try:
        os.replace(temporary, destination)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    _flush_folder(destination.parent)
    if destination.read_bytes() != data:
        raise ProfileError(f'{destination.name} did not read back as written. Check the USB and try again.')


def _write_asset(folder, name, data):
    """Writes folder/name unless it already holds exactly data."""
    path = _safe_path(folder / name)
    if path.is_file() and not path.is_symlink() and path.stat().st_size == len(data) and path.read_bytes() == data:
        return
    _replace(path, data)


def _stick_revision():
    return f'{datetime.now(timezone.utc):%Y-%m-%dT%H-%M-%SZ}-{secrets.token_hex(2)}'


# --- images ---

SOF_BASELINE = (0xC0, 0xC1)
SOF_OTHER = (0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF)


def image_info(data):
    """(kind, width, height, plain) from the header, or None. plain means baseline JPEG or 8-bit non-interlaced PNG."""
    if data[:8] == PNG_MAGIC and len(data) >= 29 and data[12:16] == b'IHDR':
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


def _stick_image(data):
    """The image's kind when it passes spec section 5.2 (8 bytes to 8 MiB, 4096 a side, 4 megapixels), else None."""
    if not MIN_LOGO_BYTES <= len(data) <= MAX_LOGO_BYTES:
        return None
    info = image_info(data)
    if not info:
        return None
    kind, width, height, _ = info
    return kind if 0 < width <= MAX_SIDE and 0 < height <= MAX_SIDE and width * height <= MAX_PIXELS else None


def _image_format(data):
    if data.startswith(PNG_MAGIC):
        return 'png'
    if data.startswith(JPEG_MAGIC):
        return 'jpeg'
    raise ProfileError('Choose a PNG or JPEG image. This file is neither.')


def _decode(data):
    from PIL import Image
    kind = _image_format(data)
    try:
        image = Image.open(io.BytesIO(data))
        width, height = image.size
        if not (0 < width <= MAX_INPUT_SIDE and 0 < height <= MAX_INPUT_SIDE and width * height <= MAX_INPUT_PIXELS):
            raise ProfileError('This logo is too large. Use at most 8192 pixels per side and 16 megapixels.')
        image.load()
    except ProfileError:
        raise
    except Exception as exc:
        raise ProfileError(f'This logo could not be opened as a {kind.upper()} image ({exc}).') from None
    return image, kind


def _check_logo_size(size):
    if size < MIN_LOGO_BYTES:
        raise ProfileError('This logo file is empty or damaged.')
    if size > MAX_LOGO_BYTES:
        raise ProfileError('This logo is larger than 8 MB. Choose a smaller file.')


def fit_stick_limits(data, image, kind):
    """(data, image, kind) within spec 5.2: a logo over 4096 px a side or 4 megapixels is scaled down by area
    average and written as PNG (contract 0.3 step 2). Others come back unchanged."""
    from PIL import Image
    width, height = image.size
    if width <= MAX_SIDE and height <= MAX_SIDE and width * height <= MAX_PIXELS:
        return data, image, kind
    scale = min(MAX_SIDE / width, MAX_SIDE / height, math.sqrt(MAX_PIXELS / (width * height)))
    size = (max(1, int(width * scale)), max(1, int(height * scale)))
    smaller = image.convert('RGBA' if 'A' in image.getbands() or image.mode == 'P' else 'RGB').resize(size, Image.Resampling.BOX)
    stream = io.BytesIO()
    smaller.save(stream, 'PNG', optimize=True)
    if stream.tell() > MAX_LOGO_BYTES:
        raise ProfileError('This logo is still larger than 8 MB after scaling it to 4096 pixels. Use a simpler or smaller image.')
    return stream.getvalue(), smaller, 'png'


def _png_url(image, side):
    from PIL import Image
    copy_ = image.convert('RGBA')
    copy_.thumbnail((side, side), Image.Resampling.LANCZOS)
    stream = io.BytesIO()
    copy_.quantize(256, method=Image.Quantize.FASTOCTREE).save(stream, 'PNG', optimize=True)
    return 'data:image/png;base64,' + base64.b64encode(stream.getvalue()).decode('ascii')


def _fill_previews(items):
    """Sets item['preview'] for every (item, image), largest size that fits the shared budget.
    Items come in priority order; at the smallest size the lowest-priority ones get None."""
    if not items:
        return
    for side in PREVIEW_SIDES:
        urls = [_png_url(image, side) for _, image in items]
        if sum(map(len, urls)) <= PREVIEW_BUDGET or side == PREVIEW_SIDES[-1]:
            break
    spent = 0
    for (item, _), url in zip(items, urls):
        spent += len(url)
        item['preview'] = url if spent <= PREVIEW_BUDGET else None


def render_tile(image):
    """The primary logo fitted inside 160x160 over black, RGB565 little-endian, row-major."""
    from PIL import Image
    rgba = image.convert('RGBA')
    over_black = Image.new('RGB', rgba.size, (0, 0, 0))
    over_black.paste(rgba, mask=rgba.getchannel('A'))
    width, height = rgba.size

    def fit(along, across):
        return min(max((along * TILE_SIDE + across // 2) // across, 1), TILE_SIDE)

    size = (TILE_SIDE, fit(height, width)) if width >= height else (fit(width, height), TILE_SIDE)
    tile = Image.new('RGB', (TILE_SIDE, TILE_SIDE), (0, 0, 0))
    tile.paste(over_black.resize(size, Image.Resampling.BICUBIC), ((TILE_SIDE - size[0]) // 2, (TILE_SIDE - size[1]) // 2))
    return b''.join(struct.pack('<H', (r >> 3) << 11 | (g >> 2) << 5 | b >> 3) for r, g, b in struct.iter_unpack('BBB', tile.tobytes()))


def tile_image(data):
    from PIL import Image
    if len(data) != TILE_BYTES:
        raise ProfileError('The XZ tile has the wrong size')
    pixels = bytearray()
    for (value,) in struct.iter_unpack('<H', data):
        r, g, b = value >> 11, value >> 5 & 63, value & 31
        pixels += bytes((r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2))
    return Image.frombytes('RGB', (TILE_SIDE, TILE_SIDE), bytes(pixels))


def cover_jpeg(data, background='any'):
    """The profile track's front cover (spec section 9). The logo is fitted inside a square of 240 to 800 pixels,
    padded black (dark, any) or white (light), as a baseline JPEG."""
    from PIL import Image
    image, _ = _decode(data)
    rgba = image.convert('RGBA')
    width, height = rgba.size
    side = min(COVER_MAX, max(COVER_MIN, width, height))
    scale = side / max(width, height)
    size = (min(side, max(1, round(width * scale))), min(side, max(1, round(height * scale))))
    fitted = rgba.resize(size, Image.Resampling.LANCZOS)
    canvas = Image.new('RGB', (side, side), (255, 255, 255) if background == 'light' else (0, 0, 0))
    canvas.paste(fitted, ((side - size[0]) // 2, (side - size[1]) // 2), fitted)
    stream = io.BytesIO()
    canvas.save(stream, 'JPEG', quality=90, progressive=False)
    cover = stream.getvalue()
    info = image_info(cover)
    if not info or info[0] != 'jpeg' or not info[3] or info[1] != info[2] or not COVER_MIN <= info[1] <= COVER_MAX:
        raise ProfileError('The track cover did not come out as a square baseline JPEG')
    return cover


def read_logo(path):
    """A logo the DJ picked on their computer: its size, format and a preview for the editor."""
    path = _regular(path)
    size = path.stat().st_size
    _check_logo_size(size)
    data = path.read_bytes()
    image, kind = _decode(data)
    return {'path': str(path), 'width': image.width, 'height': image.height, 'bytes': len(data),
            'format': kind, 'preview': _png_url(image, PREVIEW_SIDES[0])}


# --- stick colour (spec section 11) ---

def _live_property_row(page, page_size):
    """Page offset of the djdbProperty row the row index marks present, or None. rekordbox leaves
    superseded rows in the heap and only clears their present bit, so the first row can be stale."""
    count = struct.unpack_from('<I', page, 0x18)[0] & 0x1fff
    live = None
    for group in range((count + 15) // 16):
        end = page_size - group * 36
        if end - 36 < 0x28:
            break
        present = struct.unpack_from('<H', page, end - 4)[0]
        for index in range(min(16, count - group * 16)):
            if present >> index & 1:
                start = 0x28 + struct.unpack_from('<H', page, end - 6 - 2 * index)[0]
                if start + 12 <= page_size and page[start:start + 2] == b'\x80\x02':
                    live = start
    return live


def stick_colour(root):
    """The rekordbox Background Color saved on the stick as #rrggbb, or None."""
    try:
        path = _regular(root / 'PIONEER' / 'rekordbox' / 'export.pdb')
        with path.open('rb') as stream:
            head = stream.read(0x1c + 16 * 64)
            if len(head) < 0x1c + 16 * 64:
                return None
            zero, page_size, tables = struct.unpack_from('<III', head, 0)
            if zero or not 0x40 <= page_size <= 65536 or not 1 <= tables <= 64:
                return None
            for i in range(tables):
                kind, _, first, last = struct.unpack_from('<IIII', head, 0x1c + 16 * i)
                if kind == 19:
                    break
            else:
                return None
            at = first
            for _ in range(64):
                stream.seek(at * page_size)
                page = stream.read(page_size)
                if len(page) != page_size or struct.unpack_from('<I', page, 4)[0] != at or struct.unpack_from('<I', page, 8)[0] != 19:
                    return None
                row = None if page[0x1b] & 0x40 else _live_property_row(page, page_size)
                if row is not None:
                    return STICK_COLOURS.get(page[row + 9])
                if at == last:
                    return None
                at = struct.unpack_from('<I', page, 12)[0]
    except (OSError, ValueError):
        return None
    return None


# --- reading (spec sections 3 to 6, contract 0.1) ---

@dataclass
class Folder:
    top: str
    name: str
    path: Path


@dataclass(eq=False)
class Found:
    """One profile as read. source is '1.0' (a folder with a usable document), 'folder' (level 0) or 'old'."""
    source: str
    id: str | None
    name: str
    document: dict | None = None
    folder: Folder | None = None
    logo: str | None = None
    raw: bytes | None = None
    old: dict | None = None
    old_files: dict = field(default_factory=dict)

    @property
    def ref(self):
        return self.id if self.id else self.folder.name

    def stick_path(self, file):
        if self.source == 'old':
            return self.old_files[file]
        return f'{self.folder.top}/{self.folder.name}/{file}'


@dataclass
class Stick:
    folders: list
    stick_file: dict | None
    stick_raw: bytes | None
    stick_path: Path | None
    ordered: list                # every 1.0 and level 0 profile, in profile order, duplicate ids dropped
    profiles: list               # what the stick shows: at most 16, 1.0 first, then the old store
    olds: list                   # every old-store profile no profile folder has taken over yet
    active: Found | None
    stick_active: Found | None   # what DJPROFILE.json's active resolves to
    old_index: dict | None
    old_problems: list           # old-store profiles that could not be read
    problems: list

    @property
    def revision(self):
        parts = [(self.stick_file or {}).get('revision'), (self.old_index or {}).get('revision')]
        return '|'.join(part for part in parts if part) or None


def _tops(root):
    found = {_fold(entry.name): entry.name for entry in _visible(root) if entry.is_dir() and _fold(entry.name) in TOP_NAMES}
    return [found[name] for name in TOP_NAMES if name in found]


def _candidate_folders(root, tops):
    folders = {}
    for top in tops:
        for entry in sorted(_visible(root / top), key=lambda e: _sort_key(e.name)):
            name = entry.name
            if not entry.is_dir() or _fold(name) == _fold(OLD_HOME) or not _spec_name(name):
                continue
            folders.setdefault(name_key(name), Folder(top, name, Path(entry.path)))
    return list(folders.values())


def _entry_data(folder, entry, limit):
    """The bytes of a document entry's file, checked against its size and SHA-256."""
    path = folder
    for part in entry['file'].split('/'):
        path = path / part
        if path.is_symlink():
            raise ProfileError(f'{entry["file"]} is a link; readers do not follow links')
    path = _regular(path)
    if path.stat().st_size != entry['bytes'] or entry['bytes'] > limit:
        raise ProfileError(f'{entry["file"]} is not the size its profile says')
    data = _read_file(path, limit)
    if _sha(data) != entry['sha256'].lower():
        raise ProfileError(f'{entry["file"]} does not match its recorded SHA-256')
    return data


def _level0_logo(folder):
    """The level 0 logo (spec section 4). Candidates are logo.png, logo.jpg, logo.jpeg, then the rest by name key."""
    images = [entry for entry in _visible(folder) if entry.is_file() and _fold(os.path.splitext(entry.name)[1]) in IMAGE_SUFFIXES]
    images.sort(key=lambda e: (LOGO_NAMES.index(_fold(e.name)) if _fold(e.name) in LOGO_NAMES else 3, _sort_key(e.name)))
    for entry in images:
        try:
            if entry.stat().st_size <= MAX_LOGO_BYTES and _stick_image(_read_file(entry.path, MAX_LOGO_BYTES)):
                return entry.name
        except (OSError, ValueError):
            continue
    return None


def _primary(document):
    return next((entry for entry in document.get('logos', []) if entry.get('role') == 'primary'), None)


def _document_logo(folder, document):
    """The file name a usable document's profile shows, or ProfileError."""
    if 'logos' not in document:
        logo = _level0_logo(folder)
        _need(logo is not None, 'the profile has no usable logo')
        return logo
    entry = _primary(document)
    _need(_stick_image(_entry_data(folder, entry, MAX_LOGO_BYTES)) is not None, f'{entry["file"]} is not a usable PNG or JPEG')
    return entry['file']


def _read_folder(folder):
    """(profile or None, problem or None) for one candidate folder."""
    for name in (PROFILE_FILE, PREVIOUS_FILE):
        path = _fixed(folder.path, name)
        if path is None:
            continue
        try:
            raw = _read_file(path, MAX_JSON)
            document = validate_profile(parse_strict(raw, MAX_JSON, name))
            logo = _document_logo(folder.path, document)
        except (OSError, ValueError):
            continue
        return Found('1.0', document['id'], document['name'], document, folder, logo, raw if name == PROFILE_FILE else None), None
    logo = _level0_logo(folder.path)
    if logo:
        return Found('folder', None, folder.name, folder=folder, logo=logo), None
    if any(entry.is_file() and _fold(os.path.splitext(entry.name)[1]) in IMAGE_SUFFIXES + ('.json',) for entry in _visible(folder.path)):
        return None, {'dj_id': FOLDER_ID + _sha(folder.name.encode())[:16], 'dj_name': folder.name,
                      'error': f'{folder.top}/{folder.name} has no logo a player can show (PNG or JPEG, up to 4096 pixels a side and 4 megapixels).'}
    return None, None


def _load_stick_file(root, tops):
    if not tops:
        return None, None, None
    path = _fixed(root / tops[0], _fold(STICK_FILE))
    if path is None:
        return None, None, None
    try:
        raw = _read_file(path, MAX_STICK_JSON)
        return validate_stick_file(parse_strict(raw, MAX_STICK_JSON, STICK_FILE)), raw, path
    except (OSError, ValueError):
        return None, None, path


def _resolve(reference, pool):
    """The profile a reference names (spec section 6). Ids match exactly first, then folder name keys."""
    if not isinstance(reference, str):
        return None
    by_id = next((p for p in pool if p.id == reference), None)
    return by_id or next((p for p in pool if p.folder and name_key(p.folder.name) == name_key(reference)), None)


def _profile_order(candidates, stick_file):
    ordered, seen = [], set()
    for entry in (stick_file or {}).get('profiles', []):
        if 'id' in entry:
            profile = next((p for p in candidates if p.id == entry['id']), None)
        else:
            profile = next((p for p in candidates if name_key(p.folder.name) == name_key(entry['folder'])), None)
        if profile is not None and id(profile) not in seen:
            ordered.append(profile)
            seen.add(id(profile))
    ordered += [p for p in candidates if id(p) not in seen]
    unique, ids = [], set()
    for profile in ordered:
        if profile.id is not None and profile.id in ids:
            continue
        if profile.id is not None:
            ids.add(profile.id)
        unique.append(profile)
    return unique


def _load_old_index(root):
    try:
        return validate_old_index(parse_json(_read(root, OLD_INDEX_PATH, MAX_JSON), 'Profile index'))
    except (OSError, ValueError):
        return None


def _old_profile_path(dj_id):
    return f'DJPROFILE/VJTOOLS/profiles/{dj_id}/profile.json'


def _read_old(root, taken_ids):
    """(old profiles not yet in a profile folder, read problems, the old index). VJTOOLS comes in index order, then the
    Branding card. A profile whose id a profile folder already holds is skipped (contract 0.1 step 2)."""
    index = _load_old_index(root)
    sources = [(_old_profile_path(entry['dj_id']), entry['dj_id'], entry['dj_name']) for entry in index['profiles']] if index else []
    if _exists(root, OLD_CARD_PATH):
        sources.append((OLD_CARD_PATH, None, None))
    found, problems, seen = [], [], set(taken_ids)
    for relative, dj_id, dj_name in sources:
        if dj_id in seen:
            continue
        try:
            document = validate_old_document(parse_json(_read(root, relative, MAX_JSON)))
            if dj_id and document['dj_id'] != dj_id:
                raise ProfileError('profile.json names a different DJ ID than the index')
            mapped, files = from_old(document)
            validate_profile(mapped)
        except (OSError, ValueError) as exc:
            problems.append({'dj_id': dj_id, 'dj_name': dj_name, 'error': str(exc) if dj_id else f'{relative}: {exc}'})
            continue
        if document['dj_id'] in seen:
            continue
        seen.add(document['dj_id'])
        found.append(Found('old', document['dj_id'], mapped['name'], mapped, old=document, old_files=files))
    return found, problems, index


def _scan(root):
    tops = _tops(root)
    folders = _candidate_folders(root, tops)
    stick_file, stick_raw, stick_path = _load_stick_file(root, tops)
    candidates, problems = [], []
    for folder in folders:
        profile, problem = _read_folder(folder)
        if profile:
            candidates.append(profile)
        if problem:
            problems.append(problem)
    ordered = _profile_order(candidates, stick_file)
    olds, old_problems, index = _read_old(root, {p.id for p in ordered if p.id})
    old_names = {name_key(p.name) for p in olds}
    shown = [p for p in ordered if not (p.source == 'folder' and name_key(p.folder.name) in old_names)][:MAX_PROFILES]
    profiles = (shown + olds)[:MAX_PROFILES]
    stick_active = _resolve((stick_file or {}).get('active'), shown)
    by_old_index = next((p for p in profiles if index and p.id == index['active']), None)
    active = stick_active or by_old_index or (profiles[0] if profiles else None)
    return Stick(folders, stick_file, stick_raw, stick_path, ordered, profiles, olds, active, stick_active, index,
                 old_problems, problems + old_problems)


# --- views for the UI ---

def _folder_id(name):
    return FOLDER_ID + _sha(name.encode())[:16]


def _view_id(found):
    return found.id or _folder_id(found.folder.name)


def _found_path(root, found, file):
    if found.source == 'old':
        return _confined(root, found.old_files[file])
    return found.folder.path.joinpath(*file.split('/'))


def _logo_view(root, found, entry, data, previews):
    image, kind = _decode(data)
    return _with_preview({'path': str(_found_path(root, found, entry['file'])), 'stick_path': found.stick_path(entry['file']),
                          'label': entry.get('label', ''),
                          'note': entry.get('note', ''), 'width': image.width, 'height': image.height, 'bytes': len(data),
                          'sha256': _sha(data), 'format': kind, 'background': entry.get('background', 'any'), 'preview': None},
                         image, previews)


def _with_preview(view, image, previews):
    previews.append((view, image))
    return view


def _found_bytes(root, found, entry, limit):
    if found.source == 'old':
        data = _read(root, found.old_files[entry['file']], limit)
        if len(data) != entry['bytes'] or _sha(data) != entry['sha256']:
            raise ProfileError(f'{found.old_files[entry["file"]]} does not match its recorded size and SHA-256')
        return data
    return _entry_data(found.folder.path, entry, limit)


def _track_view(root, found):
    if found.source == 'old':
        record = found.old.get('profile_track')
        try:
            path = _regular(_confined(root, record['path']))
            return {'path': str(path), 'stick_path': record['path'], 'bytes': path.stat().st_size}
        except (KeyError, TypeError, OSError, ValueError):
            return None
    track = found.document.get('track') or {}
    if 'file' not in track:
        return None
    try:
        path = _regular(found.folder.path.joinpath(*track['file'].split('/')))
        return {'path': str(path), 'stick_path': found.stick_path(track['file']), 'bytes': path.stat().st_size}
    except (OSError, ValueError):
        return None


def _our_tile(document):
    return next((r for r in document.get('renditions', []) if r.get('for') == TILE_FOR and r.get('format') == TILE_FORMAT
                 and r.get('width') == TILE_SIDE and r.get('height') == TILE_SIDE), None)


def _profile_view(root, found, colour, previews):
    blank = {'dj_id': _view_id(found), 'source': found.source, 'editable': found.source != 'folder', 'active': False,
             'dj_name': found.name, 'folder': found.folder.name if found.folder else None, 'tagline': '', 'accent': colour,
             'aliases': [], 'links': [], 'page_url': None, 'note': '', 'logos': [], 'tile': None, 'track': None}
    if found.logo and (found.source == 'folder' or 'logos' not in found.document):
        data = _read_file(found.folder.path / found.logo, MAX_LOGO_BYTES)
        logo = _logo_view(root, found, {'file': found.logo}, data, previews['preferred'])
        if found.source == 'folder':
            return {**blank, 'logos': [logo]}
        logos = [logo]
    elif not found.document.get('logos'):
        logos = []
    else:
        primary = _primary(found.document)
        entries = [primary] + [entry for entry in found.document['logos'] if entry is not primary]
        logos = [_logo_view(root, found, primary, _found_bytes(root, found, primary, MAX_LOGO_BYTES), previews['preferred'])]
        for entry in entries[1:]:
            try:
                data = _found_bytes(root, found, entry, MAX_LOGO_BYTES)
            except (OSError, ValueError):
                continue
            logos.append(_logo_view(root, found, entry, data, previews['secondary']))
    document, tile = found.document, None
    entry = _our_tile(document)
    if entry:
        try:
            data = _found_bytes(root, found, entry, TILE_BYTES)
            tile = _with_preview({'path': str(_found_path(root, found, entry['file'])), 'stick_path': found.stick_path(entry['file']),
                                  'preview': None},
                                 tile_image(data), previews['tile'])
        except (OSError, ValueError):
            tile = None
    return {**blank, 'tagline': document.get('tagline', ''), 'accent': document.get('accent', colour),
            'aliases': list(document.get('aliases', [])), 'links': [dict(link) for link in document.get('links', [])],
            'note': document.get('note', ''), 'logos': logos, 'tile': tile, 'track': _track_view(root, found)}


def _summary(stick):
    broken = [problem for problem in stick.old_problems if problem['dj_id']]
    named = next((p['dj_name'] for p in broken if stick.old_index and p['dj_id'] == stick.old_index['active']), None)
    return {'count': len(stick.profiles) + len(broken), 'active_name': stick.active.name if stick.active else named}


def _old_home(root):
    top = next((name for name in _tops(root) if _fold(name) == 'djprofile'), None)
    if top is None:
        return None
    return next((Path(entry.path) for entry in _visible(root / top) if entry.is_dir() and _fold(entry.name) == _fold(OLD_HOME)), None)


def read(volume):
    """Every DJ profile on the stick, in profile order, with previews for the editor."""
    root = _volume(volume)
    stick = _scan(root)
    colour = stick_colour(root)
    previews = {'preferred': [], 'tile': [], 'secondary': []}
    profiles, problems = [], list(stick.problems)
    for found in stick.profiles:
        try:
            view = _profile_view(root, found, colour, previews)
        except (OSError, ValueError) as exc:
            problems.append({'dj_id': found.id, 'dj_name': found.name, 'error': str(exc)})
            continue
        view['active'] = found is stick.active
        profiles.append(view)
    _fill_previews(previews['preferred'] + previews['tile'] + previews['secondary'])
    tops = _tops(root)
    return {'format': 'xz-dj-profiles/1', 'public_folder': tops[0] if tops else None, 'revision': stick.revision,
            'active': _view_id(stick.active) if stick.active else None, 'stick_colour': colour,
            'old_store': bool(stick.olds) or _old_home(root) is not None, 'profiles': profiles, 'problems': problems,
            'summary': _summary(stick)}


def summary(volume):
    """Profile count and the active name, for the USB overview."""
    return _summary(_scan(_volume(volume)))


# --- writing (spec section 7) ---

def _writable_top(root):
    """DJPROFILE at the root with exactly that name, made or renamed if needed (spec section 3)."""
    for entry in _visible(root):
        if _fold(entry.name) == 'djprofile':
            _need(entry.is_dir(), 'DJPROFILE on this USB is a file, not a folder. Rename it and try again.')
            if entry.name != TOP:
                temporary = root / f'.{TOP}-{secrets.token_hex(4)}'
                os.rename(entry.path, temporary)
                os.rename(temporary, root / TOP)
            return root / TOP
    path = _safe_path(root / TOP)
    path.mkdir()
    return path


@contextmanager
def _locked(root):
    top = _writable_top(root)
    with exclusive_lock(top / LOCK, 'Another XZ Mods window is writing to this USB'):
        yield top


def _check_revision(stick, expected):
    if stick.revision != expected:
        raise ProfileError(CONFLICT)


def _document_files(document):
    if not document:
        return {}
    entries = document.get('logos', []) + document.get('renditions', []) + ([document['track']] if 'file' in document.get('track', {}) else [])
    return {_fold(entry['file']): entry['file'] for entry in entries if isinstance(entry, dict) and _safe_file(entry.get('file'))}


def _publish_document(folder, document, read_raw):
    """profile.json replaced per spec section 7 steps 2 and 3. The live profile.json becomes profile.previous.json
    only when it is the document that was read, so an unreadable one never replaces a good previous copy."""
    data = pretty(validate_profile(document))
    _need(len(data) <= MAX_JSON, 'This profile is too large for profile.json (64 KiB). Shorten the note or links.')
    target = _fixed(folder, PROFILE_FILE) or folder / PROFILE_FILE
    if target.is_file():
        live = _read_file(target, MAX_JSON + 1)
        if live == data:
            return False
        if read_raw is not None:
            _need(live == read_raw, CONFLICT)
            _replace(_fixed(folder, PREVIOUS_FILE) or folder / PREVIOUS_FILE, live)
    _replace(target, data)
    return True


def _delete_unlisted(folder, before, after):
    """Spec section 7 step 5: delete the files the previous document listed that the new one does not.
    A logo-original file is the DJ's own and is never deleted."""
    listed = _document_files(after)
    for key, name in _document_files(before).items():
        if key in listed or _fold(name.rsplit('/', 1)[-1]).startswith(ORIGINAL_STEM):
            continue
        path = folder.joinpath(*name.split('/'))
        try:
            if path.is_file() and not path.is_symlink():
                path.unlink()
        except OSError:
            pass


def _write_stick_file(stick, top, entries, active):
    """DJPROFILE.json with profiles = entries [(folder, id or None)] and active, keeping unknown members.
    Written last (spec section 7 step 4), and only when something other than revision changed."""
    before = copy.deepcopy(stick.stick_file) if stick.stick_file else None
    document = copy.deepcopy(before) if before else {'format': STICK_FORMAT, 'revision': '', 'active': None}
    old_entries = list((before or {}).get('profiles', []))
    profiles = []
    for name, dj_id in entries[:MAX_PROFILES]:
        match = next((e for e in old_entries if (dj_id and e.get('id') == dj_id) or name_key(e['folder']) == name_key(name)), None)
        entry = copy.deepcopy(match) if match else {}
        if match:
            old_entries.remove(match)
        entry['folder'] = name
        if dj_id:
            entry['id'] = dj_id
        else:
            entry.pop('id', None)
        profiles.append(entry)
    document['profiles'] = profiles
    if active:
        document['active'] = active
    else:
        document.pop('active', None)
    if before is not None and {k: v for k, v in document.items() if k != 'revision'} == {k: v for k, v in before.items() if k != 'revision'}:
        return False
    document['revision'] = _stick_revision()
    data = pretty(validate_stick_file(document))
    _need(len(data) <= MAX_STICK_JSON, 'DJPROFILE.json would be larger than 16 KiB')
    target = stick.stick_path if stick.stick_path and stick.stick_path.parent == top else top / STICK_FILE
    _replace(target, data)
    return True


def _folder_entries(profiles):
    return [(p.folder.name, p.id) for p in profiles if p.source != 'old']


def _adopt_hand_made(folder, primary_file, primary_sha):
    """In a folder the DJ made by hand, a top-level logo.png / logo.jpg / logo.jpeg that differs from the new
    primary logo is renamed logo-original.<ext> (contract 0.3 step 2). Returns [(name, bytes, sha256)]."""
    originals = []
    used = {_fold(entry.name) for entry in _visible(folder)} | {_fold(primary_file)}
    for entry in _visible(folder):
        if not entry.is_file() or _fold(entry.name) not in LOGO_NAMES:
            continue
        data = _read_file(entry.path, MAX_LOGO_BYTES + 1)
        if _sha(data) == primary_sha:
            continue
        name = _unique_file(ORIGINAL_STEM + os.path.splitext(entry.name)[1].lower(), used)
        used.add(_fold(name))
        os.rename(entry.path, folder / name)
        originals.append((name, len(data), _sha(data)))
    return originals


def _original_entry(name, size, digest):
    return {'file': name, 'bytes': size, 'sha256': digest, 'role': 'alternate', 'background': 'any', 'label': 'Original logo'}


def _free_name(preferred, data, folder, used):
    """preferred, unless the folder already holds other content under it or it is used by this document; then a
    content-named variant, so a changed asset never takes a name a reader may still pair with the old document."""
    stem, ext = os.path.splitext(preferred)
    for candidate in (preferred, f'{stem}-{_sha(data)[:8]}{ext}'):
        path = folder / candidate
        if _fold(candidate) in used:
            continue
        if not path.exists() or (path.is_file() and path.stat().st_size == len(data) and path.read_bytes() == data):
            return candidate
    return _unique_file(f'{stem}-{_sha(data)[:8]}{ext}', used | {_fold(e.name) for e in _visible(folder)})


def _stripped(document):
    return {key: value for key, value in (document or {}).items() if key != 'revision'}


# --- moving the old store to 1.0 (contract 0.3) ---

def _old_assets(root, found):
    """{file: bytes} for an old profile, every file checked against its recorded size and SHA-256 first."""
    assets = {}
    for entry in found.document.get('logos', []) + found.document.get('renditions', []):
        path = found.old_files[entry['file']]
        data = _read(root, path, MAX_LOGO_BYTES)
        _need(len(data) == entry['bytes'] and _sha(data) == entry['sha256'], f'{path} does not match its recorded size and SHA-256')
        assets[entry['file']] = data
    _need(found.document.get('logos'), f'{found.name} has no logo. Edit it and add one to move it to the new layout.')
    return assets


def _migrate_one(root, top, found, adoptable, taken):
    """One old profile written to a profile folder (contract 0.3 steps 1 to 3). Returns the folder name."""
    document = copy.deepcopy(found.document)
    assets = _old_assets(root, found)
    for entry in document['logos']:
        data = assets.pop(entry['file'])
        image, kind = _decode(data)
        data, _, kind = fit_stick_limits(data, image, kind)
        if entry['role'] == 'primary':
            entry['file'] = 'logo.jpg' if kind == 'jpeg' else 'logo.png'
        elif kind == 'png' and not _fold(entry['file']).endswith('.png'):
            entry['file'] = os.path.splitext(entry['file'])[0] + '.png'
        entry.update(bytes=len(data), sha256=_sha(data))
        assets[entry['file']] = data
    primary = _primary(document)
    folder = adoptable.pop(name_key(found.name), None)
    if folder is not None:
        path, name = folder.path, folder.name
        originals = _adopt_hand_made(path, primary['file'], primary['sha256'])
        for entry in document['logos'] + document.get('renditions', []):
            if entry is primary:
                continue
            data = assets.pop(entry['file'])
            entry['file'] = _free_name(entry['file'], data, path, set())
            assets[entry['file']] = data
        listed = {entry['sha256'] for entry in document['logos']}
        for original, size, digest in originals:
            if digest not in listed and len(document['logos']) < MAX_LOGOS:
                document['logos'].append(_original_entry(original, size, digest))
    else:
        name = _unique_folder(folder_name(found.name), taken)
        path = top / name
        path.mkdir()
    taken.add(name_key(name))
    for file, data in assets.items():
        if file != primary['file']:
            _write_asset(path, file, data)
    _write_asset(path, primary['file'], assets[primary['file']])
    if 'track' in document:
        document['track'] = {**document['track'], 'marker': track_marker(document)}
    _publish_document(path, document, None)
    return name


def _backup_label(root):
    try:
        from . import usb
        serial = usb.target_info(root, boot_loader=False).get('volume_serial')
        if serial is not None:
            return f'{serial >> 16 & 0xFFFF:04X}-{serial & 0xFFFF:04X}'
    except Exception:
        pass
    label = re.sub(r'[^A-Za-z0-9_-]+', '_', root.name)[:32].strip('_')
    return label or 'usb'


def backup_root():
    from .engines import data_root
    return _safe_path(data_root() / BACKUPS)


def _back_up(root, home):
    """Copies home (DJPROFILE/VJTOOLS) into the app data folder and returns the backup folder after every copied
    file's SHA-256 matched the stick. Raises before anything is deleted if any file does not."""
    base = backup_root()
    base.mkdir(parents=True, exist_ok=True)
    _need(not _safe_path(base).is_relative_to(_safe_path(root)), 'The DJ profile backup folder must be on this computer, not on the USB.')
    stamp = f'{_backup_label(root)}-{datetime.now(timezone.utc):%Y%m%dT%H%M%SZ}'
    destination, n = base / stamp, 2
    while destination.exists():
        destination, n = base / f'{stamp}-{n}', n + 1
    copy_root = destination / OLD_HOME
    copy_root.mkdir(parents=True)
    files = {}
    for folder, folders, names in os.walk(home):
        for name in folders + names:
            _need(not os.path.islink(os.path.join(folder, name)), f'{name} in DJPROFILE/VJTOOLS is a link; the old folder was kept.')
        for name in names:
            source = Path(folder) / name
            relative = source.relative_to(home).as_posix()
            target = copy_root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            data = source.read_bytes()
            target.write_bytes(data)
            digest = _sha(data)
            _need(_sha(target.read_bytes()) == digest and _sha(source.read_bytes()) == digest,
                  f'The backup copy of {relative} does not match the USB; the old folder was kept.')
            files[relative] = {'bytes': len(data), 'sha256': digest}
    (destination / 'backup.json').write_bytes(pretty({'format': BACKUP_FORMAT, 'source': f'{TOP}/{home.name}',
                                                      'volume': str(root), 'created': datetime.now(timezone.utc).isoformat(),
                                                      'files': files}))
    return destination, files


def _remove_old_store(root, stick, failed):
    """Backs up DJPROFILE/VJTOOLS and deletes it, only when every profile its index lists is now in a profile
    folder. Returns (backup folder or None, kept)."""
    home = _old_home(root)
    if home is None:
        return None, False
    ids = {p.id for p in _scan(root).ordered if p.id}
    if failed or stick.old_index is None or any(entry['dj_id'] not in ids for entry in stick.old_index['profiles']):
        return None, True
    try:
        destination, files = _back_up(root, home)
    except (OSError, ValueError):
        return None, True
    for relative, record in files.items():
        if _sha((home / relative).read_bytes()) != record['sha256']:
            return str(destination), True
    shutil.rmtree(home)
    return str(destination), False


def _migrate(root, top, stick):
    """Moves every old-store profile to a profile folder in one pass, then removes DJPROFILE/VJTOOLS behind a
    verified backup. Returns what happened for the UI, or None when the stick was already 1.0 only."""
    if not stick.olds and _old_home(root) is None:
        return None
    adoptable = {name_key(f.name): f for f in stick.folders if f.top == TOP and _fixed(f.path, PROFILE_FILE) is None}
    taken = {name_key(e.name) for e in _visible(top)}
    moved, failed = [], []
    for found in stick.olds:
        try:
            moved.append((_migrate_one(root, top, found, adoptable, taken), found.id))
        except (OSError, ValueError) as exc:
            failed.append({'dj_id': found.id, 'dj_name': found.name, 'error': str(exc)})
    if moved:
        adopted = {name_key(name) for name, _ in moved}
        rest = [(name, dj_id) for name, dj_id in _folder_entries(stick.profiles) if name_key(name) not in adopted]
        moved_ids = {dj_id for _, dj_id in moved}
        before = stick.active
        active = before.ref if before and (before.source != 'old' or before.id in moved_ids) else None
        _write_stick_file(stick, top, moved + rest, active)
    backup, kept = _remove_old_store(root, stick, failed)
    return {'profiles': len(moved), 'backup': backup, 'old_kept': kept, 'failed': failed}


def _merge_moved(first, second):
    if not first or not second:
        return first or second
    return {'profiles': first['profiles'] + second['profiles'], 'backup': second['backup'] or first['backup'],
            'old_kept': second['old_kept'], 'failed': second['failed']}


# --- saving from the editor ---

def _optional_https(value, label):
    if value in (None, ''):
        return None
    return _https(value.strip() if isinstance(value, str) else value, label)


def parse_draft(request):
    """The save request, validated once. Everything after this trusts its shape."""
    if not isinstance(request, dict):
        raise ProfileError('Expected a profile save request')
    fields = request.get('profile')
    if not isinstance(fields, dict):
        raise ProfileError('Expected the profile fields')
    dj_id = fields.get('dj_id') or None
    folder_id = None
    if isinstance(dj_id, str) and dj_id.startswith(FOLDER_ID):
        folder_id, dj_id = dj_id, None
    if dj_id is not None:
        _id(dj_id)
    accent = fields.get('accent') or None
    links = []
    for link in _list(fields.get('links') or [], 'Links', MAX_LINKS):
        if not isinstance(link, dict):
            raise ProfileError('Each link needs a kind and an address')
        url = _string(link.get('url'), 'Link address').strip()
        if url.lower().startswith('http://'):
            raise ProfileError(f'Links must start with https:// ({url} uses http://)')
        links.append({'kind': _label(_string(link.get('kind'), 'Link kind').strip(), 'Each link kind', MAX_LINK_KIND),
                      'url': _https(url, 'Each link')})
    aliases = [_label(alias.strip(), 'Each alias', MAX_NAME)
               for alias in _list(fields.get('aliases') or [], 'Aliases', MAX_ALIASES)
               if _string(alias, 'Alias').strip()]
    logos = []
    for logo in _list(request.get('logos') or [], 'Logos', MAX_LOGOS):
        if not isinstance(logo, dict):
            raise ProfileError('Each logo needs a path')
        label = _string(logo.get('label') or '', 'Logo label').strip()
        if _utf8_len(label) > MAX_NAME or _control(label):
            raise ProfileError('Logo labels must be at most 160 bytes of printable characters')
        logos.append({'path': _string(logo.get('path'), 'Logo path'), 'label': label,
                      'note': _note(logo.get('note') or '', 'Logo note', MAX_LOGO_NOTE_BYTES)})
    expected = request.get('expected_revision')
    if expected is not None:
        _string(expected, 'Expected revision')
    make_active = request.get('make_active', False)
    if type(make_active) is not bool:
        raise ProfileError('make_active must be true or false')
    return {'dj_id': dj_id, 'folder_id': folder_id,
            'dj_name': _label(_string(fields.get('dj_name'), 'DJ name').strip(), 'DJ name', MAX_NAME),
            'tagline': _text((fields.get('tagline') or '').strip(), 'Tagline', MAX_TAGLINE),
            'accent': _accent(accent).lower() if accent else None,
            'aliases': aliases, 'links': links,
            'page_url': _optional_https(fields.get('page_url'), 'The profile page'),
            'note': _note(fields.get('note') or '', 'Note', MAX_NOTE_BYTES),
            'logos': logos, 'expected_revision': expected, 'make_active': make_active}


def _logo_source(root, path):
    if Path(path).is_absolute():
        return _regular(path)
    return _regular(_confined(root, path))


def _prepare_logos(root, logos):
    """Each chosen logo read into memory, checked, and scaled to the stick limits when it is larger."""
    _need(logos, 'Add a logo to save this profile. Players show a profile only when it has one.')
    prepared, total = [], 0
    for index, logo in enumerate(logos):
        source = _logo_source(root, logo['path'])
        _check_logo_size(source.stat().st_size)
        data = source.read_bytes()
        _check_logo_size(len(data))
        image, kind = _decode(data)
        data, image, kind = fit_stick_limits(data, image, kind)
        total += len(data)
        if total > MAX_TOTAL_LOGO_BYTES:
            raise ProfileError('These logos add up to more than 24 MB. Remove one or use smaller files.')
        prepared.append({'data': data, 'image': image, 'kind': kind, 'sha256': _sha(data), 'name': source.name,
                         'input_label': logo['label'], 'label': logo['label'] or ('Main logo' if index == 0 else f'Alternate {index}'),
                         'note': logo['note']})
    return prepared


def _content_revision(draft, dj_id, prepared):
    """The revision input today's writers share, so the same content gets the same revision."""
    return _sha(compact([dj_id, draft['dj_name'], draft['note'],
                         [[logo['sha256'], logo['input_label'], logo['note']] for logo in prepared],
                         [draft['tagline'], draft['accent'], draft['aliases'], draft['links'], draft['page_url']]]))


def _new_id(stick):
    """A random lowercase UUID (spec section 5), as the desktop and XZ writers mint new ids."""
    taken = {p.id for p in stick.ordered + stick.olds if p.id}
    while True:
        dj_id = str(uuid.uuid4())
        if dj_id not in taken:
            return dj_id


def _editing(stick, draft):
    """The profile the draft edits, or None for a new one."""
    if draft['dj_id']:
        found = next((p for p in stick.ordered + stick.olds if p.id == draft['dj_id']), None)
        _need(found is not None, CONFLICT)
        return found
    if draft['folder_id']:
        found = next((p for p in stick.ordered if p.source == 'folder' and _folder_id(p.folder.name) == draft['folder_id']), None)
        _need(found is not None, CONFLICT)
        return found
    return None


def _merged_links(draft, previous):
    """The draft's links, each keeping the unknown members of the same link in the previous document."""
    unused = [link for link in (previous or {}).get('links', [])]
    links = []
    wanted = list(draft['links'])
    if draft['page_url'] and not any(link['kind'] == 'page' and link['url'] == draft['page_url'] for link in wanted):
        wanted.insert(0, {'kind': 'page', 'url': draft['page_url']})
    for link in wanted[:MAX_LINKS]:
        match = next((old for old in unused if old.get('kind') == link['kind'] and old.get('url') == link['url']), None)
        if match is not None:
            unused.remove(match)
            links.append(copy.deepcopy(match))
        else:
            links.append(dict(link))
    return links


def _build_document(draft, dj_id, prepared, previous, folder, in_place, adopted):
    """(the new document, {file: bytes} to write) for the draft. It starts from the previous document, so every
    member this writer does not know survives (spec section 5.5)."""
    document = copy.deepcopy(previous) if previous else {}
    document.update(format=FORMAT, id=dj_id, revision=document.get('revision', ''), name=draft['dj_name'])
    for key in ('tagline', 'accent', 'aliases', 'note'):
        if draft[key]:
            document[key] = draft[key]
        else:
            document.pop(key, None)
    links = _merged_links(draft, previous)
    if links:
        document['links'] = links
    else:
        document.pop('links', None)
    primary = prepared[0]
    primary_file = 'logo.jpg' if primary['kind'] == 'jpeg' else 'logo.png'
    originals = _adopt_hand_made(folder, primary_file, primary['sha256']) if adopted else []
    original_names = {digest: name for name, _, digest in originals}
    previous_logos = {entry['sha256'].lower(): entry for entry in (previous or {}).get('logos', [])}
    used, assets, entries = {_fold(primary_file)}, {}, []
    for index, logo in enumerate(prepared):
        old = previous_logos.get(logo['sha256'])
        entry = copy.deepcopy(old) if old else {}
        if index == 0:
            name = primary_file
        elif logo['sha256'] in original_names:
            name = original_names[logo['sha256']]
        elif old and in_place and _fold(old['file']) not in ('logo.png', 'logo.jpg') and _fold(old['file']) not in used:
            name = old['file']
        else:
            name = _free_name(f'logo-{logo["sha256"][:8]}.{FORMATS[logo["kind"]][0]}', logo['data'], folder, used)
        used.add(_fold(name))
        entry.update(file=name, bytes=len(logo['data']), sha256=logo['sha256'], role='primary' if index == 0 else 'alternate')
        entry.setdefault('background', 'any')
        for key, value in (('label', logo['input_label']), ('note', logo['note'])):
            if value:
                entry[key] = value
            else:
                entry.pop(key, None)
        entries.append(entry)
        if name not in original_names.values():
            assets[name] = logo['data']
    listed = {entry['sha256'] for entry in entries}
    for name, size, digest in originals:
        if digest not in listed and len(entries) < MAX_LOGOS:
            entries.append(_original_entry(name, size, digest))
            used.add(_fold(name))
    document['logos'] = entries
    old_tile = _our_tile(previous or {})
    keep_others = previous and (_primary(previous) or {}).get('sha256', '').lower() == primary['sha256']
    others = [copy.deepcopy(r) for r in (previous or {}).get('renditions', []) if r is not old_tile] if keep_others else []
    tile = render_tile(primary['image'])
    if old_tile and in_place and old_tile['sha256'].lower() == _sha(tile) and _fold(old_tile['file']) not in used:
        tile_entry = copy.deepcopy(old_tile)
    else:
        tile_entry = {'file': _free_name(TILE_FILE, tile, folder, used), 'bytes': TILE_BYTES, 'sha256': _sha(tile),
                      'width': TILE_SIDE, 'height': TILE_SIDE, 'format': TILE_FORMAT, 'for': TILE_FOR}
    assets[tile_entry['file']] = tile
    document['renditions'] = [tile_entry] + others[:MAX_RENDITIONS - 1]
    track = document.pop('track', None)
    if track and hash12(document) == track['marker'][-MARKER_HASH:] and (
            'file' not in track or (in_place and folder.joinpath(*track['file'].split('/')).is_file())):
        document['track'] = track
    return document, assets


def _save_profile(root, top, stick, draft, prepared):
    """Writes the draft as a profile folder (spec section 7) and returns its id."""
    editing = _editing(stick, draft)
    previous = editing.document if editing else None
    in_place = editing is not None and editing.source != 'old' and editing.folder.top == TOP
    want = folder_name(draft['dj_name'])
    if in_place:
        folder = editing.folder
    else:
        folder = next((f for f in stick.folders if f.top == TOP and name_key(f.name) == name_key(want)
                       and _fixed(f.path, PROFILE_FILE) is None), None)
    adopted = (editing.source == 'folder') if in_place else folder is not None
    slot = next((p for p in stick.ordered if folder and p.folder and p.folder.path == folder.path), None)
    taken = {name_key(e.name) for e in _visible(top)}
    if folder is None:
        name = _unique_folder(want, taken)
        path = top / name
    else:
        path, name = folder.path, folder.name
    replaces = [p for p in (editing, slot) if p is not None]
    _need(any(p in stick.profiles for p in replaces) or len(stick.profiles) < MAX_PROFILES,
          f'This USB already holds {MAX_PROFILES} DJ profiles')
    dj_id = editing.id if editing and editing.id else _new_id(stick)
    path.mkdir(exist_ok=True)
    document, assets = _build_document(draft, dj_id, prepared, previous, path, in_place, adopted)
    if in_place and _stripped(document) == _stripped(previous):
        document = previous
    else:
        revision = _content_revision(draft, dj_id, prepared)
        document['revision'] = revision if revision != (previous or {}).get('revision') else _stick_revision()
    primary = _primary(document)['file']
    for file, data in assets.items():
        if file != primary:
            _write_asset(path, file, data)
    _write_asset(path, primary, assets[primary])
    _publish_document(path, document, editing.raw if in_place else None)
    if want != name:
        target = _unique_folder(want, taken - {name_key(name)})
        os.rename(path, top / target)
        path, name = top / target, target
    entries, placed = [], False
    for p in stick.profiles:
        if p.source == 'old':
            continue
        if any(p is r for r in replaces):
            if not placed:
                entries.append((name, dj_id))
                placed = True
        else:
            entries.append((p.folder.name, p.id))
    if not placed:
        entries.append((name, dj_id))
    current = stick.stick_active or stick.active
    active = dj_id if draft['make_active'] or current is None or any(current is r for r in replaces) else current.ref
    _write_stick_file(stick, top, entries, active)
    if in_place:
        _delete_unlisted(path, previous, document)
    return dj_id


def save(volume, request):
    root = _volume(volume)
    draft = parse_draft(request)
    prepared = _prepare_logos(root, draft['logos'])
    with _locked(root) as top:
        stick = _scan(root)
        _check_revision(stick, draft['expected_revision'])
        moved = _migrate(root, top, stick)
        if moved:
            stick = _scan(root)
        dj_id = _save_profile(root, top, stick, draft, prepared)
        if _old_home(root) is not None or any(p.source == 'old' for p in _scan(root).profiles):
            moved = _merge_moved(moved, _migrate(root, top, _scan(root)))
    result = read(root)
    result['saved'] = dj_id
    result['moved'] = moved
    return result


def set_active(volume, dj_id, expected_revision):
    """DJPROFILE.json's active, keeping every other member (spec section 6). While an old index remains and lists
    the profile, its active is rewritten in place too, so an XZ on an older runtime agrees (contract 0.3 step 5)."""
    root = _volume(volume)
    _need(isinstance(dj_id, str) and (dj_id.startswith(FOLDER_ID) or _ID.fullmatch(dj_id)), 'Choose a DJ profile to make active')
    with _locked(root) as top:
        stick = _scan(root)
        _check_revision(stick, expected_revision)
        moved = _migrate(root, top, stick)
        if moved:
            stick = _scan(root)
        found = next((p for p in stick.profiles if _view_id(p) == dj_id and p.source != 'old'), None)
        _need(found is not None, 'Save this DJ profile to the USB before making it active')
        entries = [(e['folder'], e.get('id')) for e in stick.stick_file['profiles']] if stick.stick_file else _folder_entries(stick.profiles)
        if all(name_key(name) != name_key(found.folder.name) for name, _ in entries):
            entries.append((found.folder.name, found.id))
        _write_stick_file(stick, top, entries, found.ref)
        index = stick.old_index
        if index and found.id and index['active'] != found.id and any(entry['dj_id'] == found.id for entry in index['profiles']):
            index['active'] = found.id
            index['revision'] = _stick_revision()
            _replace(_confined(root, OLD_INDEX_PATH), pretty(validate_old_index(index)))
    result = read(root)
    result['moved'] = moved
    return result


# --- the profile track: silent MPEG-1 Layer III with the profile in ID3v2.4 (spec section 9) ---

TRACK_SECONDS = 10
FRAME_SAMPLES = 1152
SAMPLE_RATE = 44100
# 128 kbps, 44.1 kHz, no padding: 144 * 128000 / 44100 = 417 bytes. Every frame is identical.
FRAME_HEADER = bytes((0xFF, 0xFB, 0x90, 0x40))  # MPEG-1 Layer III, no CRC, 128k, 44.1k, joint stereo
FRAME_BYTES = 417
SILENT_FRAME = FRAME_HEADER + bytes(FRAME_BYTES - 4)
TRACK_FRAMES = -(-TRACK_SECONDS * SAMPLE_RATE // FRAME_SAMPLES)
MAX_TAG_BYTES = 32 * 1024 * 1024
APIC_FRONT_COVER = 3
APIC_ARTIST_LOGOTYPE = 0x13
TRACK_MARKER = 'djprofile:1:'
MARKER_HASH = 12
PROFILE_PLAYLIST = 'dj profile'
MARKER_TOKEN = re.compile(r'(?<!\S)djprofile:1:[A-Za-z0-9_-]{1,128}:[0-9a-f]{12}(?!\S)')


def _synchsafe(value):
    if not 0 <= value < 1 << 28:
        raise ProfileError('ID3 frame is too large')
    return bytes((value >> 21 & 0x7F, value >> 14 & 0x7F, value >> 7 & 0x7F, value & 0x7F))


def _unsynchsafe(data):
    if any(byte & 0x80 for byte in data):
        raise ProfileError('ID3 size is not synchsafe')
    return data[0] << 21 | data[1] << 14 | data[2] << 7 | data[3]


def _frame(frame_id, payload):
    return frame_id.encode('ascii') + _synchsafe(len(payload)) + b'\0\0' + payload


def _utf8(text):
    return text.encode('utf-8')


def _url(url):
    """W-frame text is ISO-8859-1, so non-ASCII characters are percent-encoded."""
    return ''.join(c if ord(c) < 128 else quote(c) for c in url).encode('ascii')


def _text_frame(frame_id, text):
    return frame_id, b'\x03' + _utf8(text)


def _unique(description, used):
    candidate, n = description, 2
    while candidate in used:
        candidate, n = f'{description} {n}', n + 1
    used.add(candidate)
    return candidate


def track_name(dj_name):
    cleaned = ''.join(c for c in dj_name if c not in FAT_FORBIDDEN and not _control(c))[:200].strip().rstrip('. ')
    return f'DJ PROFILE - {cleaned}.mp3' if cleaned else 'DJ PROFILE.mp3'


def track_payload(document):
    """What TXXX:DJPROFILE carries. That is the profile document without its track member, as canonical JSON."""
    return canonical({key: value for key, value in document.items() if key != 'track'})


def track_frames(document, logos, cover):
    """The ID3 frames for the profile track, in order, and the labels of logos that did not fit.
    logos: [{'data', 'mime', 'label'}], primary first, embedded at full resolution as band logotypes.
    cover: the front cover JPEG (cover_jpeg)."""
    links = document.get('links', [])
    epk = next((link for link in links if link['kind'].lower() in EPK_KINDS), None)
    website = next((link['url'] for link in links if link['kind'].lower() == 'website'), None)
    page = next((link['url'] for link in links if link['kind'].lower() == 'page'), None)
    shared = page or (epk and epk['url'])
    name = document['name']
    frames = [_text_frame('TIT2', name), _text_frame('TPE1', name), _text_frame('TALB', 'DJ PROFILE'),
              _text_frame('TCON', 'DJ Profile'), _text_frame('TPE2', name)]
    if document.get('tagline'):
        frames.append(_text_frame('TPUB', document['tagline']))
    frames.append(('COMM', b'\x03eng\0' + _utf8(track_marker(document) + (' ' + shared if shared else ''))))
    if page or website:
        frames.append(('WOAR', _url(page or website)))
    used = set()
    if epk:
        frames.append(('WXXX', b'\x03' + _utf8(_unique('EPK', used)) + b'\0' + _url(epk['url'])))
    for link in links:
        if link is not epk and link['kind'].lower() != 'page':
            frames.append(('WXXX', b'\x03' + _utf8(_unique(link['kind'], used)) + b'\0' + _url(link['url'])))
    frames.append(('TXXX', b'\x03DJPROFILE\0' + track_payload(document)))
    frames.append(('APIC', b'\x03image/jpeg\0' + bytes((APIC_FRONT_COVER,)) + b'\0' + cover))
    size = 10 + sum(10 + len(body) for _, body in frames)
    skipped, embedded, descriptions = [], 0, set()
    for i, logo in enumerate(logos):
        description = _unique(logo['label'] or ('Main logo' if i == 0 else f'Alternate {i}'), descriptions)
        body = (b'\x03' + logo['mime'].encode('ascii') + b'\0' + bytes((APIC_ARTIST_LOGOTYPE,))
                + _utf8(description) + b'\0' + logo['data'])
        if size + 10 + len(body) > MAX_TAG_BYTES:
            skipped.append(logo['label'] or f'logo {i + 1}')
            continue
        frames.append(('APIC', body))
        size += 10 + len(body)
        embedded += 1
    return frames, embedded, skipped


def id3_tag(frames):
    body = b''.join(_frame(frame_id, payload) for frame_id, payload in frames)
    return b'ID3\x04\x00\x00' + _synchsafe(len(body)) + body


def read_id3(data):
    """(frames as [(id, payload)], offset of the audio) for an ID3v2.4 tag with no extended header."""
    if len(data) < 10 or data[:3] != b'ID3' or data[3] != 4:
        raise ProfileError('The track has no ID3v2.4 tag')
    if data[5] & 0xF0:
        raise ProfileError('Unexpected ID3 tag flags')
    end = 10 + _unsynchsafe(data[6:10])
    at, frames = 10, []
    while at + 10 <= end and data[at] != 0:
        frame_id = data[at:at + 4].decode('ascii')
        size = _unsynchsafe(data[at + 4:at + 8])
        if data[at + 8:at + 10] != b'\0\0' or at + 10 + size > end:
            raise ProfileError(f'ID3 frame {frame_id} is malformed')
        frames.append((frame_id, data[at + 10:at + 10 + size]))
        at += 10 + size
    return frames, end


def decode_frame(frame_id, payload):
    """A readable view of the frames this module writes."""
    def split(body, encoding):
        cut = body.index(b'\0')
        return body[:cut].decode(encoding), body[cut + 1:]
    if frame_id.startswith('T') and frame_id != 'TXXX':
        return payload[1:].decode('utf-8')
    if frame_id == 'TXXX':
        description, value = split(payload[1:], 'utf-8')
        return {'description': description, 'value': value.decode('utf-8')}
    if frame_id == 'COMM':
        description, text = split(payload[4:], 'utf-8')
        return {'language': payload[1:4].decode('ascii'), 'description': description, 'text': text.decode('utf-8')}
    if frame_id == 'WXXX':
        description, url = split(payload[1:], 'utf-8')
        return {'description': description, 'url': url.decode('latin-1')}
    if frame_id.startswith('W'):
        return payload.decode('latin-1')
    if frame_id == 'APIC':
        mime, rest = split(payload[1:], 'latin-1')
        description, data = split(rest[1:], 'utf-8')
        return {'mime': mime, 'type': rest[0], 'description': description, 'data': data}
    return payload


def check_silent_audio(data, offset):
    """Every audio frame is the one all-zero frame: identical header, zero side info and main data."""
    audio = data[offset:]
    if len(audio) != TRACK_FRAMES * FRAME_BYTES:
        raise ProfileError(f'Expected {TRACK_FRAMES} audio frames of {FRAME_BYTES} bytes')
    for i in range(TRACK_FRAMES):
        if audio[i * FRAME_BYTES:(i + 1) * FRAME_BYTES] != SILENT_FRAME:
            raise ProfileError(f'Audio frame {i} is not silent')


def ffmpeg_silence(path, ffmpeg=None):
    """Decodes with ffmpeg: (every sample is zero, seconds decoded). None when ffmpeg is absent."""
    ffmpeg = ffmpeg or shutil.which('ffmpeg')
    if not ffmpeg:
        return None
    result = subprocess.run([ffmpeg, '-v', 'error', '-i', str(path), '-f', 's16le', '-'], capture_output=True,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0), timeout=60)
    if result.returncode or result.stderr.strip():
        raise ProfileError(f'ffmpeg could not decode the profile track: {result.stderr.decode(errors="replace")[:200]}')
    pcm = result.stdout
    return not pcm.strip(b'\0'), len(pcm) / 4 / SAMPLE_RATE


def write_track(document, logos, cover, destination):
    """Writes `DJ PROFILE - <name>.mp3` into the destination folder and proves it before publishing it."""
    validate_profile(document)
    folder = _safe_path(destination)
    folder.mkdir(parents=True, exist_ok=True)
    if not folder.is_dir():
        raise ProfileError('The track folder is not a folder')
    frames, embedded, skipped = track_frames(document, logos, cover)
    data = id3_tag(frames) + SILENT_FRAME * TRACK_FRAMES
    parsed, offset = read_id3(data)
    if parsed != frames:
        raise ProfileError('The profile track tags did not read back as written')
    check_silent_audio(data, offset)
    path = folder / track_name(document['name'])
    temporary = folder / f'.djprofile-{secrets.token_hex(8)}.mp3'
    _write_new(temporary, data)
    try:
        written = temporary.read_bytes()
        reread, offset = read_id3(written)
        if reread != frames:
            raise ProfileError('The profile track tags did not read back from disk as written')
        check_silent_audio(written, offset)
        decoded = ffmpeg_silence(temporary)
        if decoded is not None and (not decoded[0] or abs(decoded[1] - TRACK_SECONDS) > 0.5):
            raise ProfileError('ffmpeg decoded sound in the profile track; it was not saved')
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    return {'path': str(path), 'bytes': len(data), 'sha256': _sha(data), 'marker': track_marker(document),
            'duration_seconds': round(TRACK_FRAMES * FRAME_SAMPLES / SAMPLE_RATE, 3),
            'logos_embedded': embedded, 'logos_skipped': skipped, 'verified_silent': True,
            'ffmpeg_verified': decoded is not None}


def write_profile_track(volume, dj_id, destination, folder=None):
    """The profile track for a profile on the stick, into a computer folder or into its profile folder."""
    root = _volume(volume)
    _id(dj_id)
    if destination not in ('folder', 'usb'):
        raise ProfileError("Choose where the track goes: 'folder' or 'usb'")
    with _locked(root) as top:
        stick = _scan(root)
        moved = _migrate(root, top, stick)
        if moved:
            stick = _scan(root)
        found = next((p for p in stick.ordered if p.id == dj_id), None)
        _need(found is not None, 'Save this DJ profile to the USB before making its track')
        document = found.document
        logos = []
        if document.get('logos'):
            primary = _primary(document)
            for entry in [primary] + [e for e in document['logos'] if e is not primary]:
                data = _entry_data(found.folder.path, entry, MAX_LOGO_BYTES)
                logos.append({'data': data, 'mime': FORMATS[_image_format(data)][1], 'label': entry.get('label', '')})
            background = primary.get('background', 'any')
        else:
            data = _read_file(found.folder.path / found.logo, MAX_LOGO_BYTES)
            logos.append({'data': data, 'mime': FORMATS[_image_format(data)][1], 'label': ''})
            background = 'any'
        cover = cover_jpeg(logos[0]['data'], background)
        if destination == 'folder':
            target = Path(folder) if folder else Path.home() / 'Music' / 'DJ Profile'
            result = write_track(document, logos, cover, target)
        else:
            _need(found.folder.top == TOP and found.raw is not None, 'Save this profile once in XZ Mods, then make its track')
            result = write_track(document, logos, cover, found.folder.path)
            updated = copy.deepcopy(document)
            updated['track'] = {'file': Path(result['path']).name, 'bytes': result['bytes'], 'sha256': result['sha256'],
                                'marker': result['marker']}
            if _read_file(_fixed(found.folder.path, PROFILE_FILE), MAX_JSON + 1) != found.raw:
                raise ProfileError('The profile changed while its track was written. Reload the USB and try again.')
            _publish_document(found.folder.path, updated, found.raw)
            _delete_unlisted(found.folder.path, document, updated)
    return {'format': 'xz-dj-profile-track/1', 'destination': destination, 'moved': moved, **result}


# --- is the profile track on the stick (read-only) ---

def _device_library_tracks(root):
    path = root.joinpath(*rekordbox_pdb.EXPORT_PDB)
    if not path.is_file():
        return None
    return rekordbox_pdb.tracks_with_comment_containing(path, TRACK_MARKER)


def _onelibrary_tracks(root):
    if not onelibrary.present(root):
        return None
    with onelibrary.open_library(root) as connection:
        return [{**track, 'comment': track['djComment']} for track in onelibrary.tracks_with_comment_containing(connection, TRACK_MARKER)]


# Each reader returns the tracks whose comment carries a profile marker, or None when the stick has no such library.
LIBRARY_READERS = {'device_library': _device_library_tracks, 'onelibrary': _onelibrary_tracks}


def _scan_library(reader, root):
    try:
        tracks = reader(root)
    except (OSError, ValueError) as exc:
        return {'state': 'unreadable', 'error': str(exc)}, []
    if tracks is None:
        return {'state': 'absent', 'error': None}, []
    return {'state': 'present', 'error': None}, tracks


def _track_in_library(library, tracks, document):
    """found / stale / missing for one profile in one library, and whether it sits in a DJ PROFILE playlist."""
    if library['state'] != 'present':
        return {'library': library['state'], 'track': None, 'in_profile_playlist': False, 'playlists': []}
    marker = track_marker(document)
    # The marker is a token anywhere in the comment: some tools prepend text such as a key or energy rating.
    profile = marker[:-MARKER_HASH]
    mine = [track for track in tracks if any(token.startswith(profile) for token in MARKER_TOKEN.findall(track['comment']))]
    current = [track for track in mine if marker in MARKER_TOKEN.findall(track['comment'])]
    chosen = current or mine
    playlists = sorted({name for track in chosen for name in track['playlists']})
    return {'library': 'present', 'track': 'found' if current else 'stale' if mine else 'missing',
            'in_profile_playlist': any(name.strip().casefold() == PROFILE_PLAYLIST for name in playlists),
            'playlists': playlists}


def profile_track_status(volume):
    """For every profile with an id, whether its profile track reached each rekordbox library on the stick.

    Tracks are matched by the comment marker (track_marker), recomputed from the current document, so a track
    made before the last edit, or before the move to 1.0, shows as stale. Nothing is written.
    """
    root = _volume(volume)
    scans = {name: _scan_library(reader, root) for name, reader in LIBRARY_READERS.items()}
    documents = [p.document for p in _scan(root).profiles if p.document]
    profiles = [{'dj_id': document['id'], 'dj_name': document['name'], 'marker': track_marker(document),
                 **{name: _track_in_library(library, tracks, document) for name, (library, tracks) in scans.items()}}
                for document in documents]
    return {'format': 'xz-dj-profile-track-status/1', 'libraries': {name: library for name, (library, _) in scans.items()},
            'profiles': profiles}
