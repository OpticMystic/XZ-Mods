"""Copy the manual's explainer clips, rendered from the true-scale Blender model of the XDJ-XZ, into the app.

  python tools/import_manual_media.py <render-out> [--out <ui-dir>]
  python tools/import_manual_media.py --none                       # no renders yet: built-in clips only

<render-out> holds manifest.json and the clips it names:

  { "<kind>": { "w": 1280, "h": 480, "scenes": [ { "video": "<kind>/scene-1.mp4", "poster": "<kind>/scene-1.webp",
                                                   "seconds": 6.5, "loop": false, "screen": ... }, ... ] },
    "hero": { "w": 1280, "h": 480, "video": "hero/hero.mp4", "poster": "hero/hero.webp", "seconds": 12, "loop": true } }

Each <kind> is an explainer in the chapters (tools/build_manual.py EXPLAINERS) and has exactly one clip per scene of
that explainer: per line of its fence, or per step of the numbered list after a `steps` explainer. "hero" is the
start page picture; it may also be shaped like an explainer with one scene. "screen" is carried through as given.

The script refuses a manifest whose scene counts differ from the chapters, a path outside <render-out>, a clip that
is not an H.264 MP4, a poster that is not WebP, or (when ffprobe is on PATH) a clip whose size or length differs from
the manifest. It writes <ui-dir>/assets/manual/<kind>/scene-<n>.mp4 and .webp, and <ui-dir>/assets/manual/media.js,
which the Manual page loads. Kinds the manifest leaves out stay hidden in the app until they are rendered; the chapter
text around them carries every fact. Without a hero clip the start page shows the loopback-cable render.
Re-run it whenever the renders change; it replaces everything under assets/manual/.
"""
import argparse
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_manual  # noqa: E402

APP = build_manual.APP
UI = APP / 'ui'
KIND = re.compile(r'^[a-z0-9]+(-[a-z0-9]+)*$')
# Renders that already ship with the app: the four-deck guide's cable clip (tools/build_cable_media.py).
CABLE = {'w': 1280, 'h': 400, 'scenes': [{'video': 'assets/wizard/loopback-cable.mp4', 'poster': 'assets/wizard/loopback-cable-poster.webp',
                                          'seconds': 13, 'loop': True}]}
MAX_SECONDS, MAX_BYTES = 60, 12 * 1048576


def fail(message):
    raise SystemExit('import_manual_media: ' + message)


def inside(root, relative, what):
    if not isinstance(relative, str) or not relative or relative.startswith(('/', '\\')) or ':' in relative or '..' in Path(relative).parts:
        fail(f'{what}: {relative!r} must be a relative path inside the render folder')
    path = (root / relative).resolve()
    if root not in path.parents or not path.is_file(): fail(f'{what}: {relative} is missing')
    if path.stat().st_size > MAX_BYTES: fail(f'{what}: {relative} is {path.stat().st_size / 1048576:.1f} MiB; keep a clip under {MAX_BYTES // 1048576} MiB')
    return path


def check_video(path, what, w, h, seconds):
    head = path.read_bytes()[:12]
    if path.suffix.lower() != '.mp4' or head[4:8] != b'ftyp': fail(f'{what}: {path.name} is not an MP4')
    probe = shutil.which('ffprobe')
    if not probe: return 'not probed (no ffprobe on PATH)'
    out = subprocess.run([probe, '-v', 'error', '-show_streams', '-show_format', '-of', 'json', str(path)], capture_output=True, text=True)
    if out.returncode: fail(f'{what}: ffprobe cannot read {path.name}: {out.stderr.strip()}')
    info = json.loads(out.stdout)
    video = [s for s in info['streams'] if s.get('codec_type') == 'video']
    if len(video) != 1: fail(f'{what}: {path.name} has {len(video)} video streams')
    v = video[0]
    # H.264 in yuv420p is what both WebView2 and the macOS web view play.
    if v.get('codec_name') != 'h264' or v.get('pix_fmt') != 'yuv420p': fail(f'{what}: {path.name} is {v.get("codec_name")} {v.get("pix_fmt")}; encode H.264 yuv420p')
    if (int(v['width']), int(v['height'])) != (w, h): fail(f'{what}: {path.name} is {v["width"]} x {v["height"]}, the manifest says {w} x {h}')
    length = float(info['format'].get('duration') or v.get('duration') or 0)
    if abs(length - seconds) > 0.25: fail(f'{what}: {path.name} runs {length:.2f} s, the manifest says {seconds} s')
    return 'audio stream ignored (clips play muted)' if any(s.get('codec_type') == 'audio' for s in info['streams']) else ''


