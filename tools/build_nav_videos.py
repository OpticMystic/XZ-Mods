"""Navigation videos for the manual: the firmware's own screen frames beside the true-scale Blender render of the
control being operated, synced to the navigation capture's timeline.

  build  --captures <nav-capture dir> --controls <control clips dir> [--frames DIR] [--flow GLOB]
         write the videos and nav-media.json (--controls: the clips' out folder, or their root, where out-v2 wins)
  stills --controls <control clips dir>                                               the hardware chapter's top-down stills
  check                                                                               fail when a video is stale

Inputs (none is drawn here):
  - the navigation capture (tools/capture_manual_nav.py): per flow the firmware's frames, frames.json, timeline.json.
    Its own --check must pass first, so the frames are what the current firmware C draws.
  - the control clips (the XDJ-XZ Blender model, rendered per control): <controls>/out/manifest.json and the untagged
    frame sequences in <controls>/tmp/frames/<control>/640x480/<gesture>/f####.png, with each camera's screen quad and
    glass mask.

Each video is 1280 x 480 at 30 fps. Left, 800 x 480: the firmware frame at 1:1, so its text stays as sharp as on the
player. Right, 480 x 480: the close-up of the control being operated, its press, turn or push frames played at the
timeline's times (the held frame repeated for as long as the key is held), and the same firmware frame warped onto the
display glass where that camera sees it. A tap is a ring at the tapped point, drawn on the screen frame itself, so it
shows in both halves. Holding SHIFT while pressing another key splits the right half: SHIFT above, the other key below.
The close-up changes 350 ms before the next control is used, with a 100 ms cross-fade. No words are burned in: the
captions (one per step, in the manual's key notation) go to manual/nav-media.json and the Manual page shows them.

check (also run by build_manual.py check and gate) fails when a chapter's ```nav``` block names a flow without a
video, a video or poster file changed, or the firmware sources, flow scripts or this compositor changed since the
videos were made.
"""
import argparse
import fnmatch
import hashlib
import os
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MEDIA = ROOT / 'manual/nav-media.json'
OUT = ROOT / 'ui/assets/manual/nav'      # served by the app as assets/manual/nav/
APP_PREFIX = 'assets/manual/nav/'
FORMAT = 'xz-nav-media/1'
W, H, SCREEN_W, PANE = 1280, 480, 800, 480
FPS = 30
CRF = 30                                          # 33 ghosts the highlight bars; 30 keeps the firmware text clean
LEAD_MS, FADE_MS = 350, 100
CLIP_FPS = 60
DOWN_FRAMES, HOLD_FRAME, UP_LAST = 5, 6, 13      # press clips: 1..5 going down, 6 held, 7..13 back up
TURN_FRAMES = 8                                   # one detent of the rotary selector
AMBER = (255, 168, 46)                            # the clips' operated-part outline (0xffa82e)
BG = (12, 12, 12)                                 # the clips' background (0x0c0c0c)
NEUTRAL = 'touch-screen'                          # the close-up for an input with no control clip: the screen itself
APP_FLOWS = {'physical-rekordbox': 'ui/setup.js', 'interface-choice': 'ui/setup.js'}   # flows the desktop app shows outside the manual


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256(path):
    return sha256_bytes(Path(path).read_bytes())


def text_sha256(path):
    """A text source's hash with CRLF read as LF, so a checkout's line-ending setting never stales a video."""
    return sha256_bytes(Path(path).read_bytes().replace(b'\r\n', b'\n'))


def digest(obj):
    return sha256_bytes(json.dumps(obj, sort_keys=True).encode())


def capture_inputs():
    """The navigation capture's inputs (capture_manual_nav.inputs: firmware sources, adapters, flow scripts), each
    hashed with CRLF read as LF."""
    sys.path.insert(0, str(ROOT / 'tools'))
    from capture_manual_nav import inputs
    return {name: text_sha256(ROOT / name) for name in inputs()}


# ---------------------------------------------------------------- the timeline as physical gestures

class Gesture:
    def __init__(self, kind, control, t, detail='', point=None, effects=()):
        self.kind, self.control, self.down, self.up = kind, control, t, None
        self.detail, self.point, self.effects, self.hold, self.direction = detail, point, list(effects), False, 0
        self.clip = None          # control clip id, or None (no clip for that input)

    def __repr__(self):
        return f'{self.kind}:{self.control}@{self.down}-{self.up}'


