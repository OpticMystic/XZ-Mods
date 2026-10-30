"""Offline XZ catalogs. No service credentials or Library/Wingman configuration."""
import argparse
from collections import Counter
import hashlib
from html import escape
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import sys
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LOCALES = ('es', 'fr', 'de', 'pt', 'it', 'ja', 'ko', 'zh-Hans', 'zh-Hant', 'nl', 'pl', 'tr', 'uk', 'ar')
ATTRS = ('title', 'placeholder', 'aria-label', 'alt')
CONTROLS = ('PLAY/PAUSE', 'BEAT SYNC', 'TAG LIST', 'HOT CUE', 'CUE', 'SHIFT', 'SYNC', 'MASTER', 'MEMORY', 'USB', 'BROWSE', 'INFO', 'MENU', 'BACK')
CONTROL_RE = re.compile(r'(?<!\w)(?:' + '|'.join(re.escape(s) for s in CONTROLS) + r')(?!\w)', re.ASCII)
BRANDS = ('XZ Mods', 'XDJ-XZ', 'Pioneer DJ', 'Rekordbox', 'GZDoom')
BRAND_RE = re.compile(r'(?<![\w])(?:' + '|'.join(re.escape(s) for s in BRANDS) + r')(?![\w])', re.ASCII)
STOCK_ALERTS = ('AUTH CHIP ERROR', 'CANNOT PLAY TRACK', 'UNSUPPORTED', 'NO FILE', 'E-8307: UNSUPPORTED DEVICE')

def protected(text):
    # Authored uppercase hardware references are distinct from ordinary prose.
    return sorted(CONTROL_RE.findall(text) + BRAND_RE.findall(text) + re.findall(r'\bE-\d{4}\b', text, flags=re.ASCII))

def protected_valid(expected, text):
    actual = Counter(protected(text))
    required = Counter(expected)
    return all(actual[token] == count for token, count in required.items()) and sorted(re.findall(r'\bE-\d{4}\b', text, flags=re.ASCII)) == sorted(token for token in expected if re.fullmatch(r'E-\d{4}', token))

def digest(value):
    return hashlib.sha256(value.encode('utf-8')).hexdigest()

def normalize(text):
    return re.sub(r'\s+', ' ', text).strip()

def placeholders(text):
    return sorted(re.findall(r'\{[A-Za-z_]\w*\}|%[sdif]|\$\{[^}]+\}', text))

class TextParser(HTMLParser):
    def __init__(self, add):
        super().__init__(convert_charrefs=True)
        self.add = add
        self.skip = []
    def handle_starttag(self, tag, attrs):
        if tag in ('script', 'style', 'svg', 'textarea', 'code', 'pre'): self.skip.append(tag)
        if not self.skip:
            for name, value in attrs:
                if name in ATTRS and value: self.add(value, f'{tag}@{name}:{self.getpos()[0]}')
    def handle_endtag(self, tag):
        if self.skip and self.skip[-1] == tag: self.skip.pop()
    def handle_data(self, text):
        if not self.skip and normalize(text): self.add(text, f'text:{self.getpos()[0]}')

