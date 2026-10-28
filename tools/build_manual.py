"""Build the XZ Mods manual and prove it describes the current firmware.

  render  Draw every firmware screen from source with the host preview programs (never touches an XZ).
  check   Fail when the manual is stale: a documented screen changed or vanished, a watched behaviour
          source changed, or a new screen, MODS page or setting has no place in the manual.
  accept  Record the current screens and watched sources as reviewed (run after updating the chapters).
  proof   Draw every figure with its numbered callouts onto PNGs, for checking callout placement.
  site    Check, then write the static manual site.
  app     Check, then write the manual the desktop app shows (ui/manual/). `app --check` fails when it is not current.

The manual lives in manual/ (see manual/README.md). Screens come from the C renderers, so a pixel change
in a documented screen fails `check` until a person updates the chapter and runs `accept`.
"""
import argparse
from fnmatch import fnmatchcase
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

APP = Path(__file__).resolve().parents[1]
MANUAL = APP / 'manual'
MODS = APP / 'source/xdj-xz-toolkit/mods'
LOCK = MANUAL / 'shots.lock.json'
SOURCES = {'modern': 'shell', 'classic': 'ui'}  # shot prefix -> folder the renderers write inside the render directory
STATIC = {'app': MANUAL / 'app-shots', 'capture': MANUAL / 'captures', 'hw': MANUAL / 'hardware'}  # curated pictures


def sha256(path):
    """Content hash. Text is hashed with LF line ends, so a checkout's line-end conversion does not read as a change."""
    data = Path(path).read_bytes()
    if Path(path).suffix.lower() not in ('.png', '.webp', '.jpg'): data = data.replace(b'\r\n', b'\n')
    return hashlib.sha256(data).hexdigest()


def load_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


# ---------------------------------------------------------------- render

def render(zig, out):
    out = Path(out).resolve()
    if out.exists(): shutil.rmtree(out)
    out.mkdir(parents=True)
    zig = str(Path(zig).resolve())
    for folder, script in (('shell', MODS / 'shell/preview.py'), ('ui', MODS / 'ui/preview.py')):
        subprocess.run([sys.executable, str(script), '--zig', zig, '--output', str(out / folder)], check=True,
                       cwd=script.parent, stdout=subprocess.DEVNULL)
    print(out)


# ---------------------------------------------------------------- manual model

FIGURE_LINE = re.compile(r'^(\d+)\s*@\s*(\d+)\s*,\s*(\d+)\s+(.+?)\s*\|\s*(.+)$')


def parse_chapter(path):
    text = path.read_text(encoding='utf-8').replace('\r\n', '\n')
    meta = {}
    if text.startswith('---\n'):
        head, text = text[4:].split('\n---\n', 1)
        for line in head.splitlines():
            if line.strip():
                key, value = line.split(':', 1)
                meta[key.strip()] = value.strip()
    meta['settings'] = [s.strip() for s in meta.get('settings', '').split(',') if s.strip()]
    return meta, text


# A MODS page shot written classic:theme-{look}-<page> is drawn in each interface's own look: MODERN always
# uses the FLAGSHIP theme (settings.c xz_settings_look), CLASSIC the default classic theme.
LOOK = '{look}'
LOOKS = {'modern': '24', 'classic': '0'}


def look_shot(shot, iface):
    return shot.replace(LOOK, LOOKS[iface])


def figures(text):
    """Every ```figure and ```gallery block in a chapter body, as (kind, shots, caption, callouts)."""
    found = []
    for kind, args, body in re.findall(r'^```(figure|gallery)([^\n]*)\n(.*?)^```', text, re.M | re.S):
        lines = [l for l in body.splitlines() if l.strip()]
        if kind == 'figure':
            callouts = [FIGURE_LINE.match(l) for l in lines]
            caption = ' '.join(l for l, m in zip(lines, callouts) if not m)
            shot = args.split()[0]
            shots = [look_shot(shot, i) for i in LOOKS] if LOOK in shot else [shot]
            found.append(('figure', shots, caption, [m.groups() for m in callouts if m]))
        else:
            found.append(('gallery', [l.split()[0] for l in lines], '', []))
    return found


def load_manual(render_dir):
    book = load_json(MANUAL / 'manual.json')
    chapters = []
    for entry in book['chapters']:
        path = MANUAL / 'chapters' / (entry['slug'] + '.md')
        meta, body = parse_chapter(path) if path.is_file() else ({'settings': []}, '')
        body = expand_generated(body, render_dir)
        chapters.append({**entry, 'meta': meta, 'body': body, 'figures': figures(body), 'exists': path.is_file()})
    return book, chapters


def shot_path(shot, render_dir):
    prefix, _, name = shot.partition(':')
    if prefix in STATIC:
        matches = sorted(STATIC[prefix].glob(name + '.*'))
        return matches[0] if matches else STATIC[prefix] / (name + '.png')
    if prefix not in SOURCES: raise SystemExit(f'Unknown shot source {prefix!r} in {shot}')
    return Path(render_dir) / SOURCES[prefix] / (name + '.png')


def rendered_shots(render_dir):
    """Every screen the renderers drew, as shot ids."""
    shots = []
    for prefix, folder in SOURCES.items():
        shots += [f'{prefix}:{p.stem}' for p in sorted((Path(render_dir) / folder).glob('*.png'))]
    return shots


# ---------------------------------------------------------------- check / accept

def referenced(chapters):
    refs = {}
    for chapter in chapters:
        for _, shots, _, _ in chapter['figures']:
            for shot in shots: refs.setdefault(shot, chapter['slug'])
    return refs


