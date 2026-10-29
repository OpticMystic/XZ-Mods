"""Button glyphs: one SVG per control of the XDJ-XZ, drawn from its top panel.

  write     Write ui/assets/buttons/ (one SVG per control and lamp state, and index.js for the app).
  check     Fail when ui/assets/buttons/ is not what this file draws now.
  sheet     Write a PNG of every glyph, lit and unlit, at inline and large size (needs Edge).
  firmware  Write manual/buttons/firmware-spec.md and its PNG previews at the XZ's own text sizes (needs Edge).

Every glyph is drawn here from one table, GLYPHS, so the manual, the website and the desktop app show the same
picture of a control. Shapes, cap and lamp colours and printed labels follow the player's top panel (see
manual/buttons/README.md for the references); lamp colours the firmware sets come from mods/ui/native_led.h.
Text is outlined from the firmware's own font (Barlow Semi Condensed, SIL OFL), so a glyph looks the same in every
browser and needs no font. `tools/build_manual.py` turns [[NAME]] in a chapter into these glyphs and fails on a name
that has none, and on a brand name (tools/check_trademarks.py): no glyph prints a logo or a brand.
"""
import argparse
from functools import lru_cache
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_trademarks  # noqa: E402  no glyph is named after a brand or prints one

APP = Path(__file__).resolve().parents[1]
FONT = APP / 'source/xdj-xz-toolkit/mods/ui/fonts/BarlowSemiCondensed-Medium.ttf'
OUT = 'ui/assets/buttons'  # under APP
H = 32  # every glyph is this tall, so a row of them lines up in text; widths follow the control
PAD = 4  # panel margin round the control
GAP = 5  # between a cap and the label printed beside it

# Colours. Panel and caps from the reference photographs; lamps from the photographs or native_led.h.
PANEL, PANEL_EDGE = '#151617', '#34373b'
PRINT = '#f1f2f0'  # white panel print
BOX = '#c8cacb'  # the grey boxes round SHIFT functions (IN ADJUST, LOOP CUTTER, CALL)
CAP, CAP_HI, CAP_EDGE = '#0b0b0c', '#3a3c40', '#4b4e53'
OFF_RING, OFF_TEXT, OFF_LINE = '#3a3d41', '#8c9095', '#3d4044'
GREEN, ORANGE, AMBER, RED = '#27e35a', '#ff5f14', '#ffb11a', '#ff2424'
LIME, CYAN, AZURE, BLUE = '#c9f522', '#36d4ff', '#1f8fff', '#2f8bff'
LINK_BLUE = '#1ab8ff'
LED_OFF = {RED: '#3e1515', AMBER: '#3d2f12', GREEN: '#123d1e', '#d6f53c': '#2f3612'}
PAD_COLOURS = ['#ff3b3b', '#33d6ff', '#c6f024', '#5a5cff', '#2ee84e', '#ff8f1f', '#1f74ff', '#ffd426']  # photographed HOT CUE colours
JUMP_COLOURS = ['#ff3000', '#ff3000', '#ffa300', '#ffa300', '#ffa300', '#ffa300', '#ff3000', '#ff3000']  # native_led.h xz_jump_page2_color


# ---------------------------------------------------------------- text, outlined from the firmware's font

@lru_cache(maxsize=1)
def _font():
    from fontTools.ttLib import TTFont
    font = TTFont(str(FONT))
    return font, font.getGlyphSet(), font.getBestCmap(), font['hmtx'], font['head'].unitsPerEm


def num(v):
    s = f'{v:.2f}'.rstrip('0').rstrip('.')
    return '0' if s in ('-0', '') else s


def text_width(s, size, track=0.04):
    font, glyphs, cmap, hmtx, upm = _font()
    return sum(hmtx[cmap.get(ord(c), cmap[ord('?')])][0] for c in s) * size / upm + track * size * (len(s) - 1)


def text_path(s, x, baseline, size, track=0.04):
    """The outline of s as one path, its baseline at `baseline`, starting at x."""
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.pens.transformPen import TransformPen
    font, glyphs, cmap, hmtx, upm = _font()
    pen = SVGPathPen(glyphs, ntos=num)
    scale = size / upm
    for c in s:
        name = cmap.get(ord(c), cmap[ord('?')])
        glyphs[name].draw(TransformPen(pen, (scale, 0, 0, -scale, x, baseline)))
        x += hmtx[name][0] * scale + track * size
    return pen.getCommands()


CAP_RATIO = 0.70  # Barlow's cap height
READ = 1.28  # print on a rectangular cap is drawn this much larger than on the unit, so it reads at text size


# ---------------------------------------------------------------- drawing

class Draw:
    """SVG elements of one glyph, and the same shapes as `parts` for the firmware spec."""

    def __init__(self, quiet_text=False):
        self.body, self.parts, self.glow, self.quiet_text = [], [], False, quiet_text

    def el(self, svg, part=None):
        self.body.append(svg)
        if part: self.parts.append(part)

    def text(self, s, x, cy, size, fill, anchor='start', weight=0.55, track=0.04, part=True):
        """Text centred on cy by its cap height. Returns its width."""
        w = text_width(s, size, track)
        if self.quiet_text: return w
        x0 = x - w / 2 if anchor == 'middle' else x - w if anchor == 'end' else x
        base = cy + size * CAP_RATIO / 2
        d = text_path(s, x0, base, size, track)
        stroke = f' stroke="{fill}" stroke-width="{num(weight)}" stroke-linejoin="round"' if weight else ''
        self.el(f'<path d="{d}" fill="{fill}"{stroke}/>',
                {'kind': 'text', 'text': s, 'x': x0, 'y': cy - size * CAP_RATIO / 2, 'w': w, 'h': size * CAP_RATIO, 'colour': fill} if part else None)
        return w

    def glowing(self, svg):
        """A lamp: the shape blurred under itself."""
        self.glow = True
        self.el(f'<g filter="url(#glow)" opacity=".85">{svg}</g>')
        self.el(svg)


def circle(cx, cy, r, fill='none', stroke=None, width=0, extra=''):
    s = f' stroke="{stroke}" stroke-width="{num(width)}"' if stroke else ''
    return f'<circle cx="{num(cx)}" cy="{num(cy)}" r="{num(r)}" fill="{fill}"{s}{extra}/>'


def rect(x, y, w, h, rx, fill='none', stroke=None, width=0, extra=''):
    s = f' stroke="{stroke}" stroke-width="{num(width)}"' if stroke else ''
    return f'<rect x="{num(x)}" y="{num(y)}" width="{num(w)}" height="{num(h)}" rx="{num(rx)}" fill="{fill}"{s}{extra}/>'


def poly(points, fill):
    return f'<path d="M{" L".join(num(x) + " " + num(y) for x, y in points)}Z" fill="{fill}"/>'


def icon(d, name, cx, cy, s, fill):
    """Symbols printed on caps, drawn to size s (their height)."""
    h = s / 2
    tri = lambda x, right: poly([(x, cy - h), (x + s * .8, cy), (x, cy + h)] if right else [(x + s * .8, cy - h), (x, cy), (x + s * .8, cy + h)], fill)
    if name == 'play-pause':  # ▶/II
        d.el(tri(cx - s * 1.25, True))
        d.el(f'<path d="M{num(cx + s * .05)} {num(cy + h)} L{num(cx + s * .4)} {num(cy - h)}" stroke="{fill}" stroke-width="{num(s * .14)}"/>')
        for x in (cx + s * .55, cx + s * .9): d.el(rect(x, cy - h, s * .2, s, 0, fill))
    elif name in ('prev', 'next'):
        d.el(tri(cx - s * .4, name == 'next'))
    elif name in ('rew', 'ffwd'):  # ◀◀ ▶▶
        for i in (0, 1): d.el(tri(cx - s * .8 + i * s * .7, name == 'ffwd'))
    elif name in ('track-prev', 'track-next'):  # |◀◀ ▶▶|
        right = name == 'track-next'
        for i in (0, 1): d.el(tri(cx - s * .75 + i * s * .62 + (-s * .12 if right else s * .12), right))
        d.el(rect(cx + s * .95 if right else cx - s * 1.13, cy - h, s * .17, s, 0, fill))
    d.parts.append({'kind': 'symbol', 'name': name, 'x': cx - s, 'y': cy - h, 'w': 2 * s, 'h': s, 'colour': fill})


def cap_round(d, cx, cy, r, tint=CAP):
    """A black round cap as the XZ's: a dark bezel ring, the cap, and light from above left."""
    d.el(circle(cx, cy, r + 1.3, '#050506', CAP_EDGE, .8))
    d.el(circle(cx, cy, r, 'url(#cap)' if tint == CAP else tint),
         {'kind': 'circle', 'cx': cx, 'cy': cy, 'r': r, 'colour': tint, 'note': 'cap'})
    d.el(f'<path d="M{num(cx - r * .72)} {num(cy - r * .2)} A{num(r * .78)} {num(r * .78)} 0 0 1 {num(cx + r * .2)} {num(cy - r * .72)}" '
         f'fill="none" stroke="#fff" stroke-opacity=".22" stroke-width="{num(max(.8, r * .12))}" stroke-linecap="round"/>')


