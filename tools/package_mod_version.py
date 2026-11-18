"""Add a verified, immutable mod resource bundle to an offline version catalog."""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

APP = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(APP / 'source/xdj-xz-toolkit'))
from builder import mod_versions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--resources', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--channel', choices=('released', 'preview', 'development'), required=True)
    parser.add_argument('--description', required=True)
    parser.add_argument('--preferred', action='store_true')
    args = parser.parse_args()
    output = mod_versions.cache._safe_path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='xz-mod-catalog-') as work:
        os.environ['XZ_BUILDER_DATA'] = work
        record = mod_versions._archive(args.resources, label=args.label, channel=args.channel, description=args.description)
        source = Path(work) / 'mod-versions' / record['id']
        destination = output / record['id']
        if destination.exists():
            mod_versions._verify(destination)
        else:
            shutil.copytree(source, destination)
            mod_versions._verify(destination)
    catalog_path = output / 'catalog.json'
    catalog = mod_versions._json(catalog_path) if catalog_path.exists() else {'schema': 'xz-mod-seeds/1', 'versions': []}
    if not any(v['id'] == record['id'] for v in catalog['versions']):
        catalog['versions'].append({'id': record['id']})
    if args.preferred:
        catalog['preferred_id'] = record['id']
    mod_versions._write(catalog_path, catalog)
    print(json.dumps({k: record[k] for k in ('id', 'label', 'runtime_sha256')}))


if __name__ == '__main__':
    main()
