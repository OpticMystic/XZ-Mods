"""Exercise released-format layouts through the native readers and audio worker."""
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import wave
import zlib

ROOT = Path(__file__).resolve().parents[1]
RATE, FRAMES = 44100, 264600


def wav(path, values):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as target:
        target.setparams((2, 2, RATE, 0, "NONE", "not compressed"))
        target.writeframes(values.tobytes())


def signals(root):
    parts = []
    for frequency in (193, 557, 1297, 3719):
        values = array("h")
        for i in range(FRAMES):
            for channel in range(2):
                at = i + channel * 17
                values.append(round(2200 * (math.sin(2 * math.pi * frequency * at / RATE) +
                                           .2 * math.sin(2 * math.pi * (frequency + 71) * at / RATE))))
        parts.append(values)
    mix = array("h", (sum(samples) for samples in zip(*parts)))
    source = root / "Contents" / "Artist" / "Track.wav"
    wav(source, mix)
    return source, parts


def az_fp(source):
    data = source.read_bytes()
    value = 14695981039346656037
    chunks = [str(len(data)).encode() + b"\n"]
    chunks += [data[offset:offset + 65536] for offset in (0, max(0, len(data)//2-32768), max(0, len(data)-65536))]
    for chunk in chunks:
        for byte in chunk:
            value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"fnv1a64:{value:016x}"


def rx3_pcm(values, fmt=2, gain=1):
    reserved = struct.pack("<f", gain) + bytes(28) if fmt == 3 else bytes(32)
    if fmt == 1:
        payload = array("f", (x / 32768 for x in values)).tobytes()
    else:
        payload = array("h", (round(x/gain) for x in values)).tobytes()
    return struct.pack("<8sIIIIQ32s", b"RX3STM1\0", RATE, 2, fmt, 64, len(values)//2, reserved) + payload


def rx3_package(source, parts, count=4, fmt=2):
    base = source.parents[2] / "RX3_STEMS"
    base.mkdir(exist_ok=True)
    names, indices = ("vocals", "drums", "bass")[:count-1], (2, 0, 3)[:count-1]
    manifest = json.dumps(dict(version=3, roles=names, frames=FRAMES, sample_rate=RATE, channels=2,
                               source_sha256=hashlib.sha256(source.read_bytes()).hexdigest())).encode()
    payloads = [rx3_pcm(parts[i], fmt, 2 if fmt == 3 else 1) for i in indices] + [b"", manifest]
    offset = 64 + len(payloads)*64
    entries = []
    for i, data in enumerate(payloads):
        role = (2, 4, 8)[i] if i < len(names) else 0
        kind = 1 if i < len(names) else 2 if i == len(names) else 3
        name = names[i] if i < len(names) else "waveform" if i == len(names) else "manifest"
        entries.append(struct.pack("<IIQQII32s", kind, role, offset, len(data), zlib.crc32(data), 0, name.encode()))
        offset += len(data)
    path = base / "Completely different name.rx3stem"
    path.write_bytes(struct.pack("<8sIIIIQQ24s", b"RX3PKG2\0", 3, 64, len(payloads), 64, offset, FRAMES, bytes(24)) + b"".join(entries + payloads))
    return path


def legacy(source, parts, fmt):
    base = source.parents[2] / "RX3_STEMS"
    entries = []
    for name, index, suffix in (("vocals", 2, "rx3stem"), ("drums", 0, "rx3drums"), ("bass", 3, "rx3bass")):
        path = base / f"Track.{suffix}"
        data = rx3_pcm(parts[index], fmt, 2 if fmt == 3 else 1)
        path.write_bytes(data)
        entries.append(dict(role=name, file=path.name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    (base / "rx3-stems-manifest.json").write_text(json.dumps(dict(format=2, tracks=[dict(
        source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), stems=entries)])))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", required=True)
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not args.ffmpeg:
        parser.error("ffmpeg required for independently encoded Vorbis fixtures")
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="xz-foreign-", dir=args.output) as temporary:
        work = args.output or Path(temporary)
        work.mkdir(parents=True, exist_ok=True)
        binary = work / "test-foreign.exe"
        sources = ["foreign_stems.c", "stem_media.c", "stem_mp3.c", "stem_decode.c", "overcue_file.c", "overcue_stream.c",
                   "vendor/miniz/miniz_tinfl.c", "vendor/sha256/sha256.c", "tests/test_foreign_stems.c"]
        subprocess.run([str(Path(args.zig).resolve()), "cc", "-std=c11", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                        "-Wno-misleading-indentation", "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_DEFLATE_APIS",
                        *[str(ROOT / source) for source in sources], "-pthread", "-lm", "-o", str(binary)], check=True)
        source, parts = signals(Path(temporary) / "usb")
        def run(count, *extra):
            subprocess.run([str(binary), str(source), str(count), *extra], check=True)
        def compare(count):
            result = subprocess.run([str(binary), str(source), str(count), "dump"], capture_output=True, check=True)
            values = array("f"); values.frombytes(result.stdout)
            assert len(values) == count * 2048
            for role in range(count):
                expected = [x/32768 for x in parts[role][:2048]]
                if count == 3 and role == 1:
                    expected = [(a+b)/32768 for a, b in zip(parts[1][:2048], parts[3][:2048])]
                assert max(abs(a-b) for a,b in zip(values[role*2048:(role+1)*2048], expected)) < 4/32768
        package = rx3_package(source, parts)
        run(4)
        compare(4)
        good = package.read_bytes()
        package.write_bytes(good[:-1]);run("reject")
        package.write_bytes(good[:400] + bytes([good[400] ^ 1]) + good[401:]);run("reject")
        package.write_bytes(good)
        invalid_gain = bytearray(good)
        pcm_at = struct.unpack_from("<Q", invalid_gain, 72)[0]
        pcm_size = struct.unpack_from("<Q", invalid_gain, 80)[0]
        struct.pack_into("<I", invalid_gain, pcm_at+16, 3)
        struct.pack_into("<f", invalid_gain, pcm_at+32, math.nan)
        struct.pack_into("<I", invalid_gain, 88, zlib.crc32(invalid_gain[pcm_at:pcm_at+pcm_size]))
        package.write_bytes(invalid_gain);run("reject");package.write_bytes(good)
        duplicate = package.with_name("duplicate.rx3stem");duplicate.write_bytes(good);run("reject");duplicate.unlink()
        original = source.read_bytes();source.write_bytes(original[:-1] + bytes([original[-1] ^ 1]));run("reject");source.write_bytes(original)
        rx3_package(source, parts, fmt=3);run(4, "reader");compare(4)
        rx3_package(source, parts, count=3);run(3);compare(3)
        package.unlink()
        for fmt in (1, 2, 3):
            legacy(source, parts, fmt);run(4, "reader");compare(4)
        for path in (source.parents[2]/"RX3_STEMS").iterdir(): path.unlink()
        folder = source.parents[2]/"STEMS"/"Track-12abcdef"
        folder.mkdir(parents=True)
        for name, samples in zip(("drums", "other", "vocals", "bass"), parts):
            part = Path(temporary) / f"{name}.wav";wav(part, samples)
            subprocess.run([args.ffmpeg, "-v", "error", "-y", "-i", str(part), "-c:a", "libvorbis", "-q:a", "6", str(folder/f"{name}.ogg")], check=True)
        identity = dict(file="Contents/Artist/Track.wav", size=source.stat().st_size, fp=az_fp(source))
        record = folder/"source.json";record.write_text(json.dumps(identity))
        run(4)
        identity["fp"] = "fnv1a64:0000000000000000";record.write_text(json.dumps(identity));run("reject")
        identity["fp"] = az_fp(source);record.write_text(json.dumps(identity))
        from verify_overcue import fixture
        _, oc_bundle, _, _ = fixture(Path(temporary)/"overcue")
        oc_root = source.parents[2]/"CDJMODS"
        shutil.copytree(oc_bundle.parent.parent, oc_root)
        index_path = oc_root/"index.json"
        index = json.loads(index_path.read_text())
        index["tracks"]["1"]["file_path"] = "/" + source.relative_to(source.parents[2]).as_posix()
        index_path.write_text(json.dumps(index))
        manifest_path = oc_root/"stems"/oc_bundle.name/"overcue-manifest.json"
        manifest = json.loads(manifest_path.read_text())
        manifest["source"]["sha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
        manifest_path.write_text(json.dumps(manifest))
        run("overcue-first")
        bass = folder/"bass.ogg";data=bass.read_bytes();bass.write_bytes(data[:40]);run("reject");bass.write_bytes(data)
        twin=folder.with_name("Track-second");twin.mkdir();(twin/"source.json").write_text(record.read_text());run("reject")
        print("PASS RX3 indexed/legacy/gains, AZ Vorbis, collisions, source identity, corruption, three/four-stem playback")


if __name__ == "__main__":
    main()
