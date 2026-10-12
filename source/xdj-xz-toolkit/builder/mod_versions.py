"""Archive complete mod payloads and resolve an explicit, persistent loader choice."""
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import tempfile
import uuid

from . import cache
from .engines import data_root
from .jobs import exclusive_lock

SCHEMA = 'xz-mod-bundle/1'
ID = re.compile('[0-9a-f]{64}')
OTA_GATE = b'if [ -x /dev/shm/xz-updater ]; then'
PINNED_GATE = b'if false; then # XZ Mods: selected version is pinned; skip OTA apply'
LIMIT = 512 * 1024 * 1024


def _need(ok, message):
    if not ok:
        raise ValueError(message)


def _hash(path):
    with cache._regular(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def _json(path):
    path = cache._regular(path)
    _need(path.stat().st_size <= 2 * 1024 * 1024, 'Mod metadata is too large.')
    return json.loads(path.read_text(encoding='utf-8'))


def _write(path, value):
    path = cache._safe_path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.mod-choice-', delete=False) as output:
        temporary = Path(output.name)
        output.write((json.dumps(value, indent=2) + '\n').encode())
        output.flush()
        os.fsync(output.fileno())
    try:
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def _root():
    root = cache._safe_path(data_root() / 'mod-versions')
    root.mkdir(parents=True, exist_ok=True)
    return root


def _allowed(name):
    p = PurePosixPath(name)
    return (not p.is_absolute() and '..' not in p.parts and '\\' not in name and ':' not in name
            and (name == 'bootstrap.sh' or p.parts[0] in ('runtime', 'branding', 'licenses')))


def _files(resources):
    resources = cache._safe_path(resources)
    paths = [resources / 'bootstrap.sh']
    for name in ('runtime', 'branding'):
        folder = cache._safe_path(resources / name)
        if folder.exists():
            for p in folder.rglob('*'):
                cache._safe_path(p)
                if p.is_file():
                    paths.append(p)
    licenses = cache._safe_path(resources / 'licenses')
    if licenses.is_dir():
        paths.extend(p for p in licenses.iterdir() if p.is_file())
    _need(0 < len(paths) <= 2048, 'Unexpected mod payload file count.')
    total = 0
    result = {}
    for path in paths:
        path = cache._regular(path)
        total += path.stat().st_size
        _need(total <= LIMIT, 'Mod payload exceeds the 512 MiB limit.')
        result[path.relative_to(resources).as_posix()] = {'bytes': path.stat().st_size, 'sha256': _hash(path)}
    return dict(sorted(result.items()))


def _identity(files):
    return hashlib.sha256(json.dumps(files, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def _payload(resources):
    manifest = _json(resources / 'runtime/manifest.json')
    _need(manifest.get('firmware') == 'XDJ-XZ 1.26', 'This builder supports XDJ-XZ 1.26 mod bundles only.')
    for name, field in (('libxz-mods.so', 'runtime_sha256'), ('libxz-receiver.so', 'receiver_sha256')):
        path = cache._regular(resources / 'runtime' / name)
        with path.open('rb') as source:
            header = source.read(20)
        _need(len(header) == 20 and header[:6] == b'\x7fELF\x01\x01' and struct.unpack_from('<H', header, 18)[0] == 40,
              f'{name} is not a 32-bit ARM library.')
        _need(_hash(path) == manifest.get(field), f'Mod bundle integrity failed: {name}')
    for name, field in (('xz-doom', 'doom_sha256'), ('xz-updater', 'ota_updater_sha256'),
                        ('xz-runtime-smoke', 'ota_runtime_smoke_sha256'), ('source.zip', 'source_zip_sha256'),
                        ('settings-schema.json', 'settings_schema_sha256')):
        if manifest.get(field):
            _need(_hash(resources / 'runtime' / name) == manifest[field], f'Mod bundle integrity failed: {name}')
    boot = cache._regular(resources / 'bootstrap.sh').read_bytes().replace(b'\r\n', b'\n')
    if manifest.get('bootstrap_sha256'):
        _need(_hash(resources / 'bootstrap.sh') == manifest['bootstrap_sha256'], 'Mod bundle startup script does not match its manifest.')
    _need(boot.startswith(b'#!/bin/sh\n'), 'The mod bundle has no valid startup script.')
    _need(boot.count(OTA_GATE) <= 1, 'The startup script has ambiguous network-update gates.')
    if (resources / 'runtime/xz-updater').exists():
        _need(boot.count(OTA_GATE) == 1, 'This startup script does not support version pinning.')
    return manifest


def _verify(folder):
    folder = cache._safe_path(folder)
    record = _json(folder / 'bundle.json')
    files = record.get('files')
    _need(record.get('schema') == SCHEMA and isinstance(files, dict) and files and len(files) <= 2048,
          'Invalid archived mod bundle.')
    _need(all(isinstance(n, str) and _allowed(n) for n in files), 'Invalid path in the mod bundle.')
    _need(record.get('id') == _identity(files), 'Mod bundle identity does not match its file list.')
    for key in ('label', 'description', 'channel'):
        _need(isinstance(record.get(key), str) and len(record[key]) <= 500, 'Invalid mod version description.')
    _need(_files(folder) == files, 'Archived mod files changed. Import a verified copy before using this version.')
    _payload(folder)
    return record


def _archive(resources, *, label, channel, description, replace_corrupt=False):
    resources = cache._safe_path(resources)
    manifest = _payload(resources)
    files = _files(resources)
    ident = _identity(files)
    root = _root()
    target = root / ident
    if target.exists():
        try:
            return _verify(target)
        except (OSError, ValueError):
            if not replace_corrupt:
                raise
    record = {'schema': SCHEMA, 'id': ident, 'label': label[:160], 'channel': channel,
              'description': description[:500], 'firmware': 'XDJ-XZ 1.26',
              'runtime_sha256': manifest['runtime_sha256'], 'receiver_sha256': manifest['receiver_sha256'],
              'source_commit': manifest.get('source_commit', ''), 'hardware_qualified': False,
              'archived_at': datetime.now(timezone.utc).isoformat(), 'files': files}
    with tempfile.TemporaryDirectory(prefix='.incoming-', dir=root) as work:
        staging = Path(work) / 'bundle'
        staging.mkdir()
        for name in files:
            out = staging / name
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(cache._regular(resources / name), out)
        _write(staging / 'bundle.json', record)
        _verify(staging)
        if target.exists():
            quarantine = cache._safe_path(root / ('damaged-' + ident + '-' + uuid.uuid4().hex))
            cache._safe_path(target).rename(quarantine)
        staging.rename(target)
    return record


def _selection(root):
    path = root / 'selection.json'
    value = _json(path)
    _need(isinstance(value.get('id'), str) and ID.fullmatch(value['id']) and type(value.get('pinned')) is bool,
          'Saved mod choice is invalid. Choose a version again.')
    return {**value, 'revision': _hash(path)}


def inventory(resources):
    resources = cache._safe_path(resources)
    root = _root()
    with exclusive_lock(root / '.versions.lock', 'Another XZ Mods window is changing mod versions'):
        seeds = resources / 'mod-version-seeds'
        preferred = None
        if (seeds / 'catalog.json').exists():
            catalog = _json(seeds / 'catalog.json')
            _need(catalog.get('schema') == 'xz-mod-seeds/1' and isinstance(catalog.get('versions'), list)
                  and len(catalog['versions']) <= 64, 'Invalid bundled mod catalog.')
            for item in catalog['versions']:
                _need(isinstance(item.get('id'), str) and ID.fullmatch(item['id']), 'Invalid bundled mod identifier.')
                destination = root / item['id']
                if not destination.exists():
                    source = seeds / item['id']
                    record = _verify(source)
                    _need(record['id'] == item['id'], 'Bundled version identity mismatch.')
                    _archive(source, label=record['label'], channel=record['channel'], description=record['description'])
            preferred = catalog.get('preferred_id')
        native = _payload(resources)
        bundled = _archive(resources, label='Included development build ' + native['runtime_sha256'][:8], channel='development',
                           description='Mod runtime included with this app. Physical controller stability is not certified.')
        choice = root / 'selection.json'
        if not choice.exists():
            default = preferred if isinstance(preferred, str) and (root / preferred / 'bundle.json').is_file() else bundled['id']
            _write(choice, {'id': default, 'pinned': True})
        versions = []
        for folder in root.iterdir():
            if not ID.fullmatch(folder.name):
                continue
            # List metadata cheaply; verify every byte again before selection/build.
            entry = _json(folder / 'bundle.json')
            _need(entry.get('id') == folder.name, 'Archived mod identity mismatch.')
            versions.append({k: v for k, v in entry.items() if k not in ('files', 'schema')})
        return {'format': 'xz-mod-versions/1', 'versions': sorted(versions, key=lambda v: v['label']),
                'selected': _selection(root), 'bundled_id': bundled['id'], 'storage': str(root)}


def choose(resources, request):
    inventory(resources)
    root = _root()
    with exclusive_lock(root / '.versions.lock', 'Another XZ Mods window is changing mod versions'):
        _need(_selection(root)['revision'] == request.get('expected_revision'), 'The saved choice changed. Reload mod versions.')
        ident = request.get('mod_version_id')
        _need(isinstance(ident, str) and ID.fullmatch(ident), 'Choose a listed mod version.')
        _need(type(request.get('pinned')) is bool, 'Choose whether to keep this mod version pinned.')
        _verify(root / ident)
        _write(root / 'selection.json', {'id': ident, 'pinned': request['pinned']})
    return inventory(resources)


def import_version(resources, folder):
    folder = cache._safe_path(folder)
    if (folder / 'resources/runtime/manifest.json').is_file():
        folder = folder / 'resources'
    root = _root()
    with exclusive_lock(root / '.versions.lock', 'Another XZ Mods window is changing mod versions'):
        record = _archive(folder, label='Imported ' + folder.parent.name, channel='imported',
                          description='Imported from a local resource folder. Hashes checked; origin and controller stability are unverified.', replace_corrupt=True)
    return {**inventory(resources), 'imported_id': record['id']}


BRANDING_IMAGES = ('splash.rgb565', 'logo.rgb565', 'MD5SUMS')


def current_branding(resources, view):
    """Give every loader this app's splash and logo, whichever mod version it carries.

    Archived versions keep the images they were saved with, so an older build would bring
    back an old splash. The images only patch the firmware pack through apply.sh, so they
    are swapped only when both sides use the same apply.sh."""
    current, target = Path(resources) / 'branding', Path(view) / 'branding'
    names = ('apply.sh', *BRANDING_IMAGES)
    if not all((current / name).is_file() for name in names) or not (target / 'apply.sh').is_file():
        return False
    if (current / 'apply.sh').read_bytes() != (target / 'apply.sh').read_bytes():
        return False
    for name in BRANDING_IMAGES:
        shutil.copyfile(cache._regular(current / name), target / name)
    return True


@contextmanager
def selected_resources(resources, request, job):
    state = inventory(resources)
    selected = state['selected']
    ident = request.get('mod_version_id', selected['id'])
    pinned = request.get('pin_mod_version', selected['pinned'])
    _need(ident == selected['id'] and pinned == selected['pinned'], 'The saved mod choice changed. Reload it before building a loader.')
    _need(isinstance(ident, str) and ID.fullmatch(ident) and type(pinned) is bool, 'Invalid mod selection.')
    source = _root() / ident
    record = _verify(source)
    job.progress('mod-version', 'Verifying ' + record['label'])
    with tempfile.TemporaryDirectory(prefix='xz-selected-mod-') as work:
        view = Path(work)
        for name in record['files']:
            job.check()
            target = view / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(cache._regular(source / name), target)
        _write(view / 'bundle.json', record)
        _verify(view)
        current_branding(resources, view)
        boot_path = view / 'bootstrap.sh'
        boot = boot_path.read_bytes().replace(b'\r\n', b'\n')
        if pinned:
            # The fixed loader never applies an OTA slot implicitly. Its matched
            # runtime/receiver remain unchanged; the generated boot policy is recorded.
            boot = boot.replace(OTA_GATE, PINNED_GATE)
        boot_path.write_bytes(boot)
        version = {k: record[k] for k in ('id', 'label', 'channel', 'runtime_sha256', 'receiver_sha256', 'source_commit', 'firmware')}
        version.update(format='xz-mod-loader-version/1', pinned=pinned,
                       bootstrap_sha256=hashlib.sha256(boot).hexdigest(), base_bootstrap_sha256=record['files']['bootstrap.sh']['sha256'])
        _write(view / 'mod-version.json', version)
        yield view, version


def record_loader(result, version):
    digest = result.get('sha256') or result.get('revision', {}).get('sha256')
    _need(isinstance(digest, str) and ID.fullmatch(digest), 'Verified loader digest is missing.')
    _write(_root() / 'loaders' / (digest + '.json'), {'loader_sha256': digest, 'mod_version': version})


def lookup_loader(revision):
    digest = revision.get('sha256')
    if not isinstance(digest, str) or not ID.fullmatch(digest):
        return None
    path = cache._safe_path(data_root() / 'mod-versions' / 'loaders' / (digest + '.json'))
    if not path.exists():
        return None
    record = _json(path)
    return record.get('mod_version') if record.get('loader_sha256') == digest else None


def settings_resources(volume, fallback):
    from . import managed_usb
    recorded = lookup_loader(managed_usb.revision(Path(volume) / 'autoexec.bin'))
    if not recorded:
        return fallback
    ident = recorded.get('id')
    _need(isinstance(ident, str) and ID.fullmatch(ident), 'Recorded mod version is invalid.')
    folder = _root() / ident
    _verify(folder)
    return folder


def settings_change(volume, selected, current):
    from . import managed_usb, usb_settings
    path = cache._safe_path(Path(volume) / managed_usb.SETTINGS)
    expected = managed_usb.revision(path)
    if not expected['exists']:
        return None
    _need(expected['bytes'] <= 511, 'The saved settings record is too large for these mod versions; it was preserved.')
    original = cache._regular(path).read_bytes()
    target_schema = usb_settings.contract(selected)
    try:
        usb_settings.parse(original, target_schema)
        return None
    except ValueError:
        pass
    source_schema = usb_settings.contract(current)
    values = usb_settings.parse(original, source_schema)
    target = usb_settings.defaults(target_schema)
    changed = []
    for field in target_schema['fields']:
        key = field['key']
        if key in values and field['min'] <= values[key] <= field['max']:
            target[key] = values[key]
        elif key in values:
            changed.append(key)
    data = usb_settings.serialize(target, target_schema)
    # A failed/interrupted loader switch must leave a profile the current build
    # can still read. Refuse conversions that do not meet this condition.
    usb_settings.parse(data, source_schema)
    return {'required': True, 'expected': expected, 'data': data,
            'removed_fields': sorted(values.keys() - target.keys()), 'reset_fields': changed,
            'summary': 'This older mod needs a compatible settings profile. Your original profile will be backed up; supported settings are retained.'}


def inspect_settings(resources, volume):
    state = inventory(resources)
    selected = _root() / state['selected']['id']
    _verify(selected)
    change = settings_change(volume, selected, resources)
    return {'format': 'xz-mod-settings/1', 'mod_version_id': state['selected']['id'],
            'settings_change': {k: v for k, v in change.items() if k != 'data'} if change else None}
