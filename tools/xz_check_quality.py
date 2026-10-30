"""Full source freshness, completeness and quality gate for the canonical XZ bundle."""
import argparse
import json
from pathlib import Path
from xz_i18n import ROOT, bundle, script
from xz_translation_quality import require_quality

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt',type=Path)
    args=parser.parse_args()
    data=bundle(ROOT,publish=True)
    if (ROOT/'ui/xz-l10n-data.js').read_text(encoding='utf-8')!=script(data):
        raise ValueError('Canonical bundle differs from the checked complete generation')
    receipt={'source_revision':data['source']['source_revision'],'locales':require_quality(data)}
    if args.receipt: args.receipt.write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(receipt))
    return 0

if __name__=='__main__': raise SystemExit(main())