def gestures(timeline):
    out, open_ = [], {}
    for e in timeline:
        c, a, t = e['control'], e['action'], e['t_ms']
        if c in ('clock', 'fixture runtime'): continue
        if a == 'turn':
            g = Gesture('turn', c, t, e['detail'])
            g.direction = -1 if e['detail'].startswith('-') else 1
            g.up = t + TURN_FRAMES * 1000 // CLIP_FPS
            out.append(g)
        elif a in ('press', 'push'):
            g = Gesture('touch' if c == 'touch screen' else 'push' if a == 'push' else 'press', c, t, e['detail'], e.get('point'), e['effects'])
            open_[c] = g
            out.append(g)
        elif a == 'hold':
            if c in open_: open_[c].hold = True
        elif a == 'release':
            g = open_.pop(c, None)
            if g: g.up = t
        elif a == 'input':      # an offline game input: CUE comes as press then release, the rest as single events
            if c.endswith(' CUE') and c in open_:
                open_.pop(c).up = t
            else:
                g = Gesture('jog' if 'jog wheel' in c else 'press', c, t, e['detail'], None, e['effects'])
                if c.endswith(' CUE'): open_[c] = g
                out.append(g)
    for g in out:
        if g.up is None: g.up = g.down + 120
    return out


SOFTWARE = {'PLAY': 'play-pause', 'CUE': 'cue', 'SYNC': 'sync', 'MASTER': 'master', 'LOOP IN': 'loop-in', 'LOOP OUT': 'loop-out',
            'RELOOP': 'reloop'}   # software deck commands: the key on the side that drives the deck (deck 3 left, deck 4 right)


def assign_clips(gs, manifest):
    by_string = {s: cid for cid, c in manifest['controls'].items() for s in c['timeline_control'] if cid != 'load-2'}
    for g in gs:
        c = g.control
        side = 'left' if c.startswith('left deck ') else 'right' if c.startswith('right deck ') else None
        if c == 'LOAD':
            g.clip = 'load-2' if any(e.get('deck') in (1, 3) for e in g.effects) else 'load'
        elif any(e.get('host') == 'software-player' for e in g.effects):
            deck = next(e['deck'] for e in g.effects if e.get('host') == 'software-player')
            g.clip = 'quantize' if c == 'QUANTIZE' else f'{"left" if deck == 2 else "right"}-{SOFTWARE[c]}' if c in SOFTWARE else None
        elif c in by_string:
            g.clip = by_string[c]
        elif side and c.split(' deck ', 1)[1].upper().startswith('PAD '):
            g.clip = f'{side}-pad-{c.split()[-1].lower()}' if side == 'left' else None
        else:
            g.clip = None
        if g.clip and g.clip not in manifest['controls']: g.clip = None
    return gs


# ---------------------------------------------------------------- captions: the steps in the manual's key notation

KEYS = {'BACK': '[[BACK]]', 'MENU': '[[MENU]]', 'BROWSE': '[[BROWSE]]', 'TAG LIST': '[[TAG LIST]]', 'INFO': '[[INFO]]',
        'TRACK FILTER': '[[TRACK FILTER]]', 'SHORTCUT': '[[SHORTCUT]]', 'QUANTIZE': '[[DECK QUANTIZE]]',
        'SHIFT': '[[SHIFT]]', 'SYNC': '[[SYNC]]', 'MASTER': '[[MASTER]]', 'MASTER TEMPO': '[[MASTER TEMPO]]',
        'HOT CUE': '[[HOT CUE]]', 'BEAT LOOP': '[[BEAT LOOP]]', 'SLIP LOOP': '[[SLIP LOOP]]', 'BEAT JUMP': '[[BEAT JUMP]]',
        'PLAY': '[[PLAY/PAUSE]]', 'CUE': '[[CUE]]', 'LOOP IN': '[[IN/CUE]]', 'LOOP OUT': '[[OUT]]', 'RELOOP': '[[RELOOP/EXIT]]'}


