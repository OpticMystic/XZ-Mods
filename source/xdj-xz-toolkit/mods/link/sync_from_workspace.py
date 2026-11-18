"""Copy the Ableton Link helper's shared-memory header from the xz-link-peer source, or check that it matches.

The helper's copy (helpers/linkpeer/src/xz_link_shm.h in this repository) is canonical. The player needs only the
record layout and the inline sequence-lock functions. --workspace names another helper src folder.

Usage:
  python sync_from_workspace.py [--workspace DIR]           copy xz_link_shm.h here
  python sync_from_workspace.py --check [--workspace DIR]   exit 1 when the copy differs
"""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
HEADERS = ("xz_link_shm.h",)


def text(path: Path) -> bytes:
    """Content with line endings normalised: a checkout with core.autocrlf rewrites them."""
    return path.read_bytes().replace(b"\r\n", b"\n")


def default_workspace() -> Path:
    candidate = HERE.parents[1] / "helpers" / "linkpeer" / "src"
    if candidate.is_dir():
        return candidate
    raise SystemExit("helpers/linkpeer/src not found; pass --workspace")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--workspace", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    workspace = (args.workspace or default_workspace()).resolve()
    differ = []
    for name in HEADERS:
        source = workspace / name
        if not source.is_file():
            raise SystemExit(f"missing {source}")
        target = HERE / name
        same = target.is_file() and text(target) == text(source)
        if args.check:
            if not same:
                differ.append(name)
        elif not same:
            target.write_bytes(text(source))
            print(f"copied {name}")
    if args.check:
        for name in differ:
            print(f"DIFFERS {name}: {workspace / name}", file=sys.stderr)
        print("Ableton Link header matches the workspace" if not differ else f"{len(differ)} header(s) differ")
        return 1 if differ else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
