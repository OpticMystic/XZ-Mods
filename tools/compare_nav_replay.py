"""Compare every retained PNG, by relative name and bytes, against earlier captures."""
import argparse
import json
from pathlib import Path

def compare(current, baselines):
    results = {}
    for baseline in baselines:
        files = sorted(baseline.glob('*/frame-*.png'))
        if not files:
            raise ValueError(f'baseline has no PNGs: {baseline}')
        failures = []
        flows = {p.parent.name for p in files}
        for path in files:
            relative = path.relative_to(baseline)
            target = current / relative
            if not target.is_file() or path.read_bytes() != target.read_bytes():
                failures.append(str(relative))
        extras = [str(p.relative_to(current)) for name in flows for p in (current/name).glob('frame-*.png') if not (baseline/p.relative_to(current)).exists()]
        results[baseline.name] = dict(flows=len(flows), frames=len(files), mismatches=failures, extra_frames=extras)
    return results

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('current', type=Path)
    parser.add_argument('baselines', type=Path, nargs='+')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = compare(args.current, args.baselines)
    args.out.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
    if any(r['mismatches'] or r['extra_frames'] for r in result.values()):
        raise SystemExit(1)

if __name__ == '__main__':
    main()