def key_words(g):
    c = g.control
    if c == 'LOAD': return '[[LOAD]] 2' if g.clip == 'load-2' else '[[LOAD]] 1'
    if c == 'rekordbox button above LINK': return 'the button above [[LINK]]'   # the harness's name; no maker's name in captions
    side = ''
    if c.startswith(('left deck ', 'right deck ')):
        side, c = c.split(' deck ', 1)
        side = f' on the {side} deck'
    sw = next((e['deck'] for e in g.effects if e.get('host') == 'software-player'), None)
    if sw is not None: side = ' on the left deck' if sw == 2 else ' on the right deck'
    if c.upper().startswith(('PAD ', 'PERFORMANCE PAD ')): return f'pad {c.split()[-1].upper()}{side}'
    return KEYS.get(c, f'**{c}**') + side


def steps(gs):
    """[(t_ms, text)]: one caption per thing the reader does."""
    out, i = [], 0
    shift_until = None
    while i < len(gs):
        g = gs[i]
        if g.kind == 'turn' or (g.kind == 'push' and g.control == 'ROTARY SELECTOR'):
            j, n, direction = i, 0, 0
            while j < len(gs) and gs[j].kind == 'turn' and (not direction or gs[j].direction == direction) and (j == i or gs[j].down - gs[j - 1].down <= 400):
                direction = gs[j].direction; n += 1; j += 1
            push = gs[j] if j < len(gs) and gs[j].kind == 'push' and (j == i or gs[j].down - gs[j - 1].down <= 600) else None
            label = push.detail if push and push.detail and not push.detail.startswith('physical') else ''
            way = 'clockwise' if direction > 0 else 'anticlockwise'
            if n and push and label: text = f'Turn [[ROTARY SELECTOR]] to **{label}** and push it'
            elif n and push: text = f'Turn [[ROTARY SELECTOR]] {n} {"click" if n == 1 else "clicks"} {way}, then push it'
            elif n: text = f'Turn [[ROTARY SELECTOR]] {n} {"click" if n == 1 else "clicks"} {way}'
            elif label: text = f'Push [[ROTARY SELECTOR]] to choose **{label}**'
            else: text = 'Push [[ROTARY SELECTOR]]'
            out.append((g.down, text)); i = j + (1 if push else 0); continue
        if g.kind == 'touch':
            j, typed = i, ''
            while j < len(gs) and gs[j].kind == 'touch' and gs[j].detail.startswith('keyboard '):
                ch = gs[j].detail[len('keyboard '):]
                if ch != 'SHIFT': typed += ch
                j += 1
            if typed:
                out.append((g.down, f'Type **{typed}** on the screen keyboard')); i = j; continue
            label = g.detail
            out.append((g.down, 'Tap the screen' if not label or label[0].isdigit() and ',' in label else f'Tap **{label}**'))
            i += 1; continue
        if g.kind == 'jog':
            side = 'left' if g.control.startswith('left') else 'right'
            out.append((g.down, f'Turn the {side} jog wheel')); i += 1; continue
        if g.control == 'SOURCE':
            out.append((g.down, 'Open SOURCE ([[BACK]] at the top of a list)')); i += 1; continue
        if g.control.endswith('deck SHIFT'):
            inner = [h for h in gs[i + 1:] if h.down < g.up]
            if inner:
                words = ', then '.join(key_words(h) for h in inner)
                out.append((g.down, f'Hold {key_words(g)} and press {words}'))
                i += 1 + len(inner); continue
            nxt = gs[i + 1] if i + 1 < len(gs) else None
            if nxt and nxt.control == g.control and nxt.down - g.down <= 700 and not [h for h in gs[i + 2:] if h.down < nxt.up]:
                out.append((g.down, f'Press {key_words(g)} twice quickly')); i += 2; continue
        if g.hold:
            out.append((g.down, f'Hold {key_words(g)} for a second')); i += 1; continue
        out.append((g.down, f'Press {key_words(g)}')); i += 1
    return out


# ---------------------------------------------------------------- pictures