def extract(root=ROOT):
    from xz_localization import inventory
    sources = {item['path']: item['sha256'] for item in inventory(root)['files']
               if item['path'] != 'ui/xz-l10n-data.js'}
    for name in ('tools/xz_i18n.py', 'tools/xz_extract_js.cjs', 'tools/vendor/acorn.cjs', 'tools/xz_manual_search.py'):
        sources[name] = digest((root / name).read_text(encoding='utf-8'))
    entries = {}; manual_documents = {}; manual_search = []; manual_index = {}
    paths = sorted(set(root.glob('ui/*.css')) | set(root.glob('ui/*.js')) | set(root.glob('ui/*.html')) |
                   set(root.glob('manual/chapters/*.md')) | {root / 'manual/manual.json'})
    for path in paths:
        if path.name == 'xz-l10n-data.js': continue
        sources[path.relative_to(root).as_posix()] = digest(path.read_text(encoding='utf-8'))
    def add(surface, path, text, context):
        text = normalize(text)
        if not text or not any(c.isalpha() for c in text): return
        key = surface + ':' + digest(text)[:20]
        entry = entries.setdefault(key, {'source': text, 'surface': surface, 'source_sha256': digest(text),
                                         'placeholders': placeholders(text), 'protected': protected(text),
                                         'immutable': text in STOCK_ALERTS, 'contexts': []})
        entry['contexts'].append(path + ':' + context)
    path = root / 'ui/index.html'
    TextParser(lambda text, context: add('desktop', 'ui/index.html', text, context)).feed(path.read_text(encoding='utf-8'))
    js_paths = [p for p in sorted((root / 'ui').glob('*.js')) if p.name != 'xz-l10n-data.js']
    rows = json.loads(subprocess.check_output(['node', str(root / 'tools/xz_extract_js.cjs'), *map(str, js_paths)], encoding='utf-8'))
    for row in rows:
        name = Path(row['file']).relative_to(root).as_posix()
        TextParser(lambda text, context: add('desktop', name, text, str(row['line']) + ':' + context)).feed(row['text'])
    # Generated manual scripts contain JSON, not executable translation input.
    for path in sorted((root / 'ui/manual').rglob('*.js')):
        raw = path.read_text(encoding='utf-8')
        name = path.relative_to(root).as_posix()
        sources[name] = digest(raw)
        if path.name == 'map.js': continue
        value = json.loads(raw[raw.index('=', raw.index('\n')) + 1:].rstrip().removesuffix(';')) if path.name in ('index.js', 'search.js') else json.loads(raw[raw.index(']=') + 2:].rstrip().removesuffix(';'))
        if path.parent.name == 'chapters': manual_documents[path.stem] = value.partition('</header>')[2]
        elif path.name == 'search.js': manual_search = value
        elif path.name == 'index.js': manual_index = value
        def walk(value, context):
            if isinstance(value, str):
                TextParser(lambda text, pos: add('manual', name, text, context + ':' + pos)).feed(value)
            elif isinstance(value, list):
                for i, item in enumerate(value):
                    if isinstance(item, dict): walk(item, context + f'/{i}')
            elif isinstance(value, dict):
                for key, item in value.items():
                    if key in ('title', 'summary', 'text', 'group', 'chapters', 'tour', 't', 'x'): walk(item, context + '/' + key)
        walk(value, '')
    section_candidates = []
    for row in manual_search:
        key = 'manual:' + digest(normalize(row['x']))[:20]
        if key in entries and all(c.startswith('ui/manual/search.js:') for c in entries[key]['contexts']):
            section_candidates.append({'id': key, 'chapter': row['c'], 'anchor': row['a']})
    section_counts = Counter(row['id'] for row in section_candidates)
    sections = [row for row in section_candidates if section_counts[row['id']] == 1]
    titles = {row['slug']: row['title'] for row in manual_index.get('chapters', [])}
    return {'schema': 1, 'product': 'xz-mods', 'source_revision': digest(json.dumps(sources, sort_keys=True)),
            'sources': sources, 'entries': entries,
            'manual_documents': {slug: {'title': titles[slug], 'html': body} for slug, body in manual_documents.items()},
            'manual_search_sections': sections,
            'scope': 'Desktop HTML, JavaScript literals/templates, wizard and generated manual/search text. Controller rendering remains pending.'}

