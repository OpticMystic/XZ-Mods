"""Build the manual's menu map tree (xz-menu-tree/1) from offline navigation captures.

  python tools/build_menu_tree.py --captures <nav-capture-dir> --flows <flow-spec-dir> --out <dir>

The menu crawler writes this format directly. Until its tree lands, this converter makes a provisional one from the
navigation captures (one folder per flow: frame-NNNN.png, frames.json, timeline.json, plus the capture's
manifest.json). Every node is a frame the firmware's own C drew in the capture harness; nothing is drawn here.

  - Two frames are the same node when their pixels are identical (and they belong to the same interface).
  - Consecutive settled frames are joined by the control event between them. A frame is settled when it is the
    last one before the next input; redraws without an input in between are animation and are not nodes.
  - A flow's interface starts as MODERN (CLASSIC for a flow that starts from a CLASSIC fixture) and changes when
    the flow taps CLASSIC or MODERN.
  - The flow specs (manual/nav/<flow>.json on the capture branch) name the manual section each flow shows; a node
    takes the section of the most specific flow whose subject it is.
  - `sources` records the firmware sources the capture was made from (its manifest), so build_manual.py check can
    tell when the screens may have changed.

Then import it: python tools/build_manual.py menu-tree --from <dir>
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

APP = Path(__file__).resolve().parents[1]
FORMAT = 'xz-menu-tree/1'
MODS = 'source/xdj-xz-toolkit/mods/'
# The MODS rail: these pages sit side by side under MODS, whichever page they were tapped from (ui_sections.c).
SECTIONS_SOURCE = APP / MODS / 'ui/ui_sections.c'


def mods_sections():
    text = SECTIONS_SOURCE.read_text(encoding='utf-8')
    body = text[text.index('const char *xz_ui_section_label'):]
    body = body[:body.index('\n}')]
    return set(re.findall(r'return "([^"]+)"', body))


def pixels_hash(path):
    from PIL import Image
    with Image.open(path) as image:
        image = image.convert('RGB')
        return hashlib.sha256(f'{image.size}'.encode() + image.tobytes()).hexdigest(), image.size


def chapter_anchors():
    """{slug: set of heading anchors} from manual/chapters, as build_manual.py makes them."""
    sys.path.insert(0, str(APP / 'tools'))
    import build_manual
    found = {}
    for path in sorted((APP / 'manual/chapters').glob('*.md')):
        _, body = build_manual.parse_chapter(path)
        found[path.stem] = {build_manual.slugify(build_manual.IFACE.sub('', m.group(1))) for m in re.finditer(r'^#{2,4}\s+(.+)$', body, re.M)}
    return found


def section_link(section, anchors):
    """A flow spec's section as a chapter link that exists: slug#anchor, the chapter alone when the anchor is not a
    heading, or None."""
    if not section: return None
    slug, _, anchor = section.partition('#')
    if slug not in anchors: return None
    anchor = re.sub(r'[^a-z0-9]+', '-', anchor.lower()).strip('-')
    return f'{slug}#{anchor}' if anchor in anchors[slug] else slug


def control_of(event):
    """(control, action, target, rect) of one input event, in the tree's terms."""
    control, action, detail = event['control'], event['action'], (event.get('detail') or '').strip()
    target, rect = None, None
    side = re.match(r'^(left|right) deck (.+)$', control)
    if side: control, target = side.group(2), side.group(1) + ' deck'
    if control == 'touch screen':
        control, action = 'touch', 'tap'
        point = re.fullmatch(r'(\d+),(\d+)', detail)
        if point:
            x, y = int(point.group(1)), int(point.group(2))
            rect = [max(0, x - 20), max(0, y - 20), 40, 40]
        else: target = detail or None
    elif action == 'turn':
        action = 'turn-' if detail.startswith('-') else 'turn+'
    elif action == 'push':
        target = detail or None
    return control, action, target, rect


def flow_inputs(timeline):
    """The flow's inputs in order, as (index, event). A press followed by a hold of the same control is one hold;
    releases do nothing on their own."""
    inputs = []
    for i, event in enumerate(timeline):
        if event['control'] == 'clock' or event['action'] == 'release': continue
        if event['action'] == 'hold' and inputs and inputs[-1][1]['control'] == event['control'] and inputs[-1][1]['action'] == 'press':
            inputs[-1] = (inputs[-1][0], {**event, 't_ms': inputs[-1][1]['t_ms']})
            continue
        inputs.append((i, event))
    return inputs


def settled(timeline, before_ms):
    """The row on screen when the clock reaches before_ms (the last row stamped earlier)."""
    row = timeline[0]
    for event in timeline:
        if event['t_ms'] >= before_ms: break
        row = event
    return row