def controls_dirs(controls, frames=None):
    """(folder with the clips' manifest.json, folder with their untagged frames) for --controls / --frames.

    --controls is the clips' output folder (out/, out-v2/ ...: manifest.json beside it) or the controls root holding
    it; given the root, out-v2/ wins over out/ (the re-render without any maker's print). The frames are the untagged
    sequences <control>/640x480/<gesture>/f####.png: --frames, else the render folder that belongs to that output
    (out -> tmp/frames, out-v2 -> tmp-v2/frames or tmp/frames-v2), else the output folder itself."""
    controls = Path(controls)
    if not (controls / 'manifest.json').is_file():
        controls = next((controls / n for n in ('out-v2', 'out') if (controls / n / 'manifest.json').is_file()), controls)
    if not (controls / 'manifest.json').is_file(): raise SystemExit(f'no control clips manifest.json in {controls}')
    if frames: return controls, Path(frames)
    suffix = controls.name[len('out'):] if controls.name.startswith('out') else ''
    base = controls.parent
    for candidate in ([base / 'tmp/frames'] if not suffix else [base / f'tmp{suffix}/frames', base / f'tmp/frames{suffix}']):
        if candidate.is_dir(): return controls, candidate
    return controls, controls


class Clips:
    """The control clips: frames, crop centres, screen quads and masks, loaded on demand."""

    def __init__(self, controls, frames=None):
        import numpy as np
        self.np = np
        self.out, self.frames_dir = controls_dirs(controls, frames)
        self.manifest = json.loads((self.out / 'manifest.json').read_text(encoding='utf-8'))
        self.cache, self.meta = {}, {}

    def size(self, cid):
        return self.manifest['controls'][cid]['sizes']['640x480']

    def frame(self, cid, gesture, index):
        key = (cid, gesture, index)
        if key not in self.cache:
            import cv2
            path = self.frames_dir / cid / '640x480' / gesture / f'f{index:04d}.png'
            im = cv2.imread(str(path), cv2.IMREAD_COLOR)
            if im is None: raise SystemExit(f'missing control frame {path}')
            self.cache[key] = im[:, :, ::-1].copy()
        return self.cache[key]

    def rest_gesture(self, cid):
        g = self.size(cid)['gestures']
        return 'touch' if 'touch' in g else 'turn-cw-0' if 'turn_cw' in g else 'press'

    def info(self, cid):
        """Crop centre (the amber outline's centre), screen quad and mask for a control's 640 x 480 camera."""
        if cid in self.meta: return self.meta[cid]
        np = self.np
        size = self.size(cid)
        rest = self.frame(cid, self.rest_gesture(cid), 0).astype(int)
        amber = (np.abs(rest - np.array(AMBER)).sum(axis=2) < 90)
        ys, xs = np.nonzero(amber)
        centre = (float(xs.mean()), float(ys.mean())) if len(xs) else (320.0, 240.0)
        quad, mask = size.get('screen_quad_px'), None
        if quad and size.get('screen_mask'):
            import cv2
            m = cv2.imread(str(self.frames_dir / cid / '640x480' / 'screen-mask.png'), cv2.IMREAD_GRAYSCALE)
            mask = (m.astype(np.float32) / 255.0)[:, :, None] if m is not None else None
        self.meta[cid] = dict(centre=centre, quad=quad, mask=mask)
        return self.meta[cid]


def ring_screen(base, taps, t):
    """The firmware frame with a finger-tap ring at each tap that is down or just released at t (ms)."""
    import cv2
    import numpy as np
    img = base
    for g in taps:
        if not g.point or t < g.down or t > g.up + 360: continue
        if img is base: img = base.copy()
        x, y = int(g.point[0]), int(g.point[1])
        over = img.copy()
        if t <= g.up:
            k = min(1.0, (t - g.down) / 90.0)
            r, alpha = int(14 + 10 * k), 0.85
            cv2.circle(over, (x, y), r, (255, 255, 255), -1, cv2.LINE_AA)
            img[:] = (img * (1 - 0.28 * k) + over * (0.28 * k)).astype(np.uint8)
            over = img.copy()
        else:
            k = (t - g.up) / 360.0
            r, alpha = int(24 + 18 * k), 0.85 * (1 - k)
        cv2.circle(over, (x, y), r, AMBER, 3, cv2.LINE_AA)
        img[:] = (img * (1 - alpha) + over * alpha).astype(np.uint8)
    return img


