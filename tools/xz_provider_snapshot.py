"""Freeze only a current, complete canonical generation for the packaged provider."""
import json
from xz_i18n import ROOT, bundle
from xz_translate import atomic
from xz_translation_quality import require_quality

if __name__ == '__main__':
    data = bundle(ROOT, publish=True)
    require_quality(data)
    atomic(ROOT / 'localization/source.json', json.dumps(data['source'], ensure_ascii=False))
