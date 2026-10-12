import contextlib
import io
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import numpy as np

from builder import overcue_writer as writer
from builder import stem_library
from builder import stem_waveforms as waves
from builder.tests import stem_waveform_fixture as fixture

INTERIOR = slice(40, -40)


def stereo(mono):
    samples = np.rint(np.asarray(mono) * 32767).astype("<i2")
    return np.stack([samples, samples], axis=1)


def sine(frequency, amplitude=1.0, seconds=2):
    t = np.arange(waves.RATE * seconds) / waves.RATE
    return amplitude * np.sin(2 * np.pi * frequency * t)


def anlz_section(data, tag, entry_size):
    """xz_anlz_section from mods/ui/layered_wave_source.c, line for line."""
    assert data[:4] == b"PMAI"
    header, length = struct.unpack_from(">II", data, 4)
    assert 12 <= header <= len(data) and length <= len(data)
    at = header
    while at + 12 <= length:
        head, total = struct.unpack_from(">II", data, at + 4)
        assert head >= 12 and head <= total <= length - at
        if data[at:at + 4] == tag:
            assert head >= 20 and struct.unpack_from(">I", data, at + 12)[0] == entry_size
            count = struct.unpack_from(">I", data, at + 16)[0]
            assert 0 < count <= 1000000 and count * entry_size <= total - head
            return at + head, count
        at += total
    raise AssertionError(f"no {tag}")


def load_wave(data):
    """load_wave from mods/audio/overcue_file.c, line for line."""
    assert data[:4] == b"PMAI" and struct.unpack_from(">I", data, 4)[0] >= 12
    total, offset = struct.unpack_from(">I", data, 8)[0], struct.unpack_from(">I", data, 4)[0]
    assert total <= 4 * 1024 * 1024
    while offset + 12 <= total:
        header, length = struct.unpack_from(">II", data, offset + 4)
        assert header >= 12 and header <= length <= total - offset
        if data[offset:offset + 4] == b"PWV3" and header == 24 and struct.unpack_from(">I", data, offset + 12)[0] == 1:
            n = struct.unpack_from(">I", data, offset + 16)[0]
            assert 0 < n <= length - header and n <= 2000000
            return bytes(b & 31 for b in data[offset + header:offset + header + n])
        offset += length
    raise AssertionError("no PWV3")


class BandTests(unittest.TestCase):
    def test_pure_tones_fill_only_their_own_band(self):
        for frequency, band in ((60, 0), (1000, 1), (10000, 2)):
            detail, overview = waves.analyse(stereo(sine(frequency)), 1 / 65536)
            inside = detail[INTERIOR]
            self.assertTrue((inside[:, band] >= 125).all(), (frequency, inside[:, band].min()))
            others = [b for b in range(3) if b != band]
            self.assertEqual(int(inside[:, others].max()), 0, frequency)
            peaks = overview[INTERIOR]
            if frequency < waves.COLUMNS_PER_SECOND:
                peaks = np.lib.stride_tricks.sliding_window_view(peaks, 3).max(axis=1)
            self.assertTrue((peaks == 31).all(), frequency)

    def test_bands_meet_at_the_crossovers_and_sum_flat(self):
        for frequency, pair in ((250, (0, 1)), (2000, (1, 2))):
            inside = waves.analyse(stereo(sine(frequency)), 1 / 65536)[0][INTERIOR].astype(int)
            self.assertTrue((abs(inside[:, pair[0]] - 64) <= 1).all() and (abs(inside[:, pair[1]] - 64) <= 1).all(), frequency)

    def test_silence_is_empty(self):
        detail, overview = waves.analyse(np.zeros((waves.RATE, 2), "<i2"), 1 / 65536)
        self.assertEqual((int(detail.max()), int(overview.max())), (0, 0))

    def test_headroom_gain_is_undone_so_stems_keep_the_tracks_scale(self):
        with tempfile.TemporaryDirectory() as temp:
            full = {stem: stereo(sine(f, 0.5)) for stem, f in zip(waves.STEMS, (60, 1000, 10000))}
            half = {stem: stereo(sine(f, 0.25)) for stem, f in zip(waves.STEMS, (60, 1000, 10000))}
            waves.write(full, 1.0, Path(temp) / "full")
            waves.write(half, 0.5, Path(temp) / "half")
            for stem in waves.STEMS:
                a = np.frombuffer((Path(temp) / "full" / waves.detail_file(stem)).read_bytes()[52:], np.uint8)
                b = np.frombuffer((Path(temp) / "half" / waves.detail_file(stem)).read_bytes()[52:], np.uint8)
                self.assertLessEqual(int(np.abs(a.astype(int) - b).max()), 1)


class LayoutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.work = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_files_pass_the_runtime_readers_checks(self):
        output = fixture.generate(fixture.render_roles(self.work / "roles"), self.work / "waves")
        self.assertEqual(sorted(p.name for p in output.iterdir()), sorted(writer.WAVEFORM_FILES))
        columns = waves.columns(fixture.FRAMES)
        for stem in waves.STEMS:
            detail = (output / waves.detail_file(stem)).read_bytes()
            self.assertEqual(struct.unpack_from(">4sII", detail, 0), (b"PMAI", 28, len(detail)))
            self.assertEqual(struct.unpack_from(">4sIIIII", detail, 28), (b"PWV7", 24, 24 + 3 * columns, 3, columns, 0x00960000))
            self.assertEqual(anlz_section(detail, b"PWV7", 3), (52, columns))
            overview = (output / waves.overview_file(stem)).read_bytes()
            self.assertEqual(struct.unpack_from(">4sIIIII", overview, 28), (b"PWV3", 24, 24 + columns, 1, columns, 0x00960000))
            self.assertEqual(len(load_wave(overview)), columns)
            for name in (waves.detail_file(stem), waves.overview_file(stem)):
                self.assertEqual(writer.check_waveform(output / name, fixture.FRAMES)["columns"], columns)

    def test_band_energy_lands_in_each_stems_band_after_headroom(self):
        output = fixture.generate(fixture.render_roles(self.work / "roles"), self.work / "waves")
        expected = round(fixture.TRUE_AMPLITUDE * waves.DETAIL_FULL)
        half = waves.columns(fixture.FRAMES) // 2
        for band, stem in enumerate(waves.STEMS):
            data = (output / waves.detail_file(stem)).read_bytes()
            at, count = anlz_section(data, b"PWV7", 3)
            bands = np.frombuffer(data[at:at + 3 * count], np.uint8).reshape(count, 3)
            body = bands[half + 20:-20] if stem == "vocal" else bands[20:-20]
            self.assertTrue((abs(body[:, band].astype(int) - expected) <= 1).all(), stem)
            self.assertLessEqual(int(np.delete(body, band, axis=1).max()), 1, stem)
            if stem == "vocal":
                self.assertEqual(int(bands[:half - 20].max()), 0)

    def test_column_counts_agree_between_track_and_stems(self):
        self.assertEqual(writer.WAVEFORM_COLUMN_FRAMES, waves.COLUMN_FRAMES)
        self.assertEqual(writer.WAVEFORM_STEMS, waves.STEMS)
        for source in (1, 293, 294, 295, 7901023, 44100 * 180 + 17, 61_669_000):
            resampled = -(-source * 320 // 147)
            self.assertEqual(waves.columns(resampled), -(-source // 294), source)
        output = fixture.generate(fixture.render_roles(self.work / "roles"), self.work / "waves")
        counts = {anlz_section((output / waves.detail_file(stem)).read_bytes(), b"PWV7", 3)[1] for stem in waves.STEMS}
        full_mix = (self.work / "roles" / "full-mix.s16le").stat().st_size // 4
        self.assertEqual(counts, {waves.columns(full_mix)})

    def test_bytes_per_minute(self):
        self.assertEqual(waves.BYTES_PER_MINUTE, 108000)
        output = fixture.generate(fixture.render_roles(self.work / "roles"), self.work / "waves")
        self.assertEqual(sum(p.stat().st_size for p in output.iterdir()), waves.bytes_for_frames(fixture.FRAMES))

    def test_check_waveform_rejects_what_the_player_would_misread(self):
        output = fixture.generate(fixture.render_roles(self.work / "roles"), self.work / "waves")
        good = (output / "stems-drums-waveform.2EX").read_bytes()
        broken = self.work / "stems-drums-waveform.2EX"
        for data in (good[:-1], good[:32] + struct.pack(">I", 20) + good[36:], good[:44] + struct.pack(">I", 7) + good[48:]):
            broken.write_bytes(data)
            with self.assertRaises(ValueError):
                writer.check_waveform(broken, fixture.FRAMES)
        with self.assertRaises(ValueError):
            writer.check_waveform(output / "stems-drums-waveform.2EX", fixture.FRAMES + 640)


class WriterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.work = Path(self.temp.name)
        self.root = self.work / "usb"
        self.root.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def manifest(self, folder):
        return json.loads((folder / "overcue-manifest.json").read_text())

    def test_publish_lists_waveforms_without_changing_schema_or_bundle_name(self):
        _, folder, receipt = fixture.publish(self.root, self.work)
        manifest = self.manifest(folder)
        self.assertEqual(manifest["schema"], writer.MANIFEST_SCHEMA)
        self.assertTrue(receipt["waveforms"])
        self.assertNotIn("waveform_error", receipt)
        self.assertEqual(sorted(manifest["waveforms"]), sorted(writer.WAVEFORM_FILES))
        for name, record in manifest["waveforms"].items():
            self.assertEqual(record, writer.check_waveform(folder / name, fixture.FRAMES))
        pcm = {role: manifest["roles"][role]["sha256"] for role in writer.ROLES}
        self.assertEqual(receipt["bundle"], writer.bundle_name(pcm))
        self.assertEqual([p.name for p in folder.iterdir() if p.name.startswith(".")], [])

    def test_reused_bundle_gains_waveforms_without_rewriting_stems(self):
        _, folder, first = fixture.publish(self.root, self.work, with_waveforms=False, name="first")
        self.assertFalse(first["waveforms"])
        self.assertNotIn("waveforms", self.manifest(folder))
        stems = {role: (folder / writer.role_file(role)).stat().st_mtime_ns for role in writer.ROLES}
        _, again_folder, again = fixture.publish(self.root, self.work, name="again")
        self.assertEqual((again_folder, again["reused"], again["waveforms"]), (folder, True, True))
        self.assertEqual({role: (folder / writer.role_file(role)).stat().st_mtime_ns for role in writer.ROLES}, stems)
        self.assertTrue(writer.has_waveforms(folder, fixture.FRAMES))

    def test_add_waveforms_reads_the_stems_already_on_the_usb(self):
        _, folder, _ = fixture.publish(self.root, self.work, with_waveforms=False)
        expected = fixture.generate(self.work / "roles", self.work / "expected")
        python = fixture.import_python()
        if not python:
            self.skipTest("No Python with numpy, scipy and soundfile under -I")
        resources = self.work / "resources"
        fixture.install_script(resources)
        manifest = (folder / "overcue-manifest.json").read_bytes()
        with patch.dict(os.environ, {"XZ_BUILDER_RESOURCES": str(resources)}):
            added = writer.add_waveforms(self.root, fixture.FILE_PATH, python, fixture.RunJob())
            again = writer.add_waveforms(self.root, fixture.FILE_PATH, python, fixture.RunJob())
        self.assertEqual((added["added"], again["added"]), (True, False))
        for name in writer.WAVEFORM_FILES:
            self.assertEqual((folder / name).read_bytes(), (expected / name).read_bytes(), name)
        self.assertTrue(writer.has_waveforms(folder, fixture.FRAMES))
        self.assertEqual((folder / "overcue-manifest.json").read_bytes(), manifest)

    def test_bundle_mode_releases_its_decoded_roles(self):
        _, folder, _ = fixture.publish(self.root, self.work, with_waveforms=False)
        scratch = self.work / "scratch"
        scratch.mkdir()
        with contextlib.redirect_stdout(io.StringIO()):
            waves.main(["--bundle", str(folder), "--scratch", str(scratch), "--output", str(self.work / "out")])
        for decoded in scratch.iterdir():
            decoded.unlink()
        expected = fixture.generate(self.work / "roles", self.work / "expected")
        for name in writer.WAVEFORM_FILES:
            self.assertEqual((self.work / "out" / name).read_bytes(), (expected / name).read_bytes(), name)

    def test_failed_copy_to_the_usb_keeps_the_stems(self):
        with patch.object(writer, "_copy_waveforms", side_effect=OSError("device unplugged")):
            _, folder, receipt = fixture.publish(self.root, self.work)
        self.assertEqual((receipt["waveforms"], receipt["verified"]), (False, True))
        self.assertIn("device unplugged", receipt["waveform_error"])
        self.assertNotIn("waveforms", self.manifest(folder))
        self.assertFalse(any((folder / name).exists() for name in writer.WAVEFORM_FILES))

    def test_add_waveforms_leaves_overcue_bundles_alone(self):
        _, folder, _ = fixture.publish(self.root, self.work, with_waveforms=False)
        manifest = self.manifest(folder)
        manifest["writer"]["name"] = "OverCue"
        (folder / "overcue-manifest.json").write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "OverCue prepared this track"):
            writer.add_waveforms(self.root, fixture.FILE_PATH, "unused", fixture.RunJob())
        self.assertFalse(any((folder / name).exists() for name in writer.WAVEFORM_FILES))

    def test_bad_waveforms_are_reported_and_stems_still_publish(self):
        output = fixture.generate(fixture.render_roles(self.work / "probe"), self.work / "bad")
        (output / "stems-vocal-waveform.EXT").unlink()
        receipt = writer._publish(self.root, fixture.FILE_PATH, 7, "0" * 64, "wav", self.work / "probe",
                                  "test-model", "0.0-test", fixture.RunJob(), waves=output)
        self.assertFalse(receipt["waveforms"])
        self.assertIn("stems-vocal-waveform.EXT", receipt["waveform_error"])
        self.assertTrue((self.root / "CDJMODS/index.json").is_file())

    def test_library_status_treats_waveforms_as_extras(self):
        _, folder, _ = fixture.publish(self.root, self.work, with_waveforms=False)
        index = writer.read_index(self.root / "CDJMODS")[0]
        track = {"id": 7, "file_path": fixture.FILE_PATH, "sample_rate": 44100}
        before = stem_library.stem_status(self.root, track, index, None)
        self.assertEqual((before["stem_status"], before["stem_waveforms"], before["can_add_waveforms"]), ("present", False, True))
        fixture.publish(self.root, self.work, name="again")
        after = stem_library.stem_status(self.root, track, index, None)
        self.assertEqual((after["stem_status"], after["stem_waveforms"], after["can_add_waveforms"]), ("present", True, False))
        self.assertEqual(after["detail"], before["detail"])


if __name__ == "__main__":
    unittest.main()
