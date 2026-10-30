"""Shared manual search derivation; input HTML is generated, never user markup."""
import html
import re

def plain(fragment):
    """Text of an HTML fragment, without its tags and without tag labels such as {experimental}."""
    return html.unescape(re.sub(r'<[^>]+>', '', re.sub(r'<span class="tag[^"]*">[^<]*</span>', '', fragment))).strip()


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


