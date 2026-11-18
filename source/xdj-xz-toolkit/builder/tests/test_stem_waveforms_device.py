"""Compile the XZ runtime's own waveform and bundle readers and feed them a bundle XZ Mods wrote.

The device sources live in the VJToolsLibrary monorepo (packages/xdj-xz-toolkit/mods). Point XZ_MODS_SOURCE
at that folder and XZ_CC at a C compiler command (default: zig from the monorepo's .tmp/cdj-zig, then cc).
"""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

from builder import stem_waveforms as waves
from builder.tests import stem_waveform_fixture as fixture

HERE = Path(__file__).resolve().parent
MONOREPO = HERE.parents[2] / "VJToolsLibrary"
SOURCES = ("ui/layered_wave.c", "ui/layered_wave_source.c", "audio/overcue_file.c", "audio/stem_decode.c",
           "audio/vendor/miniz/miniz_tinfl.c", "audio/vendor/sha256/sha256.c")


def mods_source():
    candidates = [os.environ.get("XZ_MODS_SOURCE"), MONOREPO / "packages/xdj-xz-toolkit/mods"]
    return next((Path(c) for c in candidates if c and (Path(c) / "ui/layered_wave_source.c").is_file()), None)


def compiler():
    if os.environ.get("XZ_CC"):
        return shlex.split(os.environ["XZ_CC"])
    for zig in (MONOREPO / ".tmp/cdj-zig/ziglang/zig.exe", shutil.which("zig")):
        if zig and Path(zig).is_file():
            return [str(zig), "cc"]
    return [shutil.which("cc")] if shutil.which("cc") else None


@unittest.skipUnless(mods_source() and compiler(), "Needs the XZ runtime sources and a C compiler")
class DeviceReaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        work = Path(cls.temp.name)
        mods = mods_source()
        cls.reader = work / "stem-waveform-reader.exe"
        subprocess.run(compiler() + ["-std=c11", "-O1", "-UNDEBUG", "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_DEFLATE_APIS",
                                     "-D_CRT_SECURE_NO_WARNINGS", "-I", str(mods / "ui"), "-I", str(mods / "audio"),
                                     str(HERE / "stem_waveform_reader.c"), *(str(mods / s) for s in SOURCES),
                                     "-lm", "-o", str(cls.reader)],
                       check=True, capture_output=True, text=True)
        root = work / "usb"
        root.mkdir()
        source, folder, cls.receipt = fixture.publish(root, work)
        result = subprocess.run([str(cls.reader), source.as_posix(), folder.as_posix()],
                                check=True, capture_output=True, text=True)
        cls.seen = json.loads(result.stdout)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_scrolling_waveform_sections_parse_with_one_column_per_640_frames(self):
        columns = waves.columns(fixture.FRAMES)
        for band, section in enumerate(self.seen["sections"]):
            self.assertEqual((section["rc"], section["count"]), (0, columns), section["stem"])
            self.assertGreaterEqual(section["peak"][band], 70, section["stem"])
            self.assertLessEqual(max(p for b, p in enumerate(section["peak"]) if b != band), 1, section["stem"])

    def test_stems_look_loads_each_stem_where_it_plays(self):
        stems = self.seen["stems"]
        columns = waves.columns(fixture.FRAMES)
        self.assertEqual((stems["rc"], stems["count"], stems["kind"]), (0, columns, 1))
        half = columns // 2
        drums, harmonics, vocal = zip(*stems["columns"])
        self.assertTrue(all(value >= 70 for value in drums[20:-20] + harmonics[20:-20]))
        self.assertEqual(max(vocal[:half - 20]), 0)
        self.assertTrue(all(value >= 70 for value in vocal[half + 20:-20]))
        self.assertGreater(stems["normalization"], 1)

    def test_bundle_opens_with_stem_overviews(self):
        opened = self.seen["open"]
        self.assertEqual((opened["rc"], opened["error"], opened["frames"]), (0, "", fixture.FRAMES))
        self.assertTrue(self.receipt["waveforms"])
        for overview in opened["overview"]:
            self.assertEqual(overview["count"], waves.columns(fixture.FRAMES))
            self.assertTrue(0 < overview["peak"] <= 31)


if __name__ == "__main__":
    unittest.main()
