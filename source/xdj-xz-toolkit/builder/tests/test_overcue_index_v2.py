import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from builder import overcue_index_v2 as v2


def synthetic(count=600):
    entry = {"title": "ignored", "frames": 4, "bundle": "0123456789abcdef",
             "three_part": 1, "page_bytes": 131072, "custom_sha256": "é", "file_path": "/a"}
    return {"schema": "overcue-index/1", "keyed_by": "export.pdb",
            "tracks": {str(i): dict(entry) for i in range(1, count + 1)},
            "tracks_onelibrary": {"1": dict(entry, frames=8)}}


class IndexV2Tests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.mods = Path(self.temp.name)
        self.directory = self.mods / "index-v2"
        self.directory.mkdir()

    def publish(self, index):
        v2.mark_pending(self.mods)
        v2.publish(self.mods, v2.prepare(index))
        return (self.directory / "current").read_bytes()

    def test_roundtrip_both_namespaces_several_buckets_and_exact_records(self):
        index = synthetic()
        current = self.publish(index)
        self.assertEqual(len(current), 64)
        self.assertGreater(int.from_bytes(current[28:32], "big"), 0)
        actual = v2.read(self.directory)
        self.assertEqual(len(actual), 601)
        self.assertEqual(actual[1, 1],
                         b'{"schema":"overcue-index/1","keyed_by":"export.pdb","tracks":{"1":'
                         b'{"frames":4,"bundle":"0123456789abcdef","three_part":1,"page_bytes":131072,"custom_sha256":"\\u00e9"}}}')
        for ns, name in ((1, "tracks"), (2, "tracks_onelibrary")):
            for key, entry in index[name].items():
                self.assertEqual(actual[ns, int(key)], v2.record(ns, key, entry))
        self.assertFalse((self.mods / "index-v2-pending").exists())

    def test_legacy_onelibrary_and_empty_namespaces(self):
        index = synthetic(1)
        del index["tracks_onelibrary"]
        index["keyed_by"] = "exportLibrary.db"
        marker = self.publish(index)
        self.assertEqual(set(v2.read(self.directory)), {(2, 1)})
        self.assertEqual(list((self.directory / marker[8:24].hex() / "1").iterdir()), [])
        marker = self.publish({"schema": "overcue-index/1"})
        self.assertEqual(v2.read(self.directory), {})
        for ns in ("1", "2"):
            self.assertTrue((self.directory / marker[8:24].hex() / ns).is_dir())

    def test_previous_generation_retained_and_current_replaced_last(self):
        old = self.publish(synthetic(1))
        old_tree = self.directory / old[8:24].hex()
        snapshot = {p.relative_to(old_tree): p.read_bytes() for p in old_tree.rglob("*.IDX")}
        replace = v2.os.replace
        def activate(source, target):
            self.assertEqual(Path(target), self.directory / "current")
            self.assertEqual((self.directory / "current").read_bytes(), old)
            self.assertTrue((self.mods / "index-v2-pending").exists())
            marker = Path(source).read_bytes()
            self.assertEqual(len(v2._read_generation(self.directory, marker)), 601)
            replace(source, target)
        with patch.object(v2.os, "replace", side_effect=activate):
            self.publish(synthetic())
        self.assertEqual(snapshot, {p.relative_to(old_tree): p.read_bytes() for p in old_tree.rglob("*.IDX")})

    def test_identical_bindings_reuse_live_generation_and_old_generations_are_pruned(self):
        first = self.publish(synthetic(3))
        self.assertEqual(self.publish(synthetic(3)), first)  # unchanged bindings: no new copy
        self.assertFalse((self.mods / "index-v2-pending").exists())
        second = self.publish(synthetic(4))
        third = self.publish(synthetic(5))
        trees = {p.name for p in self.directory.iterdir() if p.is_dir()}
        self.assertEqual(trees, {second[8:24].hex(), third[8:24].hex()})  # new + previous only
        self.assertEqual(len(v2.read(self.directory)), 6)

    def test_invalid_ids_aliases_ambiguous_maps_and_record_limit(self):
        for key in ("abc", "0", "-1", "4294967296", " 1", "１"):
            index = synthetic(0)
            index["tracks"][key] = {}
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "positive u32"):
                v2.prepare(index)
        index = synthetic(1)
        index["tracks"]["01"] = {}
        with self.assertRaisesRegex(ValueError, "duplicate numeric"):
            v2.prepare(index)
        index = synthetic(1)
        index["keyed_by"] = "exportLibrary.db"
        with self.assertRaisesRegex(ValueError, "ambiguous"):
            v2.prepare(index)
        index = synthetic(1)
        index["tracks"]["1"]["huge_sha256"] = "x" * 8192
        with self.assertRaisesRegex(ValueError, "8192"):
            v2.prepare(index)

    def test_hard_bucket_limit_and_maximum_u32(self):
        index = synthetic(0)
        inverse = pow(2654435761, -1, 1 << 32)
        # All 256 hashes have the same high 24 bits, forcing the hard limit fallback.
        index["tracks"] = {str(((256 + i) * inverse) & 0xffffffff): {} for i in range(256)}
        marker = self.publish(index)
        self.assertEqual(int.from_bytes(marker[28:32], "big"), 24)
        self.assertEqual(len(v2.read(self.directory)), 257)
        index["tracks"] = {"4294967295": {}, "01": {}}
        self.publish(index)
        self.assertEqual(set(v2.read(self.directory)), {(1, 0xffffffff), (1, 1), (2, 1)})

    def test_marker_identity_bounds_and_generation_change(self):
        good = self.publish(synthetic(1))
        for offset, value in ((0, b"WRONGMAG"), (8, bytes(16)),
                              (24, struct.pack(">I", 0)), (28, struct.pack(">I", 25))):
            bad = bytearray(good)
            bad[offset:offset + len(value)] = value
            bad[32:] = hashlib.sha256(bad[:32]).digest()
            (self.directory / "current").write_bytes(bad)
            with self.assertRaises(ValueError):
                v2.read(self.directory)
        (self.directory / "current").write_bytes(good)
        original = v2._read_generation
        def switched(directory, marker):
            records = original(directory, marker)
            (directory / "current").write_bytes(b"changed")
            return records
        with patch.object(v2, "_read_generation", side_effect=switched):
            with self.assertRaisesRegex(ValueError, "generation changed"):
                v2.read(self.directory)

    def test_corrupt_marker_rejected_and_repaired(self):
        good = self.publish(synthetic(1))
        for position in (0, 8, 24, 28, 32, 63):
            bad = bytearray(good)
            bad[position] ^= 1
            (self.directory / "current").write_bytes(bad)
            with self.assertRaises(ValueError):
                v2.read(self.directory)
        (self.directory / "current").write_bytes(b"broken")
        self.publish(synthetic(2))
        self.assertEqual(len(v2.read(self.directory)), 3)

    def test_all_bucket_bounds_orders_and_digests(self):
        marker = self.publish(synthetic(2))
        path = next((self.directory / marker[8:24].hex() / "1").rglob("*.IDX"))
        good = path.read_bytes()
        # Recompute the table digest so row validation, rather than its checksum, rejects each case.
        def changed(offset, value, reseal=True):
            data = bytearray(good)
            data[offset:offset + len(value)] = value
            if reseal:
                count = int.from_bytes(data[32:36], "big")
                data[40:72] = hashlib.sha256(data[:40] + data[96:96 + count * 48]).digest()
            return data
        cases = [good[:95], good + b"x", changed(0, b"badmagic"), changed(8, bytes(16)),
                 changed(24, struct.pack(">I", 2)), changed(28, struct.pack(">I", 1)),
                 changed(32, struct.pack(">I", 0)), changed(32, struct.pack(">I", 257)),
                 changed(36, struct.pack(">I", len(good) + 1)), changed(72, b"x"),
                 changed(96, bytes(4)), changed(144, struct.pack(">I", 1)),
                 changed(100, struct.pack(">I", 0)), changed(104, bytes(4)),
                 changed(104, struct.pack(">I", 8193)), changed(108, bytes(4)),
                 changed(112, bytes(32)), changed(40, bytes(32), False),
                 good[:-1], good[:-1] + bytes([good[-1] ^ 1])]
        for number, data in enumerate(cases):
            with self.subTest(case=number):
                path.write_bytes(data)
                with self.assertRaises(ValueError):
                    v2.read(self.directory)
        path.write_bytes(good)
        self.assertEqual(len(v2.read(self.directory)), 3)

    def test_failed_readback_keeps_current_and_pending(self):
        old = self.publish(synthetic(1))
        v2.mark_pending(self.mods)
        with patch.object(v2, "_read_generation", return_value={}):
            with self.assertRaisesRegex(ValueError, "readback"):
                v2.publish(self.mods, v2.prepare(synthetic(2)))
        self.assertEqual((self.directory / "current").read_bytes(), old)
        self.assertTrue((self.mods / "index-v2-pending").exists())