def problems(render_dir):
    book, chapters = load_manual(render_dir)
    coverage = load_json(MANUAL / 'coverage.json')
    lock = load_json(LOCK) if LOCK.is_file() else {'shots': {}, 'watch': {}}
    out = []
    for chapter in chapters:
        if not chapter['exists']: out.append(f"chapter {chapter['slug']}: manual/chapters/{chapter['slug']}.md is missing")
    refs = referenced(chapters)
    for shot, slug in sorted(refs.items()):
        path = shot_path(shot, render_dir)
        if not path.is_file():
            out.append(f'{slug}: screen {shot} no longer renders. Replace or remove that figure.')
        elif lock['shots'].get(shot) != sha256(path):
            out.append(f'{slug}: screen {shot} changed since the manual was reviewed. Check its text and callouts '
                       f'(tools/build_manual.py proof), then accept it.')
    excluded = [(rule['glob'], rule['why']) for rule in coverage['screens_not_documented']]
    for rule in coverage['screens_not_documented']:
        if not rule.get('why'): out.append(f"coverage.json: exclusion {rule['glob']} needs a reason")
    for shot in rendered_shots(render_dir):
        if shot not in refs and not any(fnmatchcase(shot, glob) for glob, _ in excluded):
            out.append(f'new screen {shot}: show it in a chapter, or add it to coverage.json with the reason it is not documented')
    pages = [p.stem[len('theme-0-'):] for p in (Path(render_dir) / 'ui').glob('theme-0-*.png')]
    for page in sorted(pages):
        if not any(re.fullmatch(rf'classic:theme-\d+-{re.escape(page)}', s) for s in refs) and \
           not any(fnmatchcase(f'classic:theme-0-{page}', glob) for glob, _ in excluded):
            out.append(f'MODS page {page}: no chapter shows it')
    schema = load_json(MODS / 'settings-schema.json')
    explained = {key for c in chapters for key in c['meta']['settings']}
    exempt = {row['key'] for row in coverage.get('settings_not_documented', [])}
    for field in schema['fields']:
        if field['key'] not in explained and field['key'] not in exempt:
            out.append(f"setting {field['key']} ({field['label']}): list it under settings: in the chapter that explains it")
    for key in sorted(explained - {f['key'] for f in schema['fields']}):
        out.append(f'setting {key} is listed by a chapter but is not in settings-schema.json')
    for row in coverage['watch']:
        path = APP / row['path']
        if not path.is_file():
            out.append(f"watched source {row['path']} is gone: update chapter {row['chapter']} and coverage.json")
        elif lock['watch'].get(row['path']) != sha256(path):
            out.append(f"{row['chapter']}: {row['path']} changed ({row['why']}). Check the chapter still matches, then accept.")
    return out


def check(render_dir):
    found = problems(render_dir)
    for line in found: print('STALE ' + line)
    print(f'manual: {len(found)} problem(s)' if found else 'manual: current')
    return 1 if found else 0


def accept(render_dir, only):
    _, chapters = load_manual(render_dir)
    coverage = load_json(MANUAL / 'coverage.json')
    lock = load_json(LOCK) if LOCK.is_file() else {'shots': {}, 'watch': {}}
    refs = referenced(chapters)
    shots = {}
    for shot in sorted(refs):
        path = shot_path(shot, render_dir)
        if not path.is_file(): raise SystemExit(f'{shot} does not render; fix the chapter first')
        shots[shot] = sha256(path) if not only or shot in only else lock['shots'].get(shot)
    watch = {row['path']: sha256(APP / row['path']) for row in coverage['watch'] if not only or row['path'] in only}
    lock = {'format': 'xz-manual-lock/1', 'shots': {k: v for k, v in shots.items() if v},
            'watch': {**({} if not only else lock['watch']), **watch}}
    LOCK.write_text(json.dumps(lock, indent=1, sort_keys=True) + '\n', encoding='utf-8')
    print(f"accepted {len(lock['shots'])} screens and {len(lock['watch'])} watched sources")


# ---------------------------------------------------------------- markdown (the manual's controlled subset)

def inline(text):
    text = html.escape(text, quote=False)
    text = re.sub(r'`([^`]+)`', lambda m: '<code>' + m.group(1) + '</code>', text)
    text = re.sub(r'\[\[([^\]]+)\]\]', lambda m: '<kbd>' + m.group(1) + '</kbd>', text)
    text = re.sub(r'\{(experimental|modern|classic|new)\}', lambda m: f'<span class="tag tag--{m.group(1)}">{m.group(1)}</span>', text)
    text = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', text)
    text = re.sub(r'(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])', r'<em>\1</em>', text)
    text = re.sub(r'\[([^\]]+)\]\(([^)\s]+)\)', lambda m: LINK_TAG(m.group(1), m.group(2)), text)
    return text


def site_link_tag(label, target):
    return f'<a href="{link(target)}">{label}</a>'


LINK_TAG = site_link_tag  # the app build swaps in its own (app_link_tag)


def link(target):
    if re.match(r'^[a-z0-9-]+(#[\w-]+)?$', target):  # another chapter, optionally a section in it
        slug, _, anchor = target.partition('#')
        return BASE + slug + ('#' + anchor if anchor else '')
    if target.startswith('#') or re.match(r'^https?://', target): return html.escape(target)
    raise SystemExit(f'Link {target!r}: use a chapter slug, #section or an https URL')


def slugify(text):
    return re.sub(r'[^a-z0-9]+', '-', re.sub(r'<[^>]+>|\[\[|\]\]|\{\w+\}', '', text).lower()).strip('-')


IFACE = re.compile(r'\s*\{(modern|classic)\}\s*$')