def led(d, cx, cy, r, colour, lit):
    if lit: d.glowing(circle(cx, cy, r, colour))
    else: d.el(circle(cx, cy, r, LED_OFF.get(colour, '#2a2a2a')))
    d.parts.append({'kind': 'circle', 'cx': cx, 'cy': cy, 'r': r, 'colour': colour if lit else LED_OFF.get(colour, '#2a2a2a'), 'note': 'LED'})


def rect_cap(d, x, y, w, h, outline, lit, text=None, text_colour=PRINT, size=13.5, rx=2.6, lamp_text=False, fill=CAP):
    """A rectangular cap with a lit outline (BROWSE, LINK, pad modes, VINYL). Printed white text stays white when
    the lamp is off; text that is part of the lamp (pad modes, VINYL) goes grey."""
    d.el(rect(x - 1.2, y - 1.2, w + 2.4, h + 2.4, rx + 1, '#050506', CAP_EDGE, .7))
    d.el(rect(x, y, w, h, rx, fill), {'kind': 'rect', 'x': x, 'y': y, 'w': w, 'h': h, 'colour': fill, 'note': 'cap'})
    ring = rect(x + 1.1, y + 1.1, w - 2.2, h - 2.2, rx - .6, 'none', outline if lit else OFF_LINE, 1.9)
    d.glowing(ring) if lit and outline else d.el(ring)
    d.parts.append({'kind': 'outline', 'x': x + 1.1, 'y': y + 1.1, 'w': w - 2.2, 'h': h - 2.2, 'stroke': 1.9, 'colour': outline if lit else OFF_LINE})
    if text: d.text(text, x + w / 2, y + h / 2, size, (outline if lit else OFF_TEXT) if lamp_text else text_colour, 'middle')


# ---------------------------------------------------------------- the kinds of control

def k_round(d, g, lit, x):
    """A black round cap, optionally with an LED in its centre or a symbol printed on it."""
    r = g.get('r', 7.5)
    cx = x + r + 1.3
    cap_round(d, cx, H / 2, r)
    if g.get('led'): led(d, cx, H / 2, r * .36, g['led'], lit)
    if g.get('icon'): icon(d, g['icon'], cx, H / 2, r * .62, PRINT)
    return 2 * r + 2.6


def k_ring(d, g, lit, x):
    """A round cap in a lit ring: CUE, PLAY/PAUSE, SYNC, MASTER, TAP."""
    r, ring_w = g.get('r', 12.5), g.get('ring', 2.6)
    cx, cy = x + r + .5, H / 2
    d.el(circle(cx, cy, r + .4, '#050506'))
    ring = circle(cx, cy, r - ring_w / 2, 'none', g['colour'] if lit else OFF_RING, ring_w)
    d.glowing(ring) if lit else d.el(ring)
    d.parts.append({'kind': 'ring', 'cx': cx, 'cy': cy, 'r': r, 'stroke': ring_w, 'colour': g['colour'] if lit else OFF_RING})
    inner = r - ring_w - .3
    tint = g.get('tint') if lit and g.get('tint') else CAP
    d.el(circle(cx, cy, inner, 'url(#spun)' if g.get('spun') else 'url(#cap)' if tint == CAP else tint),
         {'kind': 'circle', 'cx': cx, 'cy': cy, 'r': inner, 'colour': tint, 'note': 'cap'})
    if g.get('spun'):  # the concentric finish of the big caps: two light wedges
        for a in ('M{0} {1} L{2} {3} L{4} {5}Z', 'M{0} {1} L{6} {7} L{8} {9}Z'):
            d.el('<path d="' + a.format(*map(num, (cx, cy, cx - inner * .95, cy - inner * .15, cx - inner * .9, cy + inner * .28,
                                                    cx + inner * .95, cy + inner * .15, cx + inner * .9, cy - inner * .28))) + '" fill="#fff" fill-opacity=".1"/>')
    if g.get('icon'): icon(d, g['icon'], cx + inner * .12, cy, inner * .42, PRINT)
    if g.get('cap'):
        colour = (g['colour'] if lit else OFF_TEXT) if g.get('lamp_text') else PRINT
        size = min(g.get('cap_size', 9), inner * 1.62 / text_width(g['cap'], 1, .02))
        d.text(g['cap'], cx, cy, size, colour, 'middle', weight=.45, track=.02)
    return 2 * r + 1


def k_litcap(d, g, lit, x):
    """A translucent round cap that glows as a whole: IN/CUE, OUT, BEAT FX ON/OFF."""
    r = g.get('r', 9)
    cx, cy = x + r + 1.3, H / 2
    d.el(circle(cx, cy, r + 1.3, '#050506', CAP_EDGE, .8))
    if lit:
        d.glowing(circle(cx, cy, r, f'url(#lit-{g["id"]})'))
    else:
        d.el(circle(cx, cy, r, 'url(#cap)'))
    d.parts.append({'kind': 'circle', 'cx': cx, 'cy': cy, 'r': r, 'colour': g['colour'] if lit else '#1e1e20', 'note': 'whole cap lit' if lit else 'cap, lamp off'})
    d.el(f'<path d="M{num(cx - r * .7)} {num(cy - r * .25)} A{num(r * .75)} {num(r * .75)} 0 0 1 {num(cx + r * .25)} {num(cy - r * .7)}" '
         f'fill="none" stroke="#fff" stroke-opacity="{".45" if lit else ".2"}" stroke-width="1.1" stroke-linecap="round"/>')
    return 2 * r + 2.6


def k_rect(d, g, lit, x):
    size = g.get('size', 13.5) * READ
    w, h = g.get('w') or text_width(g['cap'], size) + 12, g.get('h', 25)
    rect_cap(d, x + 1.2, (H - h) / 2, w, h, g.get('colour'), lit, g['cap'], size=size, lamp_text=g.get('lamp_text', False))
    return w + 2.4


def k_rectlit(d, g, lit, x):
    """A cap lit as a whole, with its text printed darker: DIRECTION (REV), the mixer's CUE."""
    w, h = g.get('w', 30) * 1.15, g.get('h', 19) * 1.2
    y = (H - h) / 2
    d.el(rect(x, y - 1.3, w + 2.6, h + 2.6, 3.5, '#050506', CAP_EDGE, .7))
    if lit: d.glowing(rect(x + 1.3, y, w, h, 2.6, g['colour']))
    else: d.el(rect(x + 1.3, y, w, h, 2.6, g.get('off', '#2b2a2a')))
    d.parts.append({'kind': 'rect', 'x': x + 1.3, 'y': y, 'w': w, 'h': h, 'colour': g['colour'] if lit else g.get('off', '#2b2a2a'), 'note': 'whole cap lit' if lit else 'cap, lamp off'})
    d.el(rect(x + 2.3, y + 1, w - 2, h * .42, 2, '#fff', extra=' fill-opacity=".14"'))
    if g.get('cap'): d.text(g['cap'], x + 1.3 + w / 2, H / 2, g.get('size', 11) * READ, g['ink'] if lit else '#6d6a68', 'middle', weight=.45)
    return w + 2.6


def k_shift(d, g, lit, x):
    """SHIFT: a plain rectangular cap in a bezel, its name printed in a white outlined box."""
    w, h = 46, 25
    y = (H - h) / 2
    d.el(rect(x, y - 1.5, w + 3, h + 3, 4, '#050506', CAP_EDGE, .8))
    d.el(rect(x + 1.5, y, w, h, 2.5, 'url(#cap)'), {'kind': 'rect', 'x': x + 1.5, 'y': y, 'w': w, 'h': h, 'colour': CAP, 'note': 'cap'})
    tw = text_width('SHIFT', 14.5)
    d.el(rect(x + 1.5 + (w - tw) / 2 - 3, H / 2 - 7.5, tw + 6, 15, 1.6, 'none', PRINT, 1.2),
         {'kind': 'outline', 'x': x + 1.5 + (w - tw) / 2 - 3, 'y': H / 2 - 7.5, 'w': tw + 6, 'h': 15, 'stroke': 1.1, 'colour': PRINT, 'note': 'printed box'})
    d.text('SHIFT', x + 1.5 + w / 2, H / 2, 14.5, PRINT, 'middle', weight=.45)
    return w + 3


