# SPDX-License-Identifier: MIT
"""Publish seven rendered roles as an OverCue stems USB bundle (overcue-index/1, BETA).

Stdlib only. Writes nothing outside <usb>/CDJMODS, publishes index.json last,
and never replaces another writer's entry. mods/audio/overcue_file.c is the
acceptance reader, so the manifest carries every field it requires.
"""
from __future__ import annotations
from concurrent.futures import ThreadPoolExecutor
import copy
import hashlib
import json
import math
import os
from pathlib import Path
from .host import executable
import re
import shutil
import struct
import tempfile
import zlib
import subprocess
from . import cache
from .jobs import exclusive_lock

ROLES = ("vocal", "instrumental", "drums", "harmonics", "vocals-drums", "vocals-harmonics", "full-mix")
PAGE_BYTES = 131072
MAX_PAGES = 4096
MAX_FRAMES = MAX_PAGES * PAGE_BYTES // 4
MAX_COMPRESSED = PAGE_BYTES + 4096  # overcue_file.c's compressed page buffer
MAX_JSON = 4 * 1024 * 1024
INDEX_SCHEMA = "overcue-index/1"
MANIFEST_SCHEMA = "overcue-stems/4"
WRITER = "XZ Mods"
REFUSAL = "OverCue already prepared this track. XZ Mods left it unchanged."
ID_TAKEN = ("Another OverCue entry already uses this Rekordbox track id for a different file. "
            "XZ Mods left it unchanged.")
STAGING = ".xz-mods-staging-"
BUNDLE = re.compile(r"[0-9a-f]{16}")


def role_file(role):
    return f"stems-sidecar-{role}.s16le.pgz"


def bundle_name(pcm_sha256):
    """First 16 hex of SHA-256('three-part/1:' + role digests in ROLES order joined by ':')."""
    return hashlib.sha256(("three-part/1:" + ":".join(pcm_sha256[role] for role in ROLES)).encode()).hexdigest()[:16]