def build(captures, flows_dir, out):
    captures, out = Path(captures), Path(out)
    manifest = json.loads((captures / 'manifest.json').read_text(encoding='utf-8'))
    specs = {p.stem: json.loads(p.read_text(encoding='utf-8')) for p in Path(flows_dir).glob('*.json')} if flows_dir else {}
    anchors, sections = chapter_anchors(), mods_sections()
    nodes, edges, images, seen_in = {}, {}, {}, collections.defaultdict(set)
    flows = sorted(p.parent for p in captures.glob('*/timeline.json'))
    subjects = {}
    for flow in flows:
        name = flow.name
        timeline = json.loads((flow / 'timeline.json').read_text(encoding='utf-8'))
        titles = {f['frame']: f['page'] for f in json.loads((flow / 'frames.json').read_text(encoding='utf-8'))['frames']}
        spec = specs.get(name, {})
        opening = next((e for e in timeline if e['control'] != 'clock'), {})
        iface = 'classic' if opening.get('control') == 'fixture runtime' and 'CLASSIC' in (opening.get('detail') or '') else 'modern'

        def node_at(row, iface, reached=True):
            digest, size = pixels_hash(flow / row['frame'])
            key = f'{iface[0]}-{digest[:12]}'
            if key not in nodes:
                nodes[key] = {'id': key, 'interface': iface, 'title': titles.get(row['frame'], ''), 'panel': row.get('panel', 0),
                              'kind': 'page', 'image': f'nodes/{key}.webp', 'size': list(size), 'focusables': [], 'chapter': None}
                images[key] = flow / row['frame']
            if reached: seen_in[key].add(name)  # a flow's opening screen is where it starts, not what it shows
            return key

        inputs = flow_inputs(timeline)
        times = [event['t_ms'] for _, event in inputs] + [timeline[-1]['t_ms'] + 1]
        current = node_at(settled(timeline, times[0]), iface, reached=False)
        opening_title, last = nodes[current]["title"], current
        for k, (_, event) in enumerate(inputs):
            row = settled(timeline, times[k + 1])
            if event['control'] == 'fixture runtime':  # a test fixture changes state: not something the player does
                current = node_at(row, iface, reached=False); continue
            control, action, target, rect = control_of(event)
            if control == 'touch' and target in ('CLASSIC', 'MODERN') and nodes[current]['title'] == 'SCREEN': iface = target.lower()
            after = node_at(row, iface)
            if after != current:
                key = (current, after, control, action, target)
                edges.setdefault(key, {'from': current, 'to': after, 'control': control, 'action': action, 'target': target, 'rect': rect})
            current = after
            if nodes[after]["title"] != opening_title or nodes[last]["title"] == opening_title: last = after
        subjects[name] = (nodes[last]['title'], spec.get('section'))  # the last page the flow opened
    # Chapters: a node takes the section of the smallest flow whose final page has the node's title.
    for key, node in nodes.items():
        candidates = sorted((len(json.loads((captures / f / 'frames.json').read_text(encoding='utf-8'))['frames']), f) for f in seen_in[key]
                            if subjects[f][0] == node['title'] and subjects[f][1])
        node['chapter'] = section_link(subjects[candidates[0][1]][1], anchors) if candidates else None
    for node in nodes.values():
        if node['chapter'] is None and node['title'] == 'PLAY' and node['panel'] == 0: node['chapter'] = 'play-screen'
    # Focusables we know from the captures: the labels the flows tapped or pushed on each screen.
    for edge in edges.values():
        if edge['action'] in ('tap', 'push') and (edge['target'] or edge['rect']):
            label = edge['target'] or 'Tap here'
            focus = nodes[edge['from']]['focusables']
            if not any(f['label'] == label and f['rect'] == edge['rect'] for f in focus): focus.append({'label': label, 'rect': edge['rect']})
    # Paths: breadth first from the play screen.
    starts = [key for key, node in nodes.items() if node['title'] == 'PLAY' and node['panel'] == 0]
    first = {f.name: None for f in flows}
    out_edges = collections.defaultdict(list)
    for edge in edges.values(): out_edges[edge['from']].append(edge)
    path, order = {}, []
    roots = []
    for iface in ('modern', 'classic'):
        mine = [k for k in starts if nodes[k]['interface'] == iface]
        if not mine: continue
        root = max(mine, key=lambda k: len(out_edges[k]))  # the play screen most flows start from
        roots.append({'id': root, 'label': 'Play screen'})
        queue = [k for k in mine]
        for k in mine: path.setdefault(k, ['PLAY SCREEN'])
        while queue:
            key = queue.pop(0)
            order.append(key)
            for edge in out_edges[key]:
                to = edge['to']
                if to in path: continue
                title, parent = nodes[to]['title'], nodes[key]
                if nodes[to]['panel'] and title in sections: path[to] = ['MODS', title]
                elif title == parent['title'] and nodes[to]['panel'] == parent['panel']: path[to] = path[key]
                elif not parent['panel'] and parent['title'] == 'PLAY': path[to] = [title]
                else: path[to] = path[key] + [title]
                queue.append(to)
    for root in roots: nodes[root['id']]['chapter'] = 'play-screen'
    for key, node in nodes.items():
        node['path'] = path.get(key) or [node['title']]
    # A state no flow was about takes its screen's most common section, else the heading named like the screen.
    groups = collections.defaultdict(list)
    for node in nodes.values(): groups[(node['interface'], tuple(node['path']))].append(node)
    order = [c['slug'] for c in json.loads((APP / 'manual/manual.json').read_text(encoding='utf-8'))['chapters']]
    for group in groups.values():
        known = collections.Counter(n['chapter'] for n in group if n['chapter'])
        named = re.sub(r'[^a-z0-9]+', '-', group[0]['path'][-1].lower()).strip('-')
        fallback = known.most_common(1)[0][0] if known else next((f'{slug}#{named}' for slug in order if named in anchors.get(slug, ())), None)
        for node in group: node['chapter'] = node['chapter'] or fallback
        if node['title'] == 'PLAY' and not node['panel']: node['title'] = 'PLAY SCREEN'
        node['kind'] = ('play' if node['path'][0] == 'PLAY SCREEN' else 'keyboard' if node['title'] == 'SEARCH'
                        else 'browse' if node['path'][0] in ('BROWSE', 'TAG LIST', 'SOURCE') else 'page')
        del node['panel'], node['size']
    # Entry screens: what each hardware key opens from the play screen, so the map can jump to them.
    for root in list(roots):
        for edge in out_edges[root['id']]:
            if edge['control'] in ('touch',) or nodes[edge['to']]['path'][0] == 'PLAY SCREEN': continue
            label = edge['control'] + (' (hold)' if edge['action'] == 'hold' else '')
            label = 'MODS (MENU)' if label == 'MENU' else label
            if not any(r['label'] == label for r in roots): roots.append({'id': edge['to'], 'label': label})
    # Screen freshness follows the shell/UI compilation units and their headers, not unrelated DSP or games.
    from capture_manual_nav import SHELL_SOURCES, UI_SOURCES
    compiled = {(APP / MODS / side / name).resolve().relative_to(APP).as_posix() for side, names in
                [('shell', SHELL_SOURCES), ('ui', UI_SOURCES)] for name in names}
    harness = (APP / 'tools/capture_manual_nav.py').read_text(encoding='utf-8')
    compiled.update(MODS + name for name in re.findall(r"'((?:shell|ui)/[^']+\.c)'", harness))
    compiled.discard(MODS + 'shell/shell_browse.c')
    sources = {k: v for k, v in sorted(manifest.get('source_sha256', {}).items())
               if k in compiled or (k.startswith((MODS + 'shell/', MODS + 'ui/'))
                                    and k.endswith('.h') and '/test' not in k)}
    tree = {'format': FORMAT, 'interfaces': ['modern', 'classic'], 'provenance': {
                'kind': 'provisional', 'from': 'navigation captures', 'captures': captures.name, 'flows': len(flows),
                'generator': 'tools/build_menu_tree.py'},
            'sources': sources,
            'nodes': sorted(nodes.values(), key=lambda n: (n['interface'], n['path'], n['id'])),
            'edges': sorted(edges.values(), key=lambda e: (e['from'], e['to'], e['control'], e['action'], e['target'] or '')),
            'roots': roots}
    if out.exists(): shutil.rmtree(out)
    (out / 'nodes').mkdir(parents=True)
    from PIL import Image
    for key, path_ in images.items():
        with Image.open(path_) as image: image.convert('RGB').save(out / 'nodes' / f'{key}.webp', 'WEBP', lossless=True, quality=70, method=4)
    (out / 'menu-tree.json').write_text(json.dumps(tree, indent=1) + '\n', encoding='utf-8')
    by = collections.Counter(n['interface'] for n in tree['nodes'])
    print(f"{out}: {len(tree['nodes'])} screens ({', '.join(f'{v} {k}' for k, v in sorted(by.items()))}), {len(tree['edges'])} moves, "
          f"{len(roots)} roots, {sum(1 for n in tree['nodes'] if n['chapter'])} with a manual section")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--captures', type=Path, required=True, help='a nav-capture folder: one folder per flow and manifest.json')
    p.add_argument('--flows', type=Path, help='the flow specs (manual/nav/*.json on the capture branch), for each flow\'s manual section')
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    build(a.captures, a.flows, a.out)


if __name__ == '__main__':
    main()