def k_pill(d, g, lit, x):
    """TRACK SEARCH and SEARCH: two small caps in one dark bezel."""
    r = 6.6
    w = 4 * r + 9
    d.el(rect(x, H / 2 - r - 2.5, w, 2 * r + 5, r + 2.5, '#050506', CAP_EDGE, .8),
         {'kind': 'rect', 'x': x, 'y': H / 2 - r - 2.5, 'w': w, 'h': 2 * r + 5, 'colour': '#050506', 'note': 'bezel'})
    for i, name in enumerate(g['icons']):
        cx = x + 3 + r + i * (2 * r + 3)
        d.el(circle(cx, H / 2, r, 'url(#cap)'))
        d.el(f'<path d="M{num(cx - r * .7)} {num(H / 2 - r * .25)} A{num(r * .75)} {num(r * .75)} 0 0 1 {num(cx + r * .25)} {num(H / 2 - r * .7)}" fill="none" stroke="#fff" stroke-opacity=".2" stroke-width=".9" stroke-linecap="round"/>')
        icon(d, name, cx, H / 2, 4.2, PRINT)
    return w


def k_pads(d, g, lit, x):
    """The 2 x 4 performance pads, as on the deck. `on` names the pads lit (1-8) and `colours` their colours."""
    s, gap = 11.6, 2.6
    for i in range(8):
        px, py = x + (i % 4) * (s + gap), H / 2 - s - gap / 2 + (i // 4) * (s + gap)
        on = lit and (i + 1) in g['on']
        colour = g['colours'][i]
        d.el(rect(px, py, s, s, 1.6, '#09090a'))
        line = rect(px + .9, py + .9, s - 1.8, s - 1.8, 1.2, colour + '22' if on else 'none', colour if on else OFF_LINE, 1.7)
        d.glowing(line) if on else d.el(line)
        d.parts.append({'kind': 'pad', 'n': i + 1, 'x': px, 'y': py, 'w': s, 'h': s, 'stroke': 1.7, 'colour': colour if on else OFF_LINE})
    return 4 * s + 3 * gap


KEYS = ('back', 'tag-track', 'track-filter', 'shortcut')  # the four keys round the rotary selector: top left, top right, bottom left, bottom right


def k_rotary(d, g, lit, x):
    """The rotary selector: a knurled knob in a light ring, in a block of four grey keys (two above it, two below).
    The key a glyph names is drawn light; the others stay dark grey. Turn and push add a mark."""
    import math
    w, h, cut = 25, 31, 1.2
    cx, cy = x + w / 2, H / 2
    top = cy - h / 2
    for i, name in enumerate(KEYS):
        on = g.get('key') == name
        kx, ky = x + (i % 2) * (w / 2 + cut / 2), top + (i // 2) * (h / 2 + cut / 2)
        kw, kh = w / 2 - cut / 2, h / 2 - cut / 2
        fill = '#c4c7cb' if on else '#3a3c40'
        d.el(rect(kx, ky, kw, kh, 3, fill, '#050506', .6))
        d.el(rect(kx + 1, ky + (1 if i < 2 else kh - 3), kw - 2, 2, 1, '#fff', extra=f' fill-opacity="{".35" if on else ".08"}"'))
        if on: d.parts.append({'kind': 'rect', 'x': kx, 'y': ky, 'w': kw, 'h': kh, 'colour': fill, 'note': f'the {name} key, drawn light'})
    d.el(circle(cx, cy, 9.9, '#060607'), {'kind': 'circle', 'cx': cx, 'cy': cy, 'r': 9.9, 'colour': '#060607', 'note': 'black surround over the keys'})
    d.el(circle(cx, cy, 7.9, 'none', '#dfe1e3', 2), {'kind': 'ring', 'cx': cx, 'cy': cy, 'r': 7.9, 'stroke': 2, 'colour': '#dfe1e3', 'note': 'light ring round the knob'})
    scallops = ''.join(circle(cx + 6.2 * math.cos(math.radians(a)), cy + 6.2 * math.sin(math.radians(a)), 1.05, '#1d1e20') for a in range(0, 360, 30))
    d.el(circle(cx, cy, 6.9, '#0c0c0d'))
    d.el(scallops)
    d.el(circle(cx, cy, 5.3, 'url(#cap)'), {'kind': 'circle', 'cx': cx, 'cy': cy, 'r': 5.3, 'colour': CAP, 'note': 'knurled knob'})
    if g.get('mark') == 'turn':
        for a0, a1 in ((200, 250), (20, 70)):
            r = 14.6
            p = lambda a: (cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)))
            (ax, ay), (bx, by) = p(a0), p(a1)
            d.el(f'<path d="M{num(ax)} {num(ay)} A{r} {r} 0 0 1 {num(bx)} {num(by)}" fill="none" stroke="{PRINT}" stroke-width="1.5"/>')
            t = math.radians(a1)
            tx, ty = -math.sin(t), math.cos(t)  # direction of travel at the arrow head
            nx, ny = math.cos(t), math.sin(t)
            d.el(poly([(bx + tx * 3, by + ty * 3), (bx + nx * 2.3, by + ny * 2.3), (bx - nx * 2.3, by - ny * 2.3)], PRINT))
        d.parts.append({'kind': 'symbol', 'name': 'turn arrows', 'colour': PRINT})
    if g.get('mark') == 'push':
        d.el(circle(cx, cy, 2.4, PRINT))
        d.el(circle(cx, cy, 4.4, 'none', PRINT, 1))
        d.parts.append({'kind': 'symbol', 'name': 'push dot', 'colour': PRINT})
    return w


def k_slide(d, g, lit, x):
    """A three-position slide switch in its slot, the lever at `at` (0, 1 or 2)."""
    w, h = 27, 11
    y = H / 2 - h / 2 + 3.5
    d.el(rect(x, y, w, h, 2, '#050506', CAP_EDGE, .8), {'kind': 'rect', 'x': x, 'y': y, 'w': w, 'h': h, 'colour': '#050506', 'note': 'slot'})
    for i in range(3): d.el(circle(x + 5.5 + i * 8, y - 3, 1, PRINT))
    if g.get('usb_mark'):  # the computer symbol printed over the USB position
        d.el(rect(x + 2.2, y - 10.5, 6.6, 4.4, .6, 'none', PRINT, 1))
        d.el(f'<path d="M{num(x + 1)} {num(y - 5.3)} H{num(x + 10)}" stroke="{PRINT}" stroke-width="1.1"/>')
    lx = x + 1.8 + g['at'] * 8
    d.el(rect(lx, y + 1.2, 7.4, h - 2.4, 1.2, 'url(#cap)', '#55585c', .7),
         {'kind': 'rect', 'x': lx, 'y': y + 1.2, 'w': 7.4, 'h': h - 2.4, 'colour': '#202124', 'note': f'lever at position {g["at"] + 1} of 3'})
    d.el(f'<path d="M{num(lx + 3.7)} {num(y + 2.6)} V{num(y + h - 2.6)}" stroke="#d9dbdd" stroke-width="1.2"/>')
    return w


def k_beat(d, g, lit, x):
    """BEAT ◀ ▶: two caps either side of the printed BEAT."""
    w, h = 18, 18
    y = (H - h) / 2
    tw = text_width('BEAT', 15)
    xs = [x, x + w + 2.4 + tw + 8]
    for i, cx in enumerate(xs):
        if g.get('side') not in (None, ('prev', 'next')[i]): continue
        rect_cap(d, cx + 1.2, y, w, h, None, False, rx=2)
        icon(d, ('prev', 'next')[i], cx + 1.2 + w / 2 + (.6 if i else -.6), H / 2, 8, PRINT)
    tx = x + w + 2.4 + 4
    d.text('BEAT', tx, H / 2, 15, PRINT, weight=.45)
    d.el(f'<path d="M{num(tx - 1)} {num(H / 2 - 8.5)} H{num(tx + tw + 1)} M{num(tx - 1)} {num(H / 2 + 8.5)} H{num(tx + tw + 1)}" stroke="{PRINT}" stroke-width=".8"/>')
    return xs[1] + w + 2.4 - x


def k_load(d, g, lit, x):
    """LOAD 1 / 2: the printed LOAD, then the numbered caps with their white outline."""
    lw = d.text('LOAD', x, H / 2, 17, PRINT)
    cx = x + lw + GAP
    for n in g['decks']:
        rect_cap(d, cx + 1.2, H / 2 - 11.5, 21, 23, PRINT, True, n, size=16, rx=1.8)
        cx += 21 + 2.4 + 3
    return cx - 3 - x


def k_above_link(d, g, lit, x):
    """The button above LINK: a plain cap with no print (the unit's is a logo, which XZ Mods does not show), drawn
    over the top of LINK, dimmed, so the picture says which button it is without words."""
    w = g['w']
    rect_cap(d, x + 1.2, 2.4, w, 15, g['colour'], lit, rx=2.2)
    start = len(d.body)
    rect_cap(d, x + 1.2, 20.2, w, 10.4, LINK_BLUE, True, 'LINK', size=7.4, rx=1.8, lamp_text=True)
    d.body[start:] = ['<g opacity=".5">' + ''.join(d.body[start:]) + '</g>']
    for part in d.parts[-3:]:
        if part.get('y', 0) > 19: part['note'] = 'LINK below it, at half opacity, for reference'
    return w + 2.4


