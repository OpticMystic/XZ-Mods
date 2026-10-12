# SPDX-License-Identifier: MIT
"""DJ profiles on a USB stick and the silent profile track.

Layout, JSON documents and writing order follow the VJ.Tools DJ Profile
standard (docs/architecture/dj-profile-standard.md). The validation rules
mirror the Rust reader (dj-profiles/src/profile.rs) and the shared fixtures,
so a stick written here reads the same in Wingman, the Library and on the XZ.
Profiles live only on the stick; nothing here touches PIONEER/ or the XZ.
"""
from __future__ import annotations

import base64
import copy
from datetime import datetime, timezone
import hashlib
import io
import json
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

from .cache import _regular, _safe_path
from .jobs import exclusive_lock

HOME = 'DJPROFILE/VJTOOLS'
INDEX_PATH = HOME + '/index.json'
LEGACY_PATH = 'VJ.Tools/Branding/profile.json'
INDEX_SCHEMA = 'vjtools.dj-profile-index'
PROFILE_SCHEMA = 'vj.tools.dj-profile'
MANIFEST_SCHEMA = 'vj.tools.dj-branding'
FOLDER_ID = 'folder:'
LOCK = '.xzmods-write.lock'

MAX_PROFILES = 16
MAX_JSON = 64 * 1024
MAX_NAME = 160
MAX_TAGLINE = 80
MAX_ALIASES = 8
MAX_ALIAS = 80
MAX_LINKS = 12
MAX_LINK_KIND = 40
MAX_URL = 2048
MAX_NOTE_BYTES = 8 * 1024
MAX_LOGO_NOTE_BYTES = 1024
MAX_LOGOS = 8
MIN_LOGO_BYTES = 8
MAX_LOGO_BYTES = 8 * 1024 * 1024
MAX_TOTAL_LOGO_BYTES = 24 * 1024 * 1024
MAX_IMAGE_SIDE = 8192
MAX_IMAGE_PIXELS = 16 * 1024 * 1024
TILE_SIDE = 160
TILE_FORMAT = 'rgb565le'
TILE_BYTES = TILE_SIDE * TILE_SIDE * 2
TILE_FILE = 'xz-tile-160.rgb565'
PREVIEW_SIDES = (256, 128, 64)
# The desktop host drops backend result lines over 64 KiB, so all previews share this budget.
PREVIEW_BUDGET = 40 * 1024
# rekordbox's colour-label palette, indexed by the stick's COLORID (standard §1.2).
STICK_COLOURS = {1: '#f870f8', 2: '#f87070', 3: '#ffa064', 4: '#f8e550', 5: '#1ee12b', 6: '#10e4dc', 7: '#1e50f0', 8: '#9828f0'}
PNG_MAGIC = b'\x89PNG\r\n\x1a\n'
JPEG_MAGIC = b'\xff\xd8\xff'
FORMATS = {'png': ('png', 'image/png'), 'jpeg': ('jpg', 'image/jpeg')}
EPK_KINDS = ('media-kit', 'epk', 'files')


class ProfileError(ValueError):
    pass


# --- validation shared by every document read from or written to a stick ---

_ID = re.compile(r'[A-Za-z0-9_-]{1,128}')
_DIGEST = re.compile(r'[0-9a-fA-F]{64}')
_ACCENT = re.compile(r'#[0-9a-fA-F]{6}')


def _control(text):
    return any(unicodedata.category(c) == 'Cc' for c in text)


def _string(value, label):
    if not isinstance(value, str):
        raise ProfileError(f'{label} must be text')
    return value


def _id(value, label='DJ ID'):
    if not isinstance(value, str) or not _ID.fullmatch(value):
        raise ProfileError(f'{label} must contain 1 to 128 letters, numbers, hyphens, or underscores')
    return value


def _label(value, label, max_chars):
    value = _string(value, label)
    if not value.replace('\ufeff', '').strip() or len(value) > max_chars or _control(value):
        raise ProfileError(f'{label} must contain 1 to {max_chars} printable characters')
    return value


def _text(value, label, max_chars):
    value = _string(value, label)
    if len(value) > max_chars or _control(value):
        raise ProfileError(f'{label} must be at most {max_chars} printable characters')
    return value


def _note(value, label, max_bytes):
    value = _string(value, label)
    if len(value.encode()) > max_bytes or any(unicodedata.category(c) == 'Cc' and c not in '\n\r\t' for c in value):
        raise ProfileError(f'{label} must be at most {max_bytes} bytes of plain text')
    return value


def _https(value, label):
    value = _string(value, label)
    host = re.split(r'[/?#]', value[len('https://'):], maxsplit=1)[0] if value.startswith('https://') else ''
    if not host or len(value) > MAX_URL or any(c.isspace() for c in value) or _control(value):
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


