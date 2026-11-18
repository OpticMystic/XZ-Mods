"""Synthetic stereo fixtures shared by host and ARM player checks."""
import math
import struct
import wave


def fixtures(folder):
    for rate in (44100, 48000):
        samples = [round(16384 * math.sin(2 * math.pi * 440 * n / rate)) for n in range(2 * rate)]
        with wave.open(str(folder / f"{rate}.wav"), "wb") as output:
            output.setnchannels(2); output.setsampwidth(2); output.setframerate(rate)
            output.writeframes(b"".join(struct.pack("<hh", value, value) for value in samples))
        pcm = b"".join(struct.pack(">hh", value, value) for value in samples)
        common = struct.pack(">HIHHQ", 2, 2 * rate, 16, 16383 + 15, rate << 48)
        form = b"AIFFCOMM" + struct.pack(">I", len(common)) + common + b"SSND" + struct.pack(">I", len(pcm) + 8) + b"\0" * 8 + pcm
        (folder / f"{rate}.aiff").write_bytes(b"FORM" + struct.pack(">I", len(form)) + form)
    (folder / "anlz").mkdir(exist_ok=True)