KINDS = {'round': k_round, 'ring': k_ring, 'litcap': k_litcap, 'rect': k_rect, 'rectlit': k_rectlit, 'shift': k_shift,
         'pill': k_pill, 'pads': k_pads, 'rotary': k_rotary, 'slide': k_slide, 'beat': k_beat, 'load': k_load,
         'above-link': k_above_link}


# ---------------------------------------------------------------- the controls
# id: file name. name: the name printed on the unit, which is also what screen readers and search get.
# aliases: other [[...]] spellings the manual may use. label: panel print the glyph shows beside the cap (on the
# unit it is usually above; a glyph puts it on the right so it stays legible at text size). lamp: the lit colour, or
# None. photo: where the control is in the reference photograph of the top panel (2000 x 1333, left deck), for the
# comparison sheet. where: where it is on the unit, for the README.

def G(id, name, kind, where, photo, aliases=(), label=None, lamp=None, **params):
    return {'id': id, 'name': name, 'kind': kind, 'where': where, 'photo': photo, 'aliases': list(aliases), 'label': label,
            'lamp': lamp, **params}


DECK = 'each deck'
GLYPHS = [
    # transport
    G('play-pause', 'PLAY/PAUSE', 'ring', DECK + ', bottom left; PLAY/PAUSE printed above', (178, 1026, 258, 1114), ['PLAY', 'PLAY / PAUSE', '▶/II'],
      label='PLAY/PAUSE', lamp=GREEN, colour=GREEN, icon='play-pause', spun=True, r=13.5, ring=2.8),
    G('cue', 'CUE', 'ring', DECK + ', above PLAY/PAUSE', (178, 946, 258, 1024), ['CUE (deck)'],
      label='CUE', lamp=ORANGE, colour=ORANGE, cap='CUE', cap_size=9.5, spun=True, r=13.5, ring=2.8),
    G('sync', 'SYNC', 'ring', DECK + ', BEAT SYNC pair right of the jog; hold for INST. DOUBLES', (650, 642, 692, 684), ['BEAT SYNC', 'INST. DOUBLES'],
      label='SYNC', lamp='#ececea', colour='#ececea', cap='SYNC', cap_size=8.4, r=11.5, ring=2.4),
    G('master', 'MASTER', 'ring', DECK + ', BEAT SYNC pair, right of SYNC', (682, 642, 724, 684), ['BEAT SYNC MASTER'],
      label='MASTER', lamp=ORANGE, colour='#ff6a10', cap='MASTER', cap_size=6.6, tint='#3b1d0c', r=11.5, ring=2.4),
    # tempo
    G('master-tempo', 'MASTER TEMPO', 'round', DECK + ', above the TEMPO slider; MASTER TEMPO printed above', (655, 794, 708, 842), ['KEY LOCK'],
      label='MASTER TEMPO', lamp=RED, led=RED, r=7.5),
    G('tempo-range', 'TEMPO ±6/±10/±16/WIDE', 'round', DECK + ', top of the TEMPO slider column', (652, 746, 714, 796), ['TEMPO RANGE', 'TEMPO'],
      label='TEMPO ±6/±10/±16/WIDE', r=7.5),
    G('tempo-reset', 'TEMPO RESET', 'round', DECK + ', left of the TEMPO slider; its lamp is beside the slider', (595, 944, 652, 1000), [],
      label='TEMPO RESET', lamp='#d6f53c', r=7.5, side_lamp='#d6f53c'),
    # loop
    G('in-cue', 'IN/CUE', 'litcap', DECK + ', top row; SHIFT: IN ADJUST', (160, 428, 222, 502), ['LOOP IN', 'IN', 'IN ADJUST', 'IN / CUE'],
      label='IN/CUE', lamp=AMBER, colour='#ff9a1c', r=9.5),
    G('out', 'OUT', 'litcap', DECK + ', top row; SHIFT: OUT ADJUST', (211, 428, 270, 502), ['LOOP OUT', 'OUT ADJUST'],
      label='OUT', lamp=AMBER, colour='#ff9a1c', r=9.5),
    G('reloop-exit', 'RELOOP/EXIT', 'round', DECK + ', top row', (268, 434, 332, 486), ['RELOOP', 'EXIT', 'RELOOP / EXIT'],
      label='RELOOP/EXIT', lamp=AMBER, led=AMBER, r=7.5),
    G('loop-cutter', '4/8BEAT', 'round', DECK + ', left of the jog; LOOP CUTTER in a box below it', (170, 503, 270, 547), ['4BEAT/8BEAT', '4/8 BEAT', 'LOOP CUTTER', '4/8BEAT (LOOP CUTTER)'],
      label='4/8BEAT', r=8.5),
    G('cue-loop-call', 'CUE/LOOP CALL', 'pill', DECK + ', top row; CALL printed above, 1/2X and 2X below', (405, 432, 562, 487), ['CALL', 'CUE/LOOP CALL ◀ ▶', 'CALL ◀ ▶', 'LOOP 1/2X 2X'],
      label='CALL', icons=('prev', 'next')),
    G('call-back', 'CUE/LOOP CALL ◀', 'round', DECK + ', top row; SHIFT: LOOP 1/2X', (470, 445, 512, 482), ['CALL ◀', '1/2X'],
      label='CALL', icon='prev', r=7.5),
    G('call-forward', 'CUE/LOOP CALL ▶', 'round', DECK + ', top row; SHIFT: LOOP 2X', (510, 445, 552, 482), ['CALL ▶', '2X'],
      label='CALL', icon='next', r=7.5),
    G('delete', 'DELETE', 'round', DECK + ', top row', (553, 436, 604, 486), [], label='DELETE', r=9),
    G('memory', 'MEMORY', 'round', DECK + ', top row', (594, 436, 642, 486), [], label='MEMORY', r=7.5),
    # jog
    G('slip', 'SLIP', 'round', DECK + ', left of the jog', (170, 564, 238, 604), [], label='SLIP', lamp=RED, led=RED, r=8.5),
    G('direction-rev', 'DIRECTION REV', 'rectlit', DECK + ', left of the jog; DIRECTION printed above, REV on the cap', (163, 638, 220, 690),
      ['REV', 'DIRECTION', 'REVERSE'], label='DIRECTION', lamp=RED, colour='#ff2020', ink='#a00808', cap='REV', w=27, h=17, size=10, off='#2e1c1c'),
    G('vinyl', 'VINYL', 'rect', DECK + ', JOG MODE, right of the jog', (652, 492, 720, 545), ['JOG MODE', 'VINYL (JOG MODE)'],
      lamp=BLUE, colour='#22a8ff', cap='VINYL', lamp_text=True, size=12.5),
    G('shift', 'SHIFT', 'shift', DECK + ', left of the jog', (164, 788, 218, 827), []),
    G('track-search', 'TRACK SEARCH', 'pill', DECK + ', left of the jog', (178, 834, 255, 884), ['TRACK SEARCH |◀◀ ▶▶|', 'TRACK SEARCH ◀◀ ▶▶'],
      label='TRACK SEARCH', icons=('track-prev', 'track-next')),
    G('search', 'SEARCH', 'pill', DECK + ', below TRACK SEARCH', (178, 881, 255, 932), ['SEARCH ◀◀ ▶▶', 'SEARCH (deck)'],
      label='SEARCH', icons=('rew', 'ffwd')),
    # pads
    G('hot-cue', 'HOT CUE', 'rect', DECK + ', PAD MODE row above the pads; SHIFT: CALL', (310, 943, 376, 980), ['HOT CUE mode'],
      lamp='#f2f2f2', colour='#f2f2f2', cap='HOT CUE', lamp_text=True, size=12),
    G('beat-loop', 'BEAT LOOP', 'rect', DECK + ', PAD MODE row', (376, 943, 436, 972), [], lamp=CYAN, colour=CYAN, cap='BEAT LOOP', lamp_text=True, size=12),
    G('slip-loop', 'SLIP LOOP', 'rect', DECK + ', PAD MODE row', (438, 943, 499, 972), [], lamp=AZURE, colour=AZURE, cap='SLIP LOOP', lamp_text=True, size=12),
    G('beat-jump', 'BEAT JUMP', 'rect', DECK + ', PAD MODE row', (501, 943, 564, 972), [], lamp=RED, colour='#ff3030', cap='BEAT JUMP', lamp_text=True, size=12),
    G('pads', 'Performance pads', 'pads', DECK + ', eight pads below PAD MODE (HOT CUE colours as photographed)', (308, 978, 566, 1114),
      ['PADS', 'pads', 'PERFORMANCE PADS'], lamp='#ffffff', on=set(range(1, 9)), colours=PAD_COLOURS),
    *[G(f'pad-{n}', f'Pad {n}', 'pads', DECK + f', pad {n} of 8 (top row 1 to 4, bottom row 5 to 8)', (308, 978, 566, 1114),
        [f'PAD {n}', f'pad {n}'], lamp=PAD_COLOURS[n - 1], on={n}, colours=PAD_COLOURS) for n in range(1, 9)],
    G('pads-beat-loop', 'Pads in BEAT LOOP', 'pads', 'pad colours the firmware sets in BEAT LOOP (native_led.h, provisional)', (308, 978, 566, 1114),
      [], lamp='#ff6000', on=set(range(1, 9)), colours=['#ff6000'] * 8),
    G('pads-slip-loop', 'Pads in SLIP LOOP', 'pads', 'pad colours the firmware sets in SLIP LOOP (native_led.h, provisional)', (308, 978, 566, 1114),
      [], lamp='#0060ff', on=set(range(1, 9)), colours=['#0060ff'] * 8),
    G('pads-beat-jump', 'Pads in BEAT JUMP', 'pads', 'pad colours the firmware sets in BEAT JUMP page 2 (native_led.h xz_jump_page2_color)', (308, 978, 566, 1114),
      [], lamp='#ff3000', on=set(range(1, 9)), colours=JUMP_COLOURS),
    # browse section
    *[G(i, n, 'rect', 'above the screen' + extra, photo, aliases, lamp=LIME, colour=LIME, cap=n, size=12.5)
      for i, n, extra, photo, aliases in (
          ('browse', 'BROWSE', '; hold: SEARCH', (860, 226, 937, 264), []),
          ('tag-list', 'TAG LIST', '', (936, 234, 1006, 264), []),
          ('info', 'INFO', '', (1008, 234, 1079, 264), []),
          ('menu', 'MENU', '; hold: UTILITY', (1078, 226, 1154, 264), ['UTILITY']))],
    G('above-link', 'Button above LINK', 'above-link', 'left of the screen, top of the four media buttons, directly above LINK; its '
      'cap prints a logo, which the glyph leaves out', (750, 263, 808, 294), ['BUTTON ABOVE LINK', 'ABOVE LINK', 'the button above LINK'],
      lamp='#f2f2f2', colour='#e9e9e9', w=44),
    G('link', 'LINK', 'rect', 'left of the screen, below the button above LINK', (750, 296, 810, 328), [], lamp=LINK_BLUE, colour=LINK_BLUE,
      cap='LINK', size=12.5),
    G('usb-1', 'USB 1', 'rect', 'left of the screen', (752, 328, 810, 360), ['USB1'], lamp=LIME, colour=LIME, cap='USB 1', size=12.5),
    G('usb-2', 'USB 2', 'rect', 'left of the screen', (752, 360, 812, 394), ['USB2'], lamp=LIME, colour=LIME, cap='USB 2', size=12.5),
    G('back', 'BACK', 'rotary', 'round the rotary selector, top left; BACK printed above', (1196, 248, 1246, 312), [], label='BACK', key='back'),
    G('tag-track', 'TAG TRACK/REMOVE', 'rotary', 'round the rotary selector, top right', (1230, 246, 1278, 312), ['TAG TRACK', 'TAG TRACK / REMOVE', 'REMOVE'],
      label='TAG TRACK/REMOVE', key='tag-track'),
    G('track-filter', 'TRACK FILTER/EDIT', 'rotary', 'round the rotary selector, bottom left; TRACK FILTER printed below, hold: EDIT', (1192, 364, 1246, 424),
      ['TRACK FILTER', 'EDIT', 'TRACK FILTER / EDIT'], label='TRACK FILTER', key='track-filter'),
    G('shortcut', 'SHORTCUT', 'rotary', 'round the rotary selector, bottom right; SHORT CUT printed below', (1230, 364, 1278, 424), ['SHORT CUT'],
      label='SHORTCUT', key='shortcut'),
    G('rotary', 'Rotary selector', 'rotary', 'right of the screen', (1192, 288, 1278, 388), ['ROTARY', 'rotary selector', 'rotary encoder', 'ROTARY SELECTOR']),
    G('rotary-turn', 'Turn the rotary selector', 'rotary', 'right of the screen', (1192, 288, 1278, 388), ['TURN', 'turn the rotary selector'], mark='turn'),
    G('rotary-push', 'Push the rotary selector', 'rotary', 'right of the screen', (1192, 288, 1278, 388), ['PUSH', 'push the rotary selector'], mark='push'),
    G('load', 'LOAD', 'load', 'below the rotary selector, one per side', (1188, 420, 1278, 458), ['LOAD 1, 2'], decks=('1', '2')),
    G('load-1', 'LOAD 1', 'load', 'below the rotary selector, left', (1188, 420, 1232, 458), [], decks=('1',)),
    G('load-2', 'LOAD 2', 'load', 'below the rotary selector, right', (1232, 420, 1278, 458), [], decks=('2',)),
    G('time-mode', 'TIME MODE', 'round', 'left of the screen; press for TIME MODE, hold for AUTO CUE', (748, 398, 816, 434), [], label='TIME MODE', r=7),
    G('auto-cue', 'AUTO CUE', 'round', 'left of the screen; the TIME MODE key, held', (748, 398, 816, 434), ['TIME MODE/AUTO CUE'], label='AUTO CUE', r=7),
    G('deck-quantize', 'DECK QUANTIZE', 'round', 'left of the screen, below TIME MODE', (750, 430, 816, 464), ['QUANTIZE'], label='DECK QUANTIZE', lamp=RED, led=RED, r=7),
    G('usb-stop', 'USB STOP', 'round', 'below each top USB port; hold to stop a stick', (1652, 358, 1712, 400), [], label='USB STOP', r=7),
    # mixer
    G('ch3-input', 'CH 3 INPUT', 'slide', 'mixer, top left; USB (the computer symbol), LINE, PHONO', (754, 482, 832, 532), ['CH3 INPUT', 'CH3', 'CH 3'],
      label='CH 3 INPUT', at=0, usb_mark=True),
    G('ch4-input', 'CH 4 INPUT', 'slide', 'mixer, top right; USB (the computer symbol), LINE, PHONO', (1203, 482, 1284, 532), ['CH4 INPUT', 'CH4', 'CH 4'],
      label='CH 4 INPUT', at=0, usb_mark=True),
    G('channel-cue', 'CUE (channel)', 'rectlit', 'mixer, above each channel number; the headphone CUE', (856, 794, 904, 822), ['channel CUE', 'CH CUE', 'headphones CUE'],
      lamp=AMBER, colour='#ffa21c', ink='#a35200', cap='CUE', w=29, h=16, size=10, off='#2f2c29'),
    G('crossfader-assign', 'CROSSFADER ASSIGN', 'slide', 'mixer, below each channel fader; A, THRU, B', (852, 978, 912, 1032), ['A THRU B', 'CROSSFADER ASSIGN A THRU B'],
      label='A THRU B', at=1),
    # BEAT FX
    G('beat', 'BEAT ◀ ▶', 'beat', 'BEAT FX, below the X-PAD', (1186, 646, 1284, 674), ['BEAT', 'BEAT ◀▶']),
    G('beat-back', 'BEAT ◀', 'beat', 'BEAT FX, below the X-PAD, left', (1186, 646, 1240, 674), [], side='prev'),
    G('beat-forward', 'BEAT ▶', 'beat', 'BEAT FX, below the X-PAD, right', (1230, 646, 1284, 674), [], side='next'),
    G('auto-tap', 'AUTO/TAP', 'round', 'BEAT FX, left of TAP', (1184, 676, 1216, 716), ['AUTO / TAP'], label='AUTO/TAP', r=7),
    G('tap', 'TAP', 'ring', 'BEAT FX', (1210, 676, 1257, 722), [], label='TAP', lamp=GREEN, colour=GREEN, cap='TAP', cap_size=8.4, lamp_text=True, r=11.5, ring=2),
    G('fx-quantize', 'FX QUANTIZE', 'round', 'BEAT FX, right of TAP', (1248, 676, 1287, 716), ['FX QUANTIZE'], label='FX QUANTIZE', lamp=RED, led=RED, r=7),
    *[G(f'fx-{n.lower()}', f'FX FREQUENCY {n}', 'rect', 'BEAT FX, FX FREQUENCY row', photo, [f'FX {n}'], lamp=BLUE, colour='#2f9bff', cap=n, lamp_text=True, size=12.5)
      for n, photo in (('LOW', (1184, 744, 1216, 772)), ('MID', (1218, 744, 1250, 772)), ('HI', (1250, 744, 1282, 772)))],
    G('fx-on-off', 'ON/OFF', 'litcap', 'BEAT FX, bottom', (1204, 1038, 1264, 1102), ['BEAT FX ON/OFF', 'FX ON/OFF', 'ON / OFF'],
      label='ON/OFF', lamp=BLUE, colour='#2a8cff', r=11.5),
    *[G(f'color-{n.lower().replace(" ", "-")}', n, 'rect', 'mixer, SOUND COLOR FX', photo, [f'SOUND COLOR FX {n}'], lamp='#f2f2f2', colour='#f0f0f0',
        cap=n, size=11.5) for n, photo in (('SPACE', (740, 728, 768, 750)), ('DUB ECHO', (770, 728, 800, 750)), ('SWEEP', (802, 728, 832, 750)),
                                           ('NOISE', (740, 766, 768, 790)), ('CRUSH', (770, 766, 800, 790)), ('FILTER', (802, 766, 832, 790)))],
]