def validate(source, catalog, complete=False, surface=None):
    errors = []
    if catalog.get('schema') != 1 or catalog.get('product') != 'xz-mods' or {'pt-BR': 'pt'}.get(catalog.get('locale'), catalog.get('locale')) not in LOCALES:
        return ['invalid XZ catalog identity']
    if catalog.get('source_revision') != source['source_revision']: errors.append('source changed; all translations require review')
    entries = catalog.get('entries')
    if not isinstance(entries, dict): return errors + ['invalid entries']
    for key, item in entries.items():
        original = source['entries'].get(key)
        if not original or not isinstance(item, dict): errors.append(f'{key}: unknown entry'); continue
        text = item.get('text')
        if item.get('source_sha256') != original['source_sha256']: errors.append(f'{key}: stale source')
        if not isinstance(text, str) or not text.strip(): errors.append(f'{key}: empty translation')
        elif placeholders(text) != original['placeholders']: errors.append(f'{key}: placeholders changed')
        elif not protected_valid(original['protected'], text): errors.append(f'{key}: printed control or error code changed')
        elif original['immutable'] and text != original['source']: errors.append(f'{key}: stock alert changed')
    if complete:
        for key, item in source['entries'].items():
            if (not surface or item['surface'] == surface) and key not in entries: errors.append(f'{key}: missing')
    return errors

def bundle(root=ROOT, publish=False):
    source = extract(root)
    output = root / 'ui/xz-l10n-data.js'
    if output.exists():
        raw = output.read_text(encoding='utf-8')
        current = json.loads(raw.removeprefix('window.XZ_L10N_DATA=').strip().removesuffix(';'))
        if 'pt-BR' in current['catalogs'] and 'pt' not in current['catalogs']:
            current['catalogs']['pt'] = dict(current['catalogs'].pop('pt-BR'), locale='pt')
        if current['source']['source_revision'] == source['source_revision'] and all(
                not validate(source, current['catalogs'].get(locale, {}), publish) for locale in LOCALES):
            return current
    catalogs = {}
    for locale in LOCALES:
        catalog = json.loads((root / 'localization' / f'{locale}.json').read_text(encoding='utf-8'))
        errors = validate(source, catalog, publish)
        if errors: raise ValueError(f'{locale}: ' + '; '.join(errors[:8]) + f' ({len(errors)} errors)')
        catalogs[locale] = catalog
    return {'source': source, 'catalogs': catalogs}

def script(data):
    return 'window.XZ_L10N_DATA=' + json.dumps(data, ensure_ascii=False, separators=(',', ':')).replace('</', '<\\/').replace('\u2028', '\\u2028').replace('\u2029', '\\u2029') + ';\n'

def translate_html(text, source, catalog):
    translations = {item['source']: catalog['entries'][key]['text'] for key, item in source['entries'].items()
                    if item['surface'] == 'manual' and key in catalog['entries']}
    edits = []
    lines = text.splitlines(keepends=True)
    offsets = [0]
    for line in lines: offsets.append(offsets[-1] + len(line))
    class Translator(HTMLParser):
        def __init__(self):
            super().__init__(convert_charrefs=True)
            self.skip = []
        def handle_starttag(self, tag, attrs):
            if tag in ('script', 'style', 'code', 'pre', 'svg'): self.skip.append(tag)
            if self.skip: return
            raw = self.get_starttag_text()
            replacement = raw
            for name, value in attrs:
                if name not in ATTRS or not value: continue
                translated = translations.get(normalize(value))
                if translated is None: continue
                pattern = r'(\b' + re.escape(name) + r'\s*=\s*)(["\'])(.*?)\2'
                replacement = re.sub(pattern, lambda m: m[1] + m[2] + escape(translated, quote=True) + m[2], replacement, count=1, flags=re.I | re.S)
            if replacement != raw:
                row, column = self.getpos()
                start = offsets[row - 1] + column
                edits.append((start, start + len(raw), replacement))
        def handle_endtag(self, tag):
            if self.skip and self.skip[-1] == tag: self.skip.pop()
        def handle_data(self, value):
            if self.skip: return
            translated = translations.get(normalize(value))
            if translated is not None:
                row, column = self.getpos()
                start = offsets[row - 1] + column
                end = text.find('<', start)
                if end < 0: end = len(text)
                edits.append((start, end, re.sub(r'\S(?:[\s\S]*\S)?', lambda _: escape(translated), value)))
    Translator().feed(text)
    for start, end, replacement in reversed(edits): text = text[:start] + replacement + text[end:]
    return text