def _parts(path):
    """A stick-relative path with forward slashes, split into safe components."""
    if (not isinstance(path, str) or not path or len(path) > 1024 or path.startswith('/')
            or any(c in path for c in '\\:\0')):
        raise ProfileError('Profile paths must be relative paths with forward slashes')
    parts = path.split('/')
    for part in parts:
        if (not part or part in ('.', '..') or len(part) > 255 or part.endswith((' ', '.'))
                or _control(part) or any(c in part for c in '*?"<>|')):
            raise ProfileError('Unsafe profile path component')
    return parts


def _check_bundle(bundle, dj_id):
    parts = _parts(bundle)
    if len(parts) == 4 and parts[:3] == ['VJ.Tools', 'Branding', 'bundles']:
        bundle_id = parts[3]
    elif len(parts) == 6 and parts[:3] == ['DJPROFILE', 'VJTOOLS', 'profiles'] and parts[3] == dj_id and parts[4] == 'bundles':
        bundle_id = parts[5]
    else:
        raise ProfileError("Bundle must be an immutable directory in VJ.Tools/Branding/bundles or in this DJ's DJPROFILE/VJTOOLS/profiles/<dj_id>/bundles")
    _id(bundle_id, 'Branding bundle ID')
    return bundle


def _member(path, directory, dj_id, problem):
    """The bundle that holds path as <bundle>/<directory>/<file>."""
    parts = _parts(path)
    if len(parts) < 3 or parts[-2] != directory:
        raise ProfileError(problem)
    bundle = '/'.join(parts[:-2])
    try:
        _check_bundle(bundle, dj_id)
    except ProfileError:
        raise ProfileError(problem) from None
    return bundle


LOGO_PLACEMENT = "Logos must be regular files in an immutable branding bundle's assets directory"
TILE_PLACEMENT = "The XZ tile must be a file in its branding bundle's renditions directory"


def _logo_reference(value, dj_id):
    if not isinstance(value, dict):
        raise ProfileError('Each logo must be an object')
    path = _string(value.get('path'), 'Logo path')
    size = _count(value.get('bytes'), 'Logo size')
    digest = value.get('sha256')
    if not 0 < size <= MAX_LOGO_BYTES or not isinstance(digest, str) or not _DIGEST.fullmatch(digest):
        raise ProfileError('Each logo needs a byte count of at most 8 MiB and a SHA-256 digest')
    label = _string(value.get('label', ''), 'Logo label')
    if len(label) > MAX_NAME or _control(label):
        raise ProfileError('Logo labels must be at most 160 printable characters')
    _note(value.get('note', ''), 'Logo note', MAX_LOGO_NOTE_BYTES)
    return {'path': path, 'bytes': size, 'sha256': digest, 'label': label, 'note': value.get('note', '')}


def validate_document(document):
    """Validates a parsed profile.json the way every shipped reader does. Returns it unchanged."""
    if not isinstance(document, dict):
        raise ProfileError('A profile must be a JSON object')
    if document.get('schema') != PROFILE_SCHEMA or document.get('version') != 1 or type(document.get('version')) is not int:
        raise ProfileError('Unsupported USB DJ profile schema or version')
    dj_id = _id(document.get('dj_id'))
    _id(document.get('revision'), 'Profile revision')
    _label(document.get('dj_name'), 'DJ name', MAX_NAME)
    _note(document.get('note', ''), 'Note', MAX_NOTE_BYTES)
    _text(document.get('tagline', ''), 'Tagline', MAX_TAGLINE)
    if document.get('accent') is not None:
        _accent(document['accent'])
    for alias in _list(document.get('aliases', []), 'Aliases', MAX_ALIASES):
        _label(alias, 'Each alias', MAX_ALIAS)
    for link in _list(document.get('links', []), 'Links', MAX_LINKS):
        if not isinstance(link, dict):
            raise ProfileError('Each link must be an object')
        _label(link.get('kind'), 'Each link kind', MAX_LINK_KIND)
        _https(link.get('url'), 'Each link')
    if document.get('page_url') is not None:
        _https(document['page_url'], 'The profile page')
    preferred = document.get('preferred_logo')
    secondary = _list(document.get('secondary_logos', []), 'Logos', MAX_LOGOS)
    if preferred is None and secondary:
        raise ProfileError('Choose a preferred logo before adding alternates')
    logos = ([preferred] if preferred is not None else []) + secondary
    if len(logos) > MAX_LOGOS:
        raise ProfileError('A profile may contain at most eight logos')
    references = [_logo_reference(logo, dj_id) for logo in logos]
    bundle = _string(document.get('bundle', ''), 'Bundle')
    if bundle:
        _check_bundle(bundle, dj_id)
    elif references:
        bundle = _member(references[0]['path'], 'assets', dj_id, LOGO_PLACEMENT)
    seen = set()
    for reference in references:
        if _member(reference['path'], 'assets', dj_id, LOGO_PLACEMENT) != bundle:
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
        if _member(_string(tile.get('path'), 'Tile path'), 'renditions', dj_id, TILE_PLACEMENT) != bundle:
            raise ProfileError("The XZ tile must belong to the profile's branding bundle")
        digest = tile.get('sha256')
        if (tile.get('width') != TILE_SIDE or tile.get('height') != TILE_SIDE or tile.get('format') != TILE_FORMAT
                or tile.get('bytes') != TILE_BYTES or not isinstance(digest, str) or not _DIGEST.fullmatch(digest)):
            raise ProfileError(f'The XZ tile must be {TILE_SIDE}x{TILE_SIDE} {TILE_FORMAT}, {TILE_BYTES} bytes, with a SHA-256 digest')
    return document