@lru_cache(maxsize=1)
def by_name():
    """Every name and alias, upper-cased, to its glyph."""
    found = {}
    for g in GLYPHS:
        for key in [g['id'], g['name'], *g['aliases']]:
            key = key.upper()
            if check_trademarks.find(key): raise SystemExit(f'xz_buttons: {key!r} is a brand name ({g["id"]}); name the control by what it is')
            if key in found and found[key] is not g: raise SystemExit(f'xz_buttons: {key!r} names both {found[key]["id"]} and {g["id"]}')
            found[key] = g
    return found


def find(name):
    """(glyph, state) for a [[NAME]], glyph None when it has none. NAME:off draws the lamp off, NAME:lit lit."""
    name, _, state = name.strip().partition(':')
    name, state = name.strip(), state.strip().lower() or None
    if state not in (None, 'off', 'lit'): return None, state
    g = by_name().get(name.upper())
    return (g, state) if g else (None, state)


def states(g):
    return ('lit', 'off') if g['lamp'] else ('plain',)


def file_name(g, state, panel=False):
    return g['id'] + ('.panel' if panel else '') + ('.off' if state == 'off' else '') + '.svg'


def default_file(g, state=None):
    """The file a glyph shows by default: lit when it has a lamp (the colour is how the control is known),
    unless the text asks for the lamp off."""
    return file_name(g, 'off' if state == 'off' and g['lamp'] else 'lit')