def press_index(g, t):
    if t < g.down: return 0
    if t < g.up:
        return min(HOLD_FRAME, 1 + int((t - g.down) * CLIP_FPS / 1000))
    return min(UP_LAST, HOLD_FRAME + 1 + int((t - g.up) * CLIP_FPS / 1000))


class Pane:
    """The right half: the close-up of one control (or two, split), in its state at time t."""

    def __init__(self, clips, gs):
        self.clips, self.gs = clips, gs
        p = 0                       # the rotary selector's rest phase (its 12 ribs repeat every 2 detents)
        for g in gs:
            if g.clip == 'rotary-selector':
                g.phase = p
                if g.kind == 'turn': p = 1 - p

    def control_frame(self, cid, t):
        clips = self.clips
        mine = [g for g in self.gs if g.clip == cid and g.kind != 'touch']
        if cid == 'rotary-selector':
            phase = 0
            for g in mine:
                if t < g.down: break
                if g.kind == 'turn':
                    e = t - g.down
                    if e < TURN_FRAMES * 1000 / CLIP_FPS:
                        i = int(e * CLIP_FPS / 1000)
                        if g.direction > 0: return clips.frame(cid, f'turn-cw-{g.phase}', i)
                        return clips.frame(cid, f'turn-cw-{1 - g.phase}', TURN_FRAMES - i)
                    phase = 1 - g.phase
                else:
                    phase = g.phase
                    i = press_index(g, t)
                    if i not in (0, UP_LAST): return clips.frame(cid, f'push-{phase}', i)
            return clips.frame(cid, f'turn-cw-{phase}', 0)
        gesture = clips.rest_gesture(cid)
        if gesture == 'touch': return clips.frame(cid, 'touch', 0)
        for g in mine:
            if g.down <= t <= g.up + (UP_LAST - HOLD_FRAME) * 1000 / CLIP_FPS:
                return clips.frame(cid, 'press', press_index(g, t))
        return clips.frame(cid, 'press', 0)

    def closeup(self, cid, t, screen):
        """The 640 x 480 close-up at t with the firmware frame on its glass."""
        import cv2
        import numpy as np
        img = self.control_frame(cid, t)
        info = self.clips.info(cid)
        if info['quad'] is not None and info['mask'] is not None:
            src = np.float32([[0, 0], [SCREEN_W, 0], [SCREEN_W, H], [0, H]])
            M = cv2.getPerspectiveTransform(src, np.float32(info['quad']))
            warped = cv2.warpPerspective(screen, M, (640, 480), flags=cv2.INTER_AREA)
            img = (img * (1 - info['mask']) + warped * info['mask']).astype(np.uint8)
        return img

    def render(self, layout, t, screen):
        import cv2
        import numpy as np
        out = np.empty((PANE, PANE, 3), np.uint8); out[:] = BG
        if len(layout) == 1:
            cid = layout[0]
            img = self.closeup(cid, t, screen)
            if cid == NEUTRAL:      # the whole screen close-up, letterboxed
                out[60:420] = cv2.resize(img, (480, 360), interpolation=cv2.INTER_AREA)
            else:
                cx = int(round(min(max(self.clips.info(cid)['centre'][0], 240), 400)))
                out[:] = img[:, cx - 240:cx + 240]
            return out
        for k, cid in enumerate(layout[:2]):
            img = self.closeup(cid, t, screen)
            cx, cy = self.clips.info(cid)['centre']
            cx = int(round(min(max(cx, 240), 400))); cy = int(round(min(max(cy, 120), 360)))
            out[k * 240:(k + 1) * 240] = img[cy - 120:cy + 120, cx - 240:cx + 240]
        out[238:242] = (40, 40, 40)
        return out


def layouts(gs):
    """[(start_ms, layout tuple)]: which close-up the right half shows from when."""
    marks = []
    for i, g in enumerate(gs):
        cid = g.clip or NEUTRAL
        holder = next((h for h in gs[:i] if h.control.endswith('deck SHIFT') and h.down <= g.down < h.up and h.clip), None)
        if holder: lay = (holder.clip, cid)
        elif g.control.endswith('deck SHIFT') and g.clip:
            inner = next((h for h in gs[i + 1:] if h.down < g.up), None)
            lay = (g.clip, inner.clip or NEUTRAL) if inner else (cid,)
        else: lay = (cid,)
        marks.append((g, lay))
    out = []
    for i, (g, lay) in enumerate(marks):
        if out and out[-1][1] == lay: continue
        if not out: out.append((0, lay)); continue
        prev = marks[i - 1][0]
        end = prev.up + (0 if prev.kind == 'turn' else (UP_LAST - HOLD_FRAME) * 1000 / CLIP_FPS)
        out.append((min(max(end, g.down - LEAD_MS), g.down - FADE_MS), lay))   # faded in before the press starts
    return out or [(0, (NEUTRAL,))]


