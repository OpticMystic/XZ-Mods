"""Verify the production track owner without stem files; Linux ASan/UBSan only."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
MODS = HERE.parent.parent
with tempfile.TemporaryDirectory(prefix="xz-no-stem-source-") as temporary:
    sources = [HERE / "test_no_stems.c"]
    sources += [MODS / "audio" / name for name in (
        "native_reader.c", "stem_cache.c", "stem_mix.c", "stem_decode.c",
        "overcue_file.c", "overcue_stream.c", "foreign_stems.c", "stem_media.c", "stem_mp3.c", "trace.c",
        "vendor/miniz/miniz_tinfl.c", "vendor/sha256/sha256.c")]
    sources += [MODS / "ram" / name for name in (
        "cache.c", "runtime.c", "removal.c", "presentation.c", "pin.c")]
    binary = Path(temporary) / "no-stem-source"
    command = shlex.split(os.environ.get("CC", "gcc")) + [
        "-std=c11", "-O1", "-g", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
        "-Wno-misleading-indentation", "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all", "-DMINIZ_NO_ARCHIVE_APIS", "-DMINIZ_NO_DEFLATE_APIS"]
    subprocess.run(command + list(map(str, sources)) + ["-pthread", "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=90)
