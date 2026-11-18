"""Independent XZ manual translation catalog validation.

This validates coverage and source freshness only; it does not assess translation
quality or Markdown/figure correctness. A separate review is required.
"""
import hashlib

LOCALES = ('es', 'fr', 'de', 'pt-BR', 'it', 'ja', 'ko', 'zh-Hans', 'zh-Hant',
           'nl', 'pl', 'tr', 'uk', 'ar')


def validate_manual(inventory, translations):
    """Return validation errors; callers must not publish on any error."""
    if inventory.get('schema') != 1 or inventory.get('kind') != 'xz-source-inventory':
        return ['invalid source inventory']
    sources = {e['path']: e for e in inventory['files']
               if e['path'] == 'manual/manual.json' or e['path'].startswith('manual/chapters/')}
    if not sources or 'manual/manual.json' not in sources:
        return ['manual sources missing']
    errors = []
    if set(translations) != set(LOCALES):
        errors.append('locale set must contain exactly all 14 requested languages')
    for locale in LOCALES:
        entries = translations.get(locale)
        if not isinstance(entries, dict):
            errors.append(f'{locale}: catalog missing')
            continue
        if set(entries) != set(sources):
            errors.append(f'{locale}: manual source coverage mismatch')
        for path in set(entries) & set(sources):
            item = entries[path]
            if not isinstance(item, dict) or item.get('source_sha256') != sources[path]['sha256']:
                errors.append(f'{locale}: stale {path}')
            elif not isinstance(item.get('text'), str) or not item['text'].strip():
                errors.append(f'{locale}: empty {path}')
    return errors
