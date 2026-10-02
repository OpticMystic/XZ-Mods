import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from builder import overcue_writer as writer


class QuietJob:
    def check(self):
        pass

    def progress(self, stage, message):
        pass


def render_roles(folder, frames, seed=0):
    """Stand-in for overcue_roles.py output: seven raw s16le roles plus result.json."""
    folder.mkdir()
    roles = {}
    for number, role in enumerate(writer.ROLES):
        data = bytes((i * (number + 3) + seed) % 251 for i in range(frames * 4))
        (folder / f"{role}.s16le").write_bytes(data)
        roles[role] = {"file": f"{role}.s16le", "sha256": hashlib.sha256(data).hexdigest()}
    (folder / "result.json").write_text(json.dumps({"frames": frames, "sample_rate": 96000, "channels": 2,
        "format": "s16le", "headroom_gain": 0.5, "roles": roles}))
    return roles


class PackTests(unittest.TestCase):
    def test_pack_role_byte_layout(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            raw = bytes(range(256)) * 520  # 133120 bytes: one full page and a 2048-byte tail
            (root / "role.s16le").write_bytes(raw)
            record = writer.pack_role(root / "role.s16le", root / "role.pgz")
            data = (root / "role.pgz").read_bytes()
            self.assertEqual(data[:8], b"OVPGZ001")
            self.assertEqual(struct.unpack(">IIQ", data[8:24]), (131072, 2, len(raw)))
            offset = 24 + 2 * 48
            pages = [raw[:131072], raw[131072:]]
            for number, page in enumerate(pages):
                start, compressed, expanded = struct.unpack_from(">QII", data, 24 + number * 48)
                self.assertEqual((start, expanded), (offset, len(page)))
                self.assertEqual(data[24 + number * 48 + 16:24 + number * 48 + 48], hashlib.sha256(page).digest())
                self.assertEqual(zlib.decompress(data[start:start + compressed]), page)
                offset += compressed
            self.assertEqual(offset, len(data))
            self.assertEqual(record, {"sha256": hashlib.sha256(raw).hexdigest(),
                                      "page_table_sha256": hashlib.sha256(data[:24 + 2 * 48]).hexdigest()})
            self.assertEqual(writer.verify_role(root / "role.pgz", record["sha256"], len(raw) // 4),
                             record["page_table_sha256"])
            broken = bytearray(data)
            broken[-1] ^= 1
            (root / "role.pgz").write_bytes(broken)
            with self.assertRaises(ValueError):
                writer.verify_role(root / "role.pgz", record["sha256"], len(raw) // 4)

    def test_rejects_partial_frames_and_page_cap(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "odd.s16le").write_bytes(b"\0" * 6)
            with self.assertRaises(ValueError):
                writer.pack_role(root / "odd.s16le", root / "odd.pgz")
            self.assertEqual(writer.MAX_FRAMES * 4, 4096 * 131072)

    def test_bundle_name(self):
        digests = {role: f"{number:064x}" for number, role in enumerate(writer.ROLES)}
        text = "three-part/1:" + ":".join(f"{number:064x}" for number in range(7))
        self.assertEqual(writer.bundle_name(digests), hashlib.sha256(text.encode()).hexdigest()[:16])
        self.assertEqual(writer.ROLES, ("vocal", "instrumental", "drums", "harmonics",
                                        "vocals-drums", "vocals-harmonics", "full-mix"))


class MergeTests(unittest.TestCase):
    ENTRY = {"bundle": "0123456789abcdef", "file_path": "/Contents/a.wav"}

    def index(self, **tracks):
        return {"schema": "overcue-index/1", "keyed_by": "export.pdb", "updated": "kept", "vendor": {"x": [1]},
                "tracks": tracks, "tracks_onelibrary": {"5": {"bundle": "ffffffffffffffff", "file_path": "/Contents/b.wav"}}}

    def test_preserves_foreign_entries_and_unknown_fields(self):
        foreign = {"bundle": "aaaaaaaaaaaaaaaa", "file_path": "/Contents/b.wav", "title": "B", "extra": {"n": 1}}
        index = self.index(**{"7": foreign})
        merged, replaced = writer.merge(index, 12, self.ENTRY, lambda bundle: False)
        self.assertFalse(replaced)
        self.assertEqual(merged["tracks"], {"7": foreign, "12": self.ENTRY})
        self.assertEqual({k: v for k, v in merged.items() if k != "tracks"}, {k: v for k, v in index.items() if k != "tracks"})
        self.assertEqual(index["tracks"], {"7": foreign})

    def test_refuses_foreign_overcue_entry(self):
        for tracks in ({"12": {"bundle": "aaaaaaaaaaaaaaaa", "file_path": "/Contents/a.wav"}},
                       {"99": {"bundle": "aaaaaaaaaaaaaaaa", "file_path": "/Contents/a.wav"}},
                       {"99": dict(self.ENTRY)}):
            with self.assertRaisesRegex(ValueError, "^OverCue already prepared this track"):
                writer.merge(self.index(**tracks), 12, self.ENTRY, lambda bundle: False)
        with self.assertRaisesRegex(ValueError, "track id for a different file"):
            writer.merge(self.index(**{"12": {"bundle": "../../../../x", "file_path": "/Contents/other.wav"}}),
                         12, self.ENTRY, lambda bundle: False)

    def test_replaces_xz_mods_entries(self):
        old = {"bundle": "bbbbbbbbbbbbbbbb", "file_path": "/Contents/a.wav"}
        merged, replaced = writer.merge(self.index(**{"12": old, "40": dict(old)}), 12, self.ENTRY,
                                        lambda bundle: bundle == "bbbbbbbbbbbbbbbb")
        self.assertTrue(replaced)
        self.assertEqual(merged["tracks"], {"12": self.ENTRY})

    def test_duplicate_entries_for_one_file_are_collapsed(self):
        merged, replaced = writer.merge(self.index(**{"7": dict(self.ENTRY, separation="old")}), 12, self.ENTRY,
                                        lambda bundle: bundle == self.ENTRY["bundle"])
        self.assertTrue(replaced)
        self.assertEqual(merged["tracks"], {"12": self.ENTRY})
        self.assertEqual(writer.conflicts(merged, 12, "/Contents/a.wav", self.ENTRY["bundle"]), [])

    def test_missing_bundle_folder_is_replaceable(self):
        with tempfile.TemporaryDirectory() as temp:
            stems = Path(temp)
            self.assertTrue(writer.replaceable(stems, "aaaaaaaaaaaaaaaa"))
            (stems / "aaaaaaaaaaaaaaaa").mkdir()
            self.assertFalse(writer.replaceable(stems, "aaaaaaaaaaaaaaaa"))
            (stems / "aaaaaaaaaaaaaaaa/overcue-manifest.json").write_text(json.dumps({"writer": {"name": "XZ Mods"}}))
            self.assertTrue(writer.replaceable(stems, "aaaaaaaaaaaaaaaa"))
            self.assertFalse(writer.replaceable(stems, "../aaaaaaaaaaaa"))

    def test_same_bundle_is_reused(self):
        self.assertEqual(writer.merge(self.index(**{"12": dict(self.ENTRY, title="x")}), 12, self.ENTRY,
                                      lambda bundle: False), (None, False))

    def test_ownership_never_leaves_stems(self):
        with tempfile.TemporaryDirectory() as temp:
            stems = Path(temp) / "stems"
            (stems / "0123456789abcdef").mkdir(parents=True)
            (Path(temp) / "overcue-manifest.json").write_text(json.dumps({"writer": {"name": "XZ Mods"}}))
            for bundle in ("..", "../stems/..", "0123456789ABCDEF", None, 5):
                self.assertFalse(writer.owned_by_xz_mods(stems, bundle))

    def test_index_limits(self):
        with tempfile.TemporaryDirectory() as temp:
            mods = Path(temp)
            for content in ('{"schema":"overcue-index/2","tracks":{}}', '[]', '{"schema":"overcue-index/1","tracks":[]}',
                            '{"schema":"overcue-index/1","tracks":{"1":{},"1":{}}}',
                            '{"schema":"overcue-index/1","pad":"' + "x" * (4 * 1024 * 1024) + '"}'):
                (mods / "index.json").write_text(content)
                with self.assertRaises(ValueError):
                    writer.read_index(mods)


class PublishTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "USB"
        (self.root / "Contents").mkdir(parents=True)
        self.track = self.root / "Contents" / "a.wav"
        self.track.write_bytes(b"original track bytes")
        self.source_sha = hashlib.sha256(self.track.read_bytes()).hexdigest()

    def publish(self, name, seed=0, track_id=12, file_path="/Contents/a.wav"):
        roles = render_roles(Path(self.temp.name) / name, 40000, seed)
        receipt = writer.publish(self.root, file_path, track_id, self.source_sha, "wav", Path(self.temp.name) / name,
                                 "test-model", "0.0-test", QuietJob())
        return roles, receipt

    def test_publish_verify_reuse_and_replace(self):
        (self.root / "CDJMODS").mkdir()
        foreign = {"bundle": "aaaaaaaaaaaaaaaa", "file_path": "/Contents/other.wav", "unknown": True}
        (self.root / "CDJMODS/index.json").write_text(json.dumps(
            {"schema": "overcue-index/1", "keyed_by": "export.pdb", "tracks": {"3": foreign}, "future": 1}))
        roles, receipt = self.publish("roles")
        bundle = writer.bundle_name({role: roles[role]["sha256"] for role in writer.ROLES})
        self.assertEqual(receipt, {"bundle": bundle, "frames": 40000, "reused": False, "replaced_previous": False, "verified": True, "page_codec": "zlib"})
        index = json.loads((self.root / "CDJMODS/index.json").read_text())
        self.assertEqual((index["future"], index["tracks"]["3"]), (1, foreign))
        entry = index["tracks"]["12"]
        manifest = json.loads((self.root / "CDJMODS/stems" / bundle / "overcue-manifest.json").read_text())
        self.assertEqual(manifest["schema"], "overcue-stems/4")
        self.assertEqual(manifest["runtime"], {"sample_rate": 96000, "channels": 2, "format": "s16le",
                                               "frames": 40000, "latency_pad_frames": 0, "headroom_gain": 0.5, "page_codec": "zlib"})
        self.assertEqual((manifest["source"]["sha256"], manifest["writer"]["name"], manifest["writer"]["beta"]),
                         (self.source_sha, "XZ Mods", True))
        for role in writer.ROLES:
            key = role.replace("-", "_")
            table = writer.verify_role(self.root / "CDJMODS/stems" / bundle / writer.role_file(role), roles[role]["sha256"], 40000)
            self.assertEqual((entry[key + "_sha256"], entry[key + "_page_table_sha256"]), (roles[role]["sha256"], table))
            self.assertEqual(manifest["roles"][role]["page_table_sha256"], table)
            self.assertEqual((manifest["roles"][role]["bytes"], manifest["roles"][role]["loudness_gain"]), (160000, 1.0))
        self.assertEqual((entry["frames"], entry["three_part"], entry["page_bytes"], entry["source_sha256"], entry["file_path"]),
                         (40000, 1, 131072, self.source_sha, "/Contents/a.wav"))
        self.assertEqual(sorted(p.name for p in (self.root / "CDJMODS/stems").iterdir()), [bundle])

        _, again = self.publish("again")
        self.assertTrue(again["reused"])

        _, replaced = self.publish("replacement", seed=9)
        self.assertTrue(replaced["replaced_previous"])
        self.assertNotEqual(replaced["bundle"], bundle)
        self.assertTrue((self.root / "CDJMODS/stems" / bundle).is_dir())
        index = json.loads((self.root / "CDJMODS/index.json").read_text())
        self.assertEqual((index["tracks"]["12"]["bundle"], index["tracks"]["3"]), (replaced["bundle"], foreign))

    def test_refuses_foreign_track_without_writing(self):
        foreign = self.root / "CDJMODS/stems/aaaaaaaaaaaaaaaa"
        foreign.mkdir(parents=True)
        (foreign / "overcue-manifest.json").write_text(json.dumps({"schema": "overcue-stems/4"}))
        before = json.dumps({"schema": "overcue-index/1", "tracks": {"12": {"bundle": "aaaaaaaaaaaaaaaa", "file_path": "/Contents/a.wav"}}})
        (self.root / "CDJMODS/index.json").write_text(before)
        with self.assertRaisesRegex(ValueError, "OverCue already prepared this track. XZ Mods left it unchanged."):
            self.publish("roles")
        self.assertEqual((self.root / "CDJMODS/index.json").read_text(), before)
        self.assertEqual(list((self.root / "CDJMODS/stems").iterdir()), [foreign])

    def test_entry_with_missing_bundle_folder_is_replaced(self):
        (self.root / "CDJMODS").mkdir()
        (self.root / "CDJMODS/index.json").write_text(json.dumps(
            {"schema": "overcue-index/1", "tracks": {"12": {"bundle": "aaaaaaaaaaaaaaaa", "file_path": "/Contents/a.wav"}}}))
        _, receipt = self.publish("roles")
        self.assertTrue(receipt["replaced_previous"])

    def test_own_damaged_or_outdated_bundle_is_moved_aside_and_rebuilt(self):
        roles, first = self.publish("roles")
        folder = self.root / "CDJMODS/stems" / first["bundle"]
        manifest = json.loads((folder / "overcue-manifest.json").read_text())
        manifest["roles"]["vocal"]["loudness_gain"] = 0.5  # written by the earlier double-gain builder
        (folder / "overcue-manifest.json").write_text(json.dumps(manifest))
        _, second = self.publish("again")
        self.assertEqual((second["bundle"], second["reused"]), (first["bundle"], False))
        (folder / "stems-sidecar-drums.s16le.pgz").unlink()
        _, third = self.publish("third")
        self.assertEqual((third["bundle"], third["reused"]), (first["bundle"], False))
        aside = sorted(p.name for p in (self.root / "CDJMODS/stems").iterdir() if p.name != first["bundle"])
        self.assertEqual(len(aside), 2)
        self.assertTrue(all(name.startswith(".xz-mods-replaced-" + first["bundle"]) for name in aside))
        for role in writer.ROLES:
            writer.verify_role(folder / writer.role_file(role), roles[role]["sha256"], 40000)

    def test_sweeps_crash_leftovers(self):
        stems = self.root / "CDJMODS/stems"
        (stems / ".xz-mods-staging-crashed").mkdir(parents=True)
        (stems / ".xz-mods-staging-crashed/stems-sidecar-vocal.s16le.pgz").write_bytes(b"partial")
        (self.root / "CDJMODS/.index.json.abc.tmp").write_text("{")
        (stems / ".other-tool").mkdir()
        self.publish("roles")
        self.assertFalse((stems / ".xz-mods-staging-crashed").exists())
        self.assertFalse((self.root / "CDJMODS/.index.json.abc.tmp").exists())
        self.assertTrue((stems / ".other-tool").is_dir())

    def test_existing_bundle_with_other_content_is_left_alone(self):
        roles = render_roles(Path(self.temp.name) / "probe", 40000)
        bundle = writer.bundle_name({role: roles[role]["sha256"] for role in writer.ROLES})
        squatter = self.root / "CDJMODS/stems" / bundle
        squatter.mkdir(parents=True)
        (squatter / "stems-sidecar-vocal.s16le.pgz").write_bytes(b"not ours")
        with self.assertRaisesRegex(ValueError, "invalid header|different content"):
            self.publish("roles")
        self.assertEqual((squatter / "stems-sidecar-vocal.s16le.pgz").read_bytes(), b"not ours")
        self.assertFalse((self.root / "CDJMODS/index.json").exists())

    @unittest.skipIf(os.name != "nt" and os.geteuid() == 0, "root ignores permissions")
    def test_linked_cdjmods_is_rejected(self):
        outside = Path(self.temp.name) / "outside"
        outside.mkdir()
        try:
            (self.root / "CDJMODS").symlink_to(outside, target_is_directory=True)
        except OSError:
            self.skipTest("Creating symlinks is unavailable")
        with self.assertRaisesRegex(ValueError, "Links and junctions"):
            self.publish("roles")
        self.assertEqual(list(outside.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