def localize_output(out, locale, draft=False, root=ROOT):
    source = extract(root)
    catalog = bundle(root)['catalogs'][locale]
    errors = validate(source, catalog, complete=not draft, surface='manual')
    if errors: raise ValueError('; '.join(errors[:8]))
    for path in Path(out).rglob('*.html'):
        text = translate_html(path.read_text(encoding='utf-8'), source, catalog)
        text = re.sub(r'<html[^>]*>', f'<html lang="{locale}" dir="{"rtl" if locale == "ar" else "ltr"}">', text, count=1)
        if draft: text = re.sub(r'(<body[^>]*>)', r'\1<p role="status">Translation preview: incomplete text falls back to English. Do not publish.</p>', text, count=1)
        path.write_text(text, encoding='utf-8')
    # Embedded manual scripts retain their assignment and all links/anchors.
    translations = {item['source']: catalog['entries'][key]['text'] for key, item in source['entries'].items()
                    if item['surface'] == 'manual' and key in catalog['entries']}
    def walk(value):
        if isinstance(value, str): return translate_html(value, source, catalog) if '<' in value else translations.get(normalize(value), value)
        if isinstance(value, list): return [walk(item) for item in value]
        if isinstance(value, dict): return {key: walk(item) if key in ('title', 'summary', 'text', 'group', 'chapters', 'tour', 't', 'x') else item for key, item in value.items()}
        return value
    for path in Path(out).rglob('*.js'):
        raw = path.read_text(encoding='utf-8')
        if not raw.startswith('// Generated by tools/build_manual.py'): continue
        split = raw.index(']=') + 2 if 'window.XZ_MANUAL_CHAPTERS' in raw else raw.index('=', raw.index('\n')) + 1
        value = walk(json.loads(raw[split:].strip().removesuffix(';')))
        path.write_text(raw[:split] + json.dumps(value, ensure_ascii=False).replace('</', '<\\/') + ';\n', encoding='utf-8')
    (Path(out) / 'localization.json').write_text(json.dumps({'locale': locale, 'draft': draft, 'source_revision': source['source_revision'], 'translated': len(catalog['entries'])}), encoding='utf-8')

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('command', choices=('extract', 'build', 'check', 'import'))
    p.add_argument('--file', type=Path)
    p.add_argument('--publish', action='store_true')
    a = p.parse_args()
    try:
        if a.command == 'extract':
            if not a.file: p.error('--file is required')
            a.file.write_text(json.dumps(extract(), ensure_ascii=False, indent=2) + '\n', encoding='utf-8'); return 0
        if a.command == 'import':
            if not a.file: p.error('--file is required')
            catalog = json.loads(a.file.read_text(encoding='utf-8'))
            if catalog.get('locale') == 'pt-BR': catalog['locale'] = 'pt'
            errors = validate(extract(), catalog, complete=True)
            if errors: raise ValueError('; '.join(errors[:8]))
            data = bundle(publish=True)
            data['catalogs'][catalog['locale']] = catalog
            from xz_translate import atomic
            atomic(ROOT / 'ui/xz-l10n-data.js', script(data))
        data = bundle(publish=a.publish or a.command in ('build', 'import'))
        output = ROOT / 'ui/xz-l10n-data.js'
        expected = script(data)
        if a.command == 'check':
            if not output.exists() or output.read_text(encoding='utf-8') != expected: raise ValueError('generated XZ catalog is stale; run tools/xz_i18n.py build')
        else:
            from xz_translate import atomic
            atomic(output, expected)
        totals = Counter(e['surface'] for e in data['source']['entries'].values())
        for locale, catalog in data['catalogs'].items():
            counts = Counter(data['source']['entries'][key]['surface'] for key in catalog['entries'])
            print(locale, ', '.join(f'{kind} {counts[kind]}/{total}' for kind, total in totals.items()))
        return 0
    except (OSError, ValueError, KeyError) as exc:
        print(f'XZ localization: {exc}', file=sys.stderr); return 1

if __name__ == '__main__': sys.exit(main())
