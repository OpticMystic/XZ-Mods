"""Capture real C interface handlers against deterministic, offline fixture snapshots."""
import fnmatch
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


def linux(path):
    path = str(Path(path).resolve()).replace('\\', '/')
    return '/mnt/' + path[0].lower() + path[2:]


def build(zig, exe):
    sources = {(MODS / side / p).resolve() for side, names in [('shell', SHELL_SOURCES), ('ui', UI_SOURCES)] for p in names}
    sources.discard((MODS/'shell/shell_browse.c').resolve())
    sources.add(MODS/'shell/nav_browse.c')
    sources.update(MODS / p for p in ['shell/nav_capture.c', 'shell/nav_runtime.c', 'deck_select.c',
        'ui/native_touch.c', 'ui/stem_pads.c', 'ui/beat_jump.c', 'ui/wave_viewport.c', 'settings.c', 'usb_profile.c',
        'shell/nav_clock.c', 'doom/wad_catalog.c', 'ota/native_update.c', 'ui/extra_detail.c', 'ui/extra_lane.c',
        'ui/layered_wave.c', 'key/control.c', 'key/browser.c', 'key/catalog.c', 'key/traffic.c',
        'arcade/arcade.c', 'arcade/render.c', 'arcade/input.c', 'arcade/clock.c', 'arcade/native_clock.c',
        'audio/vendor/sha256/sha256.c', 'pc_control/stems.c', 'kart/xz_kart_input.c', 'kart/xz_kart_runtime.c'])
    subprocess.run(['wsl', '-d', 'Ubuntu', '--', 'gcc', '-O1', '-std=c11', '-Wall', '-Wextra', '-Werror',
        '-Wno-misleading-indentation', '-Wno-format-truncation', *[linux(p) for p in sorted(sources)], '-pthread', '-ldl', '-lm', '-o', linux(exe)], check=True)


class Capture:
    def __init__(self, exe, folder):
        self.folder = folder
        folder.mkdir(parents=True, exist_ok=True)
        self.raw = folder / 'screen.ppm'
        import array
        with Image.open(ROOT/'manual/captures/classic-play.png') as bg:
            values = array.array('H', [((r>>3)<<11)|((g>>2)<<5)|(b>>3) for r,g,b in bg.convert('RGB').getdata()])
        self.background = folder/'stock.rgb565'
        self.background.write_bytes(values.tobytes())
        self.proc = subprocess.Popen(["wsl", "-d", "Ubuntu", "--", linux(exe), linux(self.raw), linux(self.background)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
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
                                  frame=self.frames[-1]['frame'], panel=self.state['panel'], page=self.state['page'], effects=self.state.get('effects', []), observations=self.state.get('observations', {})))
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
        elif 'panel_select' in step:
            for _ in range(90):
                matches = [w for w in self.state['widgets'] if w['label'] == step['panel_select']]
                if matches:
                    w = matches[step.get('match',0)]
                    self.tap(w['x'],w['y'],step['panel_select'])
                    break
                self.send('turn',1,control='ROTARY SELECTOR',action='turn',detail='+1 detent')
                self.wait(160)
            else:
                raise RuntimeError('unreachable panel row: '+step['panel_select'])
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
        elif 'digits' in step:
            for char in step['digits']:
                w = next(w for w in self.state['widgets'] if w['label'] == char)
                self.tap(w['x'], w['y'], char)
        elif 'settings' in step:
            controls = [(w['kind'], w['index']) for w in self.state['widgets'] if w.get('setting')]
            if not controls:
                raise RuntimeError('no supported setting controls on page')
            for kind, index in controls:
                w = next(w for w in self.state['widgets'] if w.get('kind') == kind and w.get('index') == index)
                self.tap(w['x'], w['y'], w['label'] or f'setting {kind}:{index}')
        elif 'type' in step:
            for char in step['type']:
                matches = [w for w in self.state['widgets'] if w.get('index') == ord(char) and w.get('keyboard')]
                if not matches and char.isalpha():
                    shift = next(w for w in self.state['widgets'] if w['label'] in ('SHIFT', 'SHIFT ON'))
                    self.tap(shift['x'], shift['y'], 'keyboard SHIFT')
                    matches = [w for w in self.state['widgets'] if w.get('index') == ord(char) and w.get('keyboard')]
                if len(matches) != 1:
                    raise RuntimeError(f'keyboard character {char!r} unavailable')
                self.tap(matches[0]['x'], matches[0]['y'], f'keyboard {char}')
        elif 'shift' in step:
            side = step.get('side', 0)
            self.send('physical', 0 if step['shift'] else 100, side,
                      ('left' if side == 0 else 'right') + ' deck SHIFT', 'press' if step['shift'] else 'release')
            self.wait(120)
        elif 'gesture' in step:
            g = step['gesture']
            side = step.get('side', 0)
            control = g if step['code'] == 30 else ('left' if side == 0 else 'right') + ' deck ' + g
            self.send('physical', step['code'], side, control, 'press', g)
            self.wait(120)
            self.send('physical', step['code'] + 100, side, control, 'release', g)
            self.wait(step.get('dwell', 180))
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
            self.wait(120)
            self.send('wait', control='BACK', action='release')
            self.wait(380)
        elif 'key' in step:
            keys = {'BROWSE': 2, 'TAG LIST': 3, 'BROWSE hold': 20, 'MENU': 6,
                    'MENU hold': 7, 'SHORTCUT': 8, 'INFO': 9, 'SOURCE': 10, 'LOAD': 14, 'TRACK FILTER': 17, 'QUANTIZE': 13}
            if 'hold' in step['key']:
                self.send('wait', control=step['key'].split(' hold')[0], action='press')
                self.wait(1000)
            self.send('key', keys[step['key']], control=step['key'].split(' hold')[0], action='hold' if 'hold' in step['key'] else 'press')
            self.wait(120)
            self.send('wait', control=step['key'].split(' hold')[0], action='release', detail='activation handler completed')
            self.wait(380)
        if 'expect_page' in step and self.state['page'] != step['expect_page']:
            raise RuntimeError(f'{self.folder.name}: expected page {step["expect_page"]}, got {self.state}')

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=10)
        if self.proc.returncode:
            raise RuntimeError(f'C renderer exited {self.proc.returncode}')
        self.raw.unlink(missing_ok=True)
        self.background.unlink(missing_ok=True)

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


