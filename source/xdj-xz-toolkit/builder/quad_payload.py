# SPDX-License-Identifier: MIT
"""Validate an explicitly pinned public quad closure before staging any payload."""
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import zipfile

PIN_PATH = Path(__file__).with_name('quad-profile.sha256')
PROFILE_SHA256 = PIN_PATH.read_text().strip() if PIN_PATH.is_file() else '2cd08884c2377e3c7ed66da6f96368376d50304a71743c588a06d948557ccf0b'
RETAINED_PROFILES = frozenset({'a6ff51c7027fd7e4d173951836c808a27dac60ffdf900e30da2deeb04bbca6ff',
    'a0a7b0a35c42de9fe1e920767175ca1a1e1c668d1ca8ab2d5b9d764ccc0a25a6',
    'aa3eebdc9ea8aa2a4ee617ac2a33341a8dd9a6ae6f85d1f4c3a0766906877ba6',
    'd612f61330eca4c2b6448d150268c315538cfdc18a290f19bad5e2d3f5675bdd',
    '86e320f351c9069e261d80c7017b10367943eda8db3ea8731db160270a34aa6e',
    'ad6920e0e633b89ea5043b0734a63966883b098ab538ec801efc3c2c59cdaf7f',
    '49b4d38c611c16a8bf71c5a9746b0d2ded298c6dd46f9957b44f08065e549c26',
    '95b74d4bc5793aa65f38c4e4653457758c65146023e7fad991c25f43ba963415',
    'bdc0cc2b1ea1bc7c36f5cdf20e04a2759e6ada2b292ff3c546752aa483fe31cd',
    '69a2b76e9029f44a5df531826f006fea155918a73e70052355b3e5c5b9943821',
    'd6845d4f5d8e069e93229dc5eed741d83f8113d5d2b2df6e79232d0b373319cb',
    'c620ad3027f9a0d55f05e9854e75773a87c30308a1a30ab03c3ad4ad016d5ec6'})
REQUIRED = {'bootstrap.sh', 'runtime/libxz-mods.so', 'runtime/libxz-receiver.so',
            'runtime/source.zip', 'runtime/quad/xz-auto-bridge',
            'runtime/quad/xz_hub_depth.ko', 'runtime/quad/detach'}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def safe_name(name):
    p = PurePosixPath(name)
    if not name or not p.parts or p.as_posix() != name or p.is_absolute() or '..' in p.parts or '\\' in name or ':' in name:
        raise ValueError('Unsafe quad resource path')
    if p.suffix.lower() in ('.key', '.upd', '.wad', '.pth', '.ckpt', '.safetensors') or p.name.lower() in ('rbp', 'rbp.patched', 'autoexec.bin', 'imagedata.dat', 'xdjxz_v126.zip'):
        raise ValueError('Prohibited public resource')

def _read(root, name):
    safe_name(name)
    path = root / name
    if not path.resolve().is_relative_to(root.resolve()) or any(p.is_symlink() or (hasattr(p, 'is_junction') and p.is_junction()) for p in [path, *path.parents]) or not path.is_file():
        raise ValueError('Missing or unsafe quad resource: ' + name)
    return path.read_bytes()

