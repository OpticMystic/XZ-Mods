"""Verify OverCue source-rate conversion before the stock mixer and either DAC.

This exercises the actual decoder and stream mixer with synthetic audio.
It never opens a device, selects a hardware sample rate or emulates either DAC.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import zlib

from flac_fixture import repack
from verify_overcue import fixture, four_file

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
RATES = (32000, 44100, 48000, 88200, 96000)
SOURCES = ("overcue_file.c", "stem_decode.c", "overcue_stream.c", "stem_pool.c", "foreign_stems.c",
           "stem_media.c", "stem_mp3.c", "vendor/miniz/miniz_tinfl.c",
           "vendor/sha256/sha256.c")


def complete_legacy_index(source: Path, bundle: Path) -> None:
    manifest = json.loads((bundle / "overcue-manifest.json").read_text())
    index_path = source.parents[2] / "CDJMODS/index.json"
    index = json.loads(index_path.read_text())
    entry = index["tracks"]["1"]
    entry.update(frames=manifest["runtime"]["frames"], three_part=1, page_bytes=131072,
                 page_codec="zlib", source_sha256=hashlib.sha256(source.read_bytes()).hexdigest())
    for role, metadata in manifest["roles"].items():
        data = (bundle / f"stems-sidecar-{role}.s16le.pgz").read_bytes()
        pages = struct.unpack_from(">I", data, 12)[0]
        digest = hashlib.sha256()
        for page in range(pages):
            offset, size = struct.unpack_from(">QI", data, 24 + page * 48)
            digest.update(zlib.decompress(data[offset:offset + size]))
        key = role.replace("-", "_")
        entry[key + "_sha256"] = digest.hexdigest()
        entry[key + "_page_table_sha256"] = metadata["page_table_sha256"]
    index_path.write_text(json.dumps(index), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    compiler = parser.add_mutually_exclusive_group(required=True)
    compiler.add_argument("--zig", type=Path)
    compiler.add_argument("--cc")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--reader-rate", type=int, choices=RATES, action="append")
    parser.add_argument("--no-negative-control", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    rates = list(dict.fromkeys(args.reader_rate or RATES))
    cc = [str(args.zig.resolve()), "cc"] if args.zig else shlex.split(args.cc)
    flags = ["-std=c11", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
             "-Wno-misleading-indentation", "-fsanitize=undefined",
             "-fno-sanitize-recover=all", "-DMINIZ_NO_ARCHIVE_APIS",
             "-DMINIZ_NO_DEFLATE_APIS", "-I" + str(ROOT)]
    commands = []
    report = {"schema": "xz-overcue-sample-rate-compatibility/1", "status": "fail",
              "hardware_tested": [], "hardware_sample_rate_changed": False,
              "dac_selection": "stock mixer firmware, outside the stem PCM path",
              "cases": []}
    report_path = output / "verification.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def run(argv: list[str], *, check: bool = True, timeout: int = 60):
        result = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        commands.append({"argv": argv, "exit_code": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr})
        if check and result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return result

    def build(binary: Path, stream: Path | None = None) -> None:
        paths = [stream if name == "overcue_stream.c" and stream else ROOT / name for name in SOURCES]
        run(cc + flags + [str(p) for p in paths] +
            [str(HERE / "test_overcue_sample_rates.c"), "-pthread", "-lm", "-o", str(binary)],
            timeout=180)

    try:
        binary = output / "sample-rates.exe"
        build(binary)
        fixtures = output / "fixtures"
        fixtures.mkdir(exist_ok=True)
        base = fixtures / "legacy-96-seven"
        # A fixed synthetic fixture can be reused across invocations. Its source and
        # audio hashes are retained below, so stale or changed inputs are visible.
        if not base.exists():
            source, bundle, _, _ = fixture(base, signal=True)
            complete_legacy_index(source, bundle)
        source = base / "Contents/artist/track.mp3"
        bundle = base / "CDJMODS/stems/0123456789abcdef"
        cases = []
        for profile, rate in (("legacy-96", 96000), ("flac-96", 96000), ("flac-44", 44100)):
            seven = base if profile == "legacy-96" else fixtures / (profile + "-seven")
            if seven != base and not seven.exists():
                shutil.copytree(base, seven)
                repack(seven / "Contents/artist/track.mp3",
                       seven / "CDJMODS/stems/0123456789abcdef", rate=rate)
            four = fixtures / (profile + "-four")
            if not four.exists():
                shutil.copytree(seven, four)
                four_source = four / "Contents/artist/track.mp3"
                four_bundle = four / "CDJMODS/stems/0123456789abcdef"
                four_file(four_source, four_bundle)
                index_path = four / "CDJMODS/index.json"
                index = json.loads(index_path.read_text())
                index["tracks"]["1"]["three_part"] = 2
                index["tracks"]["1"]["source_timing_policy"] = "native-mp3-header/2"
                index_path.write_text(json.dumps(index), encoding="utf-8")
            for layout, folder in (("seven", seven), ("four", four)):
                for reader_rate in rates:
                    cases.append((profile, rate, layout, folder, reader_rate))
        for number, (profile, rate, layout, folder, reader_rate) in enumerate(cases, 1):
            source = folder / "Contents/artist/track.mp3"
            bundle = folder / "CDJMODS/stems/0123456789abcdef"
            expected_roles = 4 if layout == "four" else 7
            if len(list(bundle.glob("stems-sidecar-*.s16le.pgz"))) != expected_roles:
                raise RuntimeError("Fixture does not have its declared four/seven-file layout")
            # A fractional native-frame lag, deliberately independent of file rate.
            lag = "2257.4" if reader_rate == 48000 else "29.4"
            result = run([str(binary), str(source), str(reader_rate), lag], timeout=30)
            item = json.loads(result.stdout)
            expected_codec = {"legacy-96": 1, "flac-96": 3, "flac-44": 4}[profile]
            if (item["prepared_hz"] != rate or item["reader_hz"] != reader_rate
                    or item["codec"] != expected_codec or item["hardware_tested"]):
                raise RuntimeError("The sample-rate case did not exercise its declared contract")
            item.update(profile=profile, layout=layout)
            report["cases"].append(item)
            print(f"PASS {number}/{len(cases)} {profile} {layout}: {rate} -> {reader_rate} reader PCM", flush=True)
        if not args.no_negative_control:
            original = (ROOT / "overcue_stream.c").read_text(encoding="utf-8")
            target = "return s->source_rate?s->source_rate:XZ_OC_RATE;"
            if original.count(target) != 1:
                raise RuntimeError("Prepared/native-rate negative-control target changed")
            broken = output / "wrong-rate-stream.c"
            broken.write_text(original.replace(target, "return s->rate;"), encoding="utf-8")
            mutant = output / "wrong-rate.exe"
            build(mutant, broken)
            source = base / "Contents/artist/track.mp3"
            failed = run([str(mutant), str(source), "44100", "29.4"], check=False, timeout=30)
            if failed.returncode != 1 or "alignment native=44100 prepared=96000" not in failed.stderr or "status.state == XZ_OC_READY" not in failed.stderr:
                raise RuntimeError("Treating 96-kHz source frames as native-rate frames escaped the checks")
            report["negative_control"] = "wrong source/native ratio rejected by actual alignment"
        else:
            report["negative_control"] = "not_run"
        report["source_sha256"] = {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in [*(ROOT / name for name in SOURCES), ROOT / "overcue.h",
                         HERE / "test_overcue_sample_rates.c", Path(__file__).resolve(),
                         HERE / "verify_overcue.py", HERE / "flac_fixture.py"]
        }
        report["fixture_sha256"] = {
            str(path.relative_to(fixtures)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in fixtures.rglob("*") if path.is_file()
        }
        report["status"] = "pass"
        print("PASS common pre-DAC PCM path. Neither DAC's physical output was tested.", flush=True)
    finally:
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        (output / "commands.json").write_text(json.dumps(commands, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