# ---------------------------------------------------------------- one glyph

DEFS = ('<defs><radialGradient id="cap" cx=".38" cy=".32" r=".75"><stop offset="0" stop-color="#3b3d41"/><stop offset=".55" stop-color="#141516"/>'
        '<stop offset="1" stop-color="#060607"/></radialGradient>'
        '<radialGradient id="spun" cx=".5" cy=".5" r=".55"><stop offset="0" stop-color="#2a2b2e"/><stop offset=".7" stop-color="#151618"/>'
        '<stop offset="1" stop-color="#0a0a0b"/></radialGradient>{lit}'
        '<filter id="glow" x="-40%" y="-40%" width="180%" height="180%"><feGaussianBlur stdDeviation="1.15"/></filter></defs>')


# Panel print round a control, where the unit prints it, for the panel layout: rows above and below the cap (a row
# is a string or a list of pieces; ('box', text) is a SHIFT function printed in a grey box), and text left or right.
PANEL_PRINT = {
    'play-pause': {'above': ['PLAY / PAUSE']}, 'sync': {'below': ['— INST. DOUBLES']},
    'master-tempo': {'above': ['MASTER', 'TEMPO']}, 'tempo-range': {'above': ['TEMPO', '±6/±10/±16/WIDE']},
    'tempo-reset': {'above': ['TEMPO', 'RESET']},
    'in-cue': {'above': ['IN/CUE'], 'below': [[('box', 'IN ADJUST')]]}, 'out': {'above': ['OUT'], 'below': [[('box', 'OUT ADJUST')]]},
    'reloop-exit': {'above': ['RELOOP/EXIT'], 'right': ['— LOOP']},
    'loop-cutter': {'right': ['• 4 / — 8BEAT', [('box', 'LOOP CUTTER')]]},
    'cue-loop-call': {'above': ['CALL'], 'left': 'CUE/LOOP', 'below': [['1/2X  ', ('box', 'LOOP'), '  2X']]},
    'call-back': {'above': ['CALL'], 'below': ['1/2X']}, 'call-forward': {'above': ['CALL'], 'below': ['2X']},
    'delete': {'above': ['DELETE']}, 'memory': {'above': ['MEMORY']}, 'slip': {'right': ['SLIP']},
    'direction-rev': {'above': ['DIRECTION']}, 'vinyl': {'above': ['JOG MODE']},
    'track-search': {'above': ['TRACK SEARCH']}, 'search': {'above': ['SEARCH']},
    'hot-cue': {'below': [[('box', 'CALL')]]}, 'browse': {'above': ['— SEARCH']}, 'menu': {'above': ['— UTILITY']},
    'back': {'above': ['BACK']}, 'tag-track': {'above': ['TAG TRACK', '/ REMOVE']},
    'track-filter': {'below': ['• TRACK FILTER', '— EDIT']}, 'shortcut': {'below': ['SHORT', 'CUT']},
    'time-mode': {'left': '• TIME MODE', 'right': ['— AUTO CUE']}, 'auto-cue': {'left': '• TIME MODE', 'right': ['— AUTO CUE']},
    'deck-quantize': {'above': ['DECK QUANTIZE']}, 'usb-stop': {'above': ['— USB STOP']},
    'ch3-input': {'above': ['CH 3 INPUT']}, 'ch4-input': {'above': ['CH 4 INPUT']},
    'crossfader-assign': {'above': ['A  THRU  B'], 'below': ['CROSSFADER ASSIGN']},
    'auto-tap': {'above': ['AUTO', '/ TAP']}, 'fx-quantize': {'above': ['FX QUAN-', 'TIZE']},
    'fx-low': {'above': ['FX FREQUENCY']}, 'fx-mid': {'above': ['FX FREQUENCY']}, 'fx-hi': {'above': ['FX FREQUENCY']},
    'fx-on-off': {'above': ['ON / OFF']},
}
PS, PL = 8.5, 9.5  # panel print size and line height
BOX_FILL, BOX_INK = '#c3c5c6', '#232426'


def control(g, state, x, quiet_text=False):
    """The control itself (cap, lamp, on-cap print) drawn from x; (Draw, width). quiet_text leaves its text out."""
    d, lit = Draw(quiet_text), state != 'off'
    w = KINDS[g['kind']](d, g, lit, x)
    if g.get('side_lamp'):  # TEMPO RESET: its lamp sits beside the slider, drawn next to the key
        lx = x + w + 3
        colour = g['side_lamp'] if lit else LED_OFF[g['side_lamp']]
        lamp = rect(lx, H / 2 - 1.6, 6, 3.2, 1.6, colour)
        d.glowing(lamp) if lit else d.el(lamp)
        d.parts.append({'kind': 'rect', 'x': lx, 'y': H / 2 - 1.6, 'w': 6, 'h': 3.2, 'colour': colour, 'note': 'lamp'})
        w += 9
    return d, w


def svg_doc(g, state, w, h, body):
    lit_grad = ''
    if g['kind'] == 'litcap' and state != 'off':
        c = g['colour']
        lit_grad = (f'<radialGradient id="lit-{g["id"]}" cx=".45" cy=".42" r=".6"><stop offset="0" stop-color="#fff3d6" stop-opacity=".9"/>'
                    f'<stop offset=".3" stop-color="{c}"/><stop offset="1" stop-color="{c}" stop-opacity=".78"/></radialGradient>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {num(w)} {num(h)}" width="{num(w)}" height="{num(h)}">'
            f'<title>{esc(g["name"])}</title>{DEFS.format(lit=lit_grad)}'
            f'{rect(.5, .5, w - 1, h - 1, 5, PANEL, PANEL_EDGE, 1)}' + body + '</svg>\n')


def draw(g, state):
    """The inline glyph: the control, then its name in panel print on its right, all H units tall so it reads at
    text size. (svg text, width, parts)."""
    d, w = control(g, state, PAD)
    x = PAD + w
    if g.get('label'):
        x += GAP + d.text(g['label'], x + GAP, H / 2, 17 if len(g['label']) < 14 else 15, PRINT)
    w = x + PAD
    return svg_doc(g, state, w, H, ''.join(d.body)), w, d.parts