def validate_index(index):
    if not isinstance(index, dict):
        raise ProfileError('The profile index must be a JSON object')
    if index.get('schema') != INDEX_SCHEMA or index.get('version') != 1 or type(index.get('version')) is not int:
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
        _label(entry.get('dj_name'), 'DJ name', MAX_NAME)
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


def pretty(value):
    """serde_json::to_vec_pretty's layout, so the Rust and C writers produce the same bytes."""
    return json.dumps(value, indent=2, ensure_ascii=False).encode()


def compact(value):
    return json.dumps(value, separators=(',', ':'), ensure_ascii=False).encode()


def _sha(data):
    return hashlib.sha256(data).hexdigest()


# --- stick files ---

def _volume(volume):
    root = _safe_path(volume)
    if not root.is_dir():
        raise ProfileError('Choose the USB drive or folder that holds the DJ profiles')
    return root


def _confined(root, relative):
    return _safe_path(root.joinpath(*_parts(relative)))


def _read(root, relative, max_bytes):
    path = _regular(_confined(root, relative))
    size = path.stat().st_size
    if size > max_bytes:
        raise ProfileError(f'{relative} is larger than {max_bytes} bytes')
    data = path.read_bytes()
    if len(data) != size:
        raise ProfileError(f'{relative} changed while it was read')
    return data


def _exists(root, relative):
    try:
        return _confined(root, relative).is_file()
    except (OSError, ValueError):
        return False


def _ensure_dir(root, relative):
    path = _confined(root, relative)
    path.mkdir(parents=True, exist_ok=True)
    if not _safe_path(path).is_dir():
        raise ProfileError(f'{relative} is not a folder')
    return path


def _write_new(path, data):
    with _safe_path(path).open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def _replace(destination, data):
    """Temporary file, flush, rename over the destination (standard §4)."""
    temporary = destination.parent / f'.profile-{secrets.token_hex(8)}'
    _write_new(temporary, data)
    try:
        os.replace(temporary, destination)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    if destination.read_bytes() != data:
        raise ProfileError(f'{destination.name} did not read back as written. Check the USB and try again.')


def _publish(root, relative, data):
    parts = _parts(relative)
    _ensure_dir(root, '/'.join(parts[:-1]))
    _replace(_confined(root, relative), data)


def _profile_path(dj_id):
    return f'{HOME}/profiles/{dj_id}/profile.json'


def _load_index(root):
    """The valid index document, or None when the stick has none or it is invalid."""
    try:
        return validate_index(parse_json(_read(root, INDEX_PATH, MAX_JSON), 'Profile index'))
    except (OSError, ValueError):
        return None


def _load_document(root, relative):
    data = _read(root, relative, MAX_JSON)
    return validate_document(parse_json(data)), data


def _index_revision():
    return f'{datetime.now(timezone.utc):%Y-%m-%dT%H-%M-%SZ}-{secrets.token_hex(2)}'


def _ascii_lower(text):
    return ''.join(chr(ord(c) + 32) if 'A' <= c <= 'Z' else c for c in text)


# --- images ---

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
        if not (0 < width <= MAX_IMAGE_SIDE and 0 < height <= MAX_IMAGE_SIDE and width * height <= MAX_IMAGE_PIXELS):
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


def _png_url(image, side):
    from PIL import Image
    copy = image.convert('RGBA')
    copy.thumbnail((side, side), Image.Resampling.LANCZOS)
    stream = io.BytesIO()
    copy.quantize(256, method=Image.Quantize.FASTOCTREE).save(stream, 'PNG', optimize=True)
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
    """The preferred logo fitted inside 160x160 over black, RGB565 little-endian, row-major."""
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


def read_logo(path):
    """A logo the DJ picked on their computer: its size, format and a preview for the editor."""
    path = _regular(path)
    size = path.stat().st_size
    _check_logo_size(size)
    data = path.read_bytes()
    image, kind = _decode(data)
    return {'path': str(path), 'width': image.width, 'height': image.height, 'bytes': len(data),
            'format': kind, 'preview': _png_url(image, PREVIEW_SIDES[0])}


# --- stick colour (standard §1.2) ---

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


# --- reading ---