def blocks(body, ctx):
    """Render a chapter body. Returns html and the list of (level, id, title) headings."""
    out, toc, lines, i = [], [], body.split('\n'), 0
    while i < len(lines):
        line = lines[i]
        if not line.strip(): i += 1; continue
        fence = re.match(r'^```(figure|gallery|explainer)(.*)$', line)
        if fence:
            j = i + 1
            while not lines[j].startswith('```'): j += 1
            args, inner = fence.group(2).strip(), [l for l in lines[i + 1:j] if l.strip()]
            if fence.group(1) == 'explainer':  # animated explainers exist only in the app; the site keeps the prose around them
                if ctx.get('explainer'): out.append(ctx['explainer'](args, inner))
            else: out.append(ctx['figure'](fence.group(1), args, inner))
            i = j + 1; continue
        heading = re.match(r'^(#{2,4})\s+(.+)$', line)
        if heading:
            level, title = len(heading.group(1)), heading.group(2)
            iface = IFACE.search(title)
            title = IFACE.sub('', title)
            anchor = slugify(title)
            attr = f' data-iface="{iface.group(1)}"' if iface else ''
            if level <= 3: toc.append((level, anchor, inline(title), iface.group(1) if iface else ''))
            badge = f' <span class="tag tag--{iface.group(1)}">{iface.group(1)}</span>' if iface else ''
            out.append(f'<h{level} id="{anchor}"{attr}><a class="anchor" href="#{anchor}" aria-hidden="true">#</a>{inline(title)}{badge}</h{level}>')
            i += 1; continue
        note = re.match(r'^>\s*\*\*(Tip|Note|Caution|Experimental)\.\*\*\s*(.*)$', line)
        if line.startswith('>'):
            j = i
            while j < len(lines) and lines[j].startswith('>'): j += 1
            text = ' '.join(l[1:].strip() for l in lines[i:j])
            kind = note.group(1).lower() if note else 'note'
            text = re.sub(r'^\*\*(Tip|Note|Caution|Experimental)\.\*\*\s*', '', text)
            label = note.group(1) if note else 'Note'
            out.append(f'<aside class="callout callout--{kind}"><b>{label}</b><p>{inline(text)}</p></aside>')
            i = j; continue
        if line.startswith('|'):
            j = i
            while j < len(lines) and lines[j].startswith('|'): j += 1
            rows = [[c.strip() for c in l.strip().strip('|').split('|')] for l in lines[i:j]]
            head, body_rows = rows[0], [r for r in rows[1:] if not all(re.fullmatch(r':?-+:?', c) for c in r)]
            out.append('<div class="table"><table><thead><tr>' + ''.join(f'<th>{inline(c)}</th>' for c in head) +
                       '</tr></thead><tbody>' + ''.join('<tr>' + ''.join(f'<td>{inline(c)}</td>' for c in r) + '</tr>'
                                                       for r in body_rows) + '</tbody></table></div>')
            i = j; continue
        item = re.match(r'^(\s*)(-|\d+\.)\s+(.*)$', line)
        if item:
            ordered = item.group(2) != '-'
            items, j = [], i
            while j < len(lines):
                m = re.match(r'^(\s*)(-|\d+\.)\s+(.*)$', lines[j])
                if m and (m.group(2) != '-') == ordered and not m.group(1): items.append(m.group(3)); j += 1
                elif lines[j].startswith('   ') and items and lines[j].strip(): items[-1] += ' ' + lines[j].strip(); j += 1
                else: break
            tag = 'ol class="steps"' if ordered else 'ul'
            out.append(f'<{tag}>' + ''.join(f'<li>{inline(t)}</li>' for t in items) + f'</{tag.split()[0]}>')
            i = j; continue
        j = i
        while j < len(lines) and lines[j].strip() and not re.match(r'^(#{2,4}\s|```|>|\||\s*(-|\d+\.)\s)', lines[j]): j += 1
        out.append(f'<p>{inline(" ".join(l.strip() for l in lines[i:j]))}</p>')
        i = j
    return '\n'.join(out), toc


# ---------------------------------------------------------------- site

BASE = '/manual/'