# ---------------------------------------------------------------- one flow

def render_flow(flow, cap_dir, clips, out_dir, poster_quality=55):
    import cv2
    import numpy as np
    folder = Path(cap_dir) / flow
    timeline = json.loads((folder / 'timeline.json').read_text())
    frames = json.loads((folder / 'frames.json').read_text())
    gs = assign_clips(gestures(timeline), clips.manifest)
    taps = [g for g in gs if g.kind == 'touch']
    pane = Pane(clips, gs)
    lays = layouts(gs)
    duration = frames['duration_ms']
    screens = {}

    def screen_at(t):
        f = frames['frames'][0]
        for x in frames['frames']:
            if x['t_ms'] <= t: f = x
            else: break
        if f['frame'] not in screens:
            screens[f['frame']] = cv2.imread(str(folder / f['frame']), cv2.IMREAD_COLOR)[:, :, ::-1].copy()
        return screens[f['frame']]

    def layout_at(t):
        cur, prev, since = lays[0][1], None, -1e9
        for start, lay in lays:
            if start <= t: prev, cur, since = cur, lay, start
        return cur, prev, since

    def frame_at(t):
        canvas = np.empty((H, W, 3), np.uint8)
        screen = ring_screen(screen_at(t), taps, t)
        canvas[:, :SCREEN_W] = screen
        canvas[:, SCREEN_W:SCREEN_W + 2] = (34, 34, 34)
        lay, prev, since = layout_at(t)
        right = pane.render(lay, t, screen)
        if prev is not None and prev != lay and t - since < FADE_MS:
            k = (t - since) / FADE_MS
            right = (pane.render(prev, t, screen) * (1 - k) + right * k).astype(np.uint8)
        canvas[:, SCREEN_W + 2:] = right[:, 2:]
        return canvas

    out_dir.mkdir(parents=True, exist_ok=True)
    video, poster = out_dir / f'{flow}.mp4', out_dir / f'{flow}.webp'
    n = int(round(duration * FPS / 1000))
    cmd = ['ffmpeg', '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}', '-r', str(FPS), '-i', '-',
           '-an', '-c:v', 'libx264', '-preset', 'slower', '-tune', 'stillimage', '-crf', str(CRF), '-g', str(FPS * 10),
           '-pix_fmt', 'yuv420p', '-movflags', '+faststart', '-map_metadata', '-1', '-fflags', '+bitexact',
           '-flags:v', '+bitexact', str(video)]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    for k in range(n):
        proc.stdin.write(frame_at(k * 1000 / FPS).tobytes())
    proc.stdin.close()
    if proc.wait(): raise SystemExit(f'ffmpeg failed on {flow}')
    # the poster: the last step's outcome, every control back at rest
    t_poster = min(duration - 1, (gs[-1].up + 500) if gs else 0)
    from PIL import Image
    Image.fromarray(frame_at(t_poster)).save(poster, 'WEBP', quality=poster_quality, method=6)
    return dict(seconds=round(duration / 1000, 3), steps=steps(gs),
                clips=sorted({g.clip for g in gs if g.clip}), unclipped=sorted({g.control for g in gs if not g.clip and g.kind != 'touch'}))


# ---------------------------------------------------------------- build / check