def _public_folder(root):
    try:
        names = {_ascii_lower(entry.name): entry for entry in os.scandir(root)}
    except OSError:
        return None
    for name in ('DJPROFILE', 'DJPROFILES'):
        entry = names.get(_ascii_lower(name))
        if entry is not None and entry.is_dir(follow_symlinks=False):
            return name
    return None


def _image_name(name):
    return '.' in name[1:] and _ascii_lower(name.rsplit('.', 1)[1]) in ('png', 'jpg', 'jpeg')


def _folder_logo(directory):
    best = None
    for entry in os.scandir(directory):
        if entry.name.startswith('.') or not _image_name(entry.name) or not entry.is_file(follow_symlinks=False):
            continue
        if not MIN_LOGO_BYTES <= entry.stat(follow_symlinks=False).st_size <= MAX_LOGO_BYTES:
            continue
        key = (_ascii_lower(entry.name.rsplit('.', 1)[0]) != 'logo', _ascii_lower(entry.name), entry.name)
        if best is None or key < best[0]:
            best = (key, entry.name)
    return best and best[1]


def _folder_profiles(root, taken, room):
    """Folder profiles per standard §1.1, sorted by name, skipping names a VJTOOLS profile has."""
    found = {}
    try:
        tops = {_ascii_lower(entry.name): entry.name for entry in os.scandir(root) if entry.is_dir(follow_symlinks=False)}
    except OSError:
        return []
    for top in ('djprofile', 'djprofiles'):
        if top not in tops:
            continue
        for entry in os.scandir(root / tops[top]):
            name = entry.name
            if name.startswith('.') or _ascii_lower(name) == 'vjtools' or not entry.is_dir(follow_symlinks=False):
                continue
            if not name.strip(' ') or len(name) > MAX_NAME or _control(name) or _ascii_lower(name) in found:
                continue
            logo = _folder_logo(entry.path)
            if logo:
                found[_ascii_lower(name)] = (name, f'{tops[top]}/{name}/{logo}')
    ordered = sorted(found.values(), key=lambda item: (_ascii_lower(item[0]), item[0]))
    return [item for item in ordered if _ascii_lower(item[0]) not in taken][:max(room, 0)]


def _folder_id(name):
    return FOLDER_ID + _sha(name.encode())[:16]


def _logo_view(root, reference, previews):
    data = _read(root, reference['path'], MAX_LOGO_BYTES)
    if len(data) != reference['bytes'] or _sha(data) != reference['sha256'].lower():
        raise ProfileError(f'{reference["path"]} does not match its recorded size and SHA-256')
    image, kind = _decode(data)
    view = {'path': str(_confined(root, reference['path'])), 'stick_path': reference['path'],
            'label': reference.get('label', ''), 'note': reference.get('note', ''), 'width': image.width,
            'height': image.height, 'bytes': len(data), 'sha256': reference['sha256'], 'format': kind, 'preview': None}
    previews.append((view, image))
    return view


def _track_view(root, document):
    record = document.get('profile_track')
    try:
        parts = _parts(record['path'])
        if parts[:5] != ['DJPROFILE', 'VJTOOLS', 'profiles', document['dj_id'], 'track'] or len(parts) != 6:
            return None
        path = _regular(_confined(root, record['path']))
        return {'path': str(path), 'stick_path': record['path'], 'bytes': path.stat().st_size}
    except (KeyError, TypeError, OSError, ValueError):
        return None


def _profile_view(root, document, source, previews):
    logos = [_logo_view(root, reference, previews['preferred' if i == 0 else 'secondary'])
             for i, reference in enumerate(_logo_references(document))]
    tile = None
    if document.get('xz_tile'):
        data = _read(root, document['xz_tile']['path'], TILE_BYTES)
        if _sha(data) != document['xz_tile']['sha256'].lower():
            raise ProfileError(f'{document["xz_tile"]["path"]} does not match its recorded SHA-256')
        tile = {'path': str(_confined(root, document['xz_tile']['path'])), 'stick_path': document['xz_tile']['path'], 'preview': None}
        previews['tile'].append((tile, tile_image(data)))
    return {'dj_id': document['dj_id'], 'source': source, 'editable': True, 'active': False,
            'dj_name': document['dj_name'], 'tagline': document.get('tagline', ''), 'accent': document.get('accent'),
            'aliases': list(document.get('aliases', [])), 'links': [dict(link) for link in document.get('links', [])],
            'page_url': document.get('page_url'), 'note': document.get('note', ''), 'logos': logos, 'tile': tile,
            'track': _track_view(root, document)}


