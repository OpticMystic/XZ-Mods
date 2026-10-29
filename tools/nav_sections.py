"""Validate the section-to-flow inventory against the current chapter bytes.

  python tools/nav_sections.py                       check
  python tools/nav_sections.py --restamp KEY ...     accept reviewed text changes in the named sections
"""
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# A section's own ```nav``` video blocks are not its text: placing a video never stales the assignment.
NAV_BLOCK = re.compile(r'^```nav [^\n]*\n(?:(?!```)[^\n]*\n)*```\n\n?', re.M)


def sections():
    result = {}
    for path in sorted((ROOT / 'manual/chapters').glob('*.md')):
        text = path.read_text(encoding='utf-8')
        matches = list(re.finditer(r'^(#{2,3}) (.+)$', text, re.M))
        seen = {}
        for i, match in enumerate(matches):
            title = match[2].strip()
            slug = re.sub(r'\s*\{[^}]+\}', '', title).lower()
            slug = re.sub(r'[^\w\s-]', '', slug)
            slug = re.sub(r'\s+', '-', slug)
            duplicate = seen.get(slug, 0)
            seen[slug] = duplicate + 1
            anchor = slug + (f'-{duplicate}' if duplicate else '')
            body = text[match.start():matches[i+1].start() if i+1 < len(matches) else len(text)]
            body = NAV_BLOCK.sub('', body)
            key = f'{path.stem}#{anchor}'
            result[key] = dict(title=title, level=len(match[1]), chapter=path.name,
                               sha256=hashlib.sha256(body.encode()).hexdigest())
    return result


def check():
    expected = sections()
    inventory = json.loads((ROOT/'manual/nav/SECTIONS.json').read_text(encoding='utf-8'))
    rows = inventory['sections']
    if len(rows) != len({r['section'] for r in rows}) or {r['section'] for r in rows} != set(expected):
        raise ValueError('section inventory is incomplete, duplicated or stale')
    scripts = {p.stem for p in (ROOT/'manual/nav').glob('[a-z]*.json')}
    for row in rows:
        if row['sha256'] != expected[row['section']]['sha256']:
            raise ValueError('section text changed: '+row['section'])
        if row['flows'] is None:
            if not row.get('reason') or not row.get('detail'):
                raise ValueError('unassigned section needs a reason: '+row['section'])
        elif not row['flows'] or not set(row['flows']) <= scripts:
            raise ValueError('unknown or empty flow assignment: '+row['section'])
    return rows


def restamp(keys):
    """Refresh the text hashes of the named sections after reviewing their change; flows are unchanged."""
    expected = sections()
    path = ROOT/'manual/nav/SECTIONS.json'
    raw = path.read_bytes()
    inventory = json.loads(raw)
    unknown = set(keys) - {r['section'] for r in inventory['sections']}
    if unknown:
        raise SystemExit('not in the inventory (add a row first): ' + ', '.join(sorted(unknown)))
    for row in inventory['sections']:
        if row['section'] in keys:
            row['sha256'] = expected[row['section']]['sha256']
    text = json.dumps(inventory, indent=2, ensure_ascii=False) + '\n'
    path.write_bytes((text.replace('\n', '\r\n') if b'\r\n' in raw else text).encode('utf-8'))


if __name__ == '__main__':
    if sys.argv[1:2] == ['--restamp']:
        restamp(set(sys.argv[2:]))
    rows = check()
    print('section inventory current:', len(rows), 'sections;', sum(r['flows'] is not None for r in rows), 'assigned')
