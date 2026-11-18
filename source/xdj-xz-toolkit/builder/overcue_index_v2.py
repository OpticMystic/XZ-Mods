# SPDX-License-Identifier: MIT
"""Bounded OverCue USB index. Call publication helpers under the USB writer lock."""
import hashlib
import json
import os
import re
import shutil
import struct
import tempfile
import uuid
from pathlib import Path

from . import cache

GENERATION = re.compile(r"[0-9a-f]{32}")


def _require(ok, message):
    if not ok:
        raise ValueError("index-v2: " + message)


def record(namespace, key, entry):
    keep = {k: v for k, v in entry.items()
            if k in ("bundle", "frames", "three_part", "page_bytes") or k.endswith("_sha256")}
    return json.dumps({"schema": "overcue-index/1",
                       "keyed_by": "export.pdb" if namespace == 1 else "exportLibrary.db",
                       "tracks": {key: keep}}, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("utf-8")


def bucket(track, bits):
    return ((track * 2654435761) & 0xffffffff) >> (32 - bits) if bits else 0


def prepare(index):
    """Validate representability before index.json changes; return bits and bucket records."""
    _require(index.get("schema") == "overcue-index/1", "unknown schema")
    keying = index.get("keyed_by")
    _require(keying in (None, "export.pdb", "exportLibrary.db"), "unknown keyed_by")
    if keying == "exportLibrary.db":
        _require(not ("tracks" in index and "tracks_onelibrary" in index), "ambiguous OneLibrary maps")
        maps = {1: {}, 2: index.get("tracks_onelibrary", index.get("tracks", {}))}
    else:
        maps = {1: index.get("tracks", {}), 2: index.get("tracks_onelibrary", {})}
    rows = []
    for ns, table in maps.items():
        _require(isinstance(table, dict), "invalid track map")
        seen = set()
        for key, entry in table.items():
            _require(isinstance(key, str) and re.fullmatch(r"[0-9]+", key) is not None,
                     f"track key {key!r} is not a positive u32 decimal ID")
            track = int(key)
            _require(0 < track <= 0xffffffff, f"track key {key!r} is not a positive u32 decimal ID")
            _require(track not in seen, f"duplicate numeric track ID {key!r}")
            seen.add(track)
            _require(isinstance(entry, dict), "invalid track entry")
            data = record(ns, key, entry)
            _require(0 < len(data) <= 8192, f"record for track {key} exceeds 8192 bytes")
            rows.append((ns, track, data))
    # Aim at <=128 rows per bucket; never publish a bucket over the hard 256 limit.
    for bits in range(25):
        groups = {}
        for ns, track, data in rows:
            groups.setdefault((ns, bucket(track, bits)), []).append((track, data))
        largest = max(map(len, groups.values()), default=0)
        if largest <= 128 or bits == 24 and largest <= 256:
            return bits, groups
    raise ValueError("index-v2: cannot fit tracks within 256 rows per bucket")


def _write(path, data):
    path = cache._safe_path(path)
    with path.open("xb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())


def _sync_dir(path):
    # Windows does not expose directory fsync through os.open. File fsync still uses FlushFileBuffers.
    if os.name != "nt":
        fd = os.open(cache._safe_path(path), os.O_RDONLY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


def mark_pending(mods):
    path = cache._safe_path(mods / "index-v2-pending")
    if path.exists():
        cache._regular(path)
    else:
        _write(path, b"Rebuild index-v2 from index.json\n")
    _sync_dir(mods)


def _marker(data):
    _require(len(data) == 64 and data[:8] == b"OCSIDX02" and any(data[8:24]) and
             data[24:28] == struct.pack(">I", 1) and int.from_bytes(data[28:32], "big") <= 24 and
             hashlib.sha256(data[:32]).digest() == data[32:], "invalid current marker")
    return data[8:24], int.from_bytes(data[28:32], "big")


def _check_record(raw, ns, track):
    try:
        value = json.loads(raw)
        tracks = value["tracks"]
        _require(isinstance(tracks, dict) and len(tracks) == 1, "invalid record JSON")
        key, entry = next(iter(tracks.items()))
        _require(re.fullmatch(r"[0-9]+", key) and int(key) == track and isinstance(entry, dict) and
                 record(ns, key, entry) == raw, "invalid record JSON")
    except (UnicodeError, KeyError, TypeError, AttributeError, json.JSONDecodeError) as error:
        raise ValueError("index-v2: invalid record JSON") from error


def _read_generation(directory, marker):
    generation, bits = _marker(marker)
    result = {}
    for ns in (1, 2):
        folder = cache._safe_path(directory / generation.hex() / str(ns))
        _require(folder.is_dir(), "missing namespace")
        for first in folder.iterdir():
            _require(re.fullmatch(r"[0-9A-F]{2}", first.name) and cache._safe_path(first).is_dir(), "invalid bucket path")
            for second in first.iterdir():
                _require(re.fullmatch(r"[0-9A-F]{2}", second.name) and cache._safe_path(second).is_dir(), "invalid bucket path")
                for path in second.iterdir():
                    _require(re.fullmatch(r"[0-9A-F]{2}\.IDX", path.name), "invalid bucket path")
                    number = int(first.name + second.name + path.stem, 16)
                    _require(number < 1 << bits, "bucket exceeds bits")
                    path = cache._regular(path)
                    _require(path.stat().st_size <= 96 + 256 * (48 + 8192), "bucket too large")
                    data = path.read_bytes()
                    _require(len(data) >= 96 and data[:8] == b"OCSIBK02" and data[8:24] == generation and
                             data[72:96] == bytes(24), "invalid bucket header")
                    lib, got, count, size = struct.unpack(">IIII", data[24:40])
                    table = data[96:96 + count * 48]
                    _require(lib == ns and got == number and 1 <= count <= 256 and size == len(data) and
                             len(table) == count * 48 and hashlib.sha256(data[:40] + table).digest() == data[40:72],
                             "invalid bucket table")
                    end, previous = 96 + count * 48, 0
                    for at in range(0, len(table), 48):
                        track, offset, length, flags = struct.unpack_from(">IIII", table, at)
                        raw = data[offset:offset + length]
                        _require(previous < track and bucket(track, bits) == number and offset == end and
                                 0 < length <= 8192 and flags == 1 and len(raw) == length and
                                 hashlib.sha256(raw).digest() == table[at + 16:at + 48], "invalid record")
                        _check_record(raw, ns, track)
                        result[ns, track] = raw
                        end, previous = end + length, track
                    _require(end == len(data), "unreferenced bucket bytes")
    return result


def read(directory):
    """Read all active records, checking every binary bound, order and digest."""
    directory = cache._safe_path(directory)
    path = cache._regular(directory / "current")
    _require(path.stat().st_size == 64, "invalid current marker")
    marker = path.read_bytes()
    result = _read_generation(directory, marker)
    _require(cache._regular(path).read_bytes() == marker, "generation changed while reading")
    return result


def publish(mods, prepared):
    """Build and read back a fresh generation, then activate it. Never inspect or alter old trees."""
    directory = cache._safe_path(mods / "index-v2")
    _require(directory.is_dir(), "missing index-v2 directory")
    bits, groups = prepared
    wanted = {(ns, track): data for (ns, _), entries in groups.items() for track, data in entries}
    pending = cache._safe_path(mods / "index-v2-pending")
    previous = None
    try:
        previous = _marker(cache._regular(directory / "current").read_bytes())[0].hex()
        if read(directory) == wanted:
            # The live generation already holds exactly these bindings: publishing another copy only adds clutter.
            if pending.exists():
                cache._regular(pending).unlink()
                _sync_dir(mods)
            return
    except (OSError, ValueError):
        pass  # Missing, damaged or stale: rebuild from index.json below.
    generation = uuid.uuid4().bytes
    tree = cache._safe_path(directory / generation.hex())
    tree.mkdir()  # Never reuse a directory, even in the event of an ID collision.
    expected = {}
    for ns in (1, 2):
        (tree / str(ns)).mkdir()
    for (ns, number), entries in groups.items():
        table, payload = bytearray(), bytearray()
        offset = 96 + 48 * len(entries)
        for track, data in sorted(entries):
            table += struct.pack(">IIII", track, offset + len(payload), len(data), 1) + hashlib.sha256(data).digest()
            payload += data
            expected[ns, track] = data
        head = b"OCSIBK02" + generation + struct.pack(">IIII", ns, number, len(entries), offset + len(payload))
        path = tree / str(ns) / f"{number >> 16:02X}" / f"{(number >> 8) & 255:02X}" / f"{number & 255:02X}.IDX"
        path.parent.mkdir(parents=True, exist_ok=True)
        _write(path, head + hashlib.sha256(head + table).digest() + bytes(24) + table + payload)
    for parent, _, _ in os.walk(tree, topdown=False):
        _sync_dir(Path(parent))
    _sync_dir(directory)
    head = b"OCSIDX02" + generation + struct.pack(">II", 1, bits)
    marker = head + hashlib.sha256(head).digest()
    _require(_read_generation(directory, marker) == expected, "generation failed readback")
    current = cache._safe_path(directory / "current")
    if current.exists():
        cache._regular(current)
    fd, temporary = tempfile.mkstemp(prefix=".current.", suffix=".tmp", dir=directory)
    os.close(fd)
    temporary = Path(temporary)
    try:
        with temporary.open("wb") as output:
            output.write(marker)
            output.flush()
            os.fsync(output.fileno())
        _require(temporary.read_bytes() == marker, "marker failed readback")
        os.replace(temporary, current)
        _sync_dir(directory)
        _require(read(directory) == expected, "active generation failed readback")
        cache._regular(mods / "index-v2-pending").unlink()
        _sync_dir(mods)
    finally:
        if temporary.exists():
            temporary.unlink()
    _prune(directory, {generation.hex(), previous})


def _prune(directory, keep):
    """Keep the new and the previously active generation (FORMAT.md); remove older generation trees only."""
    for child in directory.iterdir():
        if GENERATION.fullmatch(child.name) and child.name not in keep and child.is_dir() and not child.is_symlink():
            try:
                shutil.rmtree(cache._safe_path(child))
            except OSError:
                pass  # A leftover tree is harmless; the next publication retries.