def read(volume):
    """Every DJ profile on the stick, in discovery order (standard §5)."""
    root = _volume(volume)
    index = _load_index(root)
    profiles, problems = [], []
    previews = {'preferred': [], 'tile': [], 'secondary': []}
    active = None
    legacy = False
    if index:
        active = index['active']
        for entry in index['profiles']:
            try:
                document, _ = _load_document(root, _profile_path(entry['dj_id']))
                if document['dj_id'] != entry['dj_id']:
                    raise ProfileError('profile.json names a different DJ ID than the index')
                profiles.append(_profile_view(root, document, 'vjtools', previews))
            except (OSError, ValueError) as exc:
                problems.append({'dj_id': entry['dj_id'], 'dj_name': entry['dj_name'], 'error': str(exc)})
    elif _exists(root, LEGACY_PATH):
        try:
            document, _ = _load_document(root, LEGACY_PATH)
            profiles.append(_profile_view(root, document, 'legacy', previews))
            active, legacy = document['dj_id'], True
        except (OSError, ValueError) as exc:
            problems.append({'dj_id': None, 'dj_name': None, 'error': f'{LEGACY_PATH}: {exc}'})
    colour = stick_colour(root)
    taken = {_ascii_lower(profile['dj_name']) for profile in profiles}
    for name, logo_path in _folder_profiles(root, taken, MAX_PROFILES - len(profiles)):
        try:
            data = _read(root, logo_path, MAX_LOGO_BYTES)
            reference = {'path': logo_path, 'bytes': len(data), 'sha256': _sha(data), 'label': 'logo', 'note': ''}
            logo = _logo_view(root, reference, previews['preferred'])
        except (OSError, ValueError) as exc:
            problems.append({'dj_id': _folder_id(name), 'dj_name': name, 'error': str(exc)})
            continue
        profiles.append({'dj_id': _folder_id(name), 'source': 'folder', 'editable': False, 'active': False,
                         'dj_name': name, 'tagline': '', 'accent': colour, 'aliases': [], 'links': [], 'page_url': None,
                         'note': '', 'logos': [logo], 'tile': None, 'track': None})
    if active is None and any(profile['source'] == 'folder' for profile in profiles):
        active = next(profile['dj_id'] for profile in profiles if profile['source'] == 'folder')
    for profile in profiles:
        profile['active'] = profile['dj_id'] == active
    _fill_previews(previews['preferred'] + previews['tile'] + previews['secondary'])
    return {'format': 'xz-dj-profiles/1', 'public_folder': _public_folder(root),
            'revision': index['revision'] if index else None, 'active': active, 'stick_colour': colour,
            'legacy': legacy, 'profiles': profiles, 'problems': problems}


def summary(volume):
    """Profile count and active name from index.json alone, for the USB overview."""
    index = _load_index(_volume(volume))
    if not index:
        return {'count': 0, 'active_name': None}
    name = next(entry['dj_name'] for entry in index['profiles'] if entry['dj_id'] == index['active'])
    return {'count': len(index['profiles']), 'active_name': name}


