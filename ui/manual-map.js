// The menu map on the Manual page (design 2026-10-28, Opus 5.5): every screen the player can reach, as a flow chart
// from the play screen, each card a screen the firmware itself drew. ui/manual.js mounts it; the tree comes from
// ui/manual/map.js (tools/build_manual.py app, from manual/menu-tree/). Nothing here draws the player or its screen.
//
// A screen on the map is every captured frame with the same interface and path: the frames are its states (the
// highlight moved, a value changed). The map lays the screens out as a tree, breadth first from the play screen, and
// labels each branch with the control that opens it. Moves that do not open a new branch (BACK, a jump across the
// tree) are listed in the screen's panel. Positions are set through CSSOM: the app's CSP allows no style attributes.
'use strict';
(() => {
  const CARD_W = 176, THUMB_W = 160, THUMB_H = 96, CARD_H = 154, COL_GAP = 156, ROW_GAP = 20;
  const CELL_W = 120, CELL_H = 100, LANE_COLS = 4, MIN_K = 0.12, MAX_K = 2;
  const SVG = 'http://www.w3.org/2000/svg';
  const motionQuery = matchMedia('(prefers-reduced-motion: reduce)');
  const reduced = () => motionQuery.matches;

  function h(tag, props, ...children) {
    const item = document.createElement(tag);
    for (const [key, value] of Object.entries(props || {})) {
      if (value === undefined || value === null || value === false) continue;
      if (key === 'class') item.className = value;
      else if (key === 'text') item.textContent = value;
      else if (key === 'dataset') Object.assign(item.dataset, value);
      else if (key.startsWith('on')) item[key] = value;
      else item.setAttribute(key, value === true ? '' : value);
    }
    item.append(...children.flat(Infinity).filter(child => child !== null && child !== undefined && child !== false && child !== ''));
    return item;
  }
  const s = (tag, attrs, ...children) => { const item = document.createElementNS(SVG, tag); for (const [k, v] of Object.entries(attrs || {})) item.setAttribute(k, v); item.append(...children); return item; };
  const glyph = d => s('svg', { viewBox: '0 0 16 16', 'aria-hidden': 'true', fill: 'none', stroke: 'currentColor', 'stroke-width': '1.6', 'stroke-linecap': 'round', 'stroke-linejoin': 'round' }, s('path', { d }));
  const G = {
    plus: 'M8 3.5v9M3.5 8h9', minus: 'M3.5 8h9', fit: 'M2.5 6V2.5H6M10 2.5h3.5V6M13.5 10v3.5H10M6 13.5H2.5V10', expand: 'M3 5.5 8 10.5l5-5', collapse: 'M3 10.5 8 5.5l5 5',
    states: 'M2.5 3.5h5v4h-5zM8.5 3.5h5v4h-5zM2.5 9.5h5v4h-5zM8.5 9.5h5v4h-5z', close: 'M4 4l8 8M12 4l-8 8', zoom: 'M9.5 2.5h4v4M6.5 13.5h-4v-4M13.5 2.5 9 7M2.5 13.5 7 9',
    left: 'M10 3.5 5.5 8l4.5 4.5', right: 'M6 3.5 10.5 8 6 12.5', book: 'M8 4C6.8 3.2 5.2 2.8 2.5 2.8v9.4c2.7 0 4.3.4 5.5 1.2 1.2-.8 2.8-1.2 5.5-1.2V2.8c-2.7 0-4.3.4-5.5 1.2zM8 4v9.4', search: 'M7 2.5a4.5 4.5 0 1 0 0 9 4.5 4.5 0 0 0 0-9zM10.5 10.5l3 3'
  };
  const norm = text => ' ' + String(text || '').toLowerCase().replace(/[^a-z0-9.]+/g, ' ').trim() + ' ';

  // ---------------------------------------------------------------- what a move is called
  // Keys are drawn as the manual draws [[KEY]]s: a keycap with the name printed on the player.
  const keyName = control => /^performance pad ([A-H])$/i.test(control) ? 'PAD ' + control.slice(-1).toUpperCase() : control;
  function moveParts(move) {
    const [, , control, action, target, rect] = move;
    if (control === 'touch') return target ? [h('span', { class: 'mm-g', text: 'Tap' }), h('span', { class: 'mm-scr', text: target })] : [h('span', { class: 'mm-g', text: rect ? 'Tap the screen here' : 'Tap the screen' })];
    const gesture = { 'turn+': 'turn ›', 'turn-': 'turn ‹', push: 'push', hold: 'hold', press: '', tap: 'tap' }[action] ?? action;
    return [h('kbd', { text: keyName(control) }), gesture ? h('span', { class: 'mm-g', text: gesture }) : null,
      target && control !== 'touch' ? h('span', { class: /deck$/.test(target) ? 'mm-g mm-side' : 'mm-scr', text: /deck$/.test(target) ? target : target }) : null];
  }
  function moveText(move) {
    const [, , control, action, target, rect] = move;
    if (control === 'touch') return target ? 'Tap ' + target : rect ? 'Tap the screen at the marked place' : 'Tap the screen';
    const key = keyName(control);
    const side = /deck$/.test(target) ? ' on the ' + target : '';
    const on = target && !side ? ' on ' + target : '';
    return ({ 'turn+': 'Turn the ' + key + ' clockwise', 'turn-': 'Turn the ' + key + ' anticlockwise', push: 'Push the ' + key + on, hold: 'Hold ' + key + side, press: 'Press ' + key + side }[action] || action + ' ' + key) + (action === 'push' || action.startsWith('turn') ? '' : on && action !== 'press' ? on : '');
  }
  // MENU first, so MODS heads the map; then the other keys, taps, the encoder, and BACK last.
  const moveRank = move => move[2] === 'MENU' && move[3] === 'press' ? -1 : move[2] === 'BACK' ? 4 : move[3].startsWith('turn') ? 3 : move[3] === 'push' ? 2 : move[2] === 'touch' ? 1 : 0;

  // ---------------------------------------------------------------- the graph of one interface
  function graph(data, iface) {
    const byId = new Map(data.nodes.map(node => [node.id, node]));
    const screens = new Map(), screenOf = new Map();
    for (const node of data.nodes) {
      if (node.i !== iface) continue;
      const key = node.p.join('\u0001');
      if (!screens.has(key)) screens.set(key, { key, iface, title: node.p.at(-1) || node.t, path: node.p, states: [], children: [], parent: null, via: null, out: [], into: [], depth: 0, open: true, lane: false, slug: slug(node.p) });
      screens.get(key).states.push(node);
      screenOf.set(node.id, screens.get(key));
    }
    const inner = new Map();  // moves between states of one screen
    for (const move of data.edges) {
      const from = screenOf.get(move[0]), to = screenOf.get(move[1]);
      if (from && to && from === to) { (inner.get(from) || inner.set(from, []).get(from)).push(move); continue; }
      if (from) from.out.push(move);
      if (to) to.into.push(move);
    }
    // Spanning tree over screens, breadth first from the roots in the tree's order; then any screen nothing reached.
    const rootIds = (data.roots || []).map(root => root.id).filter(id => screenOf.has(id));
    const roots = [], seen = new Set();
    const order = list => list.slice().sort((a, b) => moveRank(a) - moveRank(b) || (screenOf.get(a[1])?.title || '').localeCompare(screenOf.get(b[1])?.title || ''));
    const grow = (start, entry) => {
      if (seen.has(start)) return;
      seen.add(start); start.entry = entry; roots.push(start);
      const queue = [start];
      while (queue.length) {
        const screen = queue.shift();
        for (const move of order(screen.out)) {
          const child = screenOf.get(move[1]);
          if (!child || seen.has(child)) continue;
          seen.add(child);
          Object.assign(child, { parent: screen, via: move, depth: screen.depth + 1, entry: move[1] });
          screen.children.push(child);
          queue.push(child);
        }
      }
    };
    for (const id of rootIds) grow(screenOf.get(id), id);
    for (const screen of [...screens.values()].sort((a, b) => a.path.join('/').localeCompare(b.path.join('/')))) if (!seen.has(screen)) grow(screen, (screen.into[0] || [])[1] || screen.states[0].id);
    // States in the order a player meets them: from the way in, along the moves inside the screen.
    for (const screen of screens.values()) {
      const moves = inner.get(screen) || [];
      const first = screen.states.find(node => node.id === screen.entry) || screen.states[0];
      const ordered = [first], how = new Map([[first.id, null]]);
      for (let i = 0; i < ordered.length; i++) {
        for (const move of order(moves.filter(m => m[0] === ordered[i].id))) if (!how.has(move[1])) { how.set(move[1], move); ordered.push(byId.get(move[1])); }
      }
      for (const node of screen.states) if (!how.has(node.id)) { how.set(node.id, screen.into.find(m => m[1] === node.id) || null); ordered.push(node); }
      screen.states = ordered; screen.how = how; screen.inner = moves;
    }
    const all = [];
    const walk = screen => { all.push(screen); screen.children.forEach(walk); };
    roots.forEach(walk);
    all.forEach((screen, i) => { screen.n = i; screen.open = screen.depth < 2; });
    return { iface, byId, screens: all, screenOf, roots, labels: Object.fromEntries((data.roots || []).filter(r => screenOf.has(r.id)).map(r => [screenOf.get(r.id).key, r.label])) };
  }
  function slug(path) { return path.join(' ').toLowerCase().replace(/<[^>]+>/g, '').replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, ''); }

  // ---------------------------------------------------------------- the map
  let M = null;  // the mounted map

  function mount(container, options) {
    unmount();
    const data = window.XZ_MENU_MAP;
    const counts = { modern: 0, classic: 0 };
    const graphs = {};
    for (const iface of ['modern', 'classic']) { graphs[iface] = graph(data, iface); counts[iface] = graphs[iface].screens.length; }
    const m = M = { data, graphs, options, cam: { x: 0, y: 0, k: 1 }, selected: null, state: 0, panelOpen: false, focusKey: null, listeners: [] };
    const listen = (target, type, fn, opts) => { target.addEventListener(type, fn, opts); m.listeners.push(() => target.removeEventListener(type, fn, opts)); };

    // Toolbar
    m.search = h('input', { type: 'search', class: 'mm-q', placeholder: 'Find a screen', autocomplete: 'off', spellcheck: 'false', 'aria-label': 'Find a screen on the menu map', role: 'combobox', 'aria-expanded': 'false', 'aria-controls': 'mm-found', 'aria-autocomplete': 'list' });
    m.found = h('ul', { class: 'mm-found', id: 'mm-found', role: 'listbox', 'aria-label': 'Screens found', hidden: true });
    m.seg = h('div', { class: 'mm-seg', role: 'group', 'aria-label': 'Interface' }, ['modern', 'classic'].map(name =>
      h('button', { type: 'button', dataset: { iface: name }, 'aria-pressed': 'false', onclick: () => setIface(name, true) }, h('b', { text: name.toUpperCase() }), h('span', { text: counts[name] + ' screens' }))));
    const tool = (name, label, onclick, key) => h('button', { type: 'button', class: 'mm-tool', title: label + (key ? ' (' + key + ')' : ''), 'aria-label': label, dataset: { tool: name }, onclick }, glyph(G[name]));
    m.zoomText = h('output', { class: 'mm-zoom', 'aria-live': 'off', text: '100%' });
    m.lanes = h('button', { type: 'button', class: 'mm-chip', 'aria-pressed': 'false', onclick: () => setLanes(m.lanes.getAttribute('aria-pressed') !== 'true') }, glyph(G.states), h('span', { text: 'Show every state' }));
    const bar = h('div', { class: 'mm-bar' },
      h('div', { class: 'mm-title' }, h('h2', { tabindex: '-1', class: 'mm-h', text: 'Menu map' }), h('p', { text: 'Every screen you can reach from the play screen, and the control that gets you there.' })),
      m.seg,
      h('div', { class: 'mm-find' }, glyph(G.search), m.search, m.found),
      h('div', { class: 'mm-tools', role: 'toolbar', 'aria-label': 'Map view' },
        tool('minus', 'Zoom out', () => zoomBy(1 / 1.25), '−'), m.zoomText, tool('plus', 'Zoom in', () => zoomBy(1.25), '+'), tool('fit', 'Fit the map to the view', () => fit(true), '0'),
        tool('expand', 'Open every branch', () => openAll(true)), tool('collapse', 'Close every branch below the sections', () => openAll(false)), m.lanes));
    m.crumbs = h('ol', { class: 'mm-crumbs' });
    m.jumps = h('div', { class: 'mm-jumps', role: 'group', 'aria-label': 'Jump to' });

    // Stage: the viewport with the world in it, the minimap, and the screen panel.
    m.svg = s('svg', { class: 'mm-wires', 'aria-hidden': 'true' });
    m.world = h('div', { class: 'mm-world' }, m.svg);
    m.tree = h('div', { class: 'mm-tree', role: 'tree', 'aria-label': 'Menu map: screens and the controls between them' });
    m.world.append(m.tree);
    m.labels = h('div', { class: 'mm-labels', 'aria-hidden': 'true' });
    m.world.append(m.labels);
    m.mini = h('canvas', { class: 'mm-mini', width: 176, height: 110, 'aria-hidden': 'true' });
    // Computer keys get their own plain style: a keycap on the Manual page always means a key on the player.
    const key = text => h('span', { class: 'mm-key', text });
    m.hint = h('p', { class: 'mm-hint' }, 'Drag to move. Wheel or ', key('+'), ' ', key('−'), ' to zoom, ', key('0'), ' to fit. Arrow keys walk the map, ', key('Enter'), ' opens a screen.');
    m.view = h('div', { class: 'mm-view' }, m.world, m.mini, m.hint);
    m.panel = h('section', { class: 'mm-panel', 'aria-label': 'Screen', hidden: true });
    m.stage = h('div', { class: 'mm-stage' }, m.view, m.panel);
    m.root = h('div', { class: 'mm' }, bar, h('div', { class: 'mm-path', role: 'navigation', 'aria-label': 'Path to this screen' }, m.crumbs, m.jumps), m.stage);
    container.replaceChildren(m.root);
    m.root.classList.toggle('mm-still', reduced());

    // Thumbnails load as they come near the view.
    m.io = new IntersectionObserver(entries => { for (const entry of entries) if (entry.isIntersecting) { const img = entry.target; img.src = img.dataset.src; m.io.unobserve(img); } }, { root: m.view, rootMargin: '360px' });

    // Pointer: drag pans, the wheel zooms where it points, a horizontal swipe pans.
    let drag = null;
    listen(m.view, 'pointerdown', event => {
      if (event.button !== 0 || event.target.closest('.mm-mini')) return;
      drag = { id: event.pointerId, x: event.clientX, y: event.clientY, cx: m.cam.x, cy: m.cam.y, moved: false };
    });
    listen(m.view, 'pointermove', event => {
      if (!drag || event.pointerId !== drag.id) return;
      const dx = event.clientX - drag.x, dy = event.clientY - drag.y;
      if (!drag.moved && Math.hypot(dx, dy) < 4) return;
      if (!drag.moved) { drag.moved = true; m.view.setPointerCapture(drag.id); m.view.classList.add('is-dragging'); }
      stopGlide();
      setCam(drag.cx + dx, drag.cy + dy, m.cam.k);
    });
    const endDrag = event => { if (!drag || event.pointerId !== drag.id) return; if (drag.moved) { m.view.classList.remove('is-dragging'); m.suppressClick = true; setTimeout(() => { m.suppressClick = false; }, 0); } drag = null; };
    listen(m.view, 'pointerup', endDrag); listen(m.view, 'pointercancel', endDrag);
    listen(m.view, 'click', event => { if (m.suppressClick) { event.stopPropagation(); event.preventDefault(); } }, true);
    listen(m.view, 'wheel', event => {
      event.preventDefault();
      stopGlide();
      if (!event.ctrlKey && Math.abs(event.deltaX) > Math.abs(event.deltaY)) return setCam(m.cam.x - event.deltaX, m.cam.y - event.deltaY, m.cam.k);
      if (event.shiftKey) return setCam(m.cam.x - event.deltaY, m.cam.y, m.cam.k);
      const box = m.view.getBoundingClientRect();
      const step = Math.exp(-(event.deltaMode === 1 ? event.deltaY * 16 : event.deltaY) * (event.ctrlKey ? 0.01 : 0.0018));
      zoomAt(m.cam.k * step, event.clientX - box.left, event.clientY - box.top);
    }, { passive: false });
    // The minimap: press or drag on it to move the view there.
    let miniDrag = false;
    const miniGo = event => { const r = m.mini.getBoundingClientRect(), b = m.mini.bounds; if (!b) return; const wx = b.x + (event.clientX - r.left) / b.s, wy = b.y + (event.clientY - r.top) / b.s; const v = viewSize(); setCam(v.w / 2 - wx * m.cam.k, v.h / 2 - wy * m.cam.k, m.cam.k); };
    listen(m.mini, 'pointerdown', event => { miniDrag = true; m.mini.setPointerCapture(event.pointerId); miniGo(event); event.stopPropagation(); });
    listen(m.mini, 'pointermove', event => { if (miniDrag) miniGo(event); });
    listen(m.mini, 'pointerup', () => { miniDrag = false; });
    // Keys on the tree.
    listen(m.tree, 'keydown', onTreeKey);
    listen(m.view, 'keydown', event => { if (event.target === m.view) onViewKey(event); });
    listen(m.search, 'input', () => { clearTimeout(m.searchTimer); m.searchTimer = setTimeout(runFind, 60); });
    listen(m.search, 'keydown', event => {
      const items = [...m.found.querySelectorAll('[role=option]')];
      if (event.key === 'ArrowDown' && items.length) { event.preventDefault(); items[0].focus(); }
      else if (event.key === 'Enter') { event.preventDefault(); items[0]?.click(); }
      else if (event.key === 'Escape') { if (m.search.value) { event.preventDefault(); m.search.value = ''; runFind(); } }
    });
    listen(m.found, 'keydown', event => {
      const items = [...m.found.querySelectorAll('[role=option]')], at = items.indexOf(document.activeElement);
      if (event.key === 'ArrowDown' || event.key === 'ArrowUp') { event.preventDefault(); if (event.key === 'ArrowUp' && at <= 0) return m.search.focus(); items[Math.min(items.length - 1, at + (event.key === 'ArrowDown' ? 1 : -1))].focus(); }
      else if (event.key === 'Escape') { event.preventDefault(); closeFind(); m.search.focus(); }
    });
    listen(document, 'pointerdown', event => { if (!event.target.closest('.mm-find')) closeFind(); });
    m.ro = new ResizeObserver(() => { drawMini(); });
    m.ro.observe(m.view);
    listen(motionQuery, 'change', () => m.root.classList.toggle('mm-still', reduced()));

    const target = options.target ? resolve(options.target, options.iface) : null;
    m.iface = target?.iface || (options.iface === 'classic' && counts.classic ? 'classic' : 'modern');
    build(m.iface);
    if (target) requestAnimationFrame(() => go(target.screen, { state: target.state, open: true, focus: true, instant: true }));
    else requestAnimationFrame(() => { start(); select(G0().roots[0], 0, { focus: false, stay: true }); });
    return api;
  }
  const G0 = () => M.graphs[M.iface];

  // A target from a link: a screen's id, or its address (the path, slugified), in the reader's interface if it has one.
  function resolve(target, iface) {
    const m = M, order = iface === 'classic' ? ['classic', 'modern'] : ['modern', 'classic'];
    for (const name of order) {
      const g = m.graphs[name];
      const screen = g.screenOf.get(target) || g.screens.find(item => item.slug === target);
      if (screen) return { iface: name, screen, state: Math.max(0, screen.states.findIndex(node => node.id === target)) };
    }
    return null;
  }

  // ---------------------------------------------------------------- building the cards of one interface
  function build(iface) {
    const m = M, g = m.graphs[iface];
    m.iface = iface;
    for (const button of m.seg.children) button.setAttribute('aria-pressed', String(button.dataset.iface === iface));
    m.tree.replaceChildren(); m.labels.replaceChildren(); m.svg.replaceChildren();
    m.io.disconnect();
    for (const screen of g.screens) {
      const img = h('img', { alt: '', width: THUMB_W, height: THUMB_H, decoding: 'async', dataset: { src: screen.states[0].th } });
      const twist = screen.children.length ? h('button', { type: 'button', class: 'mm-twist', tabindex: '-1', onclick: event => { event.stopPropagation(); toggle(screen); } }) : null;
      const lane = screen.states.length > 1 ? h('div', { class: 'mm-lane', hidden: true }) : null;
      const card = h('div', { class: 'mm-card', role: 'treeitem', tabindex: '-1', id: 'mm-' + screen.n, dataset: { key: screen.slug, n: screen.n },
        'aria-level': String(screen.depth + 1), 'aria-selected': 'false', 'aria-label': cardLabel(screen) },
      h('span', { class: 'mm-thumb' }, img, screen.states.length > 1 ? h('span', { class: 'mm-count', text: screen.states.length + ' states' }) : null),
      h('span', { class: 'mm-name', text: screen.title }),
      h('span', { class: 'mm-sub', text: subLine(screen) }), twist, lane);
      if (screen.children.length) card.setAttribute('aria-expanded', String(screen.open));
      card.addEventListener('click', event => { if (event.target.closest('.mm-twist,.mm-lane')) return; select(screen, 0, { open: true, focus: true }); });
      card.addEventListener('dblclick', event => { if (!event.target.closest('.mm-lane') && screen.children.length) toggle(screen); });
      screen.card = card; screen.img = img; screen.twist = twist; screen.laneEl = lane;
      m.io.observe(img);
      if (screen.via) {
        const label = h('div', { class: 'mm-label' }, moveParts(screen.via));
        if (screen.parent.iface !== screen.iface) label.classList.add('is-cross');
        screen.label = label; m.labels.append(label);
        screen.wire = s('path', { class: 'mm-wire' }); m.svg.append(screen.wire);
      } else if (screen !== g.roots[0]) {
        // A screen nothing on this side of the map leads to: say where it comes from.
        const from = screen.into[0];
        const fromNode = from && (m.graphs.modern.byId.get(from[0]));
        const label = h('div', { class: 'mm-label is-orphan' }, from ? [h('span', { class: 'mm-g', text: 'From ' + (fromNode ? fromNode.i.toUpperCase() + ' ' + fromNode.t : 'elsewhere') + ':' }), moveParts(from)] : h('span', { class: 'mm-g', text: 'Starts here' }));
        screen.label = label; m.labels.append(label);
      }
      m.tree.append(card);
    }
    m.jumps.replaceChildren(h('span', { text: 'Jump to' }), ...g.roots.flatMap(root => [root, ...root.children.filter(c => g.labels[c.key])]).filter((x, i, a) => a.indexOf(x) === i).slice(0, 10).map(screen =>
      h('button', { type: 'button', class: 'mm-jump', onclick: () => go(screen, { open: true, focus: true }) }, g.labels[screen.key] || screen.title)));
    layout(true);
  }
  function subLine(screen) {
    const parts = [];
    if (screen.path.length > 1) parts.push(screen.path.slice(0, -1).join(' · '));
    else if (screen.depth === 0) parts.push(screen.iface.toUpperCase());
    return parts.join('') || ' ';
  }
  function cardLabel(screen) {
    const bits = [screen.title];
    if (screen.path.length > 1) bits.push('in ' + screen.path.slice(0, -1).join(', '));
    if (screen.via) bits.push(moveText(screen.via) + ' from ' + screen.parent.title);
    if (screen.states.length > 1) bits.push(screen.states.length + ' states');
    if (screen.children.length) bits.push(screen.children.length + ' screen' + (screen.children.length > 1 ? 's' : '') + ' lead on');
    return bits.join('. ');
  }

  // ---------------------------------------------------------------- layout: a tidy tree, left to right
  const shown = screen => { for (let p = screen.parent; p; p = p.parent) if (!p.open) return false; return true; };
  function cardSize(screen) {
    if (!screen.lane || screen.states.length < 2) return { w: CARD_W, h: CARD_H };
    const cols = Math.min(LANE_COLS, screen.states.length), rows = Math.ceil(screen.states.length / cols);
    return { w: Math.max(CARD_W, cols * CELL_W + 16), h: CARD_H + rows * CELL_H + 12 };
  }
  function layout(instant) {
    const m = M, g = G0();
    const visible = g.screens.filter(shown);
    for (const screen of visible) screen.size = cardSize(screen);
    const widths = [];
    for (const screen of visible) widths[screen.depth] = Math.max(widths[screen.depth] || 0, screen.size.w);
    const colX = [0];
    for (let d = 1; d < widths.length; d++) colX[d] = colX[d - 1] + widths[d - 1] + COL_GAP;
    let top = 0;
    const place = screen => {
      const start = top, kids = screen.open ? screen.children : [];
      if (kids.length) {
        for (const kid of kids) place(kid);
        // A parent sits level with its first branch: the map reads from the top left, like an outline.
        screen.y = start;
        top = Math.max(top, screen.y + screen.size.h + ROW_GAP);
      } else { screen.y = start; top = start + screen.size.h + ROW_GAP; }
      screen.x = colX[screen.depth];
    };
    for (const root of g.roots) { if (shown(root)) { place(root); top += 70; } }
    m.bounds = { w: Math.max(...visible.map(x => x.x + x.size.w)), h: Math.max(...visible.map(x => x.y + x.size.h)) };
    const from = new Map(g.screens.map(screen => [screen, screen.drawn]));
    for (const screen of g.screens) {
      const on = visible.includes(screen);
      screen.card.hidden = !on;
      if (screen.label) screen.label.hidden = !on;
      if (screen.wire) screen.wire.style.display = on ? '' : 'none';
      if (screen.twist) {
        const count = descendants(screen);
        screen.card.setAttribute('aria-expanded', String(screen.open));
        screen.twist.textContent = screen.open ? '−' : '+' + count;
        screen.twist.setAttribute('aria-label', (screen.open ? 'Close' : 'Open') + ' the ' + count + ' screen' + (count > 1 ? 's' : '') + ' after ' + screen.title);
      }
      if (screen.laneEl) { screen.laneEl.hidden = !screen.lane; if (screen.lane && !screen.laneEl.childElementCount) fillLane(screen); }
      screen.card.classList.toggle('has-lane', !!screen.lane);
      screen.card.style.width = (screen.size?.w || CARD_W) + 'px';
      screen.card.style.height = (screen.size?.h || CARD_H) + 'px';
    }
    // Set sizes for aria-posinset.
    for (const screen of visible) {
      const sibs = screen.parent ? screen.parent.children : g.roots;
      screen.card.setAttribute('aria-setsize', String(sibs.length)); screen.card.setAttribute('aria-posinset', String(sibs.indexOf(screen) + 1));
    }
    m.svg.setAttribute('width', m.bounds.w + 40); m.svg.setAttribute('height', m.bounds.h + 40);
    const target = new Map(visible.map(screen => [screen, { x: screen.x, y: screen.y }]));
    const draw = t => {
      for (const [screen, end] of target) {
        const begin = from.get(screen) || (screen.parent?.drawn ?? end);
        const x = begin.x + (end.x - begin.x) * t, y = begin.y + (end.y - begin.y) * t;
        screen.at = { x, y };
        screen.card.style.transform = 'translate(' + x + 'px,' + y + 'px)';
      }
      for (const screen of target.keys()) {
        if (screen.label && !screen.wire) { screen.label.style.transform = 'translate(' + screen.at.x + 'px,' + (screen.at.y - 34) + 'px)'; screen.label.style.maxWidth = '420px'; continue; }
        if (screen.label) {
          const ax = screen.at.x, ay = screen.at.y + 8 + THUMB_H / 2;
          screen.label.style.transform = 'translate(' + (ax - COL_GAP + 10) + 'px,' + (ay - 13) + 'px)';
          screen.label.style.maxWidth = (COL_GAP - 20) + 'px';
          if (screen.wire && screen.parent?.at) {
            // One trunk per parent: right from the parent, down the trunk, right to the branch's label and card.
            const p = screen.parent, px = p.at.x + p.size.w, py = p.at.y + 8 + THUMB_H / 2, tx = px + 22, r = Math.min(10, Math.abs(ay - py) / 2), dir = ay > py ? 1 : -1;
            screen.wire.setAttribute('d', Math.abs(ay - py) < 1 ? 'M' + px + ' ' + py + 'H' + ax
              : 'M' + px + ' ' + py + 'H' + (tx - r) + 'Q' + tx + ' ' + py + ' ' + tx + ' ' + (py + r * dir) + 'V' + (ay - r * dir) + 'Q' + tx + ' ' + ay + ' ' + (tx + r) + ' ' + ay + 'H' + ax);
          }
        }
      }
    };
    cancelAnimationFrame(m.layoutFrame);
    if (instant || reduced() || ![...from.values()].some(Boolean)) { draw(1); finish(); }
    else {
      const t0 = performance.now();
      const step = now => { const t = Math.min(1, (now - t0) / 240); draw(1 - Math.pow(1 - t, 3)); if (t < 1) m.layoutFrame = requestAnimationFrame(step); else finish(); };
      m.layoutFrame = requestAnimationFrame(step);
    }
    function finish() { for (const screen of target.keys()) screen.drawn = { x: screen.x, y: screen.y }; drawMini(); }
  }
  const descendants = screen => screen.children.reduce((sum, kid) => sum + 1 + descendants(kid), 0);
  function fillLane(screen) {
    screen.laneEl.append(...screen.states.map((node, i) => {
      const how = screen.how.get(node.id);
      const img = h('img', { alt: '', width: CELL_W - 8, height: 67, decoding: 'async', dataset: { src: node.th } });
      M.io.observe(img);
      return h('button', { type: 'button', class: 'mm-cell', tabindex: '-1', 'aria-label': screen.title + ', state ' + (i + 1) + (how ? ': ' + moveText(how) : ''), onclick: event => { event.stopPropagation(); select(screen, i, { open: true, focus: true }); } },
        img, h('span', { class: 'mm-cell-how' }, i === 0 ? h('span', { class: 'mm-g', text: 'Way in' }) : how ? moveParts(how) : h('span', { class: 'mm-g', text: 'From elsewhere' })));
    }));
  }
  function toggle(screen, open = !screen.open) {
    if (!screen.children.length || screen.open === open) return;
    screen.open = open;
    if (!open && M.selected && !shown(M.selected)) select(screen, 0, { focus: true });
    layout();
  }
  function openAll(open) {
    for (const screen of G0().screens) screen.open = open || screen.depth < 2;
    if (M.selected && !shown(M.selected)) { let up = M.selected; while (!shown(up)) up = up.parent; select(up, 0, { focus: false }); }
    layout();
    fit(true);
  }
  function setLanes(on) {
    M.lanes.setAttribute('aria-pressed', String(on));
    for (const screen of G0().screens) screen.lane = on && screen.states.length > 1;
    layout();
  }

  // ---------------------------------------------------------------- camera
  // The part of the view not under the screen panel (in a narrow window the panel lies over the map).
  const viewSize = () => ({ w: M.view.clientWidth - (M.panelOpen && getComputedStyle(M.panel).position === 'absolute' ? M.panel.offsetWidth : 0), h: M.view.clientHeight });
  function setCam(x, y, k) {
    const m = M;
    if (!m) return;
    k = Math.max(MIN_K, Math.min(MAX_K, k));
    m.cam = { x, y, k };
    m.world.style.transform = 'translate(' + x + 'px,' + y + 'px) scale(' + k + ')';
    m.root.classList.toggle('is-far', k < 0.45);
    m.zoomText.textContent = Math.round(k * 100) + '%';
    if (!m.miniQueued) { m.miniQueued = true; requestAnimationFrame(() => { m.miniQueued = false; drawMini(); }); }
  }
  function stopGlide() { cancelAnimationFrame(M.glide); }
  function glide(x, y, k, instant) {
    const m = M;
    stopGlide();
    if (instant || reduced()) return setCam(x, y, k);
    const a = { ...m.cam }, t0 = performance.now();
    const step = now => { const t = Math.min(1, (now - t0) / 320), e = 1 - Math.pow(1 - t, 3); setCam(a.x + (x - a.x) * e, a.y + (y - a.y) * e, a.k + (k - a.k) * e); if (t < 1) m.glide = requestAnimationFrame(step); };
    m.glide = requestAnimationFrame(step);
  }
  function zoomAt(k, px, py) {
    const m = M, nk = Math.max(MIN_K, Math.min(MAX_K, k));
    setCam(px - (px - m.cam.x) * nk / m.cam.k, py - (py - m.cam.y) * nk / m.cam.k, nk);
  }
  function zoomBy(factor) {
    const v = viewSize(), m = M, nk = Math.max(MIN_K, Math.min(MAX_K, m.cam.k * factor));
    glide(v.w / 2 - (v.w / 2 - m.cam.x) * nk / m.cam.k, v.h / 2 - (v.h / 2 - m.cam.y) * nk / m.cam.k, nk);
  }
  function fitK() { const v = viewSize(), b = M.bounds; return Math.max(MIN_K, Math.min(1, (v.w - 48) / (b.w + 20), (v.h - 48) / (b.h + 20))); }
  function fit(smooth) { const v = viewSize(), b = M.bounds, k = fitK(); glide((v.w - b.w * k) / 2, (v.h - b.h * k) / 2, k, !smooth); }
  // The first view: the whole map when it is readable, else the play screen and its first branches.
  function start() {
    const k = fitK();
    if (k >= 0.5) return fit(false);
    const root = G0().roots[0], v = viewSize(), kk = Math.min(0.85, Math.max(0.5, v.w / 1250));
    setCam(28 - root.x * kk, 26 - root.y * kk, kk);
  }
  // Keep a card in view; glide only when it is near the edge or outside.
  function reveal(screen, instant, center) {
    const m = M, v = viewSize(), k = center ? Math.max(m.cam.k, 0.7) : m.cam.k;
    const x = screen.x * k + m.cam.x, y = screen.y * k + m.cam.y, w = screen.size.w * k, hh = Math.min(screen.size.h * k, v.h - 80);
    if (!center && x > 10 && y > 10 && x + w < v.w - 10 && y + hh < v.h - 10) return;
    glide(v.w / 2 - (screen.x + screen.size.w / 2) * k, Math.min(v.h / 2 - (screen.y + CARD_H / 2) * k, 40 - screen.y * k + Math.max(0, (v.h - 80 - screen.size.h * k) / 2)), k, instant);
  }
  function drawMini() {
    const m = M;
    if (!m || !m.mini.isConnected || !m.bounds) return;
    const c = m.mini, g = G0();
    const ctx = c.getContext('2d'), W = c.width, H = c.height, b = m.bounds;
    const sc = Math.min((W - 12) / b.w, (H - 12) / b.h), ox = (W - b.w * sc) / 2, oy = (H - b.h * sc) / 2;
    c.bounds = { x: -ox / sc, y: -oy / sc, s: sc };
    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = '#0b0c0dee'; ctx.fillRect(0, 0, W, H);
    for (const screen of g.screens) {
      if (screen.card.hidden || !screen.at) continue;
      ctx.fillStyle = screen === m.selected ? '#f3e900' : screen.depth === 0 ? '#9aa19c' : '#4a5150';
      ctx.fillRect(ox + screen.at.x * sc, oy + screen.at.y * sc, Math.max(2, screen.size.w * sc), Math.max(2, screen.size.h * sc));
    }
    const v = viewSize();
    ctx.strokeStyle = '#fff98a'; ctx.lineWidth = 1.2;
    ctx.strokeRect(ox + (-m.cam.x / m.cam.k) * sc, oy + (-m.cam.y / m.cam.k) * sc, v.w / m.cam.k * sc, v.h / m.cam.k * sc);
  }

  // ---------------------------------------------------------------- selection, path, panel
  function select(screen, state = 0, opts = {}) {
    const m = M;
    if (!screen) return;
    if (!shown(screen)) { for (let p = screen.parent; p; p = p.parent) p.open = true; layout(true); }
    if (m.selected?.card) { m.selected.card.setAttribute('aria-selected', 'false'); m.selected.card.tabIndex = -1; m.selected.card.classList.remove('is-sel'); }
    for (const item of m.tree.querySelectorAll('.is-trail')) item.classList.remove('is-trail');
    for (const wire of m.svg.querySelectorAll('.is-trail')) wire.classList.remove('is-trail');
    for (const label of m.labels.querySelectorAll('.is-trail')) label.classList.remove('is-trail');
    m.selected = screen; m.state = Math.max(0, Math.min(screen.states.length - 1, state));
    screen.card.setAttribute('aria-selected', 'true'); screen.card.tabIndex = 0; screen.card.classList.add('is-sel');
    for (let p = screen; p; p = p.parent) { p.card.classList.add('is-trail'); p.wire?.classList.add('is-trail'); p.label?.classList.add('is-trail'); }
    renderCrumbs();
    if (opts.open) m.panelOpen = true;
    if (m.panelOpen) renderPanel();
    if (opts.focus) screen.card.focus({ preventScroll: true });
    if (!opts.stay) reveal(screen, opts.instant, opts.center);
    drawMini();
    m.options.onSelect?.(screen);
  }
  function go(screen, opts = {}) {
    if (screen.iface !== M.iface) build(screen.iface);
    let changed = false;
    for (let p = screen.parent; p; p = p.parent) if (!p.open) { p.open = true; changed = true; }
    if (changed) layout(true);
    select(screen, opts.state || 0, { ...opts, center: true });
  }
  function renderCrumbs() {
    const m = M, trail = [];
    for (let p = m.selected; p; p = p.parent) trail.unshift(p);
    m.crumbs.replaceChildren(...trail.map((screen, i) => h('li', null, i ? h('span', { class: 'mm-sep', 'aria-hidden': 'true', text: '›' }) : null,
      h('button', { type: 'button', 'aria-current': i === trail.length - 1 ? 'location' : null, onclick: () => go(screen, { focus: true }) },
        // The control that opens it, unless it is a tap on the screen's own name.
        i && screen.via && !(screen.via[2] === 'touch' && screen.via[4] === screen.title) ? h('span', { class: 'mm-crumb-how' }, moveParts(screen.via)) : null, h('b', { text: screen.title })))));
  }
  function closePanel() {
    const m = M;
    m.panelOpen = false; m.panel.hidden = true; m.stage.classList.remove('has-panel');
    m.selected?.card.focus({ preventScroll: true });
    drawMini();
  }
  function renderPanel() {
    const m = M, screen = m.selected, node = screen.states[m.state], g = G0();
    m.panel.hidden = false; m.stage.classList.add('has-panel');
    const how = screen.how.get(node.id);
    // The big picture: the screen at full size, its focusable parts outlined, the places a tap leads from marked.
    const shot = h('img', { class: 'mm-big-img', src: node.img, alt: screen.title + (screen.states.length > 1 ? ', state ' + (m.state + 1) : '') + ", on the player's screen", width: node.w, height: node.h, decoding: 'async' });
    const marks = h('div', { class: 'mm-marks' });
    for (const [label, rect] of node.f) {
      if (!rect) continue;
      const box = h('span', { class: 'mm-focus', title: label });
      place(box, rect, node);
      marks.append(box);
    }
    for (const move of [...screen.out, ...screen.inner].filter(mv => mv[0] === node.id && mv[5])) {
      const to = targetOf(move);
      const hot = h('button', { type: 'button', class: 'mm-hot', 'aria-label': moveText(move) + (to ? ' to open ' + to.screen.title : ''), onclick: () => follow(move) });
      place(hot, move[5], node);
      marks.append(hot);
    }
    const big = h('div', { class: 'mm-big' }, shot, marks,
      h('button', { type: 'button', class: 'mm-enlarge', 'aria-label': 'Enlarge the screen', onclick: () => enlarge(node, screen) }, glyph(G.zoom)));
    // States: the screen as the highlight moves or a value changes, with the control that does it.
    let strip = null;
    if (screen.states.length > 1) {
      strip = h('div', { class: 'mm-states' },
        h('div', { class: 'mm-states-head' }, h('b', { text: 'State ' + (m.state + 1) + ' of ' + screen.states.length }),
          how && m.state ? h('span', { class: 'mm-move' }, moveParts(how)) : h('span', { class: 'mm-g', text: m.state ? 'Reached from another screen' : 'As the screen opens' }),
          h('span', { class: 'mm-states-nav' },
            h('button', { type: 'button', class: 'mm-tool', 'aria-label': 'Previous state', disabled: m.state === 0, onclick: () => select(screen, m.state - 1) }, glyph(G.left)),
            h('button', { type: 'button', class: 'mm-tool', 'aria-label': 'Next state', disabled: m.state === screen.states.length - 1, onclick: () => select(screen, m.state + 1) }, glyph(G.right)))),
        h('div', { class: 'mm-strip', role: 'group', 'aria-label': 'States of ' + screen.title }, screen.states.map((item, i) =>
          h('button', { type: 'button', 'aria-pressed': String(i === m.state), 'aria-label': 'State ' + (i + 1), onclick: () => select(screen, i) }, h('img', { src: item.th, alt: '', width: 80, height: 48, loading: 'lazy', decoding: 'async' })))));
    }
    // The ways in and on, each a button that goes there.
    const unique = (moves, end) => { const seen = new Set(); return moves.filter(mv => { const t = end(mv), k = (t ? t.screen.key + t.iface : '') + mv[2] + mv[3] + mv[4]; if (!t || seen.has(k)) return false; seen.add(k); return true; }); };
    const ins = unique(screen.into, mv => sourceOf(mv)), outs = unique(screen.out, mv => targetOf(mv));
    const row = (move, other, dir) => h('li', null, h('button', { type: 'button', class: 'mm-link-row', onclick: () => dir === 'in' ? go(other.screen, { state: other.state, focus: true }) : follow(move) },
      h('img', { src: other.screen.states[other.state].th, alt: '', width: 64, height: 38, loading: 'lazy', decoding: 'async' }),
      h('span', null, h('b', { text: (dir === 'in' ? 'From ' : '') + other.screen.title + (other.iface !== screen.iface ? ' · ' + other.iface.toUpperCase() : '') }), h('span', { class: 'mm-move' }, moveParts(move)))));
    const lists = h('div', { class: 'mm-ways' },
      h('div', null, h('h4', { text: 'How you get here' }), ins.length ? h('ul', null, ins.map(mv => row(mv, sourceOf(mv), 'in'))) : h('p', { class: 'mm-none', text: screen.depth === 0 ? 'Where the player starts.' : 'Only from the screens around it.' })),
      h('div', null, h('h4', { text: 'Where it leads' }), outs.length ? h('ul', null, outs.map(mv => row(mv, targetOf(mv), 'out'))) : h('p', { class: 'mm-none', text: 'Nothing further from here.' })));
    // What the screen offers by name (a tap on an unnamed place is marked on the picture instead).
    const named = node.f.filter(([label, rect]) => label && !(rect && label === 'Tap here'));
    const focusList = named.length ? h('p', { class: 'mm-focusables' }, h('b', { text: 'You can pick: ' }), named.map(([label]) => h('span', { class: 'mm-scr', text: label }))) : null;
    // The manual's section for this screen.
    const chapterNode = node.c ? node : screen.states.find(item => item.c);
    const read = chapterNode ? h('button', { type: 'button', class: 'primary mm-read', onclick: () => { const [slug, anchor] = chapterNode.c.split('#'); m.options.openChapter(slug, anchor || ''); } }, glyph(G.book), h('span', null, h('small', { text: 'Read in the manual' }), h('b', { text: chapterNode.ct }))) : null;
    const prev = g.screens[screen.n - 1], next = g.screens[screen.n + 1];
    const pager = h('div', { class: 'mm-pager', role: 'navigation', 'aria-label': 'Previous and next screen' },
      h('button', { type: 'button', class: 'secondary', disabled: !prev, onclick: () => prev && go(prev, { focus: true }) }, h('small', { text: 'Previous screen' }), h('b', { text: prev ? prev.title : '—' })),
      h('button', { type: 'button', class: 'secondary', disabled: !next, onclick: () => next && go(next, { focus: true }) }, h('small', { text: 'Next screen' }), h('b', { text: next ? next.title : '—' })));
    m.panel.replaceChildren(
      h('div', { class: 'mm-panel-head' },
        h('div', null, h('p', { class: 'mm-eyebrow', text: [screen.iface.toUpperCase(), ...screen.path.slice(0, -1)].join(' · ') }), h('h3', { class: 'mm-panel-title', tabindex: '-1', text: screen.title })),
        h('button', { type: 'button', class: 'mm-tool', 'aria-label': 'Close the screen', onclick: closePanel }, glyph(G.close))),
      ...[big, strip, focusList, read, lists, pager].filter(Boolean));
    m.panel.scrollTop = 0;
  }
  function place(item, rect, node) {
    const [x, y, w, hh] = rect;
    item.style.left = (x / node.w * 100) + '%'; item.style.top = (y / node.h * 100) + '%';
    item.style.width = (w / node.w * 100) + '%'; item.style.height = (hh / node.h * 100) + '%';
  }
  function locate(id) {
    for (const name of ['modern', 'classic']) { const screen = M.graphs[name].screenOf.get(id); if (screen) return { iface: name, screen, state: Math.max(0, screen.states.findIndex(n => n.id === id)) }; }
    return null;
  }
  const targetOf = move => locate(move[1]);
  const sourceOf = move => locate(move[0]);
  function follow(move) {
    const to = targetOf(move);
    if (!to) return;
    if (to.screen === M.selected) return select(to.screen, to.state);
    go(to.screen, { state: to.state, focus: true });
  }
  function enlarge(node, screen) {
    let dialog = document.getElementById('mm-box');
    if (!dialog) {
      dialog = h('dialog', { id: 'mm-box', class: 'm-box', 'aria-label': 'Screen' });
      document.body.append(dialog);
      dialog.addEventListener('click', event => { if (event.target === dialog) dialog.close(); });
    }
    const opener = document.activeElement;
    dialog.replaceChildren(h('img', { class: 'm-box-img', src: node.img, alt: screen.title, width: node.w, height: node.h }),
      h('div', { class: 'm-box-foot' }, h('p', { text: [screen.iface.toUpperCase(), ...screen.path].join(' · ') }), h('button', { class: 'primary', type: 'button', text: 'Close', onclick: () => dialog.close() })));
    dialog.onclose = () => opener?.focus?.({ preventScroll: true });
    dialog.showModal();
  }

  // ---------------------------------------------------------------- keys
  function onTreeKey(event) {
    const m = M, screen = m.selected;
    if (!screen || event.target !== screen.card) return;
    const g = G0(), sibs = screen.parent ? screen.parent.children : g.roots.filter(shown), at = sibs.indexOf(screen);
    const visible = g.screens.filter(shown), vi = visible.indexOf(screen);
    let to = null;
    switch (event.key) {
      case 'ArrowRight': if (screen.children.length && !screen.open) { toggle(screen, true); event.preventDefault(); return; } to = screen.open ? screen.children[0] : null; break;
      case 'ArrowLeft': if (screen.children.length && screen.open && !event.shiftKey) { toggle(screen, false); event.preventDefault(); return; } to = screen.parent; break;
      case 'ArrowDown': to = sibs[at + 1] || visible[vi + 1]; break;
      case 'ArrowUp': to = sibs[at - 1] || visible[vi - 1]; break;
      case 'Home': to = visible[0]; break;
      case 'End': to = visible.at(-1); break;
      case 'Enter': case ' ': event.preventDefault(); if (m.panelOpen && event.key === 'Enter') { m.panel.querySelector('.mm-panel-title')?.focus(); } else { m.panelOpen = true; renderPanel(); drawMini(); } return;
      case 'Escape': if (m.panelOpen) { event.preventDefault(); closePanel(); } return;
      case '[': case ',': if (screen.states.length > 1) { event.preventDefault(); select(screen, m.state - 1); } return;
      case ']': case '.': if (screen.states.length > 1) { event.preventDefault(); select(screen, m.state + 1); } return;
      default: return onViewKey(event);
    }
    event.preventDefault();
    if (to) select(to, 0, { focus: true });
  }
  function onViewKey(event) {
    const m = M, v = viewSize();
    if (event.ctrlKey || event.metaKey || event.altKey) return;
    if (event.key === '+' || event.key === '=') zoomBy(1.25);
    else if (event.key === '-' || event.key === '_') zoomBy(1 / 1.25);
    else if (event.key === '0' || event.key === 'f' || event.key === 'F') fit(true);
    else if (event.shiftKey && event.key.startsWith('Arrow')) { const d = 90; glide(m.cam.x + (event.key === 'ArrowLeft' ? d : event.key === 'ArrowRight' ? -d : 0), m.cam.y + (event.key === 'ArrowUp' ? d : event.key === 'ArrowDown' ? -d : 0), m.cam.k); }
    else if (event.key === 'Escape' && m.panelOpen) closePanel();
    else return;
    event.preventDefault();
    void v;
  }

  // ---------------------------------------------------------------- find a screen
  function runFind() {
    const m = M, query = norm(m.search.value).trim();
    if (!query) return closeFind();
    const terms = query.split(' ');
    const hits = [];
    for (const name of [m.iface, m.iface === 'modern' ? 'classic' : 'modern']) {
      for (const screen of m.graphs[name].screens) {
        const title = norm(screen.title), path = norm(screen.path.join(' ')), picks = norm(screen.states.flatMap(n => n.f.map(f => f[0])).join(' '));
        let score = 0;
        for (const term of terms) {
          const w = ' ' + term;
          if (title.includes(w)) score += 10; else if (path.includes(w)) score += 4; else if (picks.includes(w)) score += 2; else { score = -1; break; }
        }
        if (score < 0) continue;
        if (title.trim() === query) score += 20;
        if (name !== m.iface) score -= 5;
        hits.push({ screen, score, name });
      }
    }
    hits.sort((a, b) => b.score - a.score || a.screen.n - b.screen.n);
    const top = hits.slice(0, 12);
    m.found.replaceChildren(...(top.length ? top.map(({ screen, name }) => h('li', { role: 'option', tabindex: '-1', 'aria-selected': 'false', onclick: () => { closeFind(); go(screen, { open: true, focus: true }); }, onkeydown: event => { if (event.key === 'Enter') { event.preventDefault(); event.currentTarget.click(); } } },
      h('img', { src: screen.states[0].th, alt: '', width: 60, height: 36, loading: 'lazy' }),
      h('span', null, h('b', { text: screen.title }), h('small', { text: [name === m.iface ? '' : name.toUpperCase(), ...screen.path.slice(0, -1)].filter(Boolean).join(' · ') || 'Top level' })))) : [h('li', { class: 'mm-nothing', text: 'No screen is called that. Try a word printed on the screen.' })]));
    m.found.hidden = false; m.search.setAttribute('aria-expanded', 'true');
  }
  function closeFind() { if (!M) return; M.found.hidden = true; M.search.setAttribute('aria-expanded', 'false'); }

  // ---------------------------------------------------------------- outside
  function setIface(name, chosen) {
    const m = M;
    if (!m || !m.graphs[name].screens.length || (name === m.iface && !chosen)) return;
    if (name === m.iface) return;
    // Keep the reader at the same place in the other interface when it has that screen.
    const same = m.selected && m.graphs[name].screens.find(screen => screen.slug === m.selected.slug);
    build(name);
    if (same) go(same, { focus: !!chosen });
    else { start(); select(G0().roots[0], 0, { focus: !!chosen, stay: true }); }
    m.options.onIface?.(name);
  }
  function unmount() {
    if (!M) return;
    for (const off of M.listeners) off();
    M.io?.disconnect(); M.ro?.disconnect();
    cancelAnimationFrame(M.glide); cancelAnimationFrame(M.layoutFrame);
    document.getElementById('mm-box')?.close();
    M = null;
  }
  const api = Object.freeze({
    mount, unmount, setIface,
    focus: (target, iface) => { const t = M && resolve(target, iface); if (t) go(t.screen, { state: t.state, open: true, focus: true }); return !!t; },
    focusSearch: () => { M?.search.focus(); },
    state: () => M ? { iface: M.iface, screen: M.selected?.slug || null, title: M.selected?.title || null, stateIndex: M.state, panel: M.panelOpen, cam: { ...M.cam },
      screens: G0().screens.length, shown: G0().screens.filter(shown).length, lanes: M.lanes.getAttribute('aria-pressed') === 'true' } : null,
    resolve: (target, iface) => { const t = M && resolve(target, iface); return t ? { iface: t.iface, screen: t.screen.slug, state: t.state } : null; },
    moveText
  });
  window.manualMap = api;
})();