def draw_panel(g, state):
    """The panel glyph: the control with the print round it where the unit has it (above, below, beside), for
    figures and the reference sheets. (svg text, width, height)."""
    spec = PANEL_PRINT.get(g['id'], {})
    t = Draw()

    def pieces(row):
        return row if isinstance(row, list) else [row]

    def row_width(row):
        return sum(text_width(i[1], PS) + 5 if isinstance(i, tuple) else text_width(i, PS) for i in pieces(row))

    def put_row(row, cx, cy):
        x = cx - row_width(row) / 2
        for item in pieces(row):
            if isinstance(item, tuple):
                tw = text_width(item[1], PS)
                t.el(rect(x, cy - 4.4, tw + 5, 8.8, 1.4, BOX_FILL))
                t.text(item[1], x + 2.5, cy, PS, BOX_INK, weight=.3)
                x += tw + 5
            else:
                x += t.text(item, x, cy, PS, PRINT, weight=.3)

    above, below, right, left = spec.get('above', []), spec.get('below', []), spec.get('right', []), spec.get('left')
    left_w = text_width(left, PS) + 4 if left else 0
    d, cw = control(g, state, 0)
    right_w = max((row_width(r) for r in right), default=0) + (4 if right else 0)
    top = len(above) * PL + (3 if above else 0)
    bottom = len(below) * PL + (2 if below else 0)
    # extents round the cap's centre: rows above and below are centred on the cap, left and right text sit beside it
    half = max([row_width(r) / 2 for r in above + below] + [0])
    lo, hi = min(-cw / 2 - left_w, -half), max(cw / 2 + right_w, half)
    w, h = hi - lo + 2 * PAD, H + top + bottom
    ccx = PAD - lo
    ox = ccx - cw / 2
    for i, row in enumerate(above): put_row(row, ccx, 3.5 + i * PL + PL / 2)
    for i, row in enumerate(below): put_row(row, ccx, top + H - 2 + i * PL + PL / 2)
    if left: t.text(left, ox - left_w, top + H / 2, PS, PRINT, weight=.3)
    for i, row in enumerate(right):
        put_row(row, ox + cw + 4 + row_width(row) / 2, top + H / 2 + (i - (len(right) - 1) / 2) * (PL + 1))
    body = f'<g transform="translate({num(ox)} {num(top)})">' + ''.join(d.body) + '</g>' + ''.join(t.body)
    return svg_doc(g, state, w, h, body), w, h


def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')


def build():
    """{file name under ui/assets/buttons: bytes}."""
    files, index = {}, {}
    for g in GLYPHS:
        entry = {'name': g['name'], 'files': {}}
        for state in states(g):
            svg, w, _ = draw(g, state)
            name = file_name(g, state)
            files[name] = svg.encode('utf-8')
            entry['files']['off' if state == 'off' else 'lit'] = [name, round(w, 2)]
            svg, w, h = draw_panel(g, state)
            files[file_name(g, state, panel=True)] = svg.encode('utf-8')
        index[g['id']] = entry
    names = {key: g['id'] for key, g in sorted(by_name().items())}
    script = ('// Generated by tools/xz_buttons.py write. Do not edit by hand.\n'
              '// Every XDJ-XZ control glyph: id -> printed name and files (lit, off) with their width at height '
              f'{H}; names maps every [[NAME]] spelling (upper case) to an id.\n'
              'window.XZ_BUTTONS=' + json.dumps({'height': H, 'glyphs': index, 'names': names}, ensure_ascii=False, separators=(',', ':')) + ';\n')
    files['index.js'] = script.encode('utf-8')
    return files


LIST = 'manual/buttons/glyphs.md'  # under APP


def glyph_list():
    """manual/buttons/glyphs.md: every glyph, the names [[...]] accepts for it, where it is and its lamp."""
    rows = ['# Button glyphs', '', 'Generated by `python tools/xz_buttons.py write` from `GLYPHS` in `tools/xz_buttons.py`. Do not edit by hand.',
            'Pictures: `ui/assets/buttons/<id>.svg` (in text) and `<id>.panel.svg` (with the panel print round it), `.off` for the lamp off.', '',
            '| Glyph | Write | Also accepted | On the unit | Lamp |', '|---|---|---|---|---|']
    for g in GLYPHS:
        also = ', '.join(f'`[[{a}]]`' for a in g['aliases']) or ' '
        lamp = f'`{g["lamp"]}`' if g['lamp'] else 'none'
        rows.append(f'| ![{g["name"]}](../../{OUT}/{file_name(g, "lit")}) | `[[{g["name"]}]]` | {also} | {g["where"]} | {lamp} |')
    return ('\n'.join(rows) + '\n').encode('utf-8')


def printed_text(g):
    """Every word a glyph prints: its cap, its label and the panel print round it."""
    rows = [r for side in PANEL_PRINT.get(g['id'], {}).values() for r in (side if isinstance(side, list) else [side])]
    pieces = [p for r in rows for p in (r if isinstance(r, list) else [r])]
    return [g.get('cap') or '', g.get('label') or ''] + [p[1] if isinstance(p, tuple) else p for p in pieces]


def problems():
    out_dir = APP / OUT
    brands = [f'button glyphs: {g["id"]} prints a brand name ({", ".join(check_trademarks.find(t))}); draw the cap plain'
              for g in GLYPHS for t in printed_text(g) if check_trademarks.find(t)]
    if brands: return brands
    files = build()
    have = {p.name for p in out_dir.glob('*')} if out_dir.is_dir() else set()
    found = [f'button glyphs: {OUT}/{n} is missing' for n in files if n not in have]
    found += [f'button glyphs: {OUT}/{n} is out of date' for n in files
              if n in have and (out_dir / n).read_bytes().replace(b'\r\n', b'\n') != files[n]]
    found += [f'button glyphs: {OUT}/{n} is no longer drawn' for n in sorted(have - set(files))]
    if not (APP / LIST).is_file() or (APP / LIST).read_bytes().replace(b'\r\n', b'\n') != glyph_list():
        found.append(f'button glyphs: {LIST} is out of date')
    return found + ([f'button glyphs: run python tools/xz_buttons.py write and commit {OUT}/ and {LIST}'] if found else [])


def write():
    out_dir = APP / OUT
    if out_dir.exists(): shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)
    files = build()
    for name, data in files.items(): (out_dir / name).write_bytes(data)
    (APP / LIST).parent.mkdir(parents=True, exist_ok=True)
    (APP / LIST).write_bytes(glyph_list())
    print(f'{out_dir}: {len(GLYPHS)} glyphs, {len(files) - 1} SVG files')


# ---------------------------------------------------------------- pictures (headless Edge)

def screenshot(html, out, width, height):
    """A PNG of an HTML page, through tools/screenshot_html.mjs (headless Edge over CDP)."""
    with tempfile.TemporaryDirectory(prefix='xz-buttons-') as temp:
        page = Path(temp) / 'page.html'
        page.write_text(html, encoding='utf-8')
        subprocess.run(['node', str(APP / 'tools/screenshot_html.mjs'), str(page), str(Path(out).resolve()), str(int(width)), str(int(height))],
                       check=True, timeout=300)


def data_uri(svg):
    import base64
    return 'data:image/svg+xml;base64,' + base64.b64encode(svg.encode('utf-8')).decode()


def sheet(out, photo=None, only=None):
    """Every glyph, lit then unlit, at text size (21 px, 1.4em of 15 px) and at 3x, with the photograph's crop beside
    it when `photo` is given (a scratch comparison; never ship the photograph)."""
    rows = []
    glyphs = [g for g in GLYPHS if not only or g['id'] in only]
    crops = {}
    if photo:
        from PIL import Image
        import base64
        from io import BytesIO
        img = Image.open(photo).convert('RGB')
        for g in glyphs:
            c = img.crop(g['photo'])
            c = c.resize((c.width * 2, c.height * 2), Image.LANCZOS)
            buf = BytesIO(); c.save(buf, 'PNG')
            crops[g['id']] = 'data:image/png;base64,' + base64.b64encode(buf.getvalue()).decode()
    for g in glyphs:
        cells = []
        for state in states(g):
            svg, w, _ = draw(g, state)
            panel, pw, ph = draw_panel(g, state)
            cells.append(f'<td><img src="{data_uri(svg)}" height="21"></td><td><img src="{data_uri(panel)}" height="{round(ph * 2.6)}"></td>')
        if len(cells) == 1: cells.append('<td></td><td class="none">no lamp</td>')
        ref = f'<td><img src="{crops[g["id"]]}"></td>' if photo else ''
        rows.append(f'<tr><th>{esc(g["name"])}<small>{g["id"]}</small></th>{"".join(cells)}{ref}</tr>')
    head = '<tr><th>Control</th><td>Lit, text size</td><td>Lit, panel print, large</td><td>Off, text size</td><td>Off, panel print, large</td>' + ('<td>Photograph</td>' if photo else '') + '</tr>'
    style = ('<!doctype html><meta charset="utf-8"><style>body{margin:0;background:#26292c;color:#e8e8e8;font:13px system-ui,sans-serif}'
             'table{border-collapse:collapse;margin:0 12px;table-layout:fixed;width:calc(100% - 24px)}td,th{padding:6px 10px;border-bottom:1px solid #3a3e42;text-align:left;vertical-align:middle}'
             'th{width:130px}td:nth-child(2),td:nth-child(4){width:190px}th small{display:block;color:#9aa;font-weight:400}.none{color:#778}img{display:block}p{margin:12px;font-size:15px}</style>')
    intro = ('<p>XDJ-XZ button glyphs, tools/xz_buttons.py. The text-size columns are 21 px tall, 1.4em of 15 px text: <b>Press</b> '
             f'<img src="{data_uri(draw(GLYPHS[0], "lit")[0])}" height="21" style="display:inline;vertical-align:middle"> to start.</p>')
    from PIL import Image
    width = 1900 if photo else 1500
    parts = []
    with tempfile.TemporaryDirectory(prefix='xz-sheet-') as temp:
        for n, start in enumerate(range(0, len(rows), 8)):
            html = style + (intro if not start else '') + f'<table>{head if not start else ""}{"".join(rows[start:start + 8])}</table>'
            screenshot(html, Path(temp) / f'{n}.png', width, 0)
            parts.append(Image.open(Path(temp) / f'{n}.png').convert('RGB'))
        sheet_img = Image.new('RGB', (width, sum(p.height for p in parts)), '#26292c')
        y = 0
        for part in parts: sheet_img.paste(part, (0, y)); y += part.height
        sheet_img.save(out, optimize=True)
    print(out)