# --- saving (standard §4) ---

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
    if isinstance(dj_id, str) and dj_id.startswith(FOLDER_ID):
        dj_id = None
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
    aliases = [_label(alias.strip(), 'Each alias', MAX_ALIAS)
               for alias in _list(fields.get('aliases') or [], 'Aliases', MAX_ALIASES)
               if _string(alias, 'Alias').strip()]
    logos = []
    for logo in _list(request.get('logos') or [], 'Logos', MAX_LOGOS):
        if not isinstance(logo, dict):
            raise ProfileError('Each logo needs a path')
        label = _string(logo.get('label') or '', 'Logo label').strip()
        if len(label) > MAX_NAME or _control(label):
            raise ProfileError('Logo labels must be at most 160 printable characters')
        logos.append({'path': _string(logo.get('path'), 'Logo path'), 'label': label,
                      'note': _note(logo.get('note') or '', 'Logo note', MAX_LOGO_NOTE_BYTES)})
    expected = request.get('expected_revision')
    if expected is not None:
        _string(expected, 'Expected revision')
    make_active = request.get('make_active', False)
    if type(make_active) is not bool:
        raise ProfileError('make_active must be true or false')
    return {'dj_id': dj_id,
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
    prepared, total = [], 0
    for index, logo in enumerate(logos):
        source = _logo_source(root, logo['path'])
        _check_logo_size(source.stat().st_size)
        data = source.read_bytes()
        _check_logo_size(len(data))
        image, kind = _decode(data)
        total += len(data)
        if total > MAX_TOTAL_LOGO_BYTES:
            raise ProfileError('These logos add up to more than 24 MB. Remove one or use smaller files.')
        label = logo['label'] or ('Preferred logo' if index == 0 else f'Alternate {index}')
        prepared.append({'data': data, 'image': image, 'kind': kind, 'sha256': _sha(data), 'name': source.name,
                         'input_label': logo['label'], 'label': label, 'note': logo['note']})
    return prepared


def slug(value):
    """profile.rs slug(): ASCII letters, digits, '-' and '_' kept, everything else '_', 80 characters."""
    result = ''.join(c if c.isascii() and (c.isalnum() or c in '-_') else '_' for c in value[:80])
    return result if result.strip('_') else 'dj-logo'


def _new_dj_id(root, index):
    """A random UUID, as the Rust/desktop and XZ writers mint new profile IDs."""
    listed = {entry['dj_id'] for entry in index['profiles']} if index else set()
    while True:
        dj_id = str(uuid.uuid4())
        if dj_id not in listed and not _confined(root, f'{HOME}/profiles/{dj_id}').exists():
            return dj_id


def _content_revision(draft, dj_id, prepared):
    """The Rust writer's revision input, so the same content gets the same bundle id."""
    return _sha(compact([dj_id, draft['dj_name'], draft['note'],
                         [[logo['sha256'], logo['input_label'], logo['note']] for logo in prepared],
                         [draft['tagline'], draft['accent'], draft['aliases'], draft['links'], draft['page_url']]]))


def _document(draft, dj_id, revision, bundle, prepared, tile):
    references = [{'path': f'{bundle}/assets/{i + 1:03d}-{slug(logo["label"])}.{FORMATS[logo["kind"]][0]}',
                   'bytes': len(logo['data']), 'sha256': logo['sha256'], 'label': logo['label'], 'note': logo['note']}
                  for i, logo in enumerate(prepared)]
    document = {'schema': PROFILE_SCHEMA, 'version': 1, 'dj_id': dj_id, 'revision': revision,
                'dj_name': draft['dj_name'], 'note': draft['note'], 'bundle': bundle,
                'preferred_logo': references[0] if references else None, 'secondary_logos': references[1:]}
    if tile is not None:
        document['xz_tile'] = {'path': f'{bundle}/renditions/{TILE_FILE}', 'bytes': len(tile), 'sha256': _sha(tile),
                               'width': TILE_SIDE, 'height': TILE_SIDE, 'format': TILE_FORMAT}
    for key in ('tagline', 'accent', 'aliases', 'links', 'page_url'):
        if draft[key]:
            document[key] = draft[key]
    return validate_document(document)


def _manifest(draft, bundle_id, document, prepared, tile):
    files = [{'role': 'logo', 'path': reference['path'][len(document['bundle']) + 1:], 'name': logo['name'],
              'bytes': reference['bytes'], 'sha256': reference['sha256']}
             for reference, logo in zip(_logo_references(document), prepared)]
    if tile is not None:
        files.append({'role': 'rendition', 'path': f'renditions/{TILE_FILE}', 'name': TILE_FILE,
                      'bytes': len(tile), 'sha256': _sha(tile)})
    return {'schema': MANIFEST_SCHEMA, 'version': 1, 'id': bundle_id, 'artist': draft['dj_name'],
            'created_at': datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%S+00:00'), 'files': files}


def _logo_references(document):
    return ([document['preferred_logo']] if document.get('preferred_logo') else []) + document.get('secondary_logos', [])


def _verify_bundle(root, document, tile):
    for reference in _logo_references(document):
        data = _read(root, reference['path'], MAX_LOGO_BYTES)
        if len(data) != reference['bytes'] or _sha(data) != reference['sha256']:
            raise ProfileError(f'{reference["path"]} on the USB does not match the logo that was chosen')
    if tile is not None and _read(root, document['xz_tile']['path'], TILE_BYTES) != tile:
        raise ProfileError('The XZ tile on the USB does not match what was written')


def _write_bundle(root, bundle, manifest, document, prepared, tile):
    """Writes the bundle complete under a stage name and renames it into place. Never replaces one."""
    parent = _ensure_dir(root, bundle.rsplit('/', 1)[0])
    destination = _confined(root, bundle)
    if not destination.exists():
        stage = parent / f'.stage-{secrets.token_hex(8)}'
        stage.mkdir()
        try:
            (stage / 'assets').mkdir()
            for reference, logo in zip(_logo_references(document), prepared):
                _write_new(stage / 'assets' / reference['path'].rsplit('/', 1)[1], logo['data'])
            if tile is not None:
                (stage / 'renditions').mkdir()
                _write_new(stage / 'renditions' / TILE_FILE, tile)
            _write_new(stage / 'manifest.json', pretty(manifest))
            os.rename(stage, destination)
        except BaseException:
            shutil.rmtree(stage, ignore_errors=True)
            raise
    _verify_bundle(root, document, tile)


def _publish_pointer(root, dj_id, pointer):
    """profile.previous.json gets the old pointer, then profile.json is replaced."""
    relative = _profile_path(dj_id)
    if _exists(root, relative):
        previous = _read(root, relative, MAX_JSON)
        if previous != pointer:
            _publish(root, f'{HOME}/profiles/{dj_id}/profile.previous.json', previous)
    _publish(root, relative, pointer)


def _write_index(root, index):
    data = pretty(validate_index(index))
    if len(data) > MAX_JSON:
        raise ProfileError('Profile index JSON exceeds 64 KiB')
    _publish(root, INDEX_PATH, data)


def _unchanged_track(root, dj_id, revision):
    """The old track record when the content it was made from is being saved again."""
    try:
        old, _ = _load_document(root, _profile_path(dj_id))
    except (OSError, ValueError):
        return None
    return old.get('profile_track') if old['revision'] == revision else None


def _check_revision(root, expected):
    index = _load_index(root)
    if (index['revision'] if index else None) != expected:
        raise ProfileError('The DJ profiles on this USB changed since they were loaded (maybe on the XZ). Reload the USB and try again.')
    return index


def _locked(root):
    return exclusive_lock(root / LOCK, 'Another XZ Mods window is writing to this USB')


def save(volume, request):
    root = _volume(volume)
    draft = parse_draft(request)
    with _locked(root):
        index = _check_revision(root, draft['expected_revision'])
        dj_id = draft['dj_id'] or _new_dj_id(root, index)
        if index and all(entry['dj_id'] != dj_id for entry in index['profiles']) and len(index['profiles']) >= MAX_PROFILES:
            raise ProfileError(f'This USB already holds {MAX_PROFILES} DJ profiles')
        prepared = _prepare_logos(root, draft['logos'])
        tile = render_tile(prepared[0]['image']) if prepared else None
        revision = _content_revision(draft, dj_id, prepared)
        bundle_id = f'{revision[:8]}-{revision[8:12]}-{revision[12:16]}-{revision[16:20]}-{revision[20:32]}'
        bundle = f'{HOME}/profiles/{dj_id}/bundles/{bundle_id}'
        document = _document(draft, dj_id, revision, bundle, prepared, tile)
        pointer = pretty(document)
        if len(pointer) > MAX_JSON:
            raise ProfileError('This profile is too large for profile.json (64 KiB). Shorten the note or links.')
        _write_bundle(root, bundle, _manifest(draft, bundle_id, document, prepared, tile), document, prepared, tile)
        track = _unchanged_track(root, dj_id, revision)
        if track:
            pointer = pretty({**document, 'profile_track': track})
        _publish_pointer(root, dj_id, pointer)
        if index is None:
            updated = {'schema': INDEX_SCHEMA, 'version': 1, 'revision': '', 'active': dj_id,
                       'profiles': [{'dj_id': dj_id, 'dj_name': draft['dj_name']}]}
        else:
            updated = copy.deepcopy(index)
            entry = next((entry for entry in updated['profiles'] if entry['dj_id'] == dj_id), None)
            if entry is None:
                updated['profiles'].append({'dj_id': dj_id, 'dj_name': draft['dj_name']})
            else:
                entry['dj_name'] = draft['dj_name']
            if draft['make_active']:
                updated['active'] = dj_id
        if updated != index:
            updated['revision'] = _index_revision()
            _check_revision(root, draft['expected_revision'])
            _write_index(root, updated)
    result = read(root)
    result['saved'] = dj_id
    return result


def set_active(volume, dj_id, expected_revision):
    """Rewrites index.json only, keeping its unknown fields."""
    root = _volume(volume)
    _id(dj_id)
    with _locked(root):
        index = _check_revision(root, expected_revision)
        if index is None or all(entry['dj_id'] != dj_id for entry in index['profiles']):
            raise ProfileError('Save this DJ profile to the USB before making it active')
        if index['active'] != dj_id:
            index['active'] = dj_id
            index['revision'] = _index_revision()
            _write_index(root, index)
    return read(root)


# --- the profile track: silent MPEG-1 Layer III with the profile in ID3v2.4 ---

TRACK_SECONDS = 20
FRAME_SAMPLES = 1152
SAMPLE_RATE = 44100
# 128 kbps, 44.1 kHz, no padding: 144 * 128000 / 44100 = 417 bytes. Every frame is identical.
FRAME_HEADER = bytes((0xFF, 0xFB, 0x90, 0x40))  # MPEG-1 Layer III, no CRC, 128k, 44.1k, joint stereo
FRAME_BYTES = 417
SILENT_FRAME = FRAME_HEADER + bytes(FRAME_BYTES - 4)
TRACK_FRAMES = -(-TRACK_SECONDS * SAMPLE_RATE // FRAME_SAMPLES)
MAX_TAG_BYTES = 32 * 1024 * 1024
FAT_FORBIDDEN = '<>:"/\\|?*'
APIC_FRONT_COVER = 3
APIC_ARTIST_LOGOTYPE = 0x13


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
    """The profile JSON the track carries: profile.json's content, compact, without its own track record."""
    return compact({key: value for key, value in document.items() if key != 'profile_track'})


def track_frames(document, logos):
    """The ID3 frames for the profile track, in order, and the labels of logos that did not fit."""
    links = document.get('links', [])
    epk = next((link for link in links if link['kind'].lower() in EPK_KINDS), None)
    website = next((link['url'] for link in links if link['kind'].lower() == 'website'), None)
    page = document.get('page_url')
    payload = track_payload(document)
    marker = f'djprofile:1:{document["dj_id"]}:{_sha(payload)[:12]}'
    shared = page or (epk and epk['url'])
    name = document['dj_name']
    frames = [_text_frame('TIT2', name), _text_frame('TPE1', name), _text_frame('TALB', 'DJ PROFILE'),
              _text_frame('TCON', 'DJ Profile'), _text_frame('TPE2', name),
              ('COMM', b'\x03eng\0' + _utf8(marker + (' ' + shared if shared else '')))]
    if page or website:
        frames.append(('WOAR', _url(page or website)))
    used = set()
    if epk:
        frames.append(('WXXX', b'\x03' + _utf8(_unique('EPK', used)) + b'\0' + _url(epk['url'])))
    for link in links:
        if link is not epk:
            frames.append(('WXXX', b'\x03' + _utf8(_unique(link['kind'], used)) + b'\0' + _url(link['url'])))
    frames.append(('TXXX', b'\x03DJPROFILE\0' + payload))
    size = 10 + sum(10 + len(body) for _, body in frames)
    skipped, embedded, descriptions = [], 0, set()
    for i, logo in enumerate(logos):
        picture_type = APIC_FRONT_COVER if i == 0 else APIC_ARTIST_LOGOTYPE
        description = '' if i == 0 else _unique(logo['label'] or f'logo {i + 1}', descriptions)
        body = (b'\x03' + logo['mime'].encode('ascii') + b'\0' + bytes((picture_type,))
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


def write_track(document, logos, destination):
    """Writes `DJ PROFILE - <name>.mp3` into the destination folder and proves it before publishing it.
    logos: [{'data', 'mime', 'label'}], preferred first."""
    validate_document(document)
    folder = _safe_path(destination)
    folder.mkdir(parents=True, exist_ok=True)
    if not folder.is_dir():
        raise ProfileError('The track folder is not a folder')
    frames, embedded, skipped = track_frames(document, logos)
    data = id3_tag(frames) + SILENT_FRAME * TRACK_FRAMES
    parsed, offset = read_id3(data)
    if parsed != frames:
        raise ProfileError('The profile track tags did not read back as written')
    check_silent_audio(data, offset)
    path = folder / track_name(document['dj_name'])
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
    return {'path': str(path), 'bytes': len(data), 'sha256': _sha(data),
            'duration_seconds': round(TRACK_FRAMES * FRAME_SAMPLES / SAMPLE_RATE, 3),
            'logos_embedded': embedded, 'logos_skipped': skipped, 'verified_silent': True,
            'ffmpeg_verified': decoded is not None}


def _saved_profile(root, dj_id):
    """(document, its stick-relative path, its bytes) for a listed profile, or the legacy one."""
    index = _load_index(root)
    if index and any(entry['dj_id'] == dj_id for entry in index['profiles']):
        relative = _profile_path(dj_id)
    elif not index and _exists(root, LEGACY_PATH):
        relative = LEGACY_PATH
    else:
        raise ProfileError('Save this DJ profile to the USB before making its track')
    document, raw = _load_document(root, relative)
    if document['dj_id'] != dj_id:
        raise ProfileError('Save this DJ profile to the USB before making its track')
    return document, relative, raw


def write_profile_track(volume, dj_id, destination, folder=None):
    """The profile track for a profile saved on the stick, into a PC folder or the stick's track folder."""
    root = _volume(volume)
    _id(dj_id)
    if destination not in ('folder', 'usb'):
        raise ProfileError("Choose where the track goes: 'folder' or 'usb'")
    with _locked(root):
        document, relative, raw = _saved_profile(root, dj_id)
        logos = []
        for reference in _logo_references(document):
            data = _read(root, reference['path'], MAX_LOGO_BYTES)
            if _sha(data) != reference['sha256'].lower():
                raise ProfileError(f'{reference["path"]} does not match its recorded SHA-256')
            logos.append({'data': data, 'mime': FORMATS[_image_format(data)][1], 'label': reference.get('label', '')})
        if destination == 'folder':
            target = Path(folder) if folder else Path.home() / 'Music' / 'DJ Profile'
            result = write_track(document, logos, target)
        else:
            if relative == LEGACY_PATH:
                raise ProfileError('Save this profile once in XZ Mods so it moves to DJPROFILE, then make its track')
            track_folder = f'{HOME}/profiles/{dj_id}/track'
            result = write_track(document, logos, _ensure_dir(root, track_folder))
            record = {'path': f'{track_folder}/{Path(result["path"]).name}', 'bytes': result['bytes'], 'sha256': result['sha256']}
            if _read(root, relative, MAX_JSON) != raw:
                raise ProfileError('The profile changed while its track was written. Reload the USB and try again.')
            # Only the track record changes, so profile.previous.json keeps the last real edit.
            _publish(root, relative, pretty({**document, 'profile_track': record}))
    return {'format': 'xz-dj-profile-track/1', 'destination': destination, **result}
