"""A USB with one published XZ Mods bundle whose stems are pure tones in separate bands."""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys

import numpy as np

from builder import overcue_writer as writer
from builder import stem_waveforms as waves
from builder.tests.test_overcue_writer import QuietJob

RATE = waves.RATE
SECONDS = 4
FRAMES = RATE * SECONDS + 123
HEADROOM = 0.5
TRUE_AMPLITUDE = 0.6
FILE_PATH = "/Contents/Artist/Track.wav"
# drums, harmonics and vocal sit in the low, mid and high bands; vocal enters halfway through.
TONES = {"drums": (60, 0.0), "harmonics": (1000, 0.0), "vocal": (10000, SECONDS / 2)}
MEMBERS = {"vocal": ("vocal",), "instrumental": ("drums", "harmonics"), "drums": ("drums",),
           "harmonics": ("harmonics",), "vocals-drums": ("vocal", "drums"),
           "vocals-harmonics": ("vocal", "harmonics"), "full-mix": ("drums", "harmonics", "vocal")}


class RunJob(QuietJob):
    def run(self, argv, **_):
        result = subprocess.run([str(arg) for arg in argv], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"Process failed ({result.returncode}): {result.stderr}")
        return result.stdout


def tone(frequency, enter):
    t = np.arange(FRAMES) / RATE
    fade = np.clip(np.minimum(t - enter, t[-1] - t) / 0.02, 0, 1)
    fade = 0.5 - 0.5 * np.cos(np.pi * fade)
    return np.sin(2 * np.pi * frequency * t) * fade * TRUE_AMPLITUDE * HEADROOM


def render_roles(folder):
    folder.mkdir()
    parts = {stem: tone(*TONES[stem]) for stem in TONES}
    roles = {}
    for role, members in MEMBERS.items():
        mono = np.rint(sum(parts[stem] for stem in members) * 32767).astype("<i2")
        data = np.stack([mono, mono], axis=1).tobytes()
        (folder / f"{role}.s16le").write_bytes(data)
        roles[role] = {"file": f"{role}.s16le", "sha256": hashlib.sha256(data).hexdigest()}
    (folder / "result.json").write_text(json.dumps({"frames": FRAMES, "sample_rate": RATE, "channels": 2,
        "format": "s16le", "headroom_gain": HEADROOM, "roles": roles}))
    return folder


def generate(roles, output):
    with contextlib.redirect_stdout(io.StringIO()):
        waves.main(["--roles", str(roles), "--output", str(output)])
    return output


def publish(root, work, with_waveforms=True, name="roles"):
    """Publish the tone bundle onto the USB at root. Returns (source track, bundle folder, receipt)."""
    source = root / FILE_PATH.lstrip("/")
    if not source.exists():
        source.parent.mkdir(parents=True)
        source.write_bytes(b"tone fixture source")
    roles = render_roles(work / name)
    generated = generate(roles, work / (name + "-waveforms")) if with_waveforms else None
    receipt = writer._publish(root, FILE_PATH, 7, hashlib.sha256(source.read_bytes()).hexdigest(), "wav", roles,
                              "test-model", "0.0-test", QuietJob(), waves=generated)
    return source, root / "CDJMODS/stems" / receipt["bundle"], receipt


def import_python():
    """A Python that imports the generator's packages under -I, preferring the app's own import venv."""
    local = Path(os.environ.get("LOCALAPPDATA", Path.home()))
    candidates = [os.environ.get("XZ_IMPORT_PYTHON"),
                  *(local / app / "import-python/Scripts/python.exe" for app in ("com.opticmystic.xzmods", "XZ Mods")),
                  sys.executable]
    for python in filter(None, candidates):
        probe = subprocess.run([str(python), "-I", "-c", "import numpy, scipy.signal, soundfile"], capture_output=True)
        if Path(python).is_file() and probe.returncode == 0:
            return str(python)
    return None


def install_script(resources):
    """Lay out XZ_BUILDER_RESOURCES the way the packaged app does, with this checkout's generator."""
    inference = Path(resources) / "inference"
    inference.mkdir(parents=True, exist_ok=True)
    (inference / "stem_waveforms.py").write_bytes(Path(waves.__file__).read_bytes())