def chapter_flows(chapters=None, root=ROOT):
    """{flow: [chapter slug, ...]} for every ```nav``` block in the chapters (the fence's flow and its extra lines).
    chapters: [(slug, text)]; default every manual/chapters/*.md. APP_FLOWS join when root holds the file showing them."""
    import re
    found = {}
    if chapters is None:
        chapters = [(p.stem, p.read_text(encoding='utf-8')) for p in sorted((ROOT / 'manual/chapters').glob('*.md'))]
    for flow, where in APP_FLOWS.items():
        if (Path(root) / where).is_file(): found.setdefault(flow, []).append(where)
    for slug, text in chapters:
        lines = text.replace('\r\n', '\n').split('\n')
        for i, line in enumerate(lines):
            m = re.match(r'^```nav\s+(\S+)', line)
            if not m: continue
            j = i + 1
            while not lines[j].startswith('```'): j += 1
            for flow in [m.group(1)] + [l.split('|')[0].strip() for l in lines[i + 1:j] if l.strip()]:
                found.setdefault(flow, []).append(slug)
    return found


def scene_digest(clips):
    """The clips' manifest and the scene source beside it (provenance; the scene lives outside this repo)."""
    root = clips.out.parent
    files = [clips.out / 'manifest.json', *sorted((root / 'src').rglob('*.py'))]
    return digest({p.relative_to(root).as_posix(): text_sha256(p) for p in files if p.is_file()})


def clip_digest(clips, cid):
    folder = clips.frames_dir / cid / '640x480'
    return digest({p.relative_to(folder).as_posix(): sha256(p) for p in sorted(folder.rglob('*.png'))})


def build(captures, controls, frames=None, only=None):
    captures = Path(captures)
    flows = chapter_flows()
    if only: flows = {f: v for f, v in flows.items() if fnmatch.fnmatchcase(f, only)}
    if not flows: raise SystemExit('no ```nav``` block in the chapters names a matching flow')
    cap_manifest = json.loads((captures / 'manifest.json').read_text())
    subprocess.run([sys.executable, str(ROOT / 'tools/capture_manual_nav.py'), '--zig', 'unused', '--out', str(captures), '--check'], check=True)
    missing = sorted(f for f in flows if f not in cap_manifest['flows'])
    if missing: raise SystemExit('flows not in the capture: ' + ', '.join(missing))
    clips = Clips(controls, frames)
    media = json.loads(MEDIA.read_text(encoding='utf-8')) if MEDIA.is_file() else {'format': FORMAT, 'flows': {}}
    media.update(format=FORMAT, fps=FPS, size=[W, H], crf=CRF, generator='tools/build_nav_videos.py',
                 capture_inputs_sha256=digest(capture_inputs()), compositor_sha256=text_sha256(Path(__file__)),
                 scene_sha256=scene_digest(clips), controls=clips.out.name, frames=os.path.relpath(clips.frames_dir, clips.out.parent).replace(os.sep, '/'))
    for flow in sorted(flows):
        print('video', flow, flush=True)
        info = render_flow(flow, captures, clips, OUT)
        outputs = {k: f'{cap_manifest["outputs_sha256"][k]}' for k in sorted(cap_manifest['outputs_sha256']) if k.startswith(flow + '/')}
        script = json.loads((ROOT / f'manual/nav/{flow}.json').read_text())
        media['flows'][flow] = dict(
            video=APP_PREFIX + f'{flow}.mp4', poster=APP_PREFIX + f'{flow}.webp', w=W, h=H, seconds=info['seconds'],
            interface=script.get('interface', '').lower(), steps=[dict(t_ms=int(t), text=s) for t, s in info['steps']],
            clips=info['clips'], unclipped=info['unclipped'],
            sha256=dict(video=sha256(OUT / f'{flow}.mp4'), poster=sha256(OUT / f'{flow}.webp'),
                        script=text_sha256(ROOT / f'manual/nav/{flow}.json'), capture=digest(outputs),
                        clips={c: clip_digest(clips, c) for c in info['clips'] + ([NEUTRAL] if NEUTRAL not in info['clips'] else [])}))
    keep = set(chapter_flows())
    for flow in [f for f in media['flows'] if f not in keep]:
        del media['flows'][flow]
        for ext in ('mp4', 'webp'): (OUT / f'{flow}.{ext}').unlink(missing_ok=True)
    text = json.dumps(media, indent=1, sort_keys=True, ensure_ascii=False) + '\n'
    MEDIA.write_bytes(text.encode('utf-8'))
    total = sum(p.stat().st_size for p in OUT.glob('*') if p.is_file())
    print(f'{len(media["flows"])} videos, {total / 2**20:.2f} MiB in {OUT.relative_to(ROOT).as_posix()}')


