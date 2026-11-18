"""Create a conservative XZ localization source inventory, or verify its freshness.

Manual entries are complete source documents, not translation-ready segments.
Code files are watched for changes, not presented as extracted UI strings.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
MODS = 'source/xdj-xz-toolkit/mods'


def inventory(root):
    root = Path(root)
    paths = set()
    for pattern in ('manual/manual.json', 'manual/chapters/*.md', 'manual/coverage.json',
                    f'{MODS}/settings-schema.json', f'{MODS}/ui/*.c', f'{MODS}/ui/*.h',
                    f'{MODS}/shell/*.c', f'{MODS}/shell/*.h', 'ui/*.html', 'ui/*.js'):
        paths.update(p for p in root.glob(pattern) if p.is_file())
    if not (root / 'manual/manual.json').is_file() or not (root / f'{MODS}/settings-schema.json').is_file():
        raise ValueError('XZ source tree is incomplete')
    entries = []
    for path in sorted(paths, key=lambda p: p.relative_to(root).as_posix()):
        name = path.relative_to(root).as_posix()
        data = path.read_bytes().replace(b'\r\n', b'\n')
        entry = {'path': name, 'sha256': hashlib.sha256(data).hexdigest()}
        if name == 'manual/manual.json' or name.startswith('manual/chapters/'):
            entry['source'] = data.decode('utf-8')
        entries.append(entry)
    return {'schema': 1, 'kind': 'xz-source-inventory', 'files': entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('extract', 'check'))
    parser.add_argument('catalog', type=Path)
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    try:
        current = inventory(args.root)
        if args.command == 'extract':
            args.catalog.write_text(json.dumps(current, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        else:
            previous = json.loads(args.catalog.read_text(encoding='utf-8'))
            if previous != current:
                print('XZ source inventory is stale; re-extract and review affected translations.', file=sys.stderr)
                return 1
    except (OSError, UnicodeError, ValueError) as exc:
        print(f'XZ source inventory error: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
