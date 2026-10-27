"""Draws the XDJ-XZ top panel schematics for the XZ Mods manual.

Layout follows the part-name drawings in the public XDJ-XZ operating instructions (DRI1625B pp.29, 32,
66, 92): power and MIC top left, browse section with the 7-inch screen top centre, MASTER and USB top
right, a deck on each side (transport on the left, tempo slider on the right of each deck), and the
four-channel mixer (channels 3 1 2 4) under the screen. Schematic only: no photos, no logos.

usage: python tools/draw_xz_panel.py manual/hardware
"""
import sys
from pathlib import Path

BODY, PANEL, STROKE = '#14181d', '#1b2026', '#3a424c'
KEY, KEY_STROKE = '#232931', '#4a535e'
USED = '#8fa3b8'            # stroke of controls XZ Mods changes or uses
INK, DIM, FAINT = '#c9d1da', '#8a94a0', '#5d6670'
FONT = "Inter, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif"


class Svg:
    def __init__(self):
        self.parts = []

    def add(self, s):
        self.parts.append(s)

    def rect(self, x, y, w, h, rx=4, fill=KEY, stroke=KEY_STROKE, sw=1.5):
        self.add(f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" rx="{rx:g}" fill="{fill}" stroke="{stroke}" stroke-width="{sw:g}"/>')

    def circle(self, cx, cy, r, fill=KEY, stroke=KEY_STROKE, sw=1.5):
        self.add(f'<circle cx="{cx:g}" cy="{cy:g}" r="{r:g}" fill="{fill}" stroke="{stroke}" stroke-width="{sw:g}"/>')

    def text(self, x, y, s, size=9, fill=DIM, anchor='middle', weight=600):
        s = s.replace('&', '&amp;').replace('<', '&lt;')
        self.add(f'<text x="{x:g}" y="{y:g}" font-size="{size:g}" fill="{fill}" text-anchor="{anchor}" '
                 f'font-weight="{weight}">{s}</text>')

    def knob(self, cx, cy, r, stroke=KEY_STROKE):
        self.circle(cx, cy, r, fill='#1f252c', stroke=stroke)
        self.add(f'<line x1="{cx:g}" y1="{cy:g}" x2="{cx:g}" y2="{cy - r + 2:g}" stroke="{DIM}" stroke-width="1.5"/>')

    def key(self, x, y, w, h, label='', size=8, used=False, rx=3):
        self.rect(x, y, w, h, rx=rx, stroke=USED if used else KEY_STROKE)
        if label: self.text(x + w / 2, y + h / 2 + size * 0.36, label, size=size, fill=INK if used else DIM)


def deck(s, letters=False):
    """One deck in local coordinates, 500 x 640. Both decks use the same layout."""
    s.rect(0, 0, 500, 640, rx=10, fill=PANEL, stroke=STROKE, sw=2)
    # Top row: loop keys, cue/loop call, delete, memory, vinyl speed adjust.
    s.circle(50, 42, 18); s.text(50, 16, 'IN/CUE', 8)
    s.circle(96, 42, 18); s.text(96, 16, 'OUT', 8)
    s.circle(148, 42, 11); s.text(148, 20, 'RELOOP/EXIT', 7.5)
    s.circle(316, 42, 10); s.circle(346, 42, 10); s.text(331, 22, 'CALL', 7.5)
    s.text(316, 45, '◀', 7, INK); s.text(346, 45, '▶', 7, INK)
    s.circle(386, 42, 12); s.text(386, 22, 'DELETE', 7.5)
    s.circle(424, 42, 10); s.text(424, 22, 'MEMORY', 7.5)
    s.knob(468, 46, 15); s.text(468, 22, 'VINYL SPEED', 7)
    # Left column.
    s.circle(46, 92, 10); s.text(64, 95, '4/8 BEAT', 7.5, anchor='start')
    s.circle(46, 136, 11); s.text(64, 139, 'SLIP', 7.5, anchor='start')
    s.text(46, 172, 'DIRECTION', 7); s.key(28, 178, 36, 22, 'REV', 7.5)
    s.key(24, 300, 44, 24, 'SHIFT', 8.5, used=True)
    s.text(46, 350, 'TRACK SEARCH', 7)
    s.key(24, 356, 20, 18, '⏮', 7); s.key(48, 356, 20, 18, '⏭', 7)
    s.text(46, 398, 'SEARCH', 7)
    s.key(24, 404, 20, 18, '◀◀', 6); s.key(48, 404, 20, 18, '▶▶', 6)
    s.circle(46, 478, 28, stroke=USED); s.text(46, 482, 'CUE', 10, INK)
    s.circle(46, 568, 28, stroke=USED); s.text(46, 572, 'PLAY', 9, INK)
    s.text(46, 534, 'PLAY/PAUSE', 7)
    # Jog wheel: ring, platter, centre display.
    s.circle(262, 232, 155, fill='#171b20', stroke=USED, sw=2)
    s.circle(262, 232, 138, fill='#1d2228', stroke=STROKE)
    s.circle(262, 232, 118, fill='#20262d', stroke=STROKE)
    s.circle(262, 232, 46, fill='#0b0d10', stroke=STROKE)
    s.text(262, 236, 'JOG', 9, FAINT)
    # Right column.
    s.text(466, 80, 'JOG MODE', 7); s.key(446, 86, 40, 18, 'VINYL', 7.5)
    s.text(466, 120, 'JOG ADJUST', 7); s.knob(466, 140, 13)
    s.text(466, 178, 'BEAT SYNC', 7)
    s.circle(447, 200, 14, stroke=USED); s.text(447, 203, 'SYNC', 6.5, INK)
    s.circle(481, 200, 14); s.text(481, 202, 'MASTER', 5, weight=700)
    s.text(466, 248, 'TEMPO', 7); s.circle(466, 262, 9)
    s.text(466, 288, 'MASTER TEMPO', 7); s.circle(466, 304, 9, stroke=USED)
    s.rect(452, 336, 28, 284, rx=5, fill='#101317', stroke=USED)
    s.add(f'<line x1="466" y1="346" x2="466" y2="610" stroke="{STROKE}" stroke-width="2"/>')
    s.rect(446, 462, 40, 22, rx=3, fill='#2b323b', stroke=KEY_STROKE)
    s.text(466, 632, 'TEMPO', 7)
    # Pad mode buttons, tempo reset, eight pads (A-D top row, E-H bottom row).
    s.text(258, 412, 'PAD MODE', 7)
    centres = (150, 222, 294, 366)
    for cx, label in zip(centres, ('HOT CUE', 'BEAT LOOP', 'SLIP LOOP', 'BEAT JUMP')):
        s.key(cx - 32, 418, 64, 16, label, 7, used=True)
    s.text(424, 412, 'TEMPO RESET', 6.5); s.circle(424, 428, 9)
    for row in range(2):
        for col, cx in enumerate(centres):
            y = 446 + row * 58
            s.rect(cx - 31, y, 62, 50, rx=5, fill='#0f1215', stroke=USED)
            if letters: s.text(cx, y + 31, 'ABCDEFGH'[row * 4 + col], 15, FAINT, weight=700)
    s.text(258, 574, 'PERFORMANCE PADS', 7)


def panel():
    s = Svg()
    s.rect(6, 6, 1588, 888, rx=18, fill=BODY, stroke=STROKE, sw=2)
    # Top left: power and MIC.
    s.rect(30, 26, 52, 200, rx=8, fill=PANEL, stroke=STROKE); s.text(56, 44, 'POWER', 7)
    s.key(44, 52, 24, 12)
    s.rect(90, 26, 440, 200, rx=8, fill=PANEL, stroke=STROKE); s.text(310, 44, 'MIC', 8)
    for row, y in enumerate((92, 160)):
        s.text(108, y + 3, str(row + 1), 9, FAINT)
        s.key(124, y - 10, 26, 18)
        for i, x in enumerate((196, 268, 318, 368)): s.knob(x, y, 13)
    s.key(440, 112, 56, 22, 'ON', 7); s.text(468, 98, 'FEEDBACK', 6.5); s.text(468, 106, 'REDUCER', 6.5)
    # Browse section: screen with keys around it.
    s.rect(546, 18, 508, 274, rx=10, fill=PANEL, stroke=STROKE, sw=2)
    s.rect(620, 62, 320, 192, rx=6, fill='#0b0d10', stroke=USED, sw=2)
    s.text(780, 162, '7-INCH TOUCH SCREEN', 11, FAINT)
    for cx, label, hold in zip((672, 744, 816, 888), ('BROWSE', 'TAG LIST', 'INFO', 'MENU'), ('SEARCH', '', '', 'UTILITY')):
        s.key(cx - 32, 36, 64, 16, label, 7.5, used=label == 'MENU')
        if hold: s.text(cx, 31, '▬ ' + hold, 6.5)
    for y, label, used in ((70, '', True), (98, 'LINK', True), (126, 'USB 1', False), (154, 'USB 2', False)):
        s.key(562, y, 48, 18, label, 7.5, used=used)
    s.circle(586, 194, 10, stroke=USED); s.text(586, 214, 'TIME MODE', 6.5); s.text(586, 222, '▬ AUTO CUE', 6)
    s.text(586, 240, 'DECK QUANTIZE', 6.5); s.circle(586, 256, 8, stroke=USED)
    # Rotary selector with BACK, TAG TRACK/REMOVE above and TRACK FILTER, SHORTCUT below.
    s.key(958, 92, 40, 28, '', used=True, rx=6); s.key(1002, 92, 40, 28, '', rx=6)
    s.key(958, 180, 40, 28, '', rx=6); s.key(1002, 180, 40, 28, '', used=True, rx=6)
    s.circle(1000, 150, 34, fill='#171b20', stroke=STROKE)
    s.circle(1000, 150, 26, fill='#2b323b', stroke=USED, sw=2)
    s.circle(1000, 150, 18, fill='#232931', stroke=KEY_STROKE)
    s.text(978, 86, 'BACK', 7.5, INK); s.text(1022, 78, 'TAG TRACK', 6.5); s.text(1022, 87, '/ REMOVE', 6.5)
    s.text(978, 220, 'TRACK FILTER', 6.5); s.text(1024, 220, 'SHORTCUT', 7, INK)
    s.text(1000, 240, 'LOAD', 7)
    s.key(968, 248, 26, 18, '1', 8, used=True); s.key(1006, 248, 26, 18, '2', 8, used=True)
    # Top right: MASTER and USB.
    s.rect(1070, 26, 262, 200, rx=8, fill=PANEL, stroke=STROKE); s.text(1201, 44, 'MASTER', 8)
    s.knob(1102, 92, 14)
    for i in range(12): s.rect(1130 + i * 13, 82, 9, 6, rx=1, fill='#101317', stroke=STROKE, sw=1)
    for i in range(12): s.rect(1130 + i * 13, 96, 9, 6, rx=1, fill='#101317', stroke=STROKE, sw=1)
    for x in (1160, 1210, 1260): s.knob(x, 160, 13)
    s.key(1090, 150, 30, 16, 'CUE', 6.5); s.knob(1306, 160, 12); s.text(1306, 136, 'BOOTH', 6.5)
    s.rect(1340, 26, 230, 200, rx=8, fill=PANEL, stroke=STROKE)
    for x, label in ((1390, 'USB 1'), (1470, 'USB 2')):
        s.text(x, 48, label, 7.5); s.rect(x - 22, 56, 44, 40, rx=4, fill='#0b0d10', stroke=KEY_STROKE)
        s.circle(x, 124, 8); s.text(x, 146, 'USB STOP', 6.5)
    s.text(1538, 62, 'MASTER REC', 6); s.circle(1538, 76, 8)
    s.text(1538, 112, 'TRACK MARK', 6); s.circle(1538, 126, 8)
    # Decks.
    for dx, label in ((30, 'LEFT DECK · DECK 1 OR 3'), (1070, 'RIGHT DECK · DECK 2 OR 4')):
        s.add(f'<g transform="translate({dx} 240)">'); deck(s); s.add('</g>')
    # Mixer and effects under the screen.
    s.rect(546, 300, 508, 580, rx=10, fill=PANEL, stroke=STROKE, sw=2)
    s.text(588, 322, 'AUX', 7.5); s.key(572, 330, 32, 12); s.knob(588, 372, 12)
    s.text(588, 410, 'SOUND COLOR FX', 6)
    for i in range(6): s.key(562 + (i % 3) * 18, 418 + (i // 3) * 20, 14, 14)
    s.text(588, 480, 'PARAMETER', 6.5); s.knob(588, 500, 12)
    s.text(588, 560, 'HEADPHONES', 6.5); s.knob(588, 590, 12); s.knob(588, 640, 12)
    for cx, ch in zip((668, 740, 812, 884), ('3', '1', '2', '4')):
        extra = ch in '34'
        s.knob(cx, 330, 11); s.text(cx, 314, 'TRIM', 6.5)
        for y in (378, 420, 462): s.knob(cx, y, 12)
        s.rect(cx - 28, 368, 6, 108, rx=2, fill='#101317', stroke=STROKE, sw=1)
        s.knob(cx, 512, 15); s.key(cx - 16, 540, 32, 14, 'CUE', 6.5)
        s.text(cx, 582, ch, 18, INK if extra else DIM, weight=700)
        s.rect(cx - 6, 600, 12, 110, rx=3, fill='#101317', stroke=USED if extra else STROKE)
        s.rect(cx - 16, 640, 32, 14, rx=2, fill='#2b323b', stroke=KEY_STROKE)
        s.key(cx - 18, 728, 36, 14, 'A  THRU  B', 5.5)
    s.key(612, 324, 26, 10); s.key(930, 324, 26, 10)
    s.text(625, 318, 'CH3 INPUT', 6); s.text(943, 318, 'CH4 INPUT', 6)
    s.rect(700, 820, 168, 14, rx=4, fill='#101317', stroke=STROKE)
    s.rect(774, 812, 20, 30, rx=3, fill='#2b323b', stroke=KEY_STROKE); s.text(784, 862, 'CROSSFADER', 7)
    s.text(990, 330, 'BEAT FX', 8)
    s.rect(940, 340, 100, 44, rx=4, fill='#101317', stroke=KEY_STROKE); s.text(990, 366, 'X-PAD', 8, FAINT)
    s.key(948, 396, 38, 16, '◀', 7); s.key(994, 396, 38, 16, '▶', 7); s.text(990, 426, 'BEAT', 6.5)
    s.circle(990, 452, 16); s.text(990, 455, 'TAP', 7)
    for i, label in enumerate(('LOW', 'MID', 'HI')): s.key(948 + i * 30, 490, 26, 14, label, 6)
    for y in (548, 604, 656, 708): s.knob(990, y, 14)
    s.circle(990, 770, 22); s.text(990, 773, 'ON/OFF', 7)
    body = '\n'.join(s.parts)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1600 900" font-family="{FONT}">\n'
            f'<title>XDJ-XZ top panel, schematic</title>\n{body}\n</svg>\n')


def deck_zoom():
    s = Svg()
    s.rect(0, 0, 800, 1000, rx=0, fill=BODY, stroke='none', sw=0)
    s.add('<g transform="translate(25 20) scale(1.5)">'); deck(s, letters=True); s.add('</g>')
    body = '\n'.join(s.parts)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 800 1000" font-family="{FONT}">\n'
            f'<title>One XDJ-XZ deck, schematic</title>\n{body}\n</svg>\n')


if __name__ == '__main__':
    out = Path(sys.argv[1])
    (out / 'xdj-xz.svg').write_text(panel(), encoding='utf-8')
    (out / 'xdj-xz-deck.svg').write_text(deck_zoom(), encoding='utf-8')
    print('wrote', out / 'xdj-xz.svg', out / 'xdj-xz-deck.svg')
