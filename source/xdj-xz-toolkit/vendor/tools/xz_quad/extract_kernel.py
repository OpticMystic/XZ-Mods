"""Extract a disposable XZ kernel tree from the hash-pinned public archives."""
import argparse
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
import tarfile
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--toolkit", type=Path, required=True)
parser.add_argument("--cache", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
sys.path.insert(0, str(args.toolkit.resolve()))
from builder.boot_support import PARTS, _Deflate64Member, _Parts


class LocalRead:
    def check(self):
        pass


args.output.mkdir(parents=True, exist_ok=False)
prefix = "pioneerdj_xdj_xz/BUILD/linux-3.0.101/"
manifest = {}
with ExitStack() as stack:
    streams = []
    for part in PARTS:
        path = args.cache / part["filename"]
        with path.open("rb") as source:
            if hashlib.file_digest(source, "sha256").hexdigest() != part["sha256"]:
                raise ValueError("Source archive hash mismatch: " + path.name)
        archive = stack.enter_context(zipfile.ZipFile(path))
        stream = _Deflate64Member(archive, archive.getinfo(part["member"]))
        stack.callback(stream.close)
        streams.append(stream)
    with tarfile.open(fileobj=_Parts(streams, LocalRead()), mode="r|bz2") as archive:
        for member in archive:
            name = member.name.removeprefix("./")
            if not name.startswith(prefix) or not member.isfile():
                continue
            relative = PurePosixPath(name[len(prefix):])
            if relative.is_absolute() or ".." in relative.parts or ":" in str(relative) or "\\" in str(relative):
                raise ValueError("Unsafe kernel member")
            data = archive.extractfile(member).read()
            digest = hashlib.sha256(data).hexdigest()
            if str(relative) in manifest and manifest[str(relative)] != digest:
                raise ValueError("Conflicting source member: " + str(relative))
            destination = args.output.joinpath(*relative.parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            manifest[str(relative)] = digest
(args.output.parent / (args.output.name + "-source-manifest.json")).write_text(json.dumps(manifest, indent=2))
print(f"Extracted {len(manifest)} files; links were not followed")