def check_poster(path, what):
    head = path.read_bytes()[:12]
    if path.suffix.lower() != '.webp' or head[:4] != b'RIFF' or head[8:12] != b'WEBP': fail(f'{what}: {path.name} is not a WebP poster')


def scenes_of(kind, entry, root, want):
    if not isinstance(entry, dict): fail(f'{kind}: expected an object')
    w, h = entry.get('w'), entry.get('h')
    if not (isinstance(w, int) and isinstance(h, int) and 64 <= w <= 3840 and 64 <= h <= 2160): fail(f'{kind}: w and h must be the clip size in pixels')
    scenes = entry.get('scenes')
    if not isinstance(scenes, list) or not scenes: fail(f'{kind}: needs a scenes list')
    if want is not None and len(scenes) != want: fail(f'{kind}: {len(scenes)} clips, but the chapter has {want} scenes')
    out, notes = [], []
    for n, scene in enumerate(scenes, 1):
        what = f'{kind} scene {n}'
        seconds = scene.get('seconds')
        if not isinstance(seconds, (int, float)) or isinstance(seconds, bool) or not math.isfinite(seconds) or not 0 < seconds <= MAX_SECONDS:
            fail(f'{what}: seconds must be the clip length, at most {MAX_SECONDS}')
        if not isinstance(scene.get('loop', False), bool): fail(f'{what}: loop must be true or false')
        video, poster = inside(root, scene.get('video'), what), inside(root, scene.get('poster'), what)
        note = check_video(video, what, w, h, seconds)
        check_poster(poster, what)
        if note: notes.append(f'{what}: {note}')
        item = {'seconds': round(float(seconds), 3), 'loop': scene.get('loop', False), 'src': video, 'poster_src': poster}
        if 'screen' in scene: item['screen'] = scene['screen']
        out.append(item)
    return {'w': w, 'h': h, 'scenes': out}, notes


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('source', nargs='?', type=Path, help='the render output folder with manifest.json')
    p.add_argument('--none', action='store_true', help='no renders yet: write media.js with the built-in clips only')
    p.add_argument('--out', type=Path, default=UI, help='the ui folder to write into (default ui/); a scratch folder stages a test copy')
    a = p.parse_args()
    if bool(a.source) == a.none: p.error('give the render folder, or --none')
    chapters = []
    for entry in build_manual.load_json(build_manual.MANUAL / 'manual.json')['chapters']:
        path = build_manual.MANUAL / 'chapters' / (entry['slug'] + '.md')
        if path.is_file(): chapters.append({'slug': entry['slug'], 'body': build_manual.parse_chapter(path)[1]})
    want = {kind: count for kind, (_, count) in build_manual.explainer_scenes(chapters).items()}
    manifest, root = {}, None
    if a.source:
        root = a.source.resolve()
        if not (root / 'manifest.json').is_file(): fail(f'{root} has no manifest.json')
        manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
        if not isinstance(manifest, dict): fail('manifest.json must be an object')
    unknown = sorted(set(manifest) - set(build_manual.EXPLAINERS) - {'hero'})
    if unknown: fail(f'manifest.json names {", ".join(unknown)}, which are not explainers (known: {", ".join(sorted(build_manual.EXPLAINERS))})')
    imported, notes = {}, []
    for kind in sorted(set(manifest) - {'hero'}):
        if not KIND.match(kind): fail(f'{kind}: not a valid explainer name')
        if kind not in want: fail(f'{kind}: no chapter shows this explainer')
        imported[kind], more = scenes_of(kind, manifest[kind], root, want[kind])
        notes += more
    hero = None
    if 'hero' in manifest:
        entry = manifest['hero']
        if isinstance(entry, dict) and 'scenes' not in entry:
            entry = {'w': entry.get('w'), 'h': entry.get('h'), 'scenes': [{k: v for k, v in entry.items() if k not in ('w', 'h')}]}
        hero, more = scenes_of('hero', entry, root, 1)
        notes += more

    out = a.out.resolve()
    target = out / 'assets/manual'
    if target.exists():
        if target.name != 'manual' or target.parent.name != 'assets': fail(f'refusing to clear {target}')
        shutil.rmtree(target)
    target.mkdir(parents=True)
    explainers = {}
    for kind, entry in sorted(imported.items()):
        (target / kind).mkdir()
        scenes = []
        for n, scene in enumerate(entry['scenes'], 1):
            shutil.copyfile(scene['src'], target / kind / f'scene-{n}.mp4')
            shutil.copyfile(scene['poster_src'], target / kind / f'scene-{n}.webp')
            scenes.append({'video': f'assets/manual/{kind}/scene-{n}.mp4', 'poster': f'assets/manual/{kind}/scene-{n}.webp',
                           **{k: v for k, v in scene.items() if k in ('seconds', 'loop', 'screen')}})
        explainers[kind] = {'w': entry['w'], 'h': entry['h'], 'scenes': scenes}
    if 'loopback-cable' not in explainers and 'loopback-cable' in want:
        if len(CABLE['scenes']) != want['loopback-cable']: fail('loopback-cable: the chapter no longer has one scene for the cable clip')
        explainers['loopback-cable'] = CABLE
    if hero:
        (target / 'hero').mkdir()
        scene = hero['scenes'][0]
        shutil.copyfile(scene['src'], target / 'hero/hero.mp4')
        shutil.copyfile(scene['poster_src'], target / 'hero/hero.webp')
        hero_entry = {'w': hero['w'], 'h': hero['h'], 'video': 'assets/manual/hero/hero.mp4', 'poster': 'assets/manual/hero/hero.webp',
                      'seconds': scene['seconds'], 'loop': scene['loop'], 'interim': False}
    else:  # the cable clip is a real render of the model too; it stands in until the hero is rendered
        hero_entry = {'w': CABLE['w'], 'h': CABLE['h'], **{k: v for k, v in CABLE['scenes'][0].items()}, 'interim': True}
    media = {'format': 'xz-manual-media/1', 'explainers': explainers, 'hero': hero_entry}
    for path in [s[k] for e in explainers.values() for s in e['scenes'] for k in ('video', 'poster')] + [hero_entry['video'], hero_entry['poster']]:
        if not re.fullmatch(r'assets/(manual|wizard)/[a-z0-9-]+(/[a-z0-9-]+)?\.(mp4|webp)', path): fail(f'{path} is not a path the app can load')
        if not (out / path).is_file() and not (UI / path).is_file(): fail(f'{path} is missing')
    problems = build_manual.media_problems(chapters, media, (out, UI))
    if problems: fail('; '.join(problems))
    text = json.dumps(media, ensure_ascii=False, separators=(',', ':'), sort_keys=True).replace('</', '<\\/')
    (target / 'media.js').write_bytes(('// Generated by tools/import_manual_media.py from the Blender renders. Do not edit by hand.\n'
                                       + build_manual.MEDIA_PREFIX + text + ';\n').encode('utf-8'))
    for kind in sorted(want):
        source = 'built in' if kind == 'loopback-cable' and kind not in imported else f'{len(explainers[kind]["scenes"])} clips' if kind in explainers else 'NOT RENDERED: hidden in the app'
        print(f'{kind:16} {source}')
    print(f'{"hero":16} {"interim: the loopback-cable render" if hero_entry["interim"] else "rendered"}')
    for note in notes: print('note: ' + note)
    print(target / 'media.js')


if __name__ == '__main__':
    main()
