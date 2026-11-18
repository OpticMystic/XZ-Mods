import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
import zlib
from unittest.mock import patch

from builder import overcue_writer as writer


class QuietJob:
    def check(self):
        pass

    def progress(self, stage, message):
        pass


def render_roles(folder, frames, seed=0, rate=96000):
    """Stand-in for overcue_roles.py output: seven raw s16le roles plus result.json."""
    folder.mkdir()
    roles = {}
    for number, role in enumerate(writer.ROLES):
        data = bytes((i * (number + 3) + seed) % 251 for i in range(frames * 4))
        (folder / f"{role}.s16le").write_bytes(data)
        roles[role] = {"file": f"{role}.s16le", "sha256": hashlib.sha256(data).hexdigest()}
    (folder / "result.json").write_text(json.dumps({"frames": frames, "sample_rate": rate, "channels": 2,
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


class SampleRateTests(unittest.TestCase):
    def test_requests_default_to_44k1_and_may_ask_for_96k(self):
        self.assertEqual(writer.DEFAULT_SAMPLE_RATE, 44100)
        for request, rate in (({}, 44100), ({"stem_sample_rate": None}, 44100), ({"stem_sample_rate": 44100}, 44100),
                              ({"stem_sample_rate": 96000}, 96000)):
            self.assertEqual(writer.requested_rate(request), rate)
        for value in ("96000", 48000, 96000.0, True):
            with self.assertRaisesRegex(ValueError, "44100 or 96000"):
                writer.requested_rate({"stem_sample_rate": value})

    def test_profiles_follow_the_published_format(self):
        self.assertEqual(writer.PROFILES[44100]["entry"], {"sample_rate": 44100, "preparation_profile": "cdj2000-44k1/1",
                                                            "source_timing_policy": "decoded-timeline/1"})
        self.assertEqual((writer.PROFILES[44100]["page_codec"], writer.PROFILES[44100]["magic"],
                          writer.PROFILES[44100]["manifest_schema"]), ("flac-44k1", b"OVPGZ004", "overcue-stems/5"))
        # 96 kHz entries stay unlabelled, exactly as before.
        self.assertEqual(writer.PROFILES[96000]["entry"], {})
        self.assertEqual(writer.PROFILES[96000]["manifest_schema"], "overcue-stems/4")

    def test_44k1_roles_need_flac_pages_and_write_nothing_without_them(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            render_roles(root / "roles", 4000, rate=44100)
            with self.assertRaisesRegex(ValueError, "FLAC pages"):
                writer.publish(root / "USB", "/Contents/a.wav", 1, "0" * 64, "wav", root / "roles", "m", "v", QuietJob())
            self.assertFalse((root / "USB").exists())

    def test_role_magic_must_match_the_rate(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "role.pgz"
            for magic, rate, message in ((b"OVPGZ004", 96000, "not at 96000"), (b"OVPGZ003", 44100, "not at 44100"),
                                         (b"OVPGZ001", 44100, "invalid header")):
                path.write_bytes(magic + struct.pack(">IIQ", 131072, 1, 400) + bytes(48))
                with self.assertRaisesRegex(ValueError, message):
                    writer.verify_role(path, "0" * 64, 100, rate)


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
        with self.assertRaisesRegex(ValueError, "id from the exported library for a different file"):
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
                            '{"schema":"overcue-index/1","pad":"' + "x" * writer.MAX_INDEX_JSON + '"}'):
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

    def publish(self, name, seed=0, track_id=12, file_path="/Contents/a.wav", codec="wav", source=None):
        roles = render_roles(Path(self.temp.name) / name, 40000, seed)
        receipt = writer.publish(self.root, file_path, track_id, self.source_sha, codec, Path(self.temp.name) / name,
                                 "test-model", "0.0-test", QuietJob(), source=source)
        return roles, receipt

    def test_v2_absent_stays_absent(self):
        self.publish("legacy")
        self.assertFalse((self.root / "CDJMODS/index-v2").exists())
        self.assertFalse((self.root / "CDJMODS/index-v2-pending").exists())

    def test_v2_publish_pending_order_replacement_and_reuse_repair(self):
        v2 = writer.overcue_index_v2
        mods = self.root / "CDJMODS"
        directory = mods / "index-v2"
        directory.mkdir(parents=True)
        replace = os.replace
        seen = []
        def observe(source, target):
            target = Path(target)
            if target.name in ("index.json", "current"):
                self.assertTrue((mods / "index-v2-pending").is_file())
                seen.append(target.name)
            replace(source, target)
        with patch.object(writer.os, "replace", side_effect=observe):
            self.publish("first")
        self.assertEqual(seen, ["index.json", "current"])
        old = (directory / "current").read_bytes()
        self.publish("replace", seed=8)
        self.assertTrue((directory / old[8:24].hex()).is_dir())
        index = writer.read_index(mods)[0]
        self.assertEqual(v2.read(directory)[1, 12], v2.record(1, "12", index["tracks"]["12"]))
        # Reusing unchanged audio/index repairs an interrupted or damaged v2 as well.
        (directory / "current").write_bytes(b"corrupt")
        (mods / "index-v2-pending").write_bytes(b"interrupted")
        self.publish("repair", seed=8)
        self.assertEqual(v2.read(directory)[1, 12], v2.record(1, "12", index["tracks"]["12"]))
        self.assertFalse((mods / "index-v2-pending").exists())

    def test_v2_invalid_track_key_refuses_index_replacement(self):
        mods = self.root / "CDJMODS"
        (mods / "index-v2").mkdir(parents=True)
        original = json.dumps({"schema": writer.INDEX_SCHEMA, "tracks": {"invalid": {}}}).encode()
        (mods / "index.json").write_bytes(original)
        with self.assertRaisesRegex(ValueError, "positive u32"):
            self.publish("invalid-key")
        self.assertEqual((mods / "index.json").read_bytes(), original)
        self.assertFalse((mods / "index-v2/current").exists())

    def test_mp3_manifest_records_the_player_timeline(self):
        mp3 = {"lame_tag": True, "delay_frames": 1105, "padding_frames": 407}
        _, receipt = self.publish("mp3", file_path="/Contents/a.mp3", codec="mp3", source={"sample_rate": 44100, "mp3": mp3})
        manifest = json.loads((self.root / "CDJMODS/stems" / receipt["bundle"] / "overcue-manifest.json").read_text())
        self.assertEqual(manifest["source"]["mp3"], {**mp3, "timeline": "player"})
        self.assertEqual((manifest["source"]["codec"], manifest["source"]["sample_rate"]), ("mp3", 44100))
        self.assertEqual(manifest["runtime"]["latency_pad_frames"], 2405)

    def test_index_reader_accepts_16_mib_but_not_more(self):
        mods=self.root/'CDJMODS';mods.mkdir()
        path=mods/'index.json'
        raw=json.dumps({'schema':writer.INDEX_SCHEMA,'tracks':{}}).encode()
        for size in (writer.MAX_JSON+1,writer.MAX_INDEX_JSON):
            path.write_bytes(raw+b' '*(size-len(raw)))
            self.assertEqual(len(writer.read_index(mods)[1]),size)
        path.write_bytes(raw+b' '*(writer.MAX_INDEX_JSON+1-len(raw)))
        with self.assertRaisesRegex(ValueError,'16 MiB'):writer.read_index(mods)

    def test_index_write_limit_depends_on_existing_size(self):
        mods=self.root/'CDJMODS';mods.mkdir()
        path=mods/'index.json'
        index={'schema':writer.INDEX_SCHEMA,'tracks':{},'padding':'x'*(writer.MAX_JSON-100)}
        original=json.dumps(index,separators=(',',':')).encode()
        self.assertLessEqual(len(original),writer.MAX_JSON)
        path.write_bytes(original)
        with self.assertRaisesRegex(ValueError,'4 MiB'):self.publish('small-index')
        self.assertEqual(path.read_bytes(),original)
        path.write_bytes(original+b' '*200)
        self.publish('large-index')
        written,raw=writer.read_index(mods)
        self.assertGreater(len(raw),writer.MAX_JSON)
        self.assertEqual(written['padding'],index['padding'])
        self.assertIn('12',written['tracks'])
        _, plain = self.publish("plain", seed=3, file_path="/Contents/a.mp3", codec="mp3",
                                source={"sample_rate": 48000, "mp3": {"lame_tag": False, "delay_frames": 0, "padding_frames": 0}})
        manifest = json.loads((self.root / "CDJMODS/stems" / plain["bundle"] / "overcue-manifest.json").read_text())
        self.assertEqual((manifest["runtime"]["latency_pad_frames"], manifest["source"]["sample_rate"]), (0, 48000))

    def test_publish_verify_reuse_and_replace(self):
        (self.root / "CDJMODS").mkdir()
        foreign = {"bundle": "aaaaaaaaaaaaaaaa", "file_path": "/Contents/other.wav", "unknown": True}
        (self.root / "CDJMODS/index.json").write_text(json.dumps(
            {"schema": "overcue-index/1", "keyed_by": "export.pdb", "tracks": {"3": foreign}, "future": 1}))
        roles, receipt = self.publish("roles")
        bundle = writer.bundle_name({role: roles[role]["sha256"] for role in writer.ROLES})
        self.assertEqual(receipt, {"bundle": bundle, "frames": 40000, "reused": False, "replaced_previous": False, "verified": True, "page_codec": "zlib", "sample_rate": 96000, "waveforms": False})
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
        # The replaced bundle was XZ Mods' own and nothing names it any more, so its folder is gone.
        self.assertEqual(replaced["removed_previous"], [bundle])
        self.assertEqual(sorted(p.name for p in (self.root / "CDJMODS/stems").iterdir()), [replaced["bundle"]])
        index = json.loads((self.root / "CDJMODS/index.json").read_text())
        self.assertEqual((index["tracks"]["12"]["bundle"], index["tracks"]["3"]), (replaced["bundle"], foreign))

    def test_replaced_bundle_another_entry_still_names_is_kept(self):
        _, first = self.publish("first")
        # A second exported copy of the same audio shares the bundle.
        index_path = self.root / "CDJMODS/index.json"
        index = json.loads(index_path.read_text())
        index["tracks"]["40"] = dict(index["tracks"]["12"], file_path="/Contents/copy.wav")
        index_path.write_text(json.dumps(index))
        _, second = self.publish("second", seed=4)
        self.assertNotIn("removed_previous", second)
        self.assertTrue((self.root / "CDJMODS/stems" / first["bundle"]).is_dir())

    def test_remove_replaced_keeps_foreign_unknown_and_named_folders(self):
        stems = self.root / "CDJMODS/stems"
        def bundle(name, writer_name):
            (stems / name).mkdir(parents=True)
            (stems / name / "overcue-manifest.json").write_text(json.dumps({"writer": {"name": writer_name}}))
        bundle("1" * 16, "XZ Mods")
        bundle("2" * 16, "OverCue")
        bundle("3" * 16, "XZ Mods")
        (stems / ("4" * 16)).mkdir()
        index = {"tracks": {"7": {"bundle": "3" * 16}}}
        removed = writer.remove_replaced(stems, index, {"1" * 16, "2" * 16, "3" * 16, "4" * 16, "../x", None})
        self.assertEqual(removed, ["1" * 16])
        self.assertEqual(sorted(p.name for p in stems.iterdir()), ["2" * 16, "3" * 16, "4" * 16])

    def test_interrupted_removal_leaves_a_leftover_the_next_publish_sweeps(self):
        _, first = self.publish("first")
        with patch.object(writer.shutil, "rmtree", side_effect=OSError("unplugged")):
            _, second = self.publish("second", seed=5)
        self.assertEqual(second["removed_previous"], [first["bundle"]])
        leftovers = [p.name for p in (self.root / "CDJMODS/stems").iterdir() if p.name.startswith(writer.STAGING)]
        self.assertEqual(len(leftovers), 1)
        self.assertFalse((self.root / "CDJMODS/stems" / first["bundle"]).exists())
        self.publish("third", seed=5)
        self.assertEqual(sorted(p.name for p in (self.root / "CDJMODS/stems").iterdir()), [second["bundle"]])

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
