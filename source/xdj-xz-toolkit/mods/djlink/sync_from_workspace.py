"""Copy the Pro DJ Link wire headers from the xz-djlink helper source, or check that they match it.

The helper's copy (helpers/djlink/ in this repository) is canonical. The player needs only the types and the
inline bus functions; djl.c stays with the helper. --workspace names another helper folder.

Usage:
  python sync_from_workspace.py [--workspace DIR]           copy djl.h and djl_bus.h here
  python sync_from_workspace.py --check [--workspace DIR]   exit 1 when a copy differs
"""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
HEADERS = ("djl.h", "djl_bus.h")


def text(path: Path) -> bytes:
    """Content with line endings normalised: a checkout with core.autocrlf rewrites them."""
    return path.read_bytes().replace(b"\r\n", b"\n")


def default_workspace() -> Path:
    candidate = HERE.parents[1] / "helpers" / "djlink"
    if candidate.is_dir():
        return candidate
    raise SystemExit("helpers/djlink not found; pass --workspace")


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
        print("djlink headers match the workspace" if not differ else f"{len(differ)} header(s) differ")
        return 1 if differ else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