def validate_observations(flow, timeline):
    for required in flow.get('requires_observations', []):
        if not any(all(t.get('observations', {}).get(k) == v for k, v in required.items()) for t in timeline):
            raise RuntimeError(f'missing observed state: {required}')
    for host, kind, deck in flow.get('requires_deck_actions', []):
        if not any(e['host'] == host and e['kind'] == kind and e['deck'] == deck for t in timeline for e in t['effects']):
            raise RuntimeError(f'missing deck action: {host}:{kind}:{deck}')


def inputs():
    # Include transitive local headers as well as the renderer and handler closures. No mtimes or absolute paths.
    paths = list(MODS.rglob('*.h')) + [MODS/side/p for side,names in [('shell',SHELL_SOURCES),('ui',UI_SOURCES)] for p in names]
    paths += [ROOT/'manual/captures/classic-play.png', MODS/'shell/shell_sources.py', MODS/'ui/ui_sources.py', MODS/'shell_runtime.h', ROOT/'tools/verify_nav_device_objects.py', ROOT/'tools/nav_sections.py', ROOT/'manual/nav/SECTIONS.json', MODS/'shell/nav_capture.c', MODS/'shell/nav_runtime.c', MODS/'ui_runtime.c', *MODS.rglob('*.c'), MODS/'ui/stem_pads.c', MODS/'ui/beat_jump.c', MODS/'deck_select.c', Path(__file__), *sorted(flow_scripts()), *sorted((ROOT/'manual/chapters').glob('*.md'))]
    return {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(set(p.resolve() for p in paths))}


