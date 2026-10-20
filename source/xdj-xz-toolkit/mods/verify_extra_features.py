"""Focused software-only tests for the extra players' formats, DSP and Classic notices."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import shutil

ROOT = Path(__file__).resolve().parent


def mp3_fixture(path):
    encoder = shutil.which("ffmpeg")
    output = str(path)
    if encoder:
        command = [encoder]
    else:
        command = ["wsl", "-d", "Ubuntu", "--", "ffmpeg"]
        text = path.resolve().as_posix()
        output = "/mnt/" + text[0].lower() + text[2:]
    subprocess.run([*command, "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
                    "-i", "sine=frequency=440:sample_rate=44100:duration=12",
                    "-ac", "2", "-c:a", "libmp3lame", "-q:a", "3", output], check=True)


def fixtures(folder):
    samples = [-.5, .25, 0, -.25, .5, -1]
    rate = struct.pack(">HQ", 16383 + 15, 48000 << 48)

    def chunk(kind, data):
        return kind + struct.pack(">I", len(data)) + data + b"\0" * (len(data) & 1)

    def make(bits=16, codec=None, offset=0, channels=2, sample_rate=rate, version=True, reverse=False):
        common = struct.pack(">HIH", channels, 3, bits) + sample_rate
        if codec:
            common += codec + b"\0\0"
        pcm = b"".join(struct.pack(">f", v) if codec == b"fl32" else
                       int(v * (1 << (bits - 1))).to_bytes(bits // 8, "little" if codec == b"sowt" else "big", signed=True)
                       for v in samples)
        chunks = [chunk(b"COMM", common), chunk(b"ANNO", b"odd"),
                  chunk(b"SSND", struct.pack(">II", offset, 0) + b"\0" * offset + pcm)]
        if reverse:
            chunks.reverse()
        if codec and version:
            chunks.insert(0, chunk(b"FVER", bytes.fromhex("a2805140")))
        form = (b"AIFC" if codec else b"AIFF") + b"".join(chunks)
        return b"FORM" + struct.pack(">I", len(form)) + form

    files = {f"pcm{bits}.aiff": make(bits) for bits in (8, 16, 24, 32)}
    files.update({"sowt.aifc": make(16, b"sowt"), "float.aifc": make(32, b"fl32"),
                  "offset.aiff": make(offset=7), "reordered.aiff": make(reverse=True)})
    normal = make()
    files.update({
        "truncated.aiff": normal[:-1],
        "oversized.aiff": normal[:4] + b"\xff" * 4 + normal[8:],
        "bad-rate.aiff": make(sample_rate=struct.pack(">HQ", 0x7fff, 1 << 63)),
        "bad-channels.aiff": make(channels=1),
        "bad-offset.aiff": normal.replace(b"SSND\0\0\0\x14\0\0\0\0", b"SSND\0\0\0\x14\xff\xff\xff\xff"),
        "duplicate.aiff": b"FORM" + struct.pack(">I", len(normal)-8+26) + normal[8:] + normal[12:38],
        "compressed.aifc": make(16, b"ima4"),
        "no-version.aifc": make(16, b"NONE", version=False),
        "bad-version.aifc": make(16, b"NONE").replace(bytes.fromhex("a2805140"), b"\0"*4),
        "bad-name.aifc": make(16, b"NONE").replace(b"NONE\0\0", b"NONE\x7f\0"),
    })
    for name, data in files.items():
        (folder / name).write_bytes(data)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--zig", required=True)
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    with tempfile.TemporaryDirectory(prefix="xz-extra-features-") as temporary:
        out = args.output or Path(temporary)
        out.mkdir(parents=True, exist_ok=True)
        data = out / "fixtures"; data.mkdir(exist_ok=True)
        fixtures(data)
        mp3_fixture(data / "seek.mp3")
        suites = {
            "stretch": ["test_extra_stretch.c"],
            "aiff": ["audio/tests/test_aiff.c"],
            "notice": ["ui/extra_lane.c", "ui/wave_viewport.c", "ui/layered_wave.c", "ui/ui_draw.c", "ui/test_extra_lane.c"],
            "seek": ["audio/stem_media.c", "audio/stem_mp3.c", "audio/stem_decode.c", "audio/tests/test_media_seek.c"],
        }
        for name, sources in suites.items():
            exe = out / (name + ".exe")
            subprocess.run([args.zig, "cc", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                            "-fsanitize=undefined", "-I" + str(ROOT), *[str(ROOT / s) for s in sources],
                            "-lm", "-o", str(exe)], check=True)
            arguments = [str(data)] if name == "aiff" else [str(data / "seek.mp3")] if name == "seek" else []
            subprocess.run([str(exe), *arguments], check=True)


if __name__ == "__main__":
    main()
