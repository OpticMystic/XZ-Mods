"""Compare the streaming C reader with the desktop Python behavioural reference."""
from pathlib import Path
import argparse
import importlib.util
import json
import subprocess
import sys

DEFAULT_REFERENCE = Path(r"C:\Users\short\Github\XZ-Mods\source\xdj-xz-toolkit\builder\rekordbox_pdb.py")

def check(dump, reference, fixture):
    # Do not create __pycache__ outside this module's allowed directory.
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location("reference_pdb", reference)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    expected = {row["id"]: row for row in module.library_tracks(fixture)}
    rows = json.loads(subprocess.check_output([str(dump), str(fixture)], encoding="utf-8"))
    actual = {row["id"]: row for row in rows}
    assert len(rows) == len(actual), "C reader emitted duplicate track IDs"
    assert actual.keys() == expected.keys(), (actual.keys(), expected.keys())
    for ident, python_row in expected.items():
        c_row = actual[ident]
        for field in ("id", "title", "artist", "file_path", "duration", "sample_rate"):
            assert c_row[field] == python_row[field], (ident, field, c_row[field], python_row[field])
        assert c_row["bpm_x100"] == round(python_row["bpm"] * 100), ident
    assert module.track_paths(fixture) == {i: t["file_path"] for i, t in actual.items()}
    print(f"PASS Python/C parity: {len(actual)} tracks, all 7 shared fields + track_paths ({Path(fixture).name})")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", type=Path, required=True)
    parser.add_argument("--reference", type=Path, default=DEFAULT_REFERENCE)
    parser.add_argument("pdb", type=Path)
    args = parser.parse_args()
    check(args.dump, args.reference, args.pdb)

if __name__ == "__main__":
    main()