def pack_role(raw, target, check=lambda: None):
    """Stream raw s16le into OVPGZ001: big-endian header and table, independent zlib pages."""
    size = Path(raw).stat().st_size
    if not 0 < size <= MAX_FRAMES * 4 or size % 4:
        raise ValueError("Role PCM must be whole stereo frames within the 4096-page limit")
    count = -(-size // PAGE_BYTES)
    header = b"OVPGZ001" + struct.pack(">IIQ", PAGE_BYTES, count, size)
    table, pcm, offset = bytearray(), hashlib.sha256(), 24 + 48 * count
    with open(raw, "rb") as source, open(target, "xb") as output:
        output.seek(offset)
        for number in range(count):
            check()
            page = source.read(PAGE_BYTES)
            if len(page) != min(PAGE_BYTES, size - number * PAGE_BYTES):
                raise ValueError("Role PCM changed while packing")
            packed = zlib.compress(page, 6)
            if len(packed) > MAX_COMPRESSED:
                raise ValueError("Compressed page exceeds the device reader limit")
            table += struct.pack(">QII", offset, len(packed), len(page)) + hashlib.sha256(page).digest()
            pcm.update(page)
            output.write(packed)
            offset += len(packed)
        if source.read(1):
            raise ValueError("Role PCM changed while packing")
        output.seek(0)
        output.write(header + table)
        output.flush()
        os.fsync(output.fileno())
    return {"sha256": pcm.hexdigest(), "page_table_sha256": hashlib.sha256(header + table).hexdigest()}


def verify_role(path, pcm_sha256, frames):
    """Decode every page as the device does; return the page-table SHA-256."""
    path = cache._regular(path)
    total = frames * 4
    with path.open("rb") as source:
        size = source.seek(0, 2)
        source.seek(0)
        header = source.read(24)
        if len(header) == 24 and header[:8] == b"OVPGZ003":
            page_bytes, count, stored = struct.unpack(">IIQ", header[8:])
            if page_bytes != PAGE_BYTES or stored != total or not 0 < count <= MAX_PAGES or count != -(-total // PAGE_BYTES):
                raise ValueError(f"{path.name}: invalid FLAC page geometry")
            table = source.read(48 * count)
            if len(table) != 48 * count:
                raise ValueError(f"{path.name}: truncated table")
            table_sha = hashlib.sha256(header + table).hexdigest()
            checker = cache._regular(executable(os.environ['XZ_BUILDER_RESOURCES'],'xz-overcue-check'))
            result = subprocess.run([str(checker), '--role', str(path), str(frames), pcm_sha256, table_sha],
                                    capture_output=True, text=True, encoding='utf-8', timeout=120,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            if result.returncode or json.loads(result.stdout).get('verified') is not True:
                raise ValueError(f"{path.name}: FLAC audio failed native verification")
            return table_sha
        if len(header) != 24 or header[:8] != b"OVPGZ001":
            raise ValueError(f"{path.name}: invalid header")
        page_bytes, count, stored = struct.unpack(">IIQ", header[8:])
        if page_bytes != PAGE_BYTES or stored != total or not 0 < count <= MAX_PAGES or count != -(-total // PAGE_BYTES):
            raise ValueError(f"{path.name}: invalid size or page count")
        table = source.read(48 * count)
        if len(table) != 48 * count:
            raise ValueError(f"{path.name}: truncated table")
        full, expected_offset = hashlib.sha256(), 24 + 48 * count
        for number in range(count):
            offset, compressed, expanded = struct.unpack_from(">QII", table, number * 48)
            if (offset != expected_offset or not 0 < compressed <= MAX_COMPRESSED or offset + compressed > size
                    or expanded != min(PAGE_BYTES, total - number * PAGE_BYTES)):
                raise ValueError(f"{path.name}: invalid page {number} bounds")
            source.seek(offset)
            decoder = zlib.decompressobj()
            try:
                page = decoder.decompress(source.read(compressed), expanded + 1)
            except zlib.error as error:
                raise ValueError(f"{path.name}: invalid page {number} data") from error
            if (not decoder.eof or decoder.unconsumed_tail or decoder.unused_data or len(page) != expanded
                    or hashlib.sha256(page).digest() != table[number * 48 + 16:number * 48 + 48]):
                raise ValueError(f"{path.name}: page {number} failed verification")
            full.update(page)
            expected_offset += compressed
        if expected_offset != size or full.hexdigest() != pcm_sha256:
            raise ValueError(f"{path.name}: audio does not match the rendered role")
    return hashlib.sha256(header + table).hexdigest()


def _read_bytes(path, what):
    path = cache._regular(path)
    if path.stat().st_size > MAX_JSON:
        raise ValueError(f"{what} exceeds 4 MiB")
    return path.read_bytes()


def _parse_json(data, what):
    def unique(pairs):
        keys = [key for key, _ in pairs]
        if len(set(keys)) != len(keys):
            raise ValueError(f"{what} has duplicate keys")
        return dict(pairs)
    value = json.loads(data.decode("utf-8"), object_pairs_hook=unique)
    if not isinstance(value, dict):
        raise ValueError(f"{what} is not a JSON object")
    return value


def read_index(mods):
    path = cache._safe_path(mods / "index.json")
    if not path.exists():
        return {"schema": INDEX_SCHEMA, "keyed_by": "export.pdb", "tracks": {}, "tracks_onelibrary": {}}, None
    raw = _read_bytes(path, "CDJMODS/index.json")
    index = _parse_json(raw, "CDJMODS/index.json")
    if index.get("schema") != INDEX_SCHEMA:
        raise ValueError("CDJMODS/index.json uses an unknown schema. XZ Mods left it unchanged.")
    for name in ("tracks", "tracks_onelibrary"):
        table = index.get(name, {})
        if not isinstance(table, dict) or not all(isinstance(entry, dict) for entry in table.values()):
            raise ValueError("CDJMODS/index.json has an invalid track map. XZ Mods left it unchanged.")
    return index, raw


def owned_by_xz_mods(stems, bundle):
    if not isinstance(bundle, str) or not BUNDLE.fullmatch(bundle):
        return False
    try:
        manifest = _parse_json(_read_bytes(stems / bundle / "overcue-manifest.json", "manifest"), "manifest")
    except (OSError, ValueError):
        return False
    writer = manifest.get("writer")
    return isinstance(writer, dict) and writer.get("name") == WRITER


def replaceable(stems, bundle):
    """An index entry XZ Mods may drop: its bundle is ours, or its folder is gone so nothing is lost."""
    if not isinstance(bundle, str) or not BUNDLE.fullmatch(bundle):
        return False
    try:
        if not cache._safe_path(stems / bundle).exists():
            return True
    except (OSError, ValueError):
        return False
    return owned_by_xz_mods(stems, bundle)


def conflicts(index, track_id, file_path, bundle=None):
    """Entries to remove so tracks[track_id] -> (bundle, file_path) is the only entry for this file.

    Readers require every entry for a file_path to agree, so duplicates under other ids are stale too.
    """
    key, found = str(track_id), []
    for name in ("tracks", "tracks_onelibrary"):
        for entry_key, entry in index.get(name, {}).items():
            current = (name, entry_key) == ("tracks", key)
            if current and (entry.get("bundle"), entry.get("file_path")) == (bundle, file_path):
                continue
            if current or entry.get("file_path") == file_path:
                found.append((name, entry_key))
    return found


def _require_replaceable(index, stale, file_path, replaceable):
    for name, entry_key in stale:
        entry = index[name][entry_key]
        if not replaceable(entry.get("bundle")):
            raise ValueError(REFUSAL if entry.get("file_path") == file_path else ID_TAKEN)


def merge(index, track_id, entry, replaceable):
    """Return (new index or None when already current, replaced_previous). Refuse foreign entries."""
    key, stale = str(track_id), conflicts(index, track_id, entry["file_path"], entry["bundle"])
    if not stale and key in index.get("tracks", {}):
        return None, False
    _require_replaceable(index, stale, entry["file_path"], replaceable)
    merged = copy.deepcopy(index)
    for name, entry_key in stale:
        del merged[name][entry_key]
    merged.setdefault("tracks", {})[key] = entry
    return merged, bool(stale)


def check_writable(usb_root, file_path, track_id):
    """Fail before a long separation if another writer already owns this track."""
    mods = cache._safe_path(Path(usb_root) / "CDJMODS")
    if mods.exists():
        index, stems = read_index(mods)[0], cache._safe_path(mods / "stems")
        _require_replaceable(index, conflicts(index, track_id, file_path), file_path,
                             lambda bundle: replaceable(stems, bundle))


def _sweep(mods, stems):
    """Remove crash leftovers; only this writer creates these names, and only under the USB lock."""
    for leftover in [*stems.glob(STAGING + "*"), *mods.glob(".index.json.*.tmp")]:
        try:
            cache._safe_path(leftover)
        except ValueError:
            continue
        if leftover.is_dir():
            shutil.rmtree(leftover)
        else:
            leftover.unlink()


def _read_roles(roles_dir):
    roles_dir = cache._safe_path(roles_dir)
    result = _parse_json(_read_bytes(roles_dir / "result.json", "Role render result"), "Role render result")
    frames, gain = result.get("frames"), result.get("headroom_gain")
    if (type(frames) is not int or not 0 < frames <= MAX_FRAMES or result.get("sample_rate") != 96000
            or not isinstance(gain, (int, float)) or not math.isfinite(gain) or not 0 < gain <= 1
            or not isinstance(result.get("roles"), dict) or set(result["roles"]) != set(ROLES)):
        raise ValueError("Role render result is invalid")
    files = {}
    for role in ROLES:
        record = result["roles"][role]
        path = cache._regular(roles_dir / Path(record["file"]).name)
        if path.stat().st_size != frames * 4 or not re.fullmatch(r"[0-9a-f]{64}", record.get("sha256", "")):
            raise ValueError(f"{role}: rendered PCM is invalid")
        files[role] = path
    return result, files


def _existing_bundle(folder, pcm, frames, source_sha256):
    """Reuse only a bundle whose every page decodes to our PCM and whose manifest the device accepts."""
    tables = {role: verify_role(folder / role_file(role), pcm[role], frames) for role in ROLES}
    manifest = _parse_json(_read_bytes(folder / "overcue-manifest.json", "manifest"), "Existing bundle manifest")
    runtime, roles = manifest.get("runtime", {}), manifest.get("roles", {})
    valid = (manifest.get("schema") == MANIFEST_SCHEMA and isinstance(runtime, dict) and isinstance(roles, dict)
             and (runtime.get("sample_rate"), runtime.get("channels"), runtime.get("format"), runtime.get("frames")) == (96000, 2, "s16le", frames)
             and isinstance(manifest.get("source"), dict) and manifest["source"].get("sha256") == source_sha256
             and all(isinstance(roles.get(role), dict) and roles[role].get("bytes") == frames * 4
                     and roles[role].get("page_table_sha256") == tables[role]
                     and roles[role].get("loudness_gain") == 1
                     for role in ROLES))
    if not valid:
        raise ValueError(f"CDJMODS/stems/{folder.name} already exists with different content. XZ Mods left it unchanged.")
    return tables


def _write_json(path, value, compact=False):
    text = json.dumps(value, ensure_ascii=False, **({"separators": (",", ":")} if compact else {"indent": 1}))
    data = text.encode("utf-8") + b"\n"
    if len(data) > MAX_JSON:
        raise ValueError(f"{path.name} would exceed 4 MiB")
    with open(path, "wb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())


def publish(usb_root, file_path, track_id, source_sha256, source_codec, roles_dir, separation, version, job, page_python=None):
    if page_python is None:
        return _publish(usb_root, file_path, track_id, source_sha256, source_codec, roles_dir, separation, version, job)
    with tempfile.TemporaryDirectory(prefix='xz-flac-pages-') as temporary:
        packed = Path(temporary) / 'pages'
        script = cache._regular(Path(os.environ['XZ_BUILDER_RESOURCES']) / 'inference/overcue_flac_pack.py')
        job.progress('pack', 'Encoding lossless FLAC stem pages')
        job.run([page_python, '-I', script, '--roles', roles_dir, '--output', packed])
        return _publish(usb_root, file_path, track_id, source_sha256, source_codec, roles_dir,
                        separation, version, job, packed)


def _publish(usb_root, file_path, track_id, source_sha256, source_codec, roles_dir, separation, version, job, packed=None):
    """Pack, verify, and index one track. Returns the receipt fields shared by both entry points."""
    result, files = _read_roles(roles_dir)
    frames, headroom = result["frames"], float(result["headroom_gain"])
    pcm = {role: result["roles"][role]["sha256"] for role in ROLES}
    prepared_tables = {role: verify_role(packed / role_file(role), pcm[role], frames) for role in ROLES} if packed else None
    codec = 'flac-96k' if packed else 'zlib'
    bundle = hashlib.sha256(('three-part/2:' + codec + ':' + ':'.join(pcm[role] for role in ROLES) + ':' +
                            ':'.join(prepared_tables[role] for role in ROLES)).encode()).hexdigest()[:16] if packed else bundle_name(pcm)
    mods = cache._safe_path(Path(usb_root) / "CDJMODS")
    mods.mkdir(exist_ok=True)
    stems = cache._safe_path(mods / "stems")
    stems.mkdir(exist_ok=True)
    is_replaceable = lambda other: replaceable(stems, other)
    with exclusive_lock(cache._safe_path(mods / ".xz-mods.lock"), "Another XZ Mods window is writing stems to this USB"):
        _sweep(mods, stems)
        index, raw_index = read_index(mods)
        _require_replaceable(index, conflicts(index, track_id, file_path, bundle), file_path, is_replaceable)
        folder = cache._safe_path(stems / bundle)
        tables = None
        if folder.exists():
            job.progress("verify", "Checking the stems already on this USB")
            try:
                tables = _existing_bundle(folder, pcm, frames, source_sha256)
            except (OSError, ValueError):
                if not owned_by_xz_mods(stems, bundle):
                    raise ValueError(f"CDJMODS/stems/{bundle} already exists with different content. "
                                     "XZ Mods left it unchanged.") from None
                # Our own damaged or outdated bundle: keep it aside rather than delete, then rebuild.
                os.rename(folder, stems / f".xz-mods-replaced-{bundle}-{os.urandom(4).hex()}")
        if tables is None:
            job.progress("pack", "Writing OverCue stem files to the USB")
            staging = Path(tempfile.mkdtemp(prefix=STAGING, dir=stems))
            try:
                if packed:
                    for role in ROLES:
                        job.check();shutil.copyfile(packed / role_file(role), staging / role_file(role))
                else:
                    with ThreadPoolExecutor(max_workers=min(len(ROLES), os.cpu_count() or 1)) as pool:
                        records = dict(zip(ROLES, pool.map(lambda role: pack_role(files[role], staging / role_file(role), job.check), ROLES)))
                    if any(records[role]["sha256"] != pcm[role] for role in ROLES):
                        raise ValueError("Rendered role changed while packing")
                job.progress("verify", "Reading back every stem page")
                tables = {role: verify_role(staging / role_file(role), pcm[role], frames) for role in ROLES}
                # loudness_gain stays 1: the device aligns the stored full mix to its own decode, and that
                # scale already undoes the headroom. Dividing by it again would play every stem too loud.
                _write_json(staging / "overcue-manifest.json", {
                    "schema": MANIFEST_SCHEMA,
                    "writer": {"name": WRITER, "version": version, "beta": True},
                    "source": {"file_path": file_path, "sha256": source_sha256, "codec": source_codec, "sample_rate": 44100},
                    "runtime": {"sample_rate": 96000, "channels": 2, "format": "s16le", "frames": frames,
                                "latency_pad_frames": 0, "headroom_gain": headroom, "page_codec": codec},
                    "separation": separation,
                    "roles": {role: {"file": role_file(role), "bytes": frames * 4, "sha256": pcm[role],
                                     "page_table_sha256": tables[role], "loudness_gain": 1.0} for role in ROLES}})
                job.check()
                cache._publish_new(staging, folder)
            finally:
                if staging.exists():
                    shutil.rmtree(cache._safe_path(staging))
            rebuilt = True
        else:
            rebuilt = False
        entry = {"bundle": bundle, "file_path": file_path, "frames": frames, "three_part": 1, "page_bytes": PAGE_BYTES, "page_codec": codec}
        for role in ROLES:
            key = role.replace("-", "_")
            entry[key + "_sha256"] = pcm[role]
            entry[key + "_page_table_sha256"] = tables[role]
        entry.update(source_sha256=source_sha256, separation=separation)
        merged, replaced = merge(index, track_id, entry, is_replaceable)
        if merged is not None:
            job.check()
            index_path = cache._safe_path(mods / "index.json")
            current = cache._regular(index_path).read_bytes() if index_path.exists() else None
            if current != raw_index:
                raise ValueError("CDJMODS/index.json changed while XZ Mods was working. Try again.")
            handle, pending = tempfile.mkstemp(prefix=".index.json.", suffix=".tmp", dir=mods)
            os.close(handle)
            pending = Path(pending)
            try:
                _write_json(pending, merged, compact=True)
                os.replace(pending, index_path)
            finally:
                if pending.exists():
                    pending.unlink()
        published, _ = read_index(mods)
        if published.get("tracks", {}).get(str(track_id), {}).get("bundle") != bundle or conflicts(published, track_id, file_path, bundle):
            raise ValueError("CDJMODS/index.json did not read back as written")
    return {"bundle": bundle, "frames": frames, "reused": merged is None and not rebuilt,
            "replaced_previous": replaced, "verified": True, "page_codec": codec}
