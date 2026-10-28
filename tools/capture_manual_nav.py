"""Capture real C interface handlers against deterministic, offline fixture snapshots."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import PIL
from PIL import Image, ImageDraw, ImageChops

ROOT = Path(__file__).resolve().parents[1]
MODS = ROOT / 'source/xdj-xz-toolkit/mods'
sys.path[:0] = [str(MODS / 'shell'), str(MODS / 'ui')]
from shell_sources import SHELL_SOURCES
from ui_sources import UI_SOURCES


def build(zig, exe):
    sources = sorted({(MODS / side / p).resolve() for side, names in [('shell', SHELL_SOURCES), ('ui', UI_SOURCES)] for p in names})
    subprocess.run([str(Path(zig).resolve()), 'cc', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    *(str(p) for p in sources), str(MODS / 'shell/nav_capture.c'), '-o', str(exe)], check=True)


class Capture:
    def __init__(self, exe, folder):
        self.folder = folder
        folder.mkdir(parents=True, exist_ok=True)
        self.raw = folder / 'screen.ppm'
        self.proc = subprocess.Popen([str(exe), str(self.raw)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self.t = 0
        self.timeline = []
        self.frames = []
        self.last = None
        self.state = None

    def send(self, cmd, a=0, b=0, control='clock', action='redraw', detail=''):
        self.proc.stdin.write(f'{cmd} {a} {b} {self.t}\n')
        self.proc.stdin.flush()
        self.state = json.loads(self.proc.stdout.readline())
        if self.state['changed']:
            with Image.open(self.raw) as raw:
                im = raw.convert('RGB')
            if self.state.get('classic_overlay'):
                background = Image.open(ROOT/'manual/captures/classic-play.png').convert('RGB')
                mask = ImageChops.difference(im, Image.new('RGB', im.size, (255,0,255))).convert('L').point(lambda x: 255 if x else 0)
                background.paste(im, (0,0), mask)
                im = background
            digest = hashlib.sha256(im.tobytes()).hexdigest()
            if digest != self.last:
                if self.frames and self.frames[-1]['t_ms'] == self.t:
                    name = self.frames[-1]['frame']
                    self.frames[-1]['page'] = self.state['title']
                else:
                    name = f'frame-{len(self.frames)+1:04d}.png'
                    self.frames.append({'frame': name, 't_ms': self.t, 'page': self.state['title']})
                im.save(self.folder / name)
                self.last = digest
        self.timeline.append(dict(t_ms=self.t, control=control, action=action, detail=detail,
                                  frame=self.frames[-1]['frame'], panel=self.state['panel'], page=self.state['page'], effects=self.state.get('effects', [])))
        return self.state

    def wait(self, ms):
        end = self.t + ms
        while self.t < end:
            self.t = min(end, self.t + 12)
            self.send('wait')

    def tap(self, x, y, label=''):
        self.send('down', x, y, 'touch screen', 'press', label or f'{x},{y}')
        self.wait(120)
        self.send('up', x, y, 'touch screen', 'release', label or f'{x},{y}')
        self.wait(400)

    def step(self, step):
        if 'fixture' in step:
            self.send('fixture', step['fixture'], control='fixture runtime', action='snapshot', detail=step.get('detail', ''))
        elif 'wait' in step:
            self.wait(step['wait'])
        elif 'tap' in step or 'select' in step:
            label = step.get('tap', step.get('select'))
            matches = [w for w in self.state['widgets'] if w['label'] == label]
            if not matches:
                raise RuntimeError(f'{self.folder.name}: {label!r} missing on {self.state["title"]}; choices: {[w["label"] for w in self.state["widgets"]]}')
            if 'select' in step:
                for _ in range(80):
                    if self.state['focus'] == label:
                        break
                    self.send('turn', 1, control='ROTARY SELECTOR', action='turn', detail='+1 detent')
                    self.wait(160)
                else:
                    raise RuntimeError(f'encoder cannot focus {label}')
                self.send('push', control='ROTARY SELECTOR', action='push', detail=label)
                self.wait(120)
                self.send('wait', control='ROTARY SELECTOR', action='release', detail='physical release; push handler already completed')
                self.wait(380)
            else:
                w = matches[step.get('match', 0)]
                self.tap(w['x'], w['y'], label)
        elif 'touch' in step:
            self.tap(*step['touch'])
        elif 'turn' in step:
            for _ in range(abs(step['turn'])):
                self.send('turn', 1 if step['turn'] > 0 else -1, control='ROTARY SELECTOR', action='turn', detail=f'{1 if step["turn"] > 0 else -1:+d} detent')
                self.wait(200)
        elif 'push' in step:
            self.send('push', control='ROTARY SELECTOR', action='push')
            self.wait(120)
            self.send('wait', control='ROTARY SELECTOR', action='release', detail='physical release; push handler already completed')
            self.wait(380)
        elif 'back' in step:
            self.send('back', control='BACK', action='press')
            self.wait(500)
        elif 'key' in step:
            keys = {'BROWSE': 2, 'TAG LIST': 3, 'PLAYLIST': 4, 'SEARCH': 5, 'MENU': 6,
                    'MENU hold': 7, 'SHORTCUT': 8, 'INFO': 9, 'SOURCE': 10, 'LOAD': 14, 'TRACK FILTER': 17, 'MODS': 22}
            self.send('key', keys[step['key']], control=step['key'].split(' hold')[0], action='hold' if 'hold' in step['key'] else 'press')
            self.wait(500)
        if 'expect_page' in step and self.state['page'] != step['expect_page']:
            raise RuntimeError(f'{self.folder.name}: expected page {step["expect_page"]}, got {self.state}')

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=10)
        if self.proc.returncode:
            raise RuntimeError(f'C renderer exited {self.proc.returncode}')
        self.raw.unlink(missing_ok=True)

    def finish(self):
        self.wait(1000)
        (self.folder / 'timeline.json').write_text(json.dumps(self.timeline, indent=2) + '\n')
        end = self.t
        durations = [(self.frames[i+1]['t_ms'] if i+1 < len(self.frames) else end+12)-f['t_ms'] for i,f in enumerate(self.frames)]
        if any(d <= 0 for d in durations):
            raise RuntimeError('saved frames must have positive hold durations')
        images = [Image.open(self.folder/f['frame']).convert('RGB') for f in self.frames]
        images[0].save(self.folder/'preview.webp', save_all=True, append_images=images[1:], duration=durations, loop=0, lossless=True)
        for im in images:
            im.close()
        (self.folder/'frames.json').write_text(json.dumps({'duration_ms': end+12, 'frames': [dict(f, hold_ms=d) for f,d in zip(self.frames,durations)]}, indent=2)+'\n')
        return len(self.frames)


def inputs():
    # Include transitive local headers as well as the renderer and handler closures. No mtimes or absolute paths.
    paths = list(MODS.rglob('*.h')) + [MODS/side/p for side,names in [('shell',SHELL_SOURCES),('ui',UI_SOURCES)] for p in names]
    paths += [ROOT/'manual/captures/classic-play.png', MODS/'shell/shell_sources.py', MODS/'ui/ui_sources.py', MODS/'shell_runtime.h', MODS/'shell/nav_capture.c', Path(__file__), *sorted((ROOT/'manual/nav').glob('*.json')), *sorted((ROOT/'manual/chapters').glob('*.md'))]
    return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(set(p.resolve() for p in paths))}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zig', required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--flow')
    p.add_argument('--check', action='store_true', help='verify retained source and output hashes without rendering')
    a = p.parse_args()
    if a.check:
        manifest = json.loads((a.out/'manifest.json').read_text())
        expected = {p.stem for p in (ROOT/'manual/nav').glob('*.json') if not a.flow or p.stem == a.flow}
        if set(manifest['flows']) != expected:
            raise SystemExit('INCOMPLETE: manifest flow set does not match requested scripts')
        if manifest['source_sha256'] != inputs():
            raise SystemExit('STALE: navigation sources, scripts or chapters changed')
        for name,digest in manifest['outputs_sha256'].items():
            if hashlib.sha256((a.out/name).read_bytes()).hexdigest() != digest:
                raise SystemExit(f'CORRUPT: {name}')
        print('navigation captures current; output hashes match')
        return
    a.out.mkdir(parents=True, exist_ok=True)
    scripts = sorted((ROOT/'manual/nav').glob('*.json'))
    if a.flow:
        scripts = [s for s in scripts if s.stem == a.flow]
    if not scripts:
        raise SystemExit('no matching flows')
    counts = {}
    captured_inputs = inputs()
    with tempfile.TemporaryDirectory(prefix='xz-nav-') as temp:
        exe = Path(temp)/'nav.exe'
        build(a.zig, exe)
        for script in scripts:
            flow = json.loads(script.read_text())
            folder = a.out/script.stem
            if folder.exists() and any(folder.iterdir()):
                raise SystemExit(f'output flow already exists: {folder}; choose a fresh output directory')
            c = Capture(exe, folder)
            try:
                steps = list(flow['steps'])
                if steps and 'fixture' in steps[0]:
                    c.step(steps.pop(0))
                else:
                    c.send('wait', detail='offline fixture snapshot; relative clock 0, native clock starts at 100000ms')
                c.wait(500)
                for step in steps:
                    c.step(step)
                for host,kind in flow.get('requires_effects', []):
                    if not any(e['host'] == host and e['kind'] == kind for event in c.timeline for e in event['effects']):
                        raise RuntimeError(f'{script.stem}: required {host} action {kind} never emitted')
                counts[script.stem] = c.finish()
                print(script.stem, counts[script.stem], flush=True)
            finally:
                c.close()
    sheet = Image.new('RGB', (1000, ((len(counts)+2)//3)*200), '#161a20')
    draw = ImageDraw.Draw(sheet)
    for i,name in enumerate(counts):
        frames = json.loads((a.out/name/'frames.json').read_text())['frames']
        meaningful = [f for f in frames if f['page'] != 'PLAY'] if name.startswith('mods-') else frames
        chosen = (meaningful or frames)[-1]['frame']
        with Image.open(a.out/name/chosen) as im:
            sheet.paste(im.resize((320,192)), ((i%3)*333, (i//3)*200))
        draw.rectangle(((i%3)*333,(i//3)*200,(i%3)*333+320,(i//3)*200+18),fill='black')
        draw.text(((i%3)*333+3,(i//3)*200+2),name,fill='white')
    sheet.save(a.out/'contact-sheet.png')
    if inputs() != captured_inputs:
        raise RuntimeError('sources changed during capture; discard this batch and rerun')
    manifest = {'schema': 1, 'toolchain': {'zig': subprocess.check_output([str(Path(a.zig).resolve()), 'version'], text=True).strip(), 'python': sys.version.split()[0], 'pillow': PIL.__version__}, 'device_access': False, 'installed': False, 'timing': 'script clock; complete real C redraw after input and each 12ms (shell_runtime.h live-frame cadence) during holds; identical adjacent pixels deduplicated',
                'source_sha256': captured_inputs, 'flows': counts,
                'outputs_sha256': {f.relative_to(a.out).as_posix(): hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(a.out.rglob('*')) if f.is_file() and f.name != 'manifest.json'}}
    (a.out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')


if __name__ == '__main__':
    main()
