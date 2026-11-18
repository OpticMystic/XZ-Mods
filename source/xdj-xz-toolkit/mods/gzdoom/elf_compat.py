"""Keep ARM REL and JMPREL ranges disjoint for the XZ's glibc 2.13 loader."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct


def normalize(data: bytes) -> bytes:
    if data[:7] != b"\x7fELF\x01\x01\x01" or struct.unpack_from("<H", data, 18)[0] != 40:
        raise ValueError("Expected little-endian ARM ELF32")
    phoff = struct.unpack_from("<I", data, 28)[0]
    phsize, phcount = struct.unpack_from("<HH", data, 42)
    dynamic = []
    for index in range(phcount):
        kind, offset, _, _, size, _, _, _ = struct.unpack_from("<8I", data, phoff + index * phsize)
        if kind == 2:
            dynamic = [(position, *struct.unpack_from("<II", data, position))
                       for position in range(offset, offset + size, 8)]
    tags = {tag: value for _, tag, value in dynamic}
    if not all(tag in tags for tag in (17, 18, 19, 23, 2)) or tags[19] != 8:
        raise ValueError("Expected ARM REL and PLT relocation tables")
    start, size, plt, plt_size = (tags[tag] for tag in (17, 18, 23, 2))
    if start + size <= plt:
        return data
    if not start < plt or start + size != plt + plt_size:
        raise ValueError("Unexpected overlapping relocation ranges")
    result = bytearray(data)
    for position, tag, _ in dynamic:
        if tag == 18:
            struct.pack_into("<I", result, position + 4, plt - start)
    return bytes(result)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = args.source.read_bytes()
    result = normalize(source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    assert normalize(result) == result
    print(args.output.name, hashlib.sha256(result).hexdigest(), "normalized" if result != source else "unchanged")


if __name__ == "__main__":
    main()
