// Animated explainers and small pictures for the Manual page (ui/manual.js). Everything is SVG built here and
// moved by classes in ui/manual.css, so the CSP needs no inline style. The words come from the chapters: a
// `steps` explainer follows the numbered list after it, a `scenes` explainer reads its scene lines (title,
// optional data, caption) from the chapter. Facts drawn here were checked against the firmware source:
// ports (ui/loopback_port.h), the pulse direction USB 1 to the rear socket (ui/route_card.c), mixer order
// 3 1 2 4, beat jump defaults (ui/beat_jump.h), page 2 pad colours (ui/native_led.h), 3-band colours
// (ui/theme_flagship.h, ui/stem_colors.h) and the RAM card (ui/ram_notice.h).
'use strict';
(() => {
  const NS = 'http://www.w3.org/2000/svg';
  const reducedQuery = matchMedia('(prefers-reduced-motion: reduce)');
  const reduced = () => reducedQuery.matches;
  function s(tag, attrs, ...children) {
    const item = document.createElementNS(NS, tag);
    for (const [key, value] of Object.entries(attrs || {})) if (value !== undefined && value !== null) item.setAttribute(key, value);
    for (const child of children.flat()) if (child !== null && child !== undefined && child !== '') item.append(child);
    return item;
  }
  const t = (x, y, text, cls, anchor) => s('text', { x, y, class: cls || 'a-t', 'text-anchor': anchor || 'middle' }, text);
  const svg = (w, h, cls) => s('svg', { viewBox: `0 0 ${w} ${h}`, class: cls, 'aria-hidden': 'true', focusable: 'false' });
  // Seeded noise, so every picture is the same each time.
  const rng = seed => () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
  // A mirrored waveform outline around y0: amp(x) gives the half height.
  function wavePath(x0, y0, w, step, amp) {
    let top = '', bottom = '';
    for (let x = 0; x <= w; x += step) { const a = Math.max(0.6, amp(x)); top += (x ? 'L' : 'M') + (x0 + x).toFixed(1) + ' ' + (y0 - a).toFixed(1); bottom = 'L' + (x0 + x).toFixed(1) + ' ' + (y0 + a).toFixed(1) + bottom; }
    return top + bottom + 'Z';
  }
  const laneWave = (x0, y0, w, h, seed) => { const r = rng(seed); return wavePath(x0, y0, w, 3, x => h * (0.25 + 0.55 * Math.abs(Math.sin(x / 9 + seed)) * (0.6 + 0.4 * r())) * (x % 24 < 4 ? 1.15 : 0.85)); };

  // ---------------------------------------------------------------- the XDJ-XZ, top down (four-deck explainer and the start page)
  // Layout follows manual/hardware/xdj-xz.svg: screen top centre, USB 1 and USB 2 top right, a deck each side with
  // SHIFT, CUE and PLAY left of its jog, the mixer under the screen in channel order 3 1 2 4 with the CH3 and
  // CH4 INPUT switches at its top corners. The rear panel is drawn as a strip above the back edge.
  function panel(root, hero) {
    root.append(s('rect', { class: 'a-bg', x: 0, y: 0, width: 960, height: 470 }));
    root.append(s('rect', { class: 'a-part', x: 150, y: 14, width: 660, height: 44, rx: 6 }), t(166, 41, 'REAR PANEL', 'a-t-s', 'start'),
      s('path', { class: 'a-part', d: 'M548 52v-16l4-4h16l4 4v16z' }), t(584, 47, 'REAR USB', 'a-t-s', 'start'));
    root.append(s('rect', { class: 'a-body', x: 30, y: 70, width: 900, height: 390, rx: 24 }));
    const deck = (dx, side) => {
      const g = s('g', { class: 'ax-deck-' + side });
      g.append(s('rect', { class: 'a-part ax-shift ax-shift-' + side, x: dx + 46, y: 246, width: 56, height: 24, rx: 5 }), t(dx + 74, 262, 'SHIFT', 'a-t-s'));
      g.append(s('circle', { class: 'a-part', cx: dx + 74, cy: 334, r: 19 }), t(dx + 74, 338, 'CUE', 'a-t-s'));
      g.append(s('circle', { class: 'a-part', cx: dx + 74, cy: 406, r: 19 }), s('path', { d: `M${dx + 68} 398v16l13-8z`, fill: '#9aa3a0' }),
        s('circle', { class: 'ax-led ax-led-' + side, cx: dx + 74, cy: 377, r: 4 }));
      const cx = dx + 220, cy = 292;
      g.append(s('g', { class: 'ax-jog ax-jog-' + side }, s('circle', { class: 'a-part', cx, cy, r: 98 }), s('circle', { class: 'a-faint', cx, cy, r: 72 }),
        s('circle', { class: 'a-dark', cx, cy, r: 24 }), s('line', { class: 'ax-jog-mark', x1: cx, y1: cy - 30, x2: cx, y2: cy - 92 })));
      return g;
    };
    root.append(deck(0, 'l'), deck(600, 'r'));
    root.append(t(220, 440, 'DECK 1', 'a-t-b a-show ax-deck1'), t(220, 440, 'DECK 3', 'a-t-b a-show ax-deck3'), t(820, 440, 'DECK 2', 'a-t-b'));
    // Screen.
    root.append(s('rect', { class: 'a-part', x: 338, y: 80, width: 284, height: 160, rx: 8 }), s('rect', { class: 'a-dark', x: 346, y: 88, width: 268, height: 144, rx: 3 }));
    // USB.
    root.append(s('rect', { class: 'a-part', x: 726, y: 80, width: 188, height: 66, rx: 6 }));
    for (const [x, label] of [[790, 'USB 1'], [862, 'USB 2']]) root.append(t(x, 140, label, 'a-t-s'), s('rect', { class: 'a-dark', x: x - 23, y: 108, width: 46, height: 16, rx: 3, stroke: '#4c5452' }));
    // Mixer.
    root.append(s('rect', { class: 'a-part', x: 338, y: 250, width: 284, height: 200, rx: 8 }));
    for (const [x, ch] of [[392, '3'], [450, '1'], [508, '2'], [566, '4']]) {
      root.append(t(x, 326, ch, 'a-t-b ax-ch-num' + (ch === '3' || ch === '4' ? ' ax-ch-34' : '')));
      root.append(s('rect', { class: 'a-dark', x: x - 3, y: 336, width: 6, height: 96, rx: 2 }), s('rect', { class: 'a-part', x: x - 12, y: 372, width: 24, height: 11, rx: 2 }));
    }
    for (const [x, label] of [[392, 'CH3 INPUT'], [566, 'CH4 INPUT']]) {
      root.append(t(x, 268, label, 'a-t-s'), s('rect', { class: 'a-dark', x: x - 20, y: 275, width: 40, height: 12, rx: 6, stroke: '#4c5452' }),
        s('circle', { class: 'ax-knob', cx: x - 13, cy: 281, r: 6, fill: '#8a9290' }), t(x + 13, 302, 'USB', 'a-t-s ax-usb-word'));
    }
    if (hero) return;
  }
  const CABLE = 'M560 14C560 -2 640 4 700 4C770 4 790 30 790 84';

  // ---------------------------------------------------------------- explainers
  const ART = {};

  ART['four-deck-setup'] = {
    count: 8, ms: { 7: 6200, 8: 5200, 6: 4800 },
    build() {
      const root = svg(960, 470, 'ax ax-four');
      panel(root);
      // Screen: two decks, the DECK VIEW picker, four decks with the card.
      // Deck badges as on the player: decks 1 and 2 a white number on grey, decks 3 and 4 black on white.
      const lane = (y, h, n, light, seed) => [s('rect', { x: 350, y, width: 18, height: Math.min(14, h - 2), rx: 2, fill: light ? '#f2f2f2' : '#5a5f62', class: n === 3 ? 'ax-tag3' : null }),
        t(359, y + 11, String(n), light ? 'a-t-s ax-dark-t' : 'a-t-s ax-light-t'), s('path', { class: 'a-wave', d: laneWave(374, y + h / 2, 236, h / 2 - 3, seed) })];
      const two = s('g', { class: 'ax-scr-two a-show' }, lane(110, 58, 1, false, 1), lane(172, 58, 2, false, 2));
      const thumb = (x, lanes, label, hit) => s('g', null, s('rect', { class: 'a-part' + (hit ? ' ax-pick-hit' : ''), x, y: 122, width: 60, height: 50, rx: 4, 'stroke-width': hit ? 2 : 1.3 }),
        lanes.map((n, i) => s('rect', { x: x + 6, y: 128 + i * (40 / lanes.length), width: 48, height: 40 / lanes.length - 3, rx: 1, fill: n > 2 ? '#d8d8d8' : '#4f5a60' })), t(x + 30, 188, label, 'a-t-s'));
      const pick = s('g', { class: 'ax-scr-pick a-show' }, s('rect', { x: 372, y: 110, width: 216, height: 88, rx: 6, fill: '#0e1214', stroke: '#59605e' }),
        thumb(382, [1, 2], '2 DECKS'), thumb(452, [1, 2, 3, 4], '1 2 3 4', true), thumb(522, [3, 1, 2, 4], '3 1 2 4'),
        s('circle', { class: 'ax-tap', cx: 482, cy: 147, r: 16, fill: '#f3e90055', stroke: '#f3e900', 'stroke-width': 2 }));
      const four = s('g', { class: 'ax-scr-four a-show' }, lane(110, 29, 1, false, 3), lane(141, 29, 2, false, 4),
        s('g', { class: 'ax-lanes34 a-show' }, lane(172, 29, 3, true, 5), lane(203, 29, 4, true, 6)),
        s('g', { class: 'ax-cardbox a-show' }, s('rect', { x: 350, y: 172, width: 260, height: 58, rx: 4, fill: '#0c0c0c', stroke: '#3c3c3c' }),
          t(480, 206, 'NO CABLE FOUND', 'a-t ax-card ax-card-a'), t(480, 206, 'STARTING DECKS 3 AND 4', 'a-t ax-card ax-card-b'), t(480, 206, 'DECKS 3 AND 4 ARE READY', 'a-t ax-card ax-card-c')));
      // The card's state colours (ui/route_card.c): amber waiting, white starting, green ready.
      four.querySelector('.ax-card-a').style.fill = '#ffa82e';
      four.querySelector('.ax-card-b').style.fill = '#ffffff';
      four.querySelector('.ax-card-c').style.fill = '#28e028';
      root.append(two, pick, four);
      root.append(s('rect', { x: 548, y: 92, width: 62, height: 13, rx: 2, fill: '#101314', stroke: '#59605e' }), t(579, 102, 'DECKS 1 2', 'a-t-s ax-decks-two a-show'), t(579, 102, 'DECKS 1234', 'a-t-s ax-decks-four a-show'));
      // Cable, flow, plugs, stick.
      root.append(s('path', { class: 'ax-cable', d: CABLE, pathLength: 1 }), s('path', { class: 'ax-flow', d: CABLE, pathLength: 1 }));
      root.append(s('g', { class: 'ax-plug-b a-move' }, s('path', { class: 'a-part a-hl', d: 'M548 38v-22h24v22l-3 4h-18z', 'stroke-width': 2 }), s('rect', { x: 554, y: 22, width: 12, height: 10, fill: '#f3e900', opacity: '.5' })));
      root.append(s('g', { class: 'ax-plug-a a-move' }, s('rect', { class: 'a-part a-hl', x: 781, y: 82, width: 18, height: 26, rx: 2, 'stroke-width': 2 }), s('rect', { x: 784, y: 106, width: 12, height: 12, fill: '#c9cfcc' })));
      root.append(s('g', { class: 'ax-stick a-move' }, t(862, 48, 'MUSIC', 'a-t-s'), s('rect', { class: 'a-part', x: 848, y: 54, width: 28, height: 50, rx: 5, 'stroke-width': 1.6, stroke: '#9aa3a0' }),
        s('rect', { x: 854, y: 104, width: 16, height: 16, fill: '#c9cfcc' }), s('circle', { class: 'ax-stick-led', cx: 862, cy: 66, r: 3 })));
      // Where to look: one or two pulsing rings.
      const ring = cls => s('g', { class: 'a-ring ' + cls }, s('circle', { r: 26 }), s('circle', { class: 'a-ring-pulse', r: 26 }));
      root.append(ring('ax-ring-a'), ring('ax-ring-b'));
      return root;
    },
    rings: { 1: [[74, 406], [674, 406]], 2: [[862, 116]], 3: [[560, 38]], 4: [[790, 116]], 5: [[392, 281], [566, 281]], 6: [[579, 98]], 7: [], 8: [[74, 258]] },
    enter(root, i) {
      const at = this.rings[i] || [];
      root.querySelectorAll('.a-ring').forEach((ring, k) => { const p = at[k]; ring.style.opacity = p ? '1' : '0'; if (p) ring.style.transform = `translate(${p[0]}px,${p[1]}px)`; });
    }
  };

  ART['loopback-signal'] = {
    count: 5,
    build(scenes) {
      const root = svg(960, 250, 'ax ax-sig');
      root.append(s('rect', { class: 'a-bg', x: 0, y: 0, width: 960, height: 250 }));
      const d = 'M192 186H232M384 186C428 34 566 34 610 186M762 186H792';
      root.append(s('path', { class: 'ax-path', d }), s('path', { class: 'ax-path-lit', d, pathLength: 1 }), s('path', { class: 'ax-dots', d, pathLength: 1 }));
      const nodes = [[40, 152], [232, 152], [421, 74], [610, 152], [792, 152]];
      nodes.forEach(([x, w], i) => {
        const y = i === 2 ? 60 : 150, h = i === 2 ? 40 : 72;
        const g = s('g', { class: 'ax-node', 'data-n': i + 1 }, s('rect', { class: 'ax-box', x, y, width: w, height: h, rx: 8 }), t(x + w / 2, y + (i === 2 ? 25 : 24), (scenes[i]?.title || '').toUpperCase(), 'a-t'));
        if (i === 0) for (const [k, n] of [[0, '3'], [1, '4']]) g.append(s('rect', { class: 'ax-chip', x: x + 40 + k * 44, y: y + 34, width: 30, height: 26, rx: 4 }), t(x + 55 + k * 44, y + 52, n, 'ax-chip-t'));
        if (i === 1 || i === 3) g.append(s('rect', { class: 'ax-slot', x: x + w / 2 - 20, y: y + 40, width: 40, height: 16, rx: 3 }));
        if (i === 4) for (const [k, n] of [[0, '3'], [1, '4']]) g.append(t(x + 52 + k * 48, y + 46, n, 'a-t-b'), s('rect', { class: 'ax-slot', x: x + 49 + k * 48, y: y + 52, width: 6, height: 14, rx: 2 }));
        root.append(g);
      });
      return root;
    },
    offsets: [1, 0.9, 0.5, 0.08, 0],
    enter(root, i) {
      root.querySelector('.ax-path-lit').style.strokeDashoffset = String(this.offsets[i - 1] ?? 0);
      root.querySelectorAll('.ax-node').forEach(node => { const n = Number(node.dataset.n); node.classList.toggle('is-lit', n <= i); node.classList.toggle('is-now', n === i); });
    }
  };

  ART['sync-master'] = {
    build() {
      const root = svg(960, 262, 'ax ax-sync');
      root.append(s('rect', { class: 'a-bg', x: 0, y: 0, width: 960, height: 262 }));
      [3, 1, 2, 4].forEach((deck, i) => {
        const x = 18 + i * 236, y = 20, w = 216;
        const g = s('g', { class: 'ax-deck', 'data-deck': deck });
        g.append(s('rect', { class: 'ax-tile', x, y, width: w, height: 222, rx: 10 }));
        const light = deck > 2;
        g.append(s('rect', { class: light ? 'ax-badge34' : 'ax-badge12', x: x + 14, y: y + 14, width: 26, height: 22, rx: 4 }), t(x + 27, y + 30, String(deck), light ? 'ax-badge34-t' : 'ax-badge12-t'));
        g.append(t(x + 50, y + 30, 'DECK ' + deck, 'a-t', 'start'));
        g.append(s('rect', { class: 'ax-chipbox', x: x + w - 98, y: y + 14, width: 84, height: 22, rx: 4 }), t(x + w - 56, y + 29, '', 'ax-chip-t'));
        g.append(t(x + 14, y + 90, '—', 'ax-bpm', 'start'), t(x + w - 14, y + 90, 'BPM', 'a-t-s', 'end'));
        const beats = s('g', { class: 'ax-beats' });
        for (let k = 0; k < 4; k++) beats.append(s('rect', { class: 'ax-beat', x: x + 14 + k * 48, y: y + 106, width: 42, height: 12, rx: 2 }));
        g.append(beats, t(x + 14, y + 140, 'BEATS LOCKED', 'a-t-s ax-lock', 'start'));
        g.append(s('circle', { class: 'ax-btn ax-sync-btn', cx: x + 44, cy: y + 182, r: 21 }), t(x + 44, y + 185, 'SYNC', 'ax-btn-t'),
          s('circle', { class: 'ax-btn ax-master-btn', cx: x + 100, cy: y + 182, r: 21 }), t(x + 100, y + 185, 'MASTER', 'ax-btn-t'));
        root.append(g);
      });
      return root;
    },
    // Data: comma-separated deck=LABEL@BPM entries and the word lock (QUANTIZE locks the beat).
    enter(root, i, scene, box) {
      const decks = {}, lock = /(^|,)\s*lock\s*(,|$)/.test(scene.data);
      for (const part of scene.data.split(',')) { const m = part.trim().match(/^([1-4])=([^@]*)@([\d.]+)$/); if (m) decks[m[1]] = { label: m[2].trim(), bpm: Number(m[3]) }; }
      const master = Object.entries(decks).find(([, d]) => d.label === 'MASTER');
      for (const g of root.querySelectorAll('.ax-deck')) {
        const deck = g.dataset.deck, d = decks[deck];
        g.classList.toggle('ax-dim', !d);
        const label = d?.label || '', kind = label === 'MASTER' ? 'is-master' : /^SYNC/.test(label) ? 'is-sync' : /^BPM/.test(label) ? 'is-bpm' : '';
        const chip = g.querySelector('.ax-chipbox'), tile = g.querySelector('.ax-tile');
        chip.setAttribute('class', 'ax-chipbox ' + kind); g.querySelector('.ax-chip-t').textContent = label;
        tile.setAttribute('class', 'ax-tile ' + (kind === 'is-master' ? 'is-master' : kind ? 'is-sync' : ''));
        g.querySelector('.ax-sync-btn').setAttribute('class', 'ax-btn ax-sync-btn' + (kind === 'is-sync' || kind === 'is-bpm' ? ' is-on' : ''));
        g.querySelector('.ax-master-btn').setAttribute('class', 'ax-btn ax-master-btn' + (kind === 'is-master' ? ' is-master' : ''));
        g.querySelector('.ax-lock').classList.toggle('is-on', lock && kind === 'is-sync');
        const bpm = g.querySelector('.ax-bpm');
        bpm.classList.toggle('is-master', kind === 'is-master');
        const beats = g.querySelector('.ax-beats');
        if (!d) { bpm.textContent = '—'; beats.classList.remove('is-run'); continue; }
        const from = Number(bpm.textContent) || d.bpm;
        tween(box, 650, k => { bpm.textContent = (from + (d.bpm - from) * k).toFixed(2); });
        const bar = 240 / d.bpm, off = (kind && !lock && master && deck !== master[0]) ? -bar * 0.18 : 0;
        beats.style.setProperty('--bar', bar.toFixed(3) + 's');
        [...beats.children].forEach((beat, k) => { beat.style.animationDuration = bar.toFixed(3) + 's'; beat.style.animationDelay = (off + (k / 4 - 1) * bar).toFixed(3) + 's'; });
        beats.classList.remove('is-run'); void beats.getBoundingClientRect(); beats.classList.add('is-run');
      }
    }
  };

  ART['beat-jump'] = {
    PX: 16,
    build() {
      const root = svg(960, 380, 'ax ax-jump page-1');
      root.append(s('rect', { class: 'a-bg', x: 0, y: 0, width: 960, height: 380 }));
      const clip = s('clipPath', { id: 'ax-jump-clip-' + Math.random().toString(36).slice(2) }, s('rect', { x: 40, y: 22, width: 880, height: 116, rx: 8 }));
      root.append(s('defs', null, clip), s('rect', { class: 'a-dark', x: 40, y: 22, width: 880, height: 116, rx: 8, stroke: '#2c3231' }));
      const strip = s('g', { class: 'ax-strip' });
      const r = rng(7), beats = 300, PX = this.PX;
      strip.append(s('path', { class: 'a-wave', d: wavePath(0, 80, beats * PX, 4, x => 34 * (0.3 + 0.7 * Math.exp(-((x % PX) / 6))) * (0.75 + 0.25 * r())) }));
      for (let b = 0; b <= beats; b++) {
        strip.append(s('line', { class: b % 4 ? 'ax-beatln' : 'ax-barln', x1: b * PX, y1: b % 4 ? 40 : 30, x2: b * PX, y2: 130 }));
        if (b % 16 === 0) strip.append(t(b * PX + 3, 40, String(b / 4 + 1), 'ax-bar-t', 'start'));
      }
      strip.append(s('rect', { class: 'ax-loop', x: 0, y: 26, width: 4 * PX, height: 108 }));
      root.append(s('g', { 'clip-path': `url(#${clip.id})` }, strip), s('line', { class: 'ax-head', x1: 480, y1: 22, x2: 480, y2: 138 }), t(480, 76, '', 'ax-pop'));
      root.append(t(480, 164, '', 'ax-pos'));
      root.append(s('rect', { class: 'ax-mode ax-mode-jump', x: 50, y: 214, width: 150, height: 32, rx: 5 }), t(125, 235, 'BEAT JUMP', 'a-t'),
        s('rect', { class: 'ax-mode ax-mode-shift', x: 50, y: 290, width: 150, height: 32, rx: 5 }), t(125, 311, 'SHIFT', 'a-t'));
      const xs = [275, 381, 487, 593];
      for (let p = 0; p < 8; p++) {
        const x = xs[p % 4], y = p < 4 ? 200 : 276;
        root.append(s('rect', { class: 'ax-pad ' + (p < 2 || p >= 6 ? 'red' : 'amber'), 'data-pad': p, x, y, width: 92, height: 62, rx: 7 }),
          t(x + 46, y + 38, '', 'ax-pad-t'), t(x + 8, y + 15, 'ABCDEFGH'[p], 'ax-pad-l', 'start'));
      }
      for (let pair = 0; pair < 4; pair++) {
        const x = xs[(pair % 2) * 2], top = pair < 2, y = top ? 190 : 348;
        root.append(s('path', { class: 'a-ln', d: top ? `M${x + 4} ${y + 6}v-6h190v6` : `M${x + 4} ${y - 6}v6h190v-6` }), t(x + 99, top ? y - 6 : y + 16, '', 'a-t-s ax-pair'));
      }
      root.append(t(720, 236, 'BACK', 'a-t-s', 'start'), t(720, 250, 'first pad', 'a-t-s', 'start'), t(720, 312, 'FORWARD', 'a-t-s', 'start'), t(720, 326, 'second pad', 'a-t-s', 'start'));
      return root;
    },
    // Data: the four sizes in beats (A/B, C/D, E/F, G/H); a second field `shift` (page 2) or `loop`.
    enter(root, i, scene, box) {
      const [sizesText = '1 2 4 8', flag = ''] = scene.data.split('|').map(v => v.trim());
      const sizes = sizesText.split(/\s+/).map(Number), page2 = /shift/.test(flag), loop = /loop/.test(flag);
      root.classList.toggle('page-1', !page2); root.classList.toggle('page-2', page2); root.classList.toggle('loop', loop);
      const beatsWord = n => n % 4 === 0 && n >= 16 ? n + ' BEATS · ' + n / 4 + ' BARS' : n + (n === 1 ? ' BEAT' : ' BEATS');
      const label = () => {
        root.querySelectorAll('.ax-pad').forEach(pad => { const p = Number(pad.dataset.pad), n = sizes[Math.floor((p % 4) / 2) + (p >= 4 ? 2 : 0)]; pad.nextSibling.textContent = p % 2 ? n + ' ▶' : '◀ ' + n; });
        root.querySelectorAll('.ax-pair').forEach((item, k) => { item.textContent = beatsWord(sizes[k]); });
      };
      const jump = root.querySelector('.ax-mode-jump'), shift = root.querySelector('.ax-mode-shift');
      const strip = root.querySelector('.ax-strip'), loopBox = root.querySelector('.ax-loop'), pos = root.querySelector('.ax-pos'), pop = root.querySelector('.ax-pop');
      this.beat = page2 ? 100 : 64;
      const place = instant => {
        strip.classList.toggle('is-instant', !!instant);
        strip.style.transform = `translateX(${480 - this.beat * this.PX}px)`;
        loopBox.style.transform = `translateX(${(this.beat - 1) * this.PX}px)`;
        pos.textContent = 'BAR ' + (Math.floor(this.beat / 4) + 1) + ' · BEAT ' + (this.beat % 4 + 1);
      };
      place(true);
      jump.setAttribute('class', 'ax-mode ax-mode-jump is-on');
      shift.setAttribute('class', 'ax-mode ax-mode-shift');
      label();
      const press = (pad, at) => later(box, at, () => {
        const n = sizes[Math.floor((pad % 4) / 2) + (pad >= 4 ? 2 : 0)], dir = pad % 2 ? 1 : -1;
        const el = root.querySelector(`.ax-pad[data-pad="${pad}"]`);
        el.classList.add('is-hit'); later(box, 260, () => el.classList.remove('is-hit'));
        this.beat += dir * n; place(false);
        pop.textContent = (dir > 0 ? '+' : '−') + beatsWord(n).replace(' · ', ' (') + (n >= 16 && n % 4 === 0 ? ')' : '');
        pop.classList.remove('is-on'); void pop.getBoundingClientRect(); pop.classList.add('is-on');
      });
      if (reduced()) return;
      if (page2) {
        shift.setAttribute('class', 'ax-mode ax-mode-shift is-press');
        later(box, 500, () => jump.setAttribute('class', 'ax-mode ax-mode-jump is-press'));
        later(box, 800, () => { jump.setAttribute('class', 'ax-mode ax-mode-jump is-on'); shift.setAttribute('class', 'ax-mode ax-mode-shift'); });
        press(7, 1500); press(4, 3300);
      } else if (loop) { press(3, 1000); press(2, 2600); }
      else { press(1, 900); press(2, 2400); }
    }
  };

  // MODERN's own 3-band colours (ui/theme_flagship.h) and the 3-BAND COLOURS choices (ui/stem_colors.h).
  const BAND = { STANDARD: null, BLUE: '#2997ff', RED: '#ff3b30', GREEN: '#30d158', AMBER: '#ff9f0a', PURPLE: '#bf5af2', CYAN: '#64d2ff', PINK: '#ff375f', WHITE: '#ffffff' };
  const BAND_STANDARD = ['#0055e1', '#ffa028', '#f5ebd7'];
  ART['three-band'] = {
    count: 4,
    build() {
      const root = svg(960, 250, 'ax ax-band');
      root.append(s('rect', { class: 'a-bg', x: 0, y: 0, width: 960, height: 250 }), s('rect', { class: 'a-dark', x: 30, y: 20, width: 760, height: 210, rx: 8, stroke: '#2c3231' }));
      const clip = s('clipPath', { id: 'ax-band-clip-' + Math.random().toString(36).slice(2) }, s('rect', { x: 30, y: 20, width: 760, height: 210, rx: 8 }));
      const r1 = rng(3), r2 = rng(5), r3 = rng(9);
      const period = 440, beat = 55;
      const lows = [], mids = [], highs = [];
      for (let x = 0; x <= period; x += 4) { lows.push(0.75 + 0.25 * r1()); mids.push(0.7 + 0.3 * r2()); highs.push(0.5 + 0.5 * r3()); }
      const at = (arr, x) => arr[Math.round((x % period) / 4) % arr.length];
      const low = x => 92 * (0.28 + 0.72 * Math.exp(-((x % beat) / 13))) * at(lows, x);
      const mid = x => 52 * (0.45 + 0.55 * Math.abs(Math.sin((x % period) / 27))) * at(mids, x);
      const high = x => 24 * (0.4 + 0.6 * (((x + beat / 2) % beat) < 10 ? 1 : 0.45)) * at(highs, x);
      const scroll = s('g', { class: 'ax-scroll' },
        s('path', { class: 'ax-layer ax-low', d: wavePath(30, 125, 760 + period, 4, low) }),
        s('path', { class: 'ax-layer ax-mid', d: wavePath(30, 125, 760 + period, 4, mid) }),
        s('path', { class: 'ax-layer ax-high', d: wavePath(30, 125, 760 + period, 4, high) }));
      root.append(s('defs', null, clip), s('g', { 'clip-path': `url(#${clip.id})` }, scroll), s('line', { class: 'ax-headln', x1: 300, y1: 20, x2: 300, y2: 230 }));
      const tag = (cls, y, ly, text) => s('g', { class: 'ax-tag ' + cls }, s('path', { class: 'a-ln', d: `M790 ${ly}H812L824 ${y - 4}H836` }), t(842, y, text, 'a-t-b', 'start'));
      root.append(tag('ax-tag-low', 54, 46, 'LOWS'), tag('ax-tag-mid', 100, 92, 'MIDS'), tag('ax-tag-high', 146, 118, 'HIGHS'));
      for (const [k, y] of [[0, 46], [1, 92], [2, 138]]) root.append(s('rect', { class: 'ax-sw ax-tag', 'data-band': k, x: 842, y: y + 14, width: 46, height: 10, rx: 2 }));
      return root;
    },
    // Data on a scene: the three colour names, lows mids highs, from the 3-BAND COLOURS choices.
    enter(root, i, scene) {
      const names = (scene.data || '').trim().split(/\s+/).filter(Boolean);
      const colour = k => (names[k] && BAND[names[k].toUpperCase()]) || BAND_STANDARD[k];
      ['.ax-low', '.ax-mid', '.ax-high'].forEach((sel, k) => { root.querySelector(sel).style.fill = colour(k); });
      root.querySelectorAll('.ax-sw').forEach(sw => { sw.style.fill = colour(Number(sw.dataset.band)); });
    }
  };

  ART['ram-usb'] = {
    count: 4,
    build(scenes) {
      const root = svg(960, 280, 'ax ax-ram');
      root.append(s('rect', { class: 'a-bg', x: 0, y: 0, width: 960, height: 280 }));
      root.append(s('rect', { class: 'a-part', x: 40, y: 92, width: 220, height: 50, rx: 8 }), t(150, 136, 'TOP PANEL USB', 'a-t-s'), s('rect', { class: 'a-dark', x: 120, y: 100, width: 60, height: 14, rx: 3, stroke: '#4c5452' }));
      root.append(s('g', { class: 'ax-stick' }, s('rect', { class: 'a-part', x: 128, y: 30, width: 44, height: 64, rx: 6, stroke: '#9aa3a0', 'stroke-width': 1.6 }), s('rect', { x: 136, y: 94, width: 28, height: 14, fill: '#c9cfcc' }), t(150, 66, 'MUSIC', 'a-t-s')));
      root.append(s('path', { class: 'ax-arrow', d: 'M150 150C150 176 150 186 150 196' }));
      root.append(s('rect', { class: 'a-part', x: 40, y: 196, width: 220, height: 66, rx: 8 }), t(56, 218, 'PLAYER MEMORY · 128 MiB', 'a-t-s', 'start'),
        s('rect', { class: 'a-dark', x: 56, y: 230, width: 188, height: 14, rx: 3, stroke: '#3a4140' }), s('rect', { class: 'ax-copy', x: 56, y: 230, width: 56, height: 14, rx: 3 }),
        t(248, 218, '✓', 'a-t-b ax-ok', 'end'), t(118, 258, '', 'a-t-s ax-mib', 'start'));
      root.append(s('rect', { class: 'a-dark', x: 300, y: 30, width: 620, height: 170, rx: 8, stroke: '#2c3231' }));
      root.append(s('rect', { x: 314, y: 42, width: 22, height: 18, rx: 3, fill: '#5a5f62' }), t(325, 56, '1', 'a-t-b ax-light-t'), t(346, 56, 'DECK 1', 'a-t', 'start'), t(906, 56, '', 'ax-state', 'end'));
      const clip = s('clipPath', { id: 'ax-ram-clip-' + Math.random().toString(36).slice(2) }, s('rect', { x: 300, y: 70, width: 620, height: 122 }));
      const r = rng(11);
      root.append(s('defs', null, clip), s('g', { 'clip-path': `url(#${clip.id})` }, s('g', { class: 'ax-wave-g' }, s('g', { class: 'ax-scroll' },
        s('path', { class: 'a-wave', d: wavePath(300, 132, 620 + 440, 4, x => 46 * (0.3 + 0.7 * Math.exp(-((x % 55) / 12))) * (0.7 + 0.3 * r())), fill: '#4f6878' })))));
      root.append(s('line', { class: 'ax-head', x1: 520, y1: 70, x2: 520, y2: 192 }));
      root.append(s('g', { class: 'ax-card' }, s('rect', { class: 'ax-card-box', x: 330, y: 100, width: 560, height: 64, rx: 6 }),
        t(610, 128, 'WHOLE TRACK IN RAM · PLAYABLE', 'ax-card-h'), t(610, 150, 'LOAD A TRACK FROM THE SAME USB OR ANOTHER TO GET THE WAVEFORM BACK', 'ax-card-s')));
      root.append(t(314, 226, 'PLAYING', 'a-t', 'start'), s('circle', { cx: 380, cy: 222, r: 4, fill: '#28e028', class: 'mt-blink' }), t(906, 226, '', 'a-t-b ax-time', 'end'));
      this.titles = scenes.map(scene => scene.title);
      return root;
    },
    // Data on the copying scene: the size of the copy in MiB.
    enter(root, i, scene, box) {
      root.querySelector('.ax-state').textContent = (scene.title || '').toUpperCase();
      const mib = Number((scene.data || '').match(/[\d.]+/)?.[0] || 0) || Number(box.dataset.mib || 38);
      box.dataset.mib = mib;
      root.querySelector('.ax-copy').setAttribute('width', String(188 * mib / 128));
      const label = root.querySelector('.ax-mib');
      if (i === 2 && !reduced()) tween(box, 3400, k => { label.textContent = (mib * k).toFixed(1) + ' MiB COPIED'; });
      else label.textContent = i >= 2 ? mib.toFixed(1) + ' MiB IN RAM' : 'NOTHING COPIED';
      // The deck keeps playing through every scene: its remaining time counts down.
      const time = root.querySelector('.ax-time');
      const tick = () => { const left = box.left = (box.left > 1 ? box.left : 222) - 1; time.textContent = '−' + String(Math.floor(left / 60)).padStart(2, '0') + ':' + String(left % 60).padStart(2, '0'); };
      tick(); if (!reduced()) every(box, 1000, tick);
    }
  };

  ART['usb-diary'] = {
    count: 4,
    build() {
      const root = svg(960, 240, 'ax ax-diary');
      root.append(s('rect', { class: 'a-bg', x: 0, y: 0, width: 960, height: 240 }));
      root.append(s('rect', { class: 'a-part', x: 40, y: 50, width: 120, height: 150, rx: 12, stroke: '#9aa3a0', 'stroke-width': 1.6 }), s('rect', { x: 70, y: 22, width: 60, height: 30, fill: '#c9cfcc' }), t(100, 132, 'USB', 'a-t-b'), t(100, 150, 'STICK', 'a-t-s'));
      root.append(t(200, 44, 'VJ.Tools/XZ-Mods-diary', 'a-t', 'start'), t(920, 44, 'AT MOST 4 MiB', 'a-t-s', 'end'));
      for (let k = 0; k < 8; k++) root.append(s('rect', { class: 'ax-seg', 'data-k': k, x: 200 + k * 90, y: 60, width: 80, height: 60, rx: 6 }), t(240 + k * 90, 96, '', 'a-t-b ax-seg-t'));
      root.append(t(200, 136, 'OLDEST', 'a-t-s', 'start'), t(910, 136, 'NEWEST', 'a-t-s', 'end'));
      root.append(s('g', { class: 'ax-pausebar' }, s('rect', { x: 200, y: 134, width: 710, height: 30, rx: 6, fill: '#2a2615', stroke: '#786b32' }), t(555, 154, 'PAUSED WHILE A TRACK LOADS, A RAM COPY RUNS OR THE USB IS BUSY', 'a-t-s')));
      const never = s('g', { class: 'ax-never' });
      ['TRACK TITLES', 'MUSIC PATHS', 'SERIAL NUMBERS', 'NETWORK IDENTITIES'].forEach((word, k) => {
        const x = 200 + k * 180;
        never.append(s('rect', { x, y: 180, width: 168, height: 34, rx: 6, fill: '#1a1414', stroke: '#5a3b36' }), t(x + 84, 202, word, 'a-t-s'), s('line', { class: 'ax-cross', x1: x + 10, y1: 197, x2: x + 158, y2: 197 }));
      });
      root.append(never);
      return root;
    },
    enter(root, i, scene, box) {
      // Segments in writing order: a number per segment, the newest lit. When the stick's share is full, the
      // oldest goes and the rest move up.
      const segs = [...root.querySelectorAll('.ax-seg')], labels = [...root.querySelectorAll('.ax-seg-t')];
      root.classList.toggle('paused', i === 3);
      const paint = (filled, first) => segs.forEach((seg, k) => {
        seg.setAttribute('class', 'ax-seg' + (k === filled - 1 ? ' is-new' : k < filled ? ' is-on' : ''));
        labels[k].textContent = k < filled ? String(first + k) : '';
      });
      if (i === 1) { if (reduced()) return paint(5, 1); paint(0, 1); for (let k = 0; k < 6; k++) later(box, 300 + k * 520, () => paint(k + 1, 1)); }
      else if (i === 2) { let first = 1; paint(8, first); if (!reduced()) every(box, 1100, () => paint(8, ++first)); }
      else paint(8, 5);
    }
  };

  // The cable video of the four-deck guide (ui/assets/wizard, made by tools/build_cable_media.py).
  ART['loopback-cable'] = { video: { src: 'assets/wizard/loopback-cable.mp4', poster: 'assets/wizard/loopback-cable-poster.webp', w: 1280, h: 400 } };

  // ---------------------------------------------------------------- timers owned by one explainer
  const live = new Set();
  function later(box, ms, fn) { const id = setTimeout(() => { box.timers?.delete(id); if (box.isConnected) fn(); }, ms); (box.timers ||= new Set()).add(id); return id; }
  function every(box, ms, fn) { const id = setInterval(() => box.isConnected ? fn() : clearInterval(id), ms); (box.intervals ||= new Set()).add(id); return id; }
  function clearTimers(box) { for (const id of box.timers || []) clearTimeout(id); box.timers?.clear(); for (const id of box.intervals || []) clearInterval(id); box.intervals?.clear(); }
  function tween(box, ms, step) {
    if (reduced()) return step(1);
    const start = performance.now();
    const frame = now => { if (!box.isConnected) return; const k = Math.min(1, (now - start) / ms); step(1 - Math.pow(1 - k, 3)); if (k < 1) requestAnimationFrame(frame); };
    requestAnimationFrame(frame);
  }

  // ---------------------------------------------------------------- the explainer frame: picture, caption, controls
  const glyph = d => s('svg', { viewBox: '0 0 16 16', 'aria-hidden': 'true' }, s('path', { d, fill: 'currentColor' }));
  const PLAY = 'M4.5 2.5v11l9-5.5z', PAUSE = 'M4 2.5h3v11H4zM9 2.5h3v11H9z', PREV = 'M3 2.5h2v11H3zM13 2.5v11L6 8z', NEXT = 'M11 2.5h2v11h-2zM3 2.5v11L10 8z';
  const el = (tag, props, ...kids) => { const item = document.createElement(tag); for (const [k, v] of Object.entries(props || {})) { if (v === undefined || v === null) continue; if (k === 'text') item.textContent = v; else if (k.startsWith('on')) item[k] = v; else item.setAttribute(k, v); } item.append(...kids.flat().filter(Boolean)); return item; };

  function mount(box) {
    const kind = box.dataset.mx, def = ART[kind];
    if (!def || box.dataset.mounted) return;
    box.dataset.mounted = '1';
    const label = box.getAttribute('aria-label') || 'Explainer';
    const head = el('div', { class: 'mx-head' }, el('span', { class: 'mx-badge', text: def.video ? 'Video' : 'Animation' }), el('h5', { text: label }));
    if (def.video) return mountVideo(box, def.video, head);
    let scenes = [], list = null;
    if (box.dataset.mode === 'steps') {
      for (let next = box.nextElementSibling; next; next = next.nextElementSibling) {
        if (next.matches('ol.steps')) { list = next; break; }
        if (next.matches('h3,h4,h5,.mx,.mf,.mg')) break;
      }
      if (list) { list.classList.add('mx-bound'); scenes = [...list.children].map((li, i) => ({ title: 'Step ' + (i + 1), data: '', html: li.innerHTML, li })); }
    } else scenes = [...box.querySelectorAll('.mx-scenes > li')].map(li => ({ title: li.dataset.title || '', data: li.dataset.v || '', html: li.querySelector('span').innerHTML }));
    if (!scenes.length) return;
    if (def.count && def.count !== scenes.length) box.dataset.mismatch = def.count + '/' + scenes.length;
    box.dataset.scenes = scenes.length;
    const art = def.build(scenes);
    const stage = el('div', { class: 'mx-stage' }, art);
    const caption = el('div', { class: 'mx-cap', 'aria-live': 'off' });
    const dots = el('div', { class: 'mx-dots', role: 'group', 'aria-label': 'Steps' });
    const count = el('span', { class: 'mx-count', 'aria-hidden': 'true' });
    const play = el('button', { type: 'button', class: 'mx-play', 'aria-label': 'Play' }, glyph(PLAY));
    const prev = el('button', { type: 'button', 'aria-label': 'Previous step' }, glyph(PREV));
    const next = el('button', { type: 'button', 'aria-label': 'Next step' }, glyph(NEXT));
    scenes.forEach((scene, i) => dots.append(el('button', { type: 'button', 'data-i': i + 1, 'aria-label': 'Step ' + (i + 1) + (box.dataset.mode === 'steps' ? '' : ': ' + scene.title) }, el('i'), el('span', { text: String(i + 1) }))));
    box.replaceChildren(head, stage, caption, el('div', { class: 'mx-ctl' }, prev, play, next, dots, count));
    let at = 0, playing = false, userPaused = false, timer = 0;
    const duration = i => (def.ms?.[i]) || Math.min(7200, Math.max(3800, 1900 + scenes[i - 1].html.replace(/<[^>]+>/g, '').length * 40));
    function go(i, user) {
      at = Math.max(1, Math.min(scenes.length, i));
      clearTimers(box);
      const classes = ['ax', ...art.getAttribute('class').split(' ').filter(c => c && !/^(sc|at)-\d+$/.test(c) && c !== 'ax'), 'sc-' + at];
      for (let k = 1; k <= at; k++) classes.push('at-' + k);
      art.setAttribute('class', [...new Set(classes)].join(' '));
      const scene = scenes[at - 1];
      def.enter?.call(def, art, at, scene, box);
      caption.setAttribute('aria-live', user ? 'polite' : 'off');
      caption.innerHTML = '';
      caption.append(el('span', { class: 'mx-cap-n', text: String(at), 'aria-hidden': 'true' }));
      const words = el('span'); words.innerHTML = (box.dataset.mode === 'steps' ? '' : '<b>' + escapeHtml(scene.title) + '.</b> ') + scene.html; caption.append(words);
      if (!reduced()) { caption.classList.remove('mx-swap'); void caption.offsetWidth; caption.classList.add('mx-swap'); }
      [...dots.children].forEach((dot, k) => { dot.toggleAttribute('aria-current', false); if (k + 1 === at) dot.setAttribute('aria-current', 'step'); dot.classList.toggle('is-done', k + 1 < at); });
      count.textContent = at + ' / ' + scenes.length;
      if (list) [...list.children].forEach((li, k) => { li.classList.toggle('is-now', k + 1 === at); li.classList.toggle('is-done', k + 1 < at); });
      box.style.setProperty('--mx-dur', duration(at) + 'ms');
      clearTimeout(timer);
      if (playing) timer = setTimeout(advance, duration(at));
    }
    function advance() {
      if (!box.isConnected) return stop();
      if (at >= scenes.length) { stop(); return; }
      go(at + 1, false);
    }
    // With reduced motion Play still steps through the scenes; the CSS shows each one at once.
    function start() {
      playing = true; box.classList.add('is-playing');
      play.replaceChildren(glyph(PAUSE)); play.setAttribute('aria-label', 'Pause');
      go(at >= scenes.length || !at ? 1 : at, false);
    }
    function stop() {
      playing = false; clearTimeout(timer); box.classList.remove('is-playing');
      play.replaceChildren(glyph(PLAY)); play.setAttribute('aria-label', at >= scenes.length ? 'Play again' : 'Play');
    }
    const user = fn => () => { userPaused = true; stop(); fn(); };
    play.onclick = () => { if (playing) { userPaused = true; stop(); } else { userPaused = false; start(); } };
    prev.onclick = user(() => go(at - 1, true));
    next.onclick = user(() => go(at + 1, true));
    dots.onclick = event => { const dot = event.target.closest('button'); if (dot) user(() => go(Number(dot.dataset.i), true))(); };
    if (list) [...list.children].forEach((li, k) => { li.onclick = event => { if (event.target.closest('a')) return; user(() => { go(k + 1, true); box.scrollIntoView({ block: 'nearest', behavior: reduced() ? 'auto' : 'smooth' }); })(); }; });
    box.mxStop = () => { stop(); clearTimers(box); };
    live.add(box);
    go(1, false);
    if (!reduced() && 'IntersectionObserver' in window) {
      const observer = new IntersectionObserver(entries => {
        for (const entry of entries) {
          if (!box.isConnected) { observer.disconnect(); return; }
          if (entry.isIntersecting && !playing && !userPaused && !box.dataset.played) { box.dataset.played = '1'; start(); }
          else if (!entry.isIntersecting && playing) { stop(); delete box.dataset.played; }
        }
      }, { threshold: 0.55 });
      observer.observe(box);
      box.mxObserver = observer;
    }
    box.mxGo = i => go(i, true);
  }

  function mountVideo(box, media, head) {
    const caption = [...box.querySelectorAll('.mx-scenes > li')].map(li => li.querySelector('span').innerHTML).join(' ');
    const video = el('video', { src: media.src, poster: media.poster, muted: '', loop: '', playsinline: '', preload: 'none', width: media.w, height: media.h, 'aria-label': box.getAttribute('aria-label') || 'Video' });
    video.muted = true;
    const play = el('button', { type: 'button', class: 'mx-play', 'aria-label': 'Play' }, glyph(PLAY));
    const cap = el('div', { class: 'mx-cap' }); cap.innerHTML = caption;
    box.replaceChildren(head, el('div', { class: 'mx-stage' }, video), cap, el('div', { class: 'mx-ctl' }, play, el('span', { class: 'mx-count', text: reduced() ? 'Plays when you press play' : '13 s loop' })));
    const sync = () => { const on = !video.paused; play.replaceChildren(glyph(on ? PAUSE : PLAY)); play.setAttribute('aria-label', on ? 'Pause' : 'Play'); };
    video.onplay = video.onpause = sync;
    let userPaused = false;
    play.onclick = () => { if (video.paused) { userPaused = false; video.play().catch(() => {}); } else { userPaused = true; video.pause(); } };
    box.mxStop = () => video.pause();
    live.add(box);
    if (!reduced() && 'IntersectionObserver' in window) {
      const observer = new IntersectionObserver(entries => { for (const entry of entries) { if (!box.isConnected) return observer.disconnect(); if (entry.isIntersecting && !userPaused) video.play().catch(() => {}); else if (!entry.isIntersecting) video.pause(); } }, { threshold: 0.5 });
      observer.observe(box);
    }
  }
  const escapeHtml = text => String(text).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

  function stopAll() { for (const box of live) { box.mxStop?.(); box.mxObserver?.disconnect(); } live.clear(); if (hero.timer) { clearInterval(hero.timer); hero.timer = 0; } }

  // ---------------------------------------------------------------- start page pictures
  const hero = {};
  function heroArt() {
    const root = svg(960, 470, 'ax ax-hero');
    panel(root, true);
    root.querySelector('.ax-deck3')?.remove();
    root.append(s('path', { class: 'ah-cable', d: CABLE }), s('path', { class: 'ah-flow', d: CABLE, pathLength: 1 }));
    root.append(s('path', { class: 'a-part', d: 'M548 38v-22h24v22l-3 4h-18z', stroke: '#f3e900' }), s('rect', { class: 'a-part', x: 781, y: 82, width: 18, height: 26, rx: 2, stroke: '#f3e900' }));
    const lanes = [[110, 1], [141, 2], [172, 3], [203, 4]];
    for (const [y, n] of lanes) root.append(s('rect', { x: 350, y, width: 18, height: 14, rx: 2, fill: n > 2 ? '#f2f2f2' : '#5a5f62' }), s('path', { class: 'a-wave', d: laneWave(374, y + 14, 236, 11, n * 3) }));
    const zones = [['ah-z1', 'M332 74h296v172h-296z', 480, 66, 'MODERN SCREEN'], ['ah-z2', 'M120 192a100 100 0 1 0 200 0a100 100 0 1 0 -200 0', 220, 186, 'DECKS 1 AND 3'],
      ['ah-z3', 'M540 4h270v130h-270z', 676, 150, '4 DECKS CABLE'], ['ah-z4', 'M332 244h296v212h-296z', 480, 240, 'CH3 · CH4'], ['ah-z5', 'M720 192a100 100 0 1 0 200 0a100 100 0 1 0 -200 0', 820, 186, 'DECKS 2 AND 4']];
    zones.forEach(([id, d, x, y, text], k) => {
      const label = t(x, y, text, 'ah-label'); label.setAttribute('data-z', k);
      root.append(s('path', { class: 'ah-zone', 'data-z': k, d, transform: id === 'ah-z2' || id === 'ah-z5' ? 'translate(0 100)' : null }), label);
    });
    return root;
  }
  function startHero(view) {
    const root = view.querySelector('.ax-hero');
    if (!root || reduced()) return;
    let k = 0;
    const show = () => { root.querySelectorAll('[data-z]').forEach(item => item.classList.toggle('is-on', Number(item.dataset.z) === k)); k = (k + 1) % 5; };
    show();
    clearInterval(hero.timer);
    hero.timer = setInterval(() => root.isConnected ? show() : (clearInterval(hero.timer), hero.timer = 0), 2700);
  }

  const TOUR = {
    modern: () => [s('rect', { class: 'mt-ln', x: 30, y: 14, width: 140, height: 78, rx: 5 }), s('rect', { class: 'mt-dim', x: 34, y: 18, width: 132, height: 9, rx: 2 }),
      s('rect', { x: 140, y: 19, width: 22, height: 7, rx: 1.5, fill: '#f3e900' }),
      s('path', { class: 'mt-wave', d: laneWave(38, 44, 124, 9, 2), fill: '#0055e1' }), s('path', { class: 'mt-wave', d: laneWave(38, 70, 124, 9, 4), fill: '#ffa028' }), s('line', { x1: 80, y1: 30, x2: 80, y2: 86, stroke: '#fff', 'stroke-width': 1.5 })],
    // The default stem order and the ORIGINAL theme's stem colours (stems chapter): vocals green, harmonics red, drums blue.
    stems: () => [['VOCALS', '#30d158', 24, 'mt-stem-a'], ['HARMONICS', '#ff3b30', 48, ''], ['DRUMS', '#2997ff', 72, 'mt-stem-b']].map(([name, colour, y, cls]) => s('g', { class: 'mt-stem ' + cls },
      s('rect', { x: 24, y: y - 9, width: 152, height: 18, rx: 4, fill: '#121516', stroke: colour }), s('path', { d: wavePath(92, y, 80, 3, x => 6 * (0.4 + 0.6 * Math.abs(Math.sin(x / 5 + y)))), fill: colour }), t(30, y + 3.5, name, 'a-t-s', 'start'))),
    'four-decks': () => [s('rect', { class: 'mt-ln', x: 20, y: 40, width: 160, height: 56, rx: 6 }), s('rect', { class: 'mt-dim', x: 120, y: 48, width: 18, height: 8, rx: 1 }), s('rect', { class: 'mt-dim', x: 146, y: 48, width: 18, height: 8, rx: 1 }),
      s('rect', { class: 'mt-ln', x: 40, y: 18, width: 14, height: 12, rx: 2 }), s('path', { class: 'mt-hl', d: 'M47 18C47 6 128 2 129 48' }), s('path', { class: 'mt-flow', d: 'M47 18C47 6 128 2 129 48', pathLength: 1 }),
      ...['3', '1', '2', '4'].map((n, i) => t(56 + i * 22, 86, n, 'a-t-b'))],
    'beat-jump': () => [s('rect', { class: 'mt-ln', x: 20, y: 14, width: 160, height: 34, rx: 4 }), s('g', { class: 'mt-jump' }, ...Array.from({ length: 20 }, (_, b) => s('line', { x1: 24 + b * 9, y1: b % 4 ? 22 : 18, x2: 24 + b * 9, y2: 44, stroke: b % 4 ? '#2f3534' : '#59605e' }))),
      s('line', { x1: 100, y1: 12, x2: 100, y2: 50, stroke: '#f3e900', 'stroke-width': 2 }), ...Array.from({ length: 8 }, (_, p) => s('rect', { x: 52 + (p % 4) * 26, y: 58 + Math.floor(p / 4) * 22, width: 22, height: 18, rx: 3, fill: '#141819', stroke: p < 2 || p >= 6 ? '#ff3000' : '#ffa300' }))],
    'key-sync': () => [s('circle', { class: 'mt-ln', cx: 100, cy: 55, r: 38 }), ...Array.from({ length: 12 }, (_, k) => s('circle', { cx: 100 + 38 * Math.sin(k * Math.PI / 6), cy: 55 - 38 * Math.cos(k * Math.PI / 6), r: 4, fill: k === 1 ? '#28e028' : '#3a4140' })),
      s('g', { class: 'mt-key' }, s('line', { x1: 100, y1: 55, x2: 100, y2: 22, stroke: '#f3e900', 'stroke-width': 3, 'stroke-linecap': 'round' })), s('circle', { cx: 100, cy: 55, r: 5, fill: '#f3e900' })],
    ram: () => [s('g', { class: 'mt-pull' }, s('rect', { class: 'mt-ln', x: 30, y: 16, width: 22, height: 34, rx: 3 }), s('rect', { x: 35, y: 50, width: 12, height: 8, fill: '#8a9290' })), s('rect', { class: 'mt-ln', x: 24, y: 58, width: 34, height: 8, rx: 2 }),
      s('rect', { x: 76, y: 22, width: 104, height: 40, rx: 4, fill: '#0d2321', stroke: '#2cc4b4' }), t(128, 40, 'IN RAM', 'a-t-s'), t(128, 54, 'PLAYABLE', 'a-t-s'), s('rect', { x: 76, y: 76, width: 104, height: 8, rx: 2, fill: '#1a1e1f' }), s('rect', { x: 76, y: 76, width: 40, height: 8, rx: 2, fill: '#2cc4b4' })],
    themes: () => [s('path', { class: 'mt-hue', d: wavePath(24, 55, 152, 3, x => 30 * (0.3 + 0.7 * Math.exp(-((x % 22) / 6)))) }), s('path', { class: 'mt-hue2', d: wavePath(24, 55, 152, 3, x => 16 * (0.5 + 0.5 * Math.abs(Math.sin(x / 7))) ) }), s('path', { d: wavePath(24, 55, 152, 3, x => 6 * (0.5 + 0.5 * Math.abs(Math.sin(x / 3)))), fill: '#f5ebd7' })],
    mods: () => [s('rect', { class: 'mt-ln', x: 40, y: 12, width: 120, height: 84, rx: 6 }), s('g', { class: 'mt-sel' }, s('rect', { x: 46, y: 20, width: 108, height: 14, rx: 3, fill: '#2a2b17', stroke: '#f3e900' })),
      ...['HOME', 'PLAY', 'STEMS', 'SCREEN', '4 DECKS'].map((w, k) => t(54, 31 + k * 16, w, 'a-t-s', 'start'))],
    profile: () => [s('circle', { class: 'mt-ln', cx: 62, cy: 52, r: 26 }), s('circle', { cx: 62, cy: 45, r: 8, fill: '#3a4140' }), s('path', { d: 'M46 70c4-9 28-9 32 0', fill: '#3a4140' }),
      s('path', { class: 'mt-hl mt-draw', d: 'M100 42h70', pathLength: 1 }), s('path', { class: 'mt-ln', d: 'M100 58h50M100 70h60' })],
    app: () => [s('rect', { class: 'mt-ln', x: 46, y: 16, width: 108, height: 66, rx: 5 }), s('path', { class: 'mt-ln', d: 'M34 88h132' }), s('rect', { x: 52, y: 22, width: 26, height: 54, rx: 2, fill: '#121516' }),
      s('rect', { x: 84, y: 28, width: 60, height: 8, rx: 2, fill: '#f3e900' }), s('rect', { x: 84, y: 42, width: 46, height: 6, rx: 2, fill: '#3a4140' }), s('path', { class: 'mt-hl mt-draw', d: 'M88 60l8 8 16-16', pathLength: 1 })]
  };
  function tour(name) { const root = svg(200, 104, 'mt'); root.append(...(TOUR[name]?.() || [])); return root; }

  window.manualArt = Object.freeze({
    enhance(article) { for (const box of article.querySelectorAll('.mx')) mount(box); },
    stopAll, hero: heroArt, startHero, tour, kinds: Object.keys(ART), counts: Object.fromEntries(Object.entries(ART).map(([k, v]) => [k, v.count || 0]))
  });
})();