def flow_scripts():
    return sorted(p for p in (ROOT/'manual/nav').glob('*.json') if p.name != 'SECTIONS.json')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zig', required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--flow')
    p.add_argument('--exe', type=Path, help='development only: reuse a locally built host binary')
    p.add_argument('--check', action='store_true', help='verify retained source and output hashes without rendering')
    a = p.parse_args()
    from nav_sections import check as check_sections
    check_sections()
    for script in flow_scripts():
        for step in json.loads(script.read_text())['steps']:
            if step.get('key') in ('PLAYLIST', 'SEARCH', 'MODS'):
                raise SystemExit(f'nonphysical entry key in {script.name}: {step["key"]}')
    if a.check:
        manifest = json.loads((a.out/'manifest.json').read_text())
        expected = {p.stem for p in flow_scripts() if not a.flow or fnmatch.fnmatchcase(p.stem, a.flow)}
        if manifest.get('reused_binary'):
            raise SystemExit('development binary reuse is not acceptable capture evidence')
        if set(manifest['flows']) != expected:
            raise SystemExit('INCOMPLETE: manifest flow set does not match requested scripts')
        if manifest['source_sha256'] != inputs():
            raise SystemExit('STALE: navigation sources, scripts or chapters changed')
        for name,digest in manifest['outputs_sha256'].items():
            if hashlib.sha256((a.out/name).read_bytes()).hexdigest() != digest:
                raise SystemExit(f'CORRUPT: {name}')
        for name in expected:
            required = [f'{name}/timeline.json', f'{name}/frames.json', f'{name}/preview.webp']
            if any(path not in manifest['outputs_sha256'] for path in required):
                raise SystemExit(f'missing output hash: {name}')
            flow = json.loads((ROOT/'manual/nav'/f'{name}.json').read_text())
            timeline = json.loads((a.out/name/'timeline.json').read_text())
            validate_observations(flow, timeline)
            frames = json.loads((a.out/name/'frames.json').read_text())
            if not timeline or not frames['frames'] or any(b['t_ms'] < x['t_ms'] for x,b in zip(timeline,timeline[1:])):
                raise SystemExit(f'invalid timeline: {name}')
            if any(f['hold_ms'] <= 0 or not (a.out/name/f['frame']).is_file() for f in frames['frames']):
                raise SystemExit(f'invalid frames: {name}')
            if sum(f['hold_ms'] for f in frames['frames']) != frames['duration_ms']:
                raise SystemExit(f'invalid duration: {name}')
            for host,kind,index in flow.get('requires_actions', []):
                if not any(e['host']==host and e['kind']==kind and e['index']==index for t in timeline for e in t['effects']):
                    raise SystemExit(f'missing action: {name}: {host}:{kind}:{index}')
            for host,kind in flow.get('requires_effects', []):
                if not any(e['host']==host and e['kind']==kind for t in timeline for e in t['effects']):
                    raise SystemExit(f'missing handler effect: {name}: {host}:{kind}')
        print('navigation captures current; output hashes, physical entries, effects and timing match')
        return
    a.out.mkdir(parents=True, exist_ok=True)
    scripts = sorted(flow_scripts())
    if a.flow:
        scripts = [s for s in scripts if fnmatch.fnmatchcase(s.stem, a.flow)]
    if not scripts:
        raise SystemExit('no matching flows')
    counts = {}
    captured_inputs = inputs()
    with tempfile.TemporaryDirectory(prefix='xz-nav-') as temp:
        exe = a.exe or Path(temp)/'nav.exe'
        if not a.exe:
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
                    if not any(e['host']==host and e['kind']==kind for t in c.timeline for e in t['effects']):
                        raise RuntimeError(f'{script.stem}: required {host} action {kind} never emitted')
                for host,kind,index in flow.get('requires_actions', []):
                    if not any(e['host']==host and e['kind']==kind and e['index']==index for t in c.timeline for e in t['effects']):
                        raise RuntimeError(f'{script.stem}: missing action {host}:{kind}:{index}')
                validate_observations(flow, c.timeline)
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
    manifest = {'schema': 2, 'reused_binary': bool(a.exe), 'toolchain': {'host': subprocess.check_output(['wsl','-d','Ubuntu','--','gcc','-dumpfullversion'],text=True).strip(), 'zig': subprocess.check_output([str(Path(a.zig).resolve()), 'version'], text=True).strip(), 'python': sys.version.split()[0], 'pillow': PIL.__version__}, 'device_access': False, 'installed': False, 'timing': 'script clock; complete real C redraw after input and each 12ms (shell_runtime.h live-frame cadence) during holds; identical adjacent pixels deduplicated',
                'source_sha256': captured_inputs, 'flows': counts,
                'outputs_sha256': {f.relative_to(a.out).as_posix(): hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(a.out.rglob('*')) if f.is_file() and f.name != 'manifest.json'}}
    (a.out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')


if __name__ == '__main__':
    main()