def build_site(render_dir, out, book, chapters, stale):
    from PIL import Image
    out = Path(out).resolve()
    if out.exists(): shutil.rmtree(out)
    (out / 'shots').mkdir(parents=True)
    template = (MANUAL / 'template/page.html').read_text(encoding='utf-8')
    images = {}

    def image(shot):
        if shot in images: return images[shot]
        path = shot_path(shot, render_dir)
        if path.suffix == '.svg':
            name = f'{sha256(path)[:16]}.svg'
            shutil.copyfile(path, out / 'shots' / name)
            w, h = svg_size(path)
        else:
            img = Image.open(path).convert('RGB')
            w, h = img.size
            name = f'{sha256(path)[:16]}.webp'
            img.save(out / 'shots' / name, 'WEBP', lossless=True, method=6)
        images[shot] = (BASE + 'shots/' + name, w, h)
        return images[shot]

    def figure(kind, args, lines):
        if kind == 'gallery':
            cells = []
            for line in lines:
                shot, _, caption = line.partition(' ')
                src, w, h = image(shot)
                cells.append(f'<figure class="gallery__item"><button class="shot" data-zoom="{src}" data-w="{w}" data-h="{h}" '
                             f'aria-label="Enlarge: {html.escape(caption.strip())}"><img src="{src}" width="{w}" height="{h}" '
                             f'loading="lazy" alt="{html.escape(caption.strip())}"></button><figcaption>{inline(caption.strip())}</figcaption></figure>')
            iface = re.search(r'\{(modern|classic)\}', args)
            attr = f' data-iface="{iface.group(1)}"' if iface else ''
            return f'<div class="gallery"{attr}>' + ''.join(cells) + '</div>'
        shot, _, label = args.partition(' ')
        iface = re.search(r'\{(modern|classic)\}', label)
        label = re.sub(r'\s*\{(modern|classic)\}', '', label).strip()
        attr = f' data-iface="{iface.group(1)}"' if iface else ''
        callouts = [FIGURE_LINE.match(l) for l in lines]
        caption = ' '.join(l.strip() for l, m in zip(lines, callouts) if not m)
        callouts = [m.groups() for m in callouts if m]
        alt = html.escape(caption or label or shot)
        variants = [(look_shot(shot, iface_name), iface_name) for iface_name in LOOKS] if LOOK in shot else [(shot, '')]
        pictures = ''
        for variant, iface_name in variants:
            src, w, h = image(variant)
            show = f' data-iface="{iface_name}"' if iface_name else ''
            pictures += (f'<img src="{src}" width="{w}" height="{h}" loading="lazy" alt="{alt}"{show}>'
                         f'<button class="zoom" data-zoom="{src}" data-w="{w}" data-h="{h}" aria-label="Enlarge screen"{show}>⤢</button>')
        bar = {'modern': 'XZ screen · MODERN', 'classic': 'XZ screen' if variants[0][1] else 'XZ screen · CLASSIC', 'app': 'XZ Mods app',
               'capture': 'XZ screen · photo of the player', 'hw': 'XDJ-XZ top panel'}[shot.partition(':')[0]]
        marks = ''.join(f'<button class="mark" style="left:{int(x) / w * 100:.3f}%;top:{int(y) / h * 100:.3f}%" '
                        f'data-n="{n}" aria-label="{n}: {html.escape(name)}">{n}</button>' for n, x, y, name, _ in callouts)
        legend = ''.join(f'<li data-n="{n}"><span class="chip">{n}</span><div><b>{inline(name)}</b><p>{inline(text)}</p></div></li>'
                         for n, x, y, name, text in callouts)
        ctx_id = f'fig-{slugify(shot)}'
        kind_class = ' figure--hw' if shot.startswith('hw:') else ''
        return (f'<figure class="figure{kind_class}" id="{ctx_id}"{attr}><div class="device"><div class="device__bar"><i></i><i></i><i></i>'
                f'<span>{html.escape(label or bar)}</span></div><div class="device__screen">{pictures}{marks}</div></div>'
                + (f'<figcaption>{inline(caption)}</figcaption>' if caption else '')
                + (f'<ol class="legend">{legend}</ol>' if legend else '') + '</figure>')

    version = load_json(APP / 'src-tauri/tauri.conf.json')['version']
    commit = subprocess.run(['git', '-C', str(APP), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    dirty = bool(subprocess.run(['git', '-C', str(APP), 'status', '--porcelain', '--', 'manual', 'source'],
                                capture_output=True, text=True).stdout.strip())
    stamp = f'{commit[:7]}{" + local edits" if dirty else ""}'
    draft = ('<p class="draft" role="note">Draft build. The manual failed its freshness check, so parts may not match the firmware. '
             'Not for publishing.</p>') if stale else ''
    schema = load_json(MODS / 'settings-schema.json')
    explained = {key: c for c in chapters for key in c['meta']['settings']}
    search, pages = [], []
    groups = []
    for c in chapters:
        if not groups or groups[-1][0] != c['group']: groups.append((c['group'], []))
        groups[-1][1].append(c)
    for index, c in enumerate(chapters):
        body = c['body']
        if c['slug'] == 'settings':
            body += '\n' + settings_table(schema, explained)
        content, toc = blocks(body, {'figure': figure})
        thumb = next((shots[0] for kind, shots, _, _ in c['figures'] if kind == 'figure'), None)
        c['thumb'] = image(thumb)[0] if thumb else ''
        c['html'], c['toc'] = content, toc
        for level, anchor, title, _ in [(2, '', html.escape(c['title']), '')] + toc:
            search.append({'t': re.sub('<[^>]+>', '', title), 'c': c['title'], 'u': BASE + c['slug'] + ('#' + anchor if anchor else '')})
        text = re.sub(r'<[^>]+>', ' ', content)
        search[-len(toc) - 1]['x'] = re.sub(r'\s+', ' ', html.unescape(text))[:6000]
    for index, c in enumerate(chapters):
        nav = ''.join(f'<div class="nav__group"><p>{html.escape(g)}</p><ul>' + ''.join(
            f'<li><a href="{BASE}{x["slug"]}"{" aria-current=page" if x is c else ""}>{html.escape(x["title"])}</a>'
            + (('<ul class="nav__sections">' + ''.join(f'<li><a href="#{a}"{f" data-iface={i}" if i else ""}>{t}</a></li>'
                                                     for lv, a, t, i in x['toc'] if lv == 2) + '</ul>') if x is c else '') + '</li>'
            for x in members) + '</ul></div>' for g, members in groups)
        toc = ''.join(f'<li class="toc--{lv}"{f" data-iface={i}" if i else ""}><a href="#{a}">{t}</a></li>' for lv, a, t, i in c['toc'])
        prev = chapters[index - 1] if index else None
        nxt = chapters[index + 1] if index + 1 < len(chapters) else None
        pager = (f'<a class="pager__prev" href="{BASE}{prev["slug"]}"><span>Previous</span>{html.escape(prev["title"])}</a>' if prev else '<span></span>') + \
                (f'<a class="pager__next" href="{BASE}{nxt["slug"]}"><span>Next</span>{html.escape(nxt["title"])}</a>' if nxt else '')
        applies = c['meta'].get('interface', 'both')
        page = fill(template, title=html.escape(c['title']) + ' · XZ Mods manual', description=html.escape(c['summary']),
                    nav=nav, toc=toc, pager=pager, version=html.escape(version), stamp=stamp, base=BASE, draft=draft,
                    main=(f'<header class="chapter"><p class="eyebrow">{html.escape(c["group"])}</p><h1>{html.escape(c["title"])}</h1>'
                          f'<p class="lede">{inline(c["summary"])}</p>{applies_line(applies)}</header>' + c['html']))
        (out / f'{c["slug"]}.html').write_text(page, encoding='utf-8')
        pages.append(c['slug'])
    cards = ''.join(f'<li><span class="toc-group">{html.escape(g)}</span><ul>' + ''.join(
        f'<li><a class="card" href="{BASE}{x["slug"]}">'
        + (f'<img src="{x["thumb"]}" alt="" loading="lazy" width="800" height="480">' if x['thumb'] else '<span class="card__blank"></span>')
        + f'<b>{html.escape(x["title"])}</b><span>{inline(x["summary"])}</span></a></li>' for x in members) + '</ul></li>'
                    for g, members in groups)
    nav = ''.join(f'<div class="nav__group"><p>{html.escape(g)}</p><ul>' + ''.join(
        f'<li><a href="{BASE}{x["slug"]}">{html.escape(x["title"])}</a></li>' for x in members) + '</ul></div>' for g, members in groups)
    home = fill(template, title='XZ Mods manual', description=html.escape(book['summary']), nav=nav, toc='', pager='',
                version=html.escape(version), stamp=stamp, base=BASE, draft=draft,
                main=(f'<header class="home"><p class="eyebrow">XZ Mods {html.escape(version)}</p><h1>{html.escape(book["title"])}</h1>'
                      f'<p class="lede">{inline(book["summary"])}</p><div class="home__actions"><a class="btn btn--gold" href="{BASE}{chapters[0]["slug"]}">'
                      f'Start with {html.escape(chapters[0]["title"])}</a><button class="btn btn--ghost" data-open-search>Search the manual <kbd>/</kbd></button></div>'
                      f'</header><ol class="cards">{cards}</ol>'))
    (out / 'index.html').write_text(home.replace('<body class="manual', '<body class="manual manual--home'), encoding='utf-8')
    for name in ('manual.css', 'manual.js'):
        shutil.copyfile(MANUAL / 'template' / name, out / name)
    (out / 'search.json').write_text(json.dumps(search, separators=(',', ':')), encoding='utf-8')
    (out / 'manifest.json').write_text(json.dumps({'format': 'xz-manual-site/1', 'version': version, 'source_commit': commit,
                                                   'source_dirty': dirty, 'draft': stale, 'pages': ['index', *pages],
                                                   'shots': {k: v[0] for k, v in sorted(images.items())}}, indent=1) + '\n', encoding='utf-8')
    print(out / 'index.html')


def applies_line(applies):
    label = {'both': 'MODERN and CLASSIC', 'modern': 'MODERN interface', 'classic': 'CLASSIC interface', 'computer': 'XZ Mods app on your computer'}[applies]
    return f'<p class="applies" data-applies="{applies}">Applies to <b>{label}</b></p>'


def settings_table(schema, explained):
    rows = ['| Setting | Values | Default | Explained in |', '|---|---|---|---|']
    for field in schema['fields']:
        if field.get('readonly'): continue
        if field['kind'] == 'boolean': values, default = 'Off / On', 'On' if field['default'] else 'Off'
        else: values, default = f"{field['min']} to {field['max']}", str(field['default'])
        chapter = explained.get(field['key'])
        where = f"[{chapter['title']}]({chapter['slug']})" if chapter else 'Not shown in the menu'
        rows.append(f"| **{field['label']}** `{field['key']}` | {values} | {default} | {where} |")
    return '\n'.join(rows)


def menu_key_table():
    """The short-MENU table from ui/menu_key.c, so the manual's MENU rules are the firmware's."""
    places = {'MODS': 'The MODS menu is open', 'GAME': 'A game is on screen', 'TAKEOVER': 'The VJ.Tools view is on screen',
              'STOCK_MENU': 'A menu, popup or dialog is open', 'TRACK_LIST': 'A track list (browse, playlist, search, history)',
              'TAG_LIST': 'TAG LIST', 'GRID_ADJUST': 'The play screen in GRID ADJUST', 'PLAY': 'The play screen',
              'SOURCE': 'The SOURCE screen', 'INFO': 'The track INFO screen', 'SHORTCUT': 'The SHORTCUT screen',
              'UTILITY': "The player's own UTILITY", 'OTHER': 'Any other screen'}
    source = (MODS / 'ui/menu_key.c').read_text(encoding='utf-8')
    rows = ['| Where you are | Short press of [[MENU]] |', '|---|---|']
    for place, job, unknown, what in re.findall(r'\[XZ_MENU_AT_(\w+)\]=\{XZ_MENU_JOB_(\w+),(\d),"([^"]*)"', source):
        if place not in places: raise SystemExit(f'menu_key.c has a new place {place}: name it in tools/build_manual.py')
        what = what[0].upper() + what[1:]
        rows.append(f"| {places[place]} | {what}{' {experimental}' if unknown == '1' else ''} |")
    return '\n'.join(rows)


def theme_gallery(render_dir):
    names = [line.split('\t') for line in (Path(render_dir) / 'ui/themes.txt').read_text(encoding='utf-8').splitlines()]
    return '```gallery {classic}\n' + '\n'.join(f'classic:theme-{n}-home {name.title()}' for n, name in names) + '\n```'


GENERATED = {'menu-key': lambda render_dir: menu_key_table(), 'themes': theme_gallery}


def expand_generated(text, render_dir):
    return re.sub(r'^```generated (\S+)\n```$', lambda m: GENERATED[m.group(1)](render_dir), text, flags=re.M)


def svg_size(path):
    box = re.search(r'viewBox="[\d.]+ [\d.]+ ([\d.]+) ([\d.]+)"', Path(path).read_text(encoding='utf-8'))
    return int(float(box.group(1))), int(float(box.group(2)))


def fill(template, **values):
    return re.sub(r'\{\{(\w+)\}\}', lambda m: values[m.group(1)], template)


# ---------------------------------------------------------------- app: the manual inside the desktop app

# `app` writes the manual the desktop app shows (ui/manual/) from the same chapters, figures and screens as the
# site. Nothing in ui/manual/ is edited by hand. The app's CSP allows no fetch of its own files, so each chapter is
# a small script the Manual page loads the first time it opens that chapter.
APP_OUT = APP / 'ui/manual'
APP_FORMAT = 'xz-manual-app/1'
APP_SRC = 'manual/'  # where ui/index.html finds the generated files
# Animated explainers drawn by ui/manual-art.js. A `steps` explainer steps through the numbered list that follows it
# in the chapter; a `scenes` explainer carries its own scenes, one `Title | data | caption` line each.
EXPLAINERS = {'four-deck-setup': 'steps', 'loopback-cable': 'video', 'loopback-signal': 'scenes', 'sync-master': 'scenes',
              'beat-jump': 'scenes', 'three-band': 'scenes', 'ram-usb': 'scenes', 'usb-diary': 'scenes'}
TOUR_ART = {'modern', 'stems', 'four-decks', 'beat-jump', 'key-sync', 'ram', 'themes', 'mods', 'profile', 'app'}
_APP = {'slug': '', 'links': []}  # the chapter being written, and every chapter link it makes (checked after the build)


def app_link_tag(label, target):
    if target.startswith('#') or re.match(r'^[a-z0-9-]+(#[\w-]+)?$', target):
        slug, anchor = (_APP['slug'], target[1:]) if target.startswith('#') else target.partition('#')[::2]
        _APP['links'].append((_APP['slug'], slug, anchor))
        extra = f' data-anchor="{anchor}"' if anchor else ''
        return f'<a class="m-link" href="#manual" data-chapter="{slug}"{extra}>{label}</a>'
    if re.match(r'^https?://', target):  # the app opens no web pages from the manual, so it shows where the page is
        return f'<span class="m-ext">{label} <span class="m-url">{html.escape(re.sub("^https?://", "", target))}</span></span>'
    raise SystemExit(f'Link {target!r}: use a chapter slug, #section or an https URL')


def app_image_bytes(path):
    """A shot as the app ships it: lossless WebP, so the firmware's pixel text stays sharp; SVG as it is."""
    from io import BytesIO
    from PIL import Image
    if path.suffix == '.svg': return path.read_bytes()
    out = BytesIO()
    Image.open(path).convert('RGB').save(out, 'WEBP', lossless=True, quality=70, method=4)  # effort 6 saves 10 % at 30 times the time
    return out.getvalue()


def app_manual(render_dir, stale):
    """Every file of the embedded manual: {path under ui/manual: bytes, or the source Path of a picture}."""
    global LINK_TAG
    book, chapters = load_manual(render_dir)
    images, files = {}, {}

    def image(shot):
        path = shot_path(shot, render_dir)
        if not path.is_file(): raise SystemExit(f'{shot} does not render; fix the chapter first')
        name = f'shots/{sha256(path)[:16]}{".svg" if path.suffix == ".svg" else ".webp"}'
        w, h = svg_size(path) if path.suffix == '.svg' else png_size(path)
        images[name] = path
        return APP_SRC + name, w, h

    counter = {'figure': 0}

    def figure(kind, args, lines):
        counter['figure'] += 1
        fid = f'f-{_APP["slug"]}-{counter["figure"]}'
        if kind == 'gallery':
            iface = re.search(r'\{(modern|classic)\}', args)
            cells = []
            for line in lines:
                shot, _, caption = line.partition(' ')
                src, w, h = image(shot)
                cap = inline(caption.strip())
                cells.append(f'<figure class="mg-item"><button class="mg-shot" type="button" data-zoom="{src}" data-w="{w}" data-h="{h}" '
                             f'aria-label="Enlarge: {html.escape(re.sub(r"<[^>]+>", "", cap))}"><img src="{src}" width="{w}" height="{h}" '
                             f'loading="lazy" decoding="async" alt=""></button><figcaption>{cap}</figcaption></figure>')
            return f'<div class="mg" id="{fid}"' + (f' data-iface="{iface.group(1)}"' if iface else '') + '>' + ''.join(cells) + '</div>'
        shot, _, label = args.partition(' ')
        iface = re.search(r'\{(modern|classic)\}', label)
        label = re.sub(r'\s*\{(modern|classic)\}', '', label).strip()
        matches = [FIGURE_LINE.match(l) for l in lines]
        caption = ' '.join(l.strip() for l, m in zip(lines, matches) if not m)
        callouts = [m.groups() for m in matches if m]
        alt = html.escape(re.sub(r'<[^>]+>', '', inline(caption)) or label or shot)
        variants = [(look_shot(shot, name), name) for name in LOOKS] if LOOK in shot else [(shot, '')]
        pictures, size = '', None
        for variant, name in variants:
            src, w, h = image(variant)
            size = size or (w, h)
            pictures += (f'<img src="{src}" width="{w}" height="{h}" loading="lazy" decoding="async" alt="{alt}"'
                         + (f' data-iface="{name}"' if name else '') + '>')
        w, h = size
        bar = {'modern': 'XZ screen · MODERN', 'classic': 'XZ screen' if variants[0][1] else 'XZ screen · CLASSIC', 'app': 'XZ Mods app',
               'capture': 'XZ screen · photo of the player', 'hw': 'XDJ-XZ top panel'}[shot.partition(':')[0]]
        marks = ''.join(f'<button class="mf-mark" type="button" tabindex="-1" data-n="{n}" data-x="{int(x) / w * 100:.3f}" '
                        f'data-y="{int(y) / h * 100:.3f}" aria-label="{n}: {html.escape(name)}" aria-describedby="{fid}-{n}">{n}</button>'
                        for n, x, y, name, _ in callouts)
        legend = ''.join(f'<li data-n="{n}" id="{fid}-{n}"><span class="mf-chip" aria-hidden="true">{n}</span><div><b>{inline(name)}</b> '
                         f'<span>{inline(text)}</span></div></li>' for n, x, y, name, text in callouts)
        kind_class = ' mf--hw' if shot.startswith('hw:') else ' mf--app' if shot.startswith('app:') else ''
        return (f'<figure class="mf{kind_class}" id="{fid}"' + (f' data-iface="{iface.group(1)}"' if iface else '') + '>'
                f'<div class="mf-frame"><div class="mf-bar"><span class="mf-label">{html.escape(label or bar)}</span></div>'
                f'<div class="mf-stage">{pictures}<span class="mf-spot" aria-hidden="true"></span>'
                + (f'<div class="mf-marks" role="group" aria-label="Numbered points on the picture">{marks}</div>' if marks else '')
                + '</div></div>'
                + (f'<figcaption>{inline(caption)}</figcaption>' if caption else '')
                + (f'<ol class="mf-legend" aria-label="What the numbers show">{legend}</ol>' if legend else '') + '</figure>')

    def explainer(args, lines):
        kind, _, label = args.partition(' ')
        if kind not in EXPLAINERS: raise SystemExit(f'{_APP["slug"]}: unknown explainer {kind!r}; known: {", ".join(sorted(EXPLAINERS))}')
        iface = re.search(r'\{(modern|classic)\}', label)
        label = re.sub(r'\s*\{(modern|classic)\}', '', label).strip()
        mode = EXPLAINERS[kind]
        if mode == 'steps' and lines: raise SystemExit(f'{_APP["slug"]}: explainer {kind} steps through the list after it; give it no lines')
        if mode != 'steps' and not lines: raise SystemExit(f'{_APP["slug"]}: explainer {kind} needs its scenes, one per line')
        scenes = ''
        for line in lines:
            parts = [p.strip() for p in line.split(' | ')]
            title, data, text = parts[0], parts[1:-1], parts[-1]
            scenes += (f'<li data-title="{html.escape(title)}"' + (f' data-v="{html.escape(" | ".join(data))}"' if data else '')
                       + f'><b>{inline(title)}</b> <span>{inline(text)}</span></li>')
        return (f'<div class="mx" data-mx="{kind}" data-mode="{mode}"' + (f' data-iface="{iface.group(1)}"' if iface else '')
                + f' role="group" aria-label="{html.escape(label or "Animated explainer")}">'
                + (f'<ol class="mx-scenes">{scenes}</ol>' if scenes else '') + '</div>')

    schema = load_json(MODS / 'settings-schema.json')
    explained = {key: c for c in chapters for key in c['meta']['settings']}
    entries, search, anchors = [], [], {}
    saved, LINK_TAG = LINK_TAG, app_link_tag
    _APP['links'] = []
    try:
        for c in chapters:
            _APP['slug'] = c['slug']; counter['figure'] = 0
            body = c['body'] + ('\n' + settings_table(schema, explained) if c['slug'] == 'settings' else '')
            content, toc = blocks(body, {'figure': figure, 'explainer': explainer})
            content = re.sub(r'<a class="anchor" href="#[^"]*" aria-hidden="true">#</a>', '', content)
            content = re.sub(r'<h([234]) id="([^"]+)"', r'<h\1 id="m-\2" data-anchor="\2"', content)  # ids apart from the app's own
            content = re.sub(r'<(/?)h([234])\b', lambda m: f'<{m.group(1)}h{int(m.group(2)) + 1}', content)  # under the chapter's h2
            anchors[c['slug']] = set(re.findall(r'<h\d id="m-[^"]+" data-anchor="([^"]+)"', content))  # headings only, not links
            thumb = next((shots[0] for kind, shots, _, _ in c['figures'] if kind == 'figure'), None)
            applies = c['meta'].get('interface', 'both')
            words = len(re.sub(r'<[^>]+>', ' ', content).split())
            head = (f'<header class="mc-head"><p class="mc-eyebrow">{html.escape(c["group"])}</p>'
                    f'<h2 class="mc-title" tabindex="-1">{html.escape(c["title"])}</h2>'
                    f'<p class="mc-lede">{inline(c["summary"])}</p>{applies_line(applies)}</header>')
            files[f'chapters/{c["slug"]}.js'] = script(f'(window.XZ_MANUAL_CHAPTERS=window.XZ_MANUAL_CHAPTERS||{{}})[{json.dumps(c["slug"])}]=',
                                                       head + content)
            entries.append({'slug': c['slug'], 'group': c['group'], 'title': c['title'], 'summary': re.sub(r'<[^>]+>', '', inline(c['summary'])),
                            'applies': applies, 'minutes': max(1, round(words / 200)), 'thumb': image(thumb)[0] if thumb else '',
                            'sections': [[level, anchor, plain(title), iface] for level, anchor, title, iface in toc]})
            search += section_text(c['slug'], c['title'], content)
    finally:
        LINK_TAG = saved
    for source, slug, anchor in _APP['links']:
        if slug not in anchors: raise SystemExit(f'{source}: link to chapter {slug!r}, which does not exist')
        if anchor and anchor not in anchors[slug]: raise SystemExit(f'{source}: link to {slug}#{anchor}, which has no such heading')
    tour = []
    for item in book.get('tour', []):
        slug, _, anchor = item['link'].partition('#')
        if slug not in anchors or (anchor and anchor not in anchors[slug]): raise SystemExit(f'manual.json tour: {item["link"]} does not exist')
        if item['art'] not in TOUR_ART: raise SystemExit(f'manual.json tour: art {item["art"]!r} is not drawn by ui/manual-art.js')
        tour.append({'title': item['title'], 'text': re.sub(r'<[^>]+>', '', inline(item['text'])), 'link': item['link'], 'art': item['art'],
                     'tag': item.get('tag', '')})
    index = {'format': APP_FORMAT, 'draft': bool(stale), 'title': book['title'], 'summary': book['summary'], 'chapters': entries, 'tour': tour}
    files['index.js'] = script('window.XZ_MANUAL=', index)
    files['search.js'] = script('window.XZ_MANUAL_SEARCH=', search)
    files.update(sorted(images.items()))
    files['manifest.json'] = (json.dumps({'format': APP_FORMAT, 'generator': 'tools/build_manual.py app', 'draft': bool(stale),
                                          'sources': {name: sha256(path) for name, path in sorted(images.items())}}, indent=1) + '\n').encode('utf-8')
    return files


def plain(fragment):
    """Text of an HTML fragment, without its tags and without tag labels such as {experimental}."""
    return html.unescape(re.sub(r'<[^>]+>', '', re.sub(r'<span class="tag[^"]*">[^<]*</span>', '', fragment))).strip()


def script(prefix, value):
    """A generated script: one JSON value assigned to a global. The text never holds '</' or a raw U+2028."""
    text = json.dumps(value, ensure_ascii=False, separators=(',', ':'))
    text = text.replace('</', '<\\/').replace('\u2028', '\\u2028').replace('\u2029', '\\u2029')
    return ('// Generated by tools/build_manual.py app from manual/. Do not edit by hand.\n' + prefix + text + ';\n').encode('utf-8')


def section_text(slug, title, content):
    """Search entries for one chapter: the chapter itself, then one per ## and ### section (h3 and h4 in the app)
    with the text under it."""
    out = [{'c': slug, 'a': '', 't': title, 'x': '', 'i': ''}]
    level2 = ''
    for part in re.split(r'(<h[34] id="m-[^"]+" data-anchor="[^"]+"[^>]*>.*?</h[34]>)', content):
        heading = re.match(r'<h([34]) id="m-[^"]+" data-anchor="([^"]+)"(?: data-iface="(\w+)")?[^>]*>(.*?)</h[34]>', part, re.S)
        if heading:
            level, anchor, iface, text = heading.groups()
            level = str(int(level) - 1)
            if level == '2': level2 = iface or ''
            out.append({'c': slug, 'a': anchor, 't': plain(text), 'x': '',
                        'i': iface or (level2 if level == '3' else '')})
        else:
            # Figure chrome (the numbers on the picture, its frame label) is not text a reader searches for.
            part = re.sub(r'<div class="mf-marks".*?</div>|<span class="mf-chip"[^>]*>\d+</span>|<div class="mf-bar">.*?</div>', ' ', part, flags=re.S)
            words = re.sub(r'\s+', ' ', html.unescape(re.sub(r'<[^>]+>', ' ', part))).strip()
            out[-1]['x'] = (out[-1]['x'] + ' ' + words).strip()
    return out


def png_size(path):
    from PIL import Image
    with Image.open(path) as img: return img.size


def app_build(render_dir, out, stale):
    files = app_manual(render_dir, stale)
    out = Path(out)
    if out.exists(): shutil.rmtree(out)
    total = 0
    for name, value in files.items():
        target = out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        data = app_image_bytes(value) if isinstance(value, Path) else value
        target.write_bytes(data); total += len(data)
    print(f'{out}: {len(files)} files, {total / 1048576:.2f} MiB' + (' (draft: the manual failed check)' if stale else ''))


def app_problems(render_dir, out=None):
    """How the committed embedded manual differs from what `app` writes now. Pictures are named by their source's
    hash, so a changed screen shows up as a changed chapter script and a missing picture."""
    out = Path(out or APP_OUT)
    if not (out / 'manifest.json').is_file(): return [f'embedded manual: {out} has none; run build_manual.py app']
    files = app_manual(render_dir, False)
    have = {p.relative_to(out).as_posix() for p in out.rglob('*') if p.is_file()}
    found = []
    for name, value in files.items():
        if name not in have: found.append(f'embedded manual: {name} is missing')
        elif isinstance(value, bytes) and (out / name).read_bytes().replace(b'\r\n', b'\n') != value: found.append(f'embedded manual: {name} is out of date')
    found += [f'embedded manual: {name} is no longer used' for name in sorted(have - set(files))]
    return found + (['embedded manual: run tools/build_manual.py app --render <render-dir> and commit ui/manual/'] if found else [])


# ---------------------------------------------------------------- proof sheets

def proof(render_dir, out):
    from PIL import Image, ImageDraw
    out = Path(out); out.mkdir(parents=True, exist_ok=True)
    _, chapters = load_manual(render_dir)
    count = 0
    for chapter in chapters:
        for kind, shots, _, callouts in chapter['figures']:
            if kind != 'figure' or not callouts: continue
            for shot in shots:
                path = shot_path(shot, render_dir)
                if path.suffix == '.svg' or not path.is_file(): continue
                img = Image.open(path).convert('RGB')
                draw = ImageDraw.Draw(img)
                for n, x, y, _, _ in callouts:
                    x, y = int(x), int(y)
                    draw.ellipse((x - 11, y - 11, x + 11, y + 11), fill=(247, 220, 88), outline=(0, 0, 0), width=2)
                    draw.text((x, y), n, fill=(0, 0, 0), anchor='mm')
                img.save(out / f"{chapter['slug']}--{shot.replace(':', '-')}.png")
                count += 1
    print(f'{count} proof sheets in {out}')


def default_zig():
    """The zig the toolkit builds with: XZ_ZIG, or the monorepo checkout beside this one."""
    import os
    path = Path(os.environ.get('XZ_ZIG') or APP.parent / 'VJToolsLibrary/.tmp/cdj-zig/ziglang/zig.exe')
    if not path.is_file(): raise SystemExit(f'zig not found at {path}; set XZ_ZIG')
    return path


def gate():
    """Release gate: render this source into a fresh folder and fail when the manual is stale, or when the manual
    embedded in the app (ui/manual/) is not the one this source and these screens make."""
    import tempfile
    with tempfile.TemporaryDirectory(prefix='xz-manual-gate-') as temp:
        render(default_zig(), Path(temp) / 'render')
        if check(Path(temp) / 'render'): return 1
        return app_check(Path(temp) / 'render')


def app_check(render_dir):
    found = app_problems(render_dir)
    for line in found: print('STALE ' + line)
    print('embedded manual: current' if not found else f'embedded manual: {len(found) - 1} problem(s)')
    return 1 if found else 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('render'); r.add_argument('--zig', type=Path); r.add_argument('--out', type=Path, required=True)
    sub.add_parser('gate', help='render into a temporary folder and check; releases run this')
    for name in ('check', 'accept', 'proof', 'site', 'app'):
        s = sub.add_parser(name); s.add_argument('--render', type=Path, required=True)
        if name in ('proof', 'site'): s.add_argument('--out', type=Path, required=True)
        if name == 'app':
            s.add_argument('--out', type=Path, default=APP_OUT, help='default ui/manual')
            s.add_argument('--draft', action='store_true', help='write it even when check fails (never release a draft)')
            s.add_argument('--check', action='store_true', help='only report whether ui/manual is current')
        if name == 'accept': s.add_argument('only', nargs='*', help='shot ids or watched paths; default every one')
        if name == 'site':
            s.add_argument('--draft', action='store_true', help='write the site even when check fails (never publish a draft)')
            s.add_argument('--base', default='/manual/')
    a = p.parse_args()
    if a.cmd == 'render': return render(a.zig or default_zig(), a.out)
    if a.cmd == 'gate': return gate()
    if a.cmd == 'check': return check(a.render)
    if a.cmd == 'accept': return accept(a.render, set(a.only))
    if a.cmd == 'proof': return proof(a.render, a.out)
    if a.cmd == 'app':
        if a.check: return app_check(a.render)
        stale = bool(check(a.render))
        if stale and not a.draft: return 1
        return app_build(a.render, a.out, stale)
    global BASE
    BASE = a.base
    stale = bool(check(a.render))
    if stale and not a.draft: return 1
    book, chapters = load_manual(a.render)
    build_site(a.render, a.out, book, chapters, stale)


if __name__ == '__main__':
    sys.exit(main())