def problems(flows=None, root=ROOT):
    """Why the navigation videos do not match the chapters and the current sources (empty when they do). flows is
    {flow: [chapter slug, ...]} (default: the chapters' nav blocks); root is the checkout holding manual/ and ui/."""
    flows = chapter_flows() if flows is None else flows
    if not flows: return []
    media_path = Path(root) / 'manual/nav-media.json'
    if not media_path.is_file(): return ['manual/nav-media.json is missing: run tools/build_nav_videos.py build']
    media = json.loads(media_path.read_text(encoding='utf-8'))
    how = ' Capture the flows again (tools/capture_manual_nav.py), then run tools/build_nav_videos.py build.'
    out = []
    if media.get('format') != FORMAT: out.append(f'manual/nav-media.json is not {FORMAT}')
    if media.get('capture_inputs_sha256') != digest(capture_inputs()):
        out.append('navigation videos are stale: the firmware sources or flow scripts changed since they were captured.' + how)
    if media.get('compositor_sha256') != text_sha256(Path(__file__)):
        out.append('navigation videos are stale: tools/build_nav_videos.py changed. Run tools/build_nav_videos.py build.')
    for flow, slugs in sorted(flows.items()):
        entry = media.get('flows', {}).get(flow)
        if not entry:
            out.append(f'{slugs[0]}: ```nav {flow}``` has no video. Run tools/build_nav_videos.py build'); continue
        script = Path(root) / f'manual/nav/{flow}.json'
        if not script.is_file(): out.append(f'{slugs[0]}: ```nav {flow}```: manual/nav/{flow}.json does not exist'); continue
        if entry['sha256']['script'] != text_sha256(script): out.append(f'{flow}: its flow script changed since the video was made.' + how)
        for key in ('video', 'poster'):
            path = Path(root) / 'ui' / entry[key]
            if not path.is_file(): out.append(f'{flow}: {entry[key]} is missing')
            elif sha256(path) != entry['sha256'][key]: out.append(f'{flow}: {entry[key]} is not the file nav-media.json records')
    extra = sorted(set(media.get('flows', {})) - set(flows))
    if extra: out.append('nav-media.json has videos no chapter shows: ' + ', '.join(extra) + '. Run tools/build_nav_videos.py build')
    return out


STILLS = {'xdj-xz-top.png': ('xdj-xz.png', None), 'xdj-xz-deck-left.png': ('xdj-xz-deck.png', 1000)}  # topdown/ -> manual/hardware/, width


def stills(controls):
    """Copy the model's top-down stills (the hw: figures in hardware.md) from the clips' topdown/ folder into
    manual/hardware/. The deck still is downscaled to 1000 px wide; its callouts in hardware.md are in that size."""
    from PIL import Image
    out, _ = controls_dirs(controls)
    for name, (target, width) in STILLS.items():
        img = Image.open(out / 'topdown' / name).convert('RGB')
        if width: img = img.resize((width, round(img.height * width / img.width)), Image.LANCZOS)
        img.save(ROOT / 'manual/hardware' / target, optimize=True)
        print(f'manual/hardware/{target} from {out.name}/topdown/{name} ({img.width} x {img.height})')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build')
    b.add_argument('--captures', required=True, type=Path)
    b.add_argument('--controls', required=True, type=Path, help='the control clips: their out folder, or the controls root (out-v2 wins)')
    b.add_argument('--frames', type=Path, help='the untagged frame sequences, when they are not beside --controls')
    b.add_argument('--flow')
    s = sub.add_parser('stills', help='copy the top-down stills of the hardware chapter from the clips')
    s.add_argument('--controls', required=True, type=Path)
    sub.add_parser('check')
    a = p.parse_args()
    if a.cmd == 'build':
        if not shutil.which('ffmpeg'): raise SystemExit('ffmpeg is not on PATH')
        build(a.captures, a.controls, a.frames, a.flow)
    elif a.cmd == 'stills':
        stills(a.controls)
    else:
        found = problems()
        for line in found: print(line)
        if found: raise SystemExit(1)
        print(f'navigation videos current: {len(chapter_flows())} flows')


if __name__ == '__main__':
    main()