# ---------------------------------------------------------------- firmware spec

FACES = (('SH_SMALL', 12, 14), ('SH_TEXT', 20, 22))  # MODS text face, its pixel size, and the glyph height beside it


def firmware_label(g):
    return (g.get('label') or g['name']).upper()


def draw_firmware(g, state, face_px, glyph_px):
    """A glyph as the XZ would draw it in a MODS text line: the control without its printed text, scaled to glyph_px,
    then its name in the firmware font at face_px. (svg, width in px, parts in design units)."""
    d, cw = control(g, state, PAD, quiet_text=True)
    k = glyph_px / H
    gw = (cw + 2 * PAD) * k
    label = firmware_label(g)
    t = Draw()
    lw = t.text(label, gw + 3, glyph_px / 2, face_px, PRINT, weight=0, track=0)
    w = gw + 3 + lw + 1
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {num(w)} {glyph_px}" width="{num(w)}" height="{glyph_px}">'
           f'{DEFS.format(lit="")}<g transform="scale({num(k)})">{rect(.5, .5, cw + 2 * PAD - 1, H - 1, 5, PANEL, PANEL_EDGE, 1)}'
           + ''.join(d.body).replace('url(#lit-', 'url(#nolit-') + '</g>' + ''.join(t.body) + '</svg>')
    if g['kind'] == 'litcap' and state != 'off':  # a whole lit cap in its flat colour
        svg = svg.replace(f'url(#nolit-{g["id"]})', g['colour'])
    return svg, w, d.parts


def firmware(out_dir):
    """manual/buttons/firmware-spec.md and a PNG per glyph at each XZ text size, for drawing the glyphs in C later."""
    out_dir = Path(out_dir)
    pics = out_dir / 'firmware'
    if pics.exists(): shutil.rmtree(pics)
    pics.mkdir(parents=True)
    lines = ['# Button glyphs on the XZ screen: firmware spec', '',
             'Generated by `python tools/xz_buttons.py firmware` from `GLYPHS` in `tools/xz_buttons.py`. Do not edit by hand.', '',
             'For a later firmware pass: the XZ\'s own MODS screens can show the same glyph wherever they name a button, as the '
             'manual and the desktop app do. The display is 800 x 480, 32-bit colour. MODS text comes in two faces of the '
             'firmware font, Barlow Semi Condensed (`mods/ui/fonts`): `SH_SMALL` at 12 px and `SH_TEXT` at 20 px.', '',
             '## How a glyph sits in a MODS line', '',
             '- The glyph is the control without its printed text, on a rounded piece of the dark panel, followed 3 px later by '
             'the name below as ordinary MODS text in the line\'s own face. At these sizes no printed text on a cap or the panel '
             'is legible, so the shape and the lamp colour identify the control and the text names it.',
             '- Beside `SH_SMALL` text the glyph is **14 px** tall (scale 0.4375 of the design below); beside `SH_TEXT` it is '
             '**22 px** tall (scale 0.6875). Centre it on the text\'s cap height.',
             '- Scale every value below by that factor and round to whole pixels. Rings, outlines and pad edges stay at least 1 px; '
             'a lamp ring 2 px at 22 px. Leave out the light-from-above highlight and the glow at 14 px; at 22 px the glow is one '
             'pixel of the lamp colour at half strength round the lit shape.',
             '- A lit lamp is drawn in its lit colour, a lamp that is off in its off colour. Use the state the screen describes '
             '(for example SYNC lit when the text says SYNC is on); with no state to show, draw it lit.',
             '- The PNGs are the browser render of the same drawing at the XZ\'s size, label included: the target to match.',
             '- The design is 32 units tall. Coordinates are x, y from the glyph\'s top left; the panel piece is the whole glyph '
             f'box, corner radius 5, fill `{PANEL}`, edge `{PANEL_EDGE}`. Colours not listed are the cap gradient\'s base `{CAP}`.', '']
    cells = []
    for g in GLYPHS:
        lines += [f'## {g["name"]} (`{g["id"]}`)', '', f'Where: {g["where"]}. Text after it: `{firmware_label(g)}`.', '']
        for state in states(g):
            title = {'lit': 'Lit', 'off': 'Lamp off', 'plain': 'No lamp'}[state]
            shown = []
            for face, face_px, glyph_px in FACES:
                svg, w, parts = draw_firmware(g, state, face_px, glyph_px)
                name = f'{g["id"]}{".off" if state == "off" else ""}-{glyph_px}.png'
                cells.append((name, svg, w, glyph_px))
                shown.append(f'![{g["name"]}, {title.lower()}, beside {face}](firmware/{name})')
            lines += [f'**{title}**: ' + ' '.join(shown), '', '| Part | Design units | Colour | Note |', '|---|---|---|---|']
            lines += [part_row(p) for p in parts] + ['']
    (out_dir / 'firmware-spec.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    # every preview on one page per size, on the XZ's black; cut each out of the screenshot
    from PIL import Image
    for _, _, glyph_px in FACES:
        items = [c for c in cells if c[3] == glyph_px]
        html, y, boxes = ['<!doctype html><style>body{margin:0;background:#000}img{position:absolute;left:0}</style>'], 0, []
        for name, svg, w, px in items:
            html.append(f'<img src="{data_uri(svg)}" style="top:{y}px" width="{num(w)}" height="{px}">')
            boxes.append((name, y, w)); y += px + 4
        shot = pics / f'_page-{glyph_px}.png'
        screenshot(''.join(html), shot, 800, y + 4)
        page = Image.open(shot).convert('RGB')
        for name, top, w in boxes: page.crop((0, top, int(-(-w // 1)), top + glyph_px)).save(pics / name, optimize=True)
        shot.unlink()
    print(out_dir / 'firmware-spec.md')


def part_row(p):
    k = p['kind']
    f = lambda v: f'{v:.1f}'
    if k in ('circle', 'ring'):
        geo = f'centre {f(p["cx"])}, {f(p["cy"])}; r {f(p["r"])}' + (f'; stroke {f(p["stroke"])}' if 'stroke' in p else '')
    elif k == 'segment':
        geo = f'centre {f(p["cx"])}, {f(p["cy"])}; r {f(p["r"][0])} to {f(p["r"][1])}; {p["angles"][0]} to {p["angles"][1]} degrees (0 = right, clockwise)'
    elif k == 'text':
        geo = f'{f(p["x"])}, {f(p["y"])}; {f(p["w"])} x {f(p["h"])} cap height'
    elif k == 'symbol':
        geo = f'{f(p["x"])}, {f(p["y"])}; {f(p["w"])} x {f(p["h"])}' if 'x' in p else 'round the knob'
    else:
        geo = f'{f(p["x"])}, {f(p["y"])}; {f(p["w"])} x {f(p["h"])}' + (f'; stroke {f(p["stroke"])}' if 'stroke' in p else '')
    name = {'text': f'text `{p.get("text", "")}`', 'symbol': f'symbol {p.get("name", "")}', 'pad': f'pad {p.get("n", "")}',
            'segment': f'key segment ({p.get("name", "")})'}.get(k, k)
    return f'| {name} | {geo} | `{p["colour"]}` | {p.get("note", "")} |'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    sub.add_parser('write'); sub.add_parser('check')
    s = sub.add_parser('sheet'); s.add_argument('--out', type=Path, required=True)
    s.add_argument('--only', nargs='*', help='glyph ids; default every glyph')
    s.add_argument('--photo', type=Path, help='reference photograph of the top panel, for a side-by-side scratch sheet')
    f = sub.add_parser('firmware'); f.add_argument('--out', type=Path, default=APP / 'manual/buttons')
    a = p.parse_args()
    if a.cmd == 'write': return write()
    if a.cmd == 'check':
        found = problems()
        for line in found: print('STALE ' + line)
        print('button glyphs: current' if not found else f'button glyphs: {len(found) - 1} problem(s)')
        return 1 if found else 0
    if a.cmd == 'sheet': return sheet(a.out, a.photo, a.only)
    if a.cmd == 'firmware': return firmware(a.out)


if __name__ == '__main__':
    sys.exit(main())