def verified_files(resources, expected=None):
    root = Path(resources)
    profile_path = root / 'runtime/quad-package.json'
    manifest = json.loads(_read(root, 'runtime/manifest.json')) if (root / 'runtime/manifest.json').exists() else {}
    if not profile_path.exists():
        if manifest.get('quad_package_sha256') or (root / 'runtime/quad').exists() or any(marker in _read(root, 'bootstrap.sh') for marker in (b'AUTO_BRIDGE', b'xz-auto-bridge', b'auto-bridge/')):
            raise ValueError('Quad profile missing')
        return None
    raw = _read(root, 'runtime/quad-package.json')
    digest = sha(raw)
    trusted = {expected} if expected is not None else ({PROFILE_SHA256} | RETAINED_PROFILES)
    if not re.fullmatch('[0-9a-f]{64}', digest) or digest not in trusted or manifest.get('quad_package_sha256') != digest:
        raise ValueError('Quad profile digest mismatch')
    profile = json.loads(raw)
    if profile['schema'] != 'xz-quad-package/2' or profile['firmware'] != 'XDJ-XZ 1.26' or profile['distribution'] != 'public-beta' or profile['profile'] != 'experimental-quad':
        raise ValueError('Incorrect quad contract/profile')
    if profile.get('restrictions') or profile.get('music_removal_supported') is not False:
        raise ValueError('Private binding or unsupported music-removal claim')
    if not REQUIRED <= profile['resources'].keys():
        raise ValueError('Incomplete quad helper closure')
    if manifest != {**profile['runtime_manifest'], 'quad_package_sha256': digest}:
        raise ValueError('Quad runtime manifest changed')
    files = {name: _read(root, name) for name in profile['resources']}
    original = profile['bootstrap_utf8'].encode('utf-8')
    if not original.startswith(b'#!/bin/sh\n') or b'\r' in original or sha(original) != profile['resources']['bootstrap.sh']:
        raise ValueError('Original bootstrap identity mismatch')
    from .mod_versions import OTA_GATE, PINNED_GATE
    if original.count(PINNED_GATE) == 1 and original.count(OTA_GATE) == 0:
        if not profile['runtime_manifest'].get('ota_unavailable_reason'):
            raise ValueError('A fixed quad bootstrap must declare its USB-only update policy')
        if (root / 'mod-version.json').is_file() and json.loads(_read(root, 'mod-version.json')).get('pinned') is not True:
            raise ValueError('This quad build cannot allow network updates')
    if files['bootstrap.sh'] != original:
        version = json.loads(_read(root, 'mod-version.json'))
        transformed = original.replace(b'\r\n', b'\n').replace(OTA_GATE, PINNED_GATE)
        if (original.count(OTA_GATE) != 1 or files['bootstrap.sh'] != transformed or version.get('pinned') is not True
                or version.get('base_bootstrap_sha256') != sha(original) or version.get('bootstrap_sha256') != sha(transformed)):
            raise ValueError('Unpermitted quad bootstrap transformation')
    for name, data in files.items():
        if name != 'bootstrap.sh' and sha(data) != profile['resources'][name]:
            raise ValueError('Quad resource hash mismatch: ' + name)
    expected_runtime = {n for n in files if n.startswith('runtime/')} | {'runtime/manifest.json', 'runtime/quad-package.json'}
    actual_runtime = {p.relative_to(root).as_posix() for p in (root / 'runtime').rglob('*') if p.is_file()}
    if expected_runtime != actual_runtime:
        raise ValueError('Quad runtime file membership mismatch')
    for field, name in [('runtime_sha256', 'runtime/libxz-mods.so'), ('receiver_sha256', 'runtime/libxz-receiver.so'), ('bootstrap_sha256', 'bootstrap.sh'), ('source_zip_sha256', 'runtime/source.zip')]:
        if manifest.get(field) != profile['resources'][name]:
            raise ValueError('Incorrect paired manifest: ' + field)
    if manifest.get('source_commit') != profile['source_commit'] or manifest.get('firmware') != profile['firmware']:
        raise ValueError('Incorrect paired source/firmware')
    contract = profile['contract']
    if sha(files[contract['path']]) != contract['sha256']:
        raise ValueError('Incorrect bridge contract')
    for component in ('runtime', 'receiver', 'bridge', 'module', 'detach'):
        evidence = profile['components'][component]
        if not evidence['evidence_refs'] or sha(files[evidence['resource']]) != evidence['sha256'] or not evidence['source_paths'] or not evidence['license_paths']:
            raise ValueError('Incomplete component evidence: ' + component)
        if not set(evidence['source_paths'] + evidence['license_paths']) <= profile['source_files'].keys():
            raise ValueError('Missing corresponding source/license: ' + component)
    with zipfile.ZipFile(io.BytesIO(files['runtime/source.zip'])) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or set(names) != profile['source_files'].keys():
            raise ValueError('Corresponding source membership mismatch')
        for name in names:
            safe_name(name)
            if (archive.getinfo(name).external_attr >> 16) & 0o170000 == 0o120000 or sha(archive.read(name)) != profile['source_files'][name]:
                raise ValueError('Corresponding source identity mismatch')
    for dest, source in profile['payload'].items():
        safe_name(dest)
        if source not in files or dest in ('rbp.patched', 'mods-mode'):
            raise ValueError('Unsafe quad payload mapping')
    required_payload = {'autoexec.sh':'bootstrap.sh', 'tools/libxz-mods.so':'runtime/libxz-mods.so', 'tools/libxz-directfb-hook.so':'runtime/libxz-receiver.so', 'licenses/xz-quad-source.zip':'runtime/source.zip', **{'tools/auto-bridge/'+n:'runtime/quad/'+n for n in ('xz-auto-bridge','xz_hub_depth.ko','detach')}}
    if any(profile['payload'].get(k) != v for k,v in required_payload.items()):
        raise ValueError('Incomplete quad payload mapping')
    return profile, files

def payload_files(resources, application, bootstrap):
    verified = verified_files(resources)
    if verified is None:
        return {}
    profile, files = verified
    if sha(application) != profile['application_sha256']:
        raise ValueError('Quad requires exact XDJ-XZ 1.26 application identity')
    if Path(bootstrap).read_bytes() != files['bootstrap.sh']:
        raise ValueError('Bootstrap path is not the verified resource')
    result = {dest: files[source] for dest,source in profile['payload'].items()}
    for name,data in list(result.items()):
        if name.startswith('tools/') and '/auto-bridge/' not in name:
            result[name+'.md5'] = (hashlib.md5(data).hexdigest()+'  '+Path(name).name+'\n').encode('ascii')
    return result

def install(prepared, resources, expected):
    verified = verified_files(prepared, expected)
    if verified is None:
        raise ValueError('Explicit quad closure required')
    _, files = verified
    files.update({n:_read(Path(prepared), n) for n in ('runtime/manifest.json','runtime/quad-package.json')})
    target = Path(resources)
    existing = {p.relative_to(target).as_posix() for p in (target/'runtime').rglob('*') if p.is_file()}
    if existing - files.keys():
        raise ValueError('Clean runtime resources required')
    for name,data in files.items():
        p = target/name
        if not p.resolve().is_relative_to(target.resolve()) or any(q.is_symlink() for q in [p,*p.parents]):
            raise ValueError('Unsafe resource destination')
        p.parent.mkdir(parents=True,exist_ok=True)
        p.write_bytes(data)
    verified_files(target, expected)
