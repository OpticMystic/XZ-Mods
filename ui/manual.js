// The Manual page: the XZ Mods manual inside the app, generated from manual/ by tools/build_manual.py app into
// ui/manual/. This file only shows it. The app's CSP allows no fetch of its own files and no style attributes,
// so chapters arrive as scripts loaded on first use, and positions are set through CSSOM.
// window.openManual(chapterId, anchorId?) opens the page at a chapter and, optionally, a heading in it.
// openManual('menu-map', screen?) opens the menu map (ui/manual-map.js), at a screen when one is named.
'use strict';
(() => {
  const KEY = 'xz-manual/1', BASE = 'manual/';
  const motion = matchMedia('(prefers-reduced-motion: reduce)');
  const reduced = () => motion.matches;
  const $ = id => document.getElementById(id);
  // On screen. (Element.checkVisibility is missing from the macOS 14 web view.)
  const visible = item => !!(item && (item.offsetWidth || item.offsetHeight || item.getClientRects().length));
  const icon = name => { const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg'); svg.setAttribute('class', 'icon'); svg.setAttribute('aria-hidden', 'true'); const use = document.createElementNS('http://www.w3.org/2000/svg', 'use'); use.setAttribute('href', '#' + name); svg.append(use); return svg; };
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
    item.append(...children.flat().filter(child => child !== null && child !== undefined && child !== false && child !== ''));
    return item;
  }
  function svgEl(tag, attrs, ...children) {
    const item = document.createElementNS('http://www.w3.org/2000/svg', tag);
    for (const [key, value] of Object.entries(attrs || {})) item.setAttribute(key, value);
    item.append(...children);
    return item;
  }

  // ---------------------------------------------------------------- saved reading state
  let saved = {};
  try { saved = JSON.parse(localStorage.getItem(KEY)) || {}; } catch { saved = {}; }
  const state = { iface: ['modern', 'classic', 'both'].includes(saved.iface) ? saved.iface : 'both', last: saved.last || null, read: saved.read || {}, ifaceChosen: !!saved.ifaceChosen };
  let saveTimer = 0;
  const save = () => { clearTimeout(saveTimer); saveTimer = setTimeout(() => { try { localStorage.setItem(KEY, JSON.stringify({ iface: state.iface, ifaceChosen: state.ifaceChosen, last: state.last, read: state.read })); } catch {} }, 250); };

  // ---------------------------------------------------------------- loading generated scripts
  const loading = {};
  const load = src => loading[src] || (loading[src] = new Promise((resolve, reject) => {
    const script = document.createElement('script');
    script.src = src;
    script.onload = () => resolve();
    script.onerror = () => { delete loading[src]; script.remove(); reject(Error('The manual file ' + src + ' is missing from this app.')); };
    document.head.append(script);
  }));
  // The explainer clips are optional: without assets/manual/media.js the page shows no clips and the text says it all.
  const loadIndex = () => Promise.all([load(BASE + 'index.js'), load('manual-art.js'), load('assets/manual/media.js').catch(() => {})]).then(() => window.XZ_MANUAL);
  const loadChapter = slug => load(BASE + 'chapters/' + slug + '.js').then(() => window.XZ_MANUAL_CHAPTERS[slug]);
  const loadSearch = () => load(BASE + 'search.js').then(() => window.XZ_MANUAL_SEARCH);
  const loadMap = () => Promise.all([load('manual-map.js'), load(BASE + 'map.js')]).then(() => window.manualMap);

  // ---------------------------------------------------------------- page skeleton
  let root, side, view, bar, input, book = null, current = { view: 'none', slug: null }, built = false;
  const ui = {};

  function build() {
    if (built) return;
    built = true;
    root = $('manual-root');
    root.className = 'm-root';
    input = h('input', { type: 'search', id: 'm-q', placeholder: 'Search the manual', autocomplete: 'off', spellcheck: 'false', 'aria-label': 'Search the manual', 'aria-controls': 'm-view' });
    const search = h('label', { class: 'm-search' }, icon('i-search'), input, h('kbd', { 'aria-hidden': 'true', text: '/' }));
    ui.toggle = h('button', { class: 'm-side-toggle secondary', type: 'button', 'aria-expanded': 'false', 'aria-controls': 'm-side-body' }, icon('i-chevron'), h('span', { text: 'Chapters' }));
    ui.ring = svgEl('circle', { class: 'm-ring-fill', cx: 17, cy: 17, r: 14, 'stroke-dasharray': '87.96', 'stroke-dashoffset': '87.96' });
    ui.progressText = h('b'); ui.progressNote = h('span');
    const progress = h('div', { class: 'm-progress', role: 'status' }, svgEl('svg', { viewBox: '0 0 34 34', 'aria-hidden': 'true' }, svgEl('circle', { class: 'm-ring-track', cx: 17, cy: 17, r: 14 }), ui.ring), h('div', null, ui.progressText, ui.progressNote));
    ui.home = h('button', { class: 'm-home', type: 'button', onclick: () => showStart(true) }, bookGlyph(), h('span', { text: 'Start page' }));
    ui.mapCount = h('small');
    ui.map = h('button', { class: 'm-home m-mapnav', type: 'button', hidden: true, onclick: () => showMap(null, true) }, mapGlyph(), h('span', { text: 'Menu map' }), ui.mapCount);
    ui.toc = h('ol', { class: 'm-toc', 'aria-label': 'Chapters' });
    ui.seg = h('div', { class: 'm-seg', role: 'group', 'aria-label': 'Interface' }, ['modern', 'classic', 'both'].map(name => h('button', { type: 'button', dataset: { iface: name }, 'aria-pressed': String(state.iface === name), text: name === 'both' ? 'Both' : name.toUpperCase(), onclick: () => setIface(name, true) })));
    const iface = h('div', { class: 'm-iface' }, h('p', { text: 'Show the parts for the interface you play on.' }), ui.seg);
    side = h('div', { class: 'm-side', role: 'navigation', 'aria-label': 'Manual' }, search, ui.toggle, h('div', { class: 'm-side-body', id: 'm-side-body' }, progress, ui.home, ui.map, ui.toc, iface));
    bar = h('span');
    view = h('div', { class: 'm-view', id: 'm-view', tabindex: '-1' });
    root.replaceChildren(side, h('div', { class: 'm-main' }, h('div', { class: 'm-bar', 'aria-hidden': 'true' }, bar), view));
    ui.toggle.onclick = () => { const open = !side.classList.contains('is-open'); side.classList.toggle('is-open', open); ui.toggle.setAttribute('aria-expanded', String(open)); };
    input.addEventListener('input', () => { clearTimeout(ui.searchTimer); ui.searchTimer = setTimeout(runSearch, 90); });
    input.addEventListener('focus', () => loadSearch().catch(() => {}), { once: true });
    input.addEventListener('keydown', event => {
      if (event.key === 'Escape' && input.value) { event.preventDefault(); input.value = ''; runSearch(); }
      else if (event.key === 'Enter') { event.preventDefault(); view.querySelector('.m-hit')?.click(); }
      else if (event.key === 'ArrowDown') { const first = view.querySelector('.m-hit'); if (first) { event.preventDefault(); first.focus(); } }
    });
    ui.toc.addEventListener('keydown', event => {
      if (!['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) return;
      const items = [...ui.toc.querySelectorAll('button')].filter(visible);
      const at = items.indexOf(document.activeElement);
      if (at < 0) return;
      event.preventDefault();
      const next = event.key === 'Home' ? 0 : event.key === 'End' ? items.length - 1 : Math.max(0, Math.min(items.length - 1, at + (event.key === 'ArrowDown' ? 1 : -1)));
      items[next].focus();
    });
    view.addEventListener('click', onViewClick);
    applyIface();
  }
  function bookGlyph() {
    return svgEl('svg', { viewBox: '0 0 24 24', 'aria-hidden': 'true', fill: 'none', stroke: 'currentColor', 'stroke-width': '1.8', 'stroke-linecap': 'round', 'stroke-linejoin': 'round' },
      svgEl('path', { d: 'M3.5 12 12 4.5l8.5 7.5' }), svgEl('path', { d: 'M5.5 10.5v9h13v-9' }));
  }
  // Three screens joined like branches: the menu map.
  function mapGlyph() {
    return svgEl('svg', { viewBox: '0 0 24 24', 'aria-hidden': 'true', fill: 'none', stroke: 'currentColor', 'stroke-width': '1.8', 'stroke-linecap': 'round', 'stroke-linejoin': 'round' },
      svgEl('path', { d: 'M2.5 9.5h6v5h-6zM15.5 3.5h6v5h-6zM15.5 15.5h6v5h-6z' }), svgEl('path', { d: 'M8.5 12h3.5V6h3.5M12 12v6h3.5' }));
  }

  // ---------------------------------------------------------------- chapter list and progress
  const fraction = slug => Math.max(0, Math.min(1, state.read[slug] || 0));
  const done = slug => fraction(slug) >= 0.9;
  function renderToc() {
    if (!book) return;
    const groups = [];
    for (const chapter of book.chapters) {
      if (!groups.length || groups.at(-1).name !== chapter.group) groups.push({ name: chapter.group, list: [] });
      groups.at(-1).list.push(chapter);
    }
    ui.toc.replaceChildren(...groups.map(group => h('li', { class: 'm-toc-group' }, h('p', { text: group.name }), h('ol', null, group.list.map(chapter => {
      const here = current.view === 'chapter' && current.slug === chapter.slug;
      const button = h('button', { class: 'm-toc-ch', type: 'button', dataset: { slug: chapter.slug }, 'aria-current': here ? 'page' : null, onclick: () => openChapter(chapter.slug, '', { focus: true }) },
        dot(chapter.slug), h('span', { text: chapter.title }));
      if (done(chapter.slug)) button.dataset.done = '';
      const sections = here ? chapter.sections.filter(([level]) => level === 2) : [];
      return h('li', null, button, sections.length ? h('ol', { class: 'm-toc-secs', 'aria-label': chapter.title + ' sections' }, sections.map(([, anchor, title, iface]) =>
        h('li', iface ? { dataset: { iface } } : null, h('button', { class: 'm-toc-sec', type: 'button', dataset: { anchor }, text: title, onclick: () => openChapter(chapter.slug, anchor, { focus: true }) })))) : null);
    })))));
    ui.home.setAttribute('aria-current', current.view === 'start' ? 'page' : 'false');
    ui.map.hidden = !book.map;
    ui.map.setAttribute('aria-current', current.view === 'map' ? 'page' : 'false');
    if (book.map) ui.mapCount.textContent = String(book.map.screens[state.iface === 'classic' && book.map.screens.classic ? 'classic' : 'modern']);
    renderProgress();
  }
  function dot(slug) {
    const fill = svgEl('circle', { class: 'm-dot-fill', cx: 7, cy: 7, r: 5.5, 'stroke-dasharray': '34.56', 'stroke-dashoffset': String(34.56 * (1 - fraction(slug))), transform: 'rotate(-90 7 7)' });
    return svgEl('svg', { class: 'm-toc-dot', viewBox: '0 0 14 14', 'aria-hidden': 'true' }, svgEl('circle', { class: 'm-dot-track', cx: 7, cy: 7, r: 5.5 }), fill, svgEl('path', { class: 'm-dot-done', d: 'M4.3 7.2l1.8 1.8 3.6-3.8' }));
  }
  function renderProgress() {
    if (!book) return;
    const total = book.chapters.length, read = book.chapters.filter(chapter => done(chapter.slug)).length;
    const share = book.chapters.reduce((sum, chapter) => sum + fraction(chapter.slug), 0) / total;
    ui.ring.setAttribute('stroke-dashoffset', String(87.96 * (1 - share)));
    ui.progressText.textContent = Math.round(share * 100) + '% read';
    ui.progressNote.textContent = read + ' of ' + total + ' chapters finished';
  }

  // ---------------------------------------------------------------- interface filter
  function setIface(name, chosen) {
    state.iface = name;
    if (chosen) state.ifaceChosen = true;
    save();
    applyIface();
    if (current.view === 'start') for (const button of view.querySelectorAll('.m-pick button')) button.setAttribute('aria-pressed', String(button.dataset.iface === name));
    if (current.view === 'search') runSearch();
    if (name !== 'both') for (const figure of view.querySelectorAll('.mf[data-look]')) setLook(figure, name);
    if (current.view === 'map' && name !== 'both') window.manualMap?.setIface(name);
    if (book) renderToc();
  }
  function applyIface() {
    if (!root) return;
    root.classList.toggle('m-if-modern', state.iface === 'modern');
    root.classList.toggle('m-if-classic', state.iface === 'classic');
    for (const button of ui.seg.children) button.setAttribute('aria-pressed', String(button.dataset.iface === state.iface));
  }

  // ---------------------------------------------------------------- views
  function leaveMap() {
    window.manualMap?.unmount();
    root.classList.remove('m-map-on');
  }
  function swap(nodes, kind) {
    window.manualArt?.stopAll();
    leaveMap();
    view.replaceChildren(...nodes);
    view.classList.remove('m-enter');
    if (!reduced()) { void view.offsetWidth; view.classList.add('m-enter'); }
    current.view = kind;
  }
  function scrollToView(top) {
    const target = top ? $('manual').getBoundingClientRect().top + scrollY - 16 : scrollY;
    scrollTo({ top: Math.max(0, target), behavior: 'auto' });
  }

  async function showStart(focus) {
    build();
    try { book = await loadIndex(); } catch (error) { return fail(error); }
    current.slug = null;
    const first = book.chapters[0], last = state.last && book.chapters.find(chapter => chapter.slug === state.last.slug);
    const lastSection = last && state.last.anchor ? last.sections.find(([, anchor]) => anchor === state.last.anchor) : null;
    const actions = h('div', { class: 'm-hero-actions' },
      h('button', { class: 'primary', type: 'button', text: 'Start with ' + first.title, onclick: () => openChapter(first.slug, '', { focus: true }) }),
      h('button', { class: 'secondary', type: 'button', text: 'Search the manual', onclick: () => { input.focus(); side.classList.add('is-open'); } }));
    // The hero picture is a render of the true-scale model of the XDJ-XZ (ui/manual-art.js), above the title.
    const hero = h('header', { class: 'm-hero' }, window.manualArt?.hero() || null,
      h('div', { class: 'm-hero-body' }, h('div', null, h('p', { class: 'mc-eyebrow', text: 'XZ Mods manual' }), h('h2', { tabindex: '-1', class: 'm-start-title', text: book.title }), h('p', { class: 'm-lede', text: book.summary })), actions));
    const resume = last ? h('button', { class: 'm-continue', type: 'button', onclick: () => openChapter(last.slug, state.last.anchor || '', { focus: true }) },
      ring(fraction(last.slug)), h('div', null, h('small', { text: 'Continue where you left off' }), h('b', { text: last.title + (lastSection ? ' · ' + lastSection[2] : '') }), h('span', { text: Math.round(fraction(last.slug) * 100) + '% of this chapter read' })), icon('i-chevron')) : null;
    const pick = h('div', { class: 'm-pick', role: 'group', 'aria-label': 'Which interface do you play on?' },
      [['modern', 'MODERN', 'XZ Mods draws the whole play screen.', ['assets/wizard/new-interface-poster.webp']],
        ['classic', 'CLASSIC', "The player's own play screen with MODS added.", ['assets/wizard/standard-interface.webp']],
        ['both', 'Both', 'Show every part of the manual, each marked with its interface.', ['assets/wizard/new-interface-poster.webp', 'assets/wizard/standard-interface.webp']]].map(([name, title, text, pictures]) =>
        h('button', { type: 'button', dataset: { iface: name }, 'aria-pressed': String(state.iface === name), onclick: () => setIface(name, true) },
          pictures.length > 1 ? h('span', { class: 'm-pick-both' }, pictures.map(src => h('img', { src, alt: '', loading: 'lazy' }))) : h('img', { src: pictures[0], alt: '', loading: 'lazy' }),
          h('b', { text: title }), h('span', { text }))));
    const tour = h('ul', { class: 'mt-grid' }, book.tour.map(item => {
      const [slug, anchor] = item.link.split('#');
      return h('li', null, h('button', { class: 'mt-card', type: 'button', onclick: () => openChapter(slug, anchor || '', { focus: true }) },
        h('span', { class: 'mt-art', 'aria-hidden': 'true' }, h('img', { src: item.img, alt: '', width: item.w, height: item.h, loading: 'lazy', decoding: 'async' })),
        h('b', null, item.title, item.tag ? h('span', { class: 'tag tag--' + item.tag, text: item.tag }) : null),
        h('span', { class: 'mt-text', text: item.text })));
    }));
    // The menu map: a doorway with real screens from it, the play screen first.
    const mapCard = book.map ? h('button', { class: 'm-mapcard', type: 'button', onclick: () => showMap(null, true) },
      h('span', { class: 'm-mapcard-art', 'aria-hidden': 'true' }, book.map.preview.flatMap(step => [
        step.move ? h('span', { class: 'm-mapcard-move' }, step.move[0] === 'touch' ? [h('span', { class: 'mm-g', text: 'Tap' }), h('span', { class: 'mm-scr', text: step.move[2] })] : h('kbd', { text: step.move[0] }), h('i')) : null,
        h('span', { class: 'm-mapcard-screen' }, h('img', { src: step.th, alt: '', width: 240, height: 144, loading: 'lazy', decoding: 'async' }), h('b', { text: step.t }))])),
      h('span', { class: 'm-mapcard-text' }, h('small', { text: 'Menu map' }), h('b', { text: 'Every screen, and how to get there' }),
        h('span', { text: book.map.screens.modern + ' screens drawn by the firmware, laid out from the play screen. Follow any path, see the control that opens each screen, and jump to the part of the manual that explains it.' }),
        h('span', { class: 'm-mapcard-go' }, 'Open the menu map', icon('i-chevron')))) : null;
    const howto = h('div', { class: 'm-howto' },
      h('div', null, h('span', { class: 'mf-chip', text: '1' }), h('b', { text: 'Numbered points' }), 'Point at a number on a picture, or tab to it, to see what it is. Play steps through them all.'),
      h('div', null, h('kbd', { text: 'SHIFT' }), h('b', { text: 'Keys on the player' }), 'A key drawn like this is a button on the XDJ-XZ, named as it is printed there.'),
      h('div', null, h('span', { class: 'tag tag--experimental', text: 'experimental' }), h('b', { text: 'Experimental' }), 'Works, but test it with your own tracks before you rely on it in a set.'));
    const groups = [];
    for (const chapter of book.chapters) {
      if (!groups.length || groups.at(-1).name !== chapter.group) groups.push({ name: chapter.group, list: [] });
      groups.at(-1).list.push(chapter);
    }
    const path = h('ol', { class: 'm-path' }, groups.map(group => h('li', null, h('p', { text: group.name }), h('ol', null, group.list.map(chapter => {
      const meter = h('i');
      meter.style.transform = 'scaleX(' + fraction(chapter.slug) + ')';
      return h('li', null, h('button', { class: 'm-path-ch', type: 'button', onclick: () => openChapter(chapter.slug, '', { focus: true }) },
        chapter.thumb ? h('img', { src: chapter.thumb, alt: '', loading: 'lazy', width: 96, height: 58 }) : h('span', { class: 'm-blank' }),
        h('span', null, h('b', { text: chapter.title }), h('span', { text: chapter.minutes + ' min · ' + chapter.summary }), h('span', { class: 'm-meter' }, meter))));
    })))));
    swap([h('div', { class: 'm-start' },
      book.draft ? h('p', { class: 'm-draft', role: 'note', text: 'Draft copy: this manual has not been checked against this build yet.' }) : null,
      hero, resume,
      h('h3', { class: 'm-h3', text: 'Which interface do you play on?' }), pick,
      mapCard ? h('h3', { class: 'm-h3', text: 'Find your way around the player' }) : null, mapCard,
      h('h3', { class: 'm-h3', text: 'What XZ Mods adds' }), tour,
      h('h3', { class: 'm-h3', text: 'Reading this manual' }), howto,
      h('h3', { class: 'm-h3', text: 'The guide, chapter by chapter' }), path)], 'start');
    renderToc();
    bar.style.transform = 'scaleX(0)';
    window.manualArt?.startHero(view);
    scrollToView(true);
    if (focus) view.querySelector('.m-start-title').focus({ preventScroll: true });
  }
  function ring(share) {
    return svgEl('svg', { class: 'm-ring-small', viewBox: '0 0 34 34', 'aria-hidden': 'true' }, svgEl('circle', { class: 'm-ring-track', cx: 17, cy: 17, r: 14, fill: 'none', stroke: '#26292a', 'stroke-width': 4 }),
      svgEl('circle', { cx: 17, cy: 17, r: 14, fill: 'none', stroke: '#f3e900', 'stroke-width': 4, 'stroke-linecap': 'round', 'stroke-dasharray': '87.96', 'stroke-dashoffset': String(87.96 * (1 - share)) }));
  }

  async function openChapter(slug, anchor, options = {}) {
    build();
    try { book = await loadIndex(); } catch (error) { return fail(error); }
    const index = book.chapters.findIndex(chapter => chapter.slug === slug);
    if (index < 0) return showStart(options.focus);
    let html;
    try { html = await loadChapter(slug); } catch (error) { return fail(error); }
    if (input.value && !options.keepSearch) input.value = '';
    const article = h('article', { class: 'm-chapter', 'aria-labelledby': 'm-title-' + slug });
    article.innerHTML = html;
    article.querySelector('.mc-title').id = 'm-title-' + slug;
    const chapter = book.chapters[index];
    const meta = h('p', { class: 'm-meta' }, h('span', { text: chapter.minutes + ' min read' }), h('span', { text: 'Chapter ' + (index + 1) + ' of ' + book.chapters.length }));
    article.querySelector('.mc-head').append(meta);
    const prev = book.chapters[index - 1], next = book.chapters[index + 1];
    const pager = h('div', { class: 'm-pager', role: 'navigation', 'aria-label': 'Previous and next chapter' },
      prev ? h('button', { class: 'm-prev', type: 'button', onclick: () => openChapter(prev.slug, '', { focus: true }) }, h('small', { text: 'Previous' }), h('b', { text: prev.title }), h('span', { text: prev.summary })) : null,
      next ? h('button', { class: 'm-next', type: 'button', onclick: () => openChapter(next.slug, '', { focus: true }) }, h('small', { text: 'Next' }), h('b', { text: next.title }), h('span', { text: next.summary })) : null);
    swap([article, pager], 'chapter');
    current.slug = slug;
    enhance(article);
    // Sections the menu map shows offer to open it at that screen.
    const onMap = book.map?.sections || {};
    for (const heading of article.querySelectorAll('h3[data-anchor],h4[data-anchor]')) {
      const key = onMap[slug + '#' + heading.dataset.anchor];
      if (key) heading.append(h('button', { class: 'm-onmap', type: 'button', dataset: { map: key }, 'aria-label': 'Show ' + heading.textContent.trim() + ' on the menu map' }, mapGlyph(), h('span', { text: 'On the menu map' })));
    }
    renderToc();
    side.classList.remove('is-open'); ui.toggle.setAttribute('aria-expanded', 'false');
    const target = anchor && article.querySelector(['h3', 'h4', 'h5'].map(tag => tag + '[data-anchor="' + CSS.escape(anchor) + '"]').join());
    if (target) {
      target.scrollIntoView({ block: 'start', behavior: 'auto' });
      if (!reduced()) { target.classList.remove('m-flash'); void target.offsetWidth; target.classList.add('m-flash'); }
      if (options.focus) { target.tabIndex = -1; target.focus({ preventScroll: true }); }
    } else {
      scrollToView(true);
      if (options.focus) article.querySelector('.mc-title').focus({ preventScroll: true });
    }
    state.last = { slug, anchor: anchor || '' };
    save();
    onScroll();
  }

  async function showMap(target, focus) {
    build();
    try { book = await loadIndex(); if (!book.map) return showStart(focus); await loadMap(); } catch (error) { return fail(error); }
    if (input.value) input.value = '';
    const host = h('div', { class: 'm-map' });
    swap([host], 'map');
    root.classList.add('m-map-on');
    current.slug = null;
    renderToc();
    bar.style.transform = 'scaleX(0)';
    side.classList.remove('is-open'); ui.toggle.setAttribute('aria-expanded', 'false');
    window.manualMap.mount(host, { target: target || null, iface: state.iface === 'classic' ? 'classic' : 'modern',
      openChapter: (slug, anchor) => openChapter(slug, anchor, { focus: true }) });
    scrollToView(true);
    if (focus && !target) host.querySelector('.mm-h').focus({ preventScroll: true });
  }

  function fail(error) {
    build();
    swap([h('p', { class: 'm-error', role: 'alert', text: String(error.message || error) })], 'error');
  }

  // ---------------------------------------------------------------- figures, galleries, links
  function enhance(article) {
    for (const figure of article.querySelectorAll('.mf')) figureBehaviour(figure);
    for (const button of article.querySelectorAll('.mg-shot')) button.onclick = () => lightbox(button);
    window.manualArt?.enhance(article, { reduced });
    if (!reduced() && 'IntersectionObserver' in window) {
      // Once built, the stagger delays go, so pointing at a number answers at once.
      const observer = new IntersectionObserver(entries => { for (const entry of entries) if (entry.isIntersecting) { const item = entry.target; item.classList.add('is-in'); observer.unobserve(item); setTimeout(() => item.classList.add('is-built'), 1500); } }, { threshold: 0.12 });
      root.classList.add('m-anim');
      for (const item of article.querySelectorAll('.mf,.mg,.mx')) observer.observe(item);
    } else root.classList.remove('m-anim');
  }

  function figureBehaviour(figure) {
    const stage = figure.querySelector('.mf-stage'), marks = [...figure.querySelectorAll('.mf-mark')];
    const legend = figure.querySelector('.mf-legend');
    const zoomable = figure.querySelector('.mf-stage img');
    marks.forEach((mark, i) => { mark.style.left = mark.dataset.x + '%'; mark.style.top = mark.dataset.y + '%'; mark.style.setProperty('--i', i); });
    [...(figure.querySelector('.mf-legend')?.children || [])].forEach((row, i) => row.style.setProperty('--i', i));
    if (marks[0]) marks[0].tabIndex = 0;
    const tip = h('div', { class: 'mf-tip', 'aria-hidden': 'true' });
    if (marks.length) stage.append(tip);
    let active = null, playing = null, steps = null;
    const activate = (n, from) => {
      active = n;
      const mark = marks.find(item => item.dataset.n === n);
      for (const item of marks) item.classList.toggle('is-on', item === mark);
      for (const row of legend?.children || []) row.classList.toggle('is-on', row.dataset.n === n);
      if (steps) [...steps.children].forEach((item, i) => { const at = marks.indexOf(mark); item.classList.toggle('is-on', i === at); item.classList.toggle('is-done', i < at); });
      if (!mark) { stage.classList.remove('has-spot'); tip.classList.remove('is-on'); return; }
      stage.style.setProperty('--sx', mark.dataset.x + '%');
      stage.style.setProperty('--sy', mark.dataset.y + '%');
      stage.classList.add('has-spot');
      const row = legend?.querySelector('[data-n="' + CSS.escape(n) + '"]');
      tip.replaceChildren(h('b', { text: n + '. ' + (row?.querySelector('b')?.textContent || '') }), row?.querySelector('div > span')?.cloneNode(true) || '');
      const x = Number(mark.dataset.x), y = Number(mark.dataset.y);
      tip.style.left = tip.style.right = tip.style.top = tip.style.bottom = '';
      if (x > 55) tip.style.right = (100 - x + 4) + '%'; else tip.style.left = (x + 4) + '%';
      if (y > 60) tip.style.bottom = (100 - y + 5) + '%'; else tip.style.top = (y + 5) + '%';
      tip.classList.add('is-on');
      if (from === 'key') for (const item of marks) item.tabIndex = item === mark ? 0 : -1;
    };
    const clear = () => { if (playing) return; activate(null); };
    for (const mark of marks) {
      mark.addEventListener('mouseenter', () => { stop(); activate(mark.dataset.n); });
      mark.addEventListener('focus', () => { stop(); activate(mark.dataset.n, 'key'); });
      mark.addEventListener('mouseleave', clear);
      mark.addEventListener('blur', clear);
      mark.addEventListener('click', () => { stop(); activate(mark.dataset.n, 'key'); });
      mark.addEventListener('keydown', event => {
        const at = marks.indexOf(mark);
        const to = { ArrowRight: at + 1, ArrowDown: at + 1, ArrowLeft: at - 1, ArrowUp: at - 1, Home: 0, End: marks.length - 1 }[event.key];
        if (event.key === 'Escape') { mark.blur(); return; }
        if (to === undefined) return;
        event.preventDefault();
        marks[(to + marks.length) % marks.length].focus();
      });
    }
    for (const row of legend?.children || []) {
      row.addEventListener('mouseenter', () => { stop(); activate(row.dataset.n); });
      row.addEventListener('mouseleave', clear);
    }
    // Play steps through the numbered points; any pointer or key use stops it.
    const tools = h('div', { class: 'mf-tools' });
    if (marks.length > 1) {
      steps = h('span', { class: 'mf-steps', 'aria-hidden': 'true' }, marks.map(() => h('i')));
      const play = h('button', { class: 'mf-tool', type: 'button', 'aria-pressed': 'false', 'aria-label': 'Step through the numbered points' }, playGlyph(), h('span', { text: 'Play' }));
      play.onclick = () => playing ? stop() : start();
      const label = play.querySelector('span');
      function start() {
        let i = marks.findIndex(mark => mark.dataset.n === active);
        const tick = () => {
          i = (i + 1) % marks.length;
          if (!figure.isConnected) return stop();
          activate(marks[i].dataset.n);
          playing = setTimeout(i === marks.length - 1 ? () => { stop(); activate(null); } : tick, 2900);
        };
        playing = -1; play.setAttribute('aria-pressed', 'true'); label.textContent = 'Pause'; tick();
      }
      figure.stopPlay = stop;
      function stop() {
        if (!playing) return;
        clearTimeout(playing); playing = null; play.setAttribute('aria-pressed', 'false'); label.textContent = 'Play';
        if (steps) for (const item of steps.children) item.classList.remove('is-on', 'is-done');
      }
      tools.append(steps, play);
    }
    // A MODS page drawn in both interfaces shows one look at a time; the reader's interface picks it.
    const looks = [...stage.querySelectorAll('img[data-iface]')];
    if (looks.length > 1) {
      figure.dataset.look = state.iface === 'classic' ? 'classic' : 'modern';
      const seg = h('span', { class: 'mf-look', role: 'group', 'aria-label': 'Interface shown' }, ['modern', 'classic'].map(name =>
        h('button', { type: 'button', dataset: { look: name }, 'aria-pressed': String(figure.dataset.look === name), text: name.toUpperCase(), onclick: () => setLook(figure, name) })));
      tools.prepend(seg);
    }
    if (zoomable) {
      const zoom = h('button', { class: 'mf-tool', type: 'button', 'aria-label': 'Enlarge the picture' }, zoomGlyph());
      zoom.onclick = () => lightbox(null, figure);
      tools.append(zoom);
    }
    figure.querySelector('.mf-bar').append(tools);
    function stop() { figure.stopPlay?.(); }
  }
  function setLook(figure, name) {
    figure.dataset.look = name;
    for (const button of figure.querySelectorAll('.mf-look button')) button.setAttribute('aria-pressed', String(button.dataset.look === name));
  }
  const glyph = (d, filled) => svgEl('svg', { viewBox: '0 0 16 16', 'aria-hidden': 'true', fill: filled ? 'currentColor' : 'none', stroke: filled ? 'none' : 'currentColor', 'stroke-width': '1.6', 'stroke-linecap': 'round' }, svgEl('path', { d }));
  const playGlyph = () => glyph('M4.5 3v10l8-5z', true);
  const zoomGlyph = () => glyph('M9.5 2.5h4v4M6.5 13.5h-4v-4M13.5 2.5 9 7M2.5 13.5 7 9');

  // One dialog shows a gallery picture or a figure at full size; arrows move through the gallery.
  function lightbox(button, figure) {
    let dialog = $('m-box');
    if (!dialog) {
      dialog = h('dialog', { id: 'm-box', class: 'm-box', 'aria-label': 'Picture' });
      document.body.append(dialog);
      dialog.addEventListener('click', event => { if (event.target === dialog) dialog.close(); });
    }
    const items = button ? [...button.closest('.mg').querySelectorAll('.mg-shot')].filter(visible) : [];
    let at = items.indexOf(button);
    const img = h('img', { class: 'm-box-img', alt: '' }), caption = h('p', { 'aria-live': 'polite' });
    const show = () => {
      if (button) { const item = items[at]; img.src = item.dataset.zoom; img.alt = item.getAttribute('aria-label').replace(/^Enlarge: /, ''); caption.innerHTML = item.parentElement.querySelector('figcaption').innerHTML; }
      else { const picture = [...figure.querySelectorAll('.mf-stage img')].find(visible); img.src = picture.src; img.alt = picture.alt; caption.innerHTML = figure.querySelector('figcaption')?.innerHTML || ''; }
    };
    const step = by => { at = (at + by + items.length) % items.length; show(); };
    const foot = h('div', { class: 'm-box-foot' }, caption,
      items.length > 1 ? h('button', { class: 'secondary', type: 'button', text: 'Previous', onclick: () => step(-1) }) : null,
      items.length > 1 ? h('button', { class: 'secondary', type: 'button', text: 'Next', onclick: () => step(1) }) : null,
      h('button', { class: 'primary', type: 'button', text: 'Close', onclick: () => dialog.close() }));
    dialog.replaceChildren(img, foot);
    dialog.onkeydown = event => { if (items.length > 1 && (event.key === 'ArrowRight' || event.key === 'ArrowLeft')) { event.preventDefault(); step(event.key === 'ArrowRight' ? 1 : -1); } };
    const opener = document.activeElement;
    dialog.onclose = () => opener?.focus?.({ preventScroll: true });
    show();
    dialog.showModal();
  }

  function onViewClick(event) {
    const toMap = event.target.closest('[data-map]');
    if (toMap && view.contains(toMap) && current.view === 'chapter') { event.preventDefault(); return showMap(toMap.dataset.map, true); }
    const link = event.target.closest('a.m-link');
    if (!link) return;
    event.preventDefault();
    openChapter(link.dataset.chapter, link.dataset.anchor || '', { focus: true });
  }

  // ---------------------------------------------------------------- search
  const norm = text => ' ' + String(text || '').toLowerCase().replace(/[‘’`]/g, "'").replace(/[^a-z0-9'/+.-]+/g, ' ').replace(/\s+/g, ' ').trim() + ' ';
  let normed = null;
  function find(query) {
    const entries = window.XZ_MANUAL_SEARCH || [];
    normed ||= entries.map(entry => ({ entry, t: norm(entry.t), x: norm(entry.x) }));
    const phrase = norm(query).trim(), terms = phrase.split(' ').filter(term => term.length > 0);
    if (!terms.length) return [];
    const found = [];
    for (const item of normed) {
      if (item.entry.i && state.iface !== 'both' && item.entry.i !== state.iface) continue;
      let score = 0, ok = true;
      for (const term of terms) {
        const word = ' ' + term;
        const inTitle = item.t.includes(word), inText = item.x.includes(word);
        if (!inTitle && !inText) { ok = false; break; }
        score += (inTitle ? 8 : 0) + (inText ? 1 + Math.min(5, item.x.split(word).length - 2) : 0);
      }
      if (!ok) continue;
      if (terms.length > 1) score += (item.t.includes(' ' + phrase + ' ') ? 14 : 0) + (item.x.includes(' ' + phrase) ? 6 : 0);
      if (!item.entry.a) score += item.t.includes(' ' + phrase) ? 10 : -4;
      found.push({ ...item.entry, score });
    }
    return found.sort((a, b) => b.score - a.score).slice(0, 40);
  }
  // Text with every search term marked, built as nodes so the typed text is never parsed as HTML.
  function marked(text, terms) {
    const pattern = terms.length ? new RegExp('(' + terms.map(term => term.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('|') + ')', 'ig') : null;
    if (!pattern) return [text];
    return text.split(pattern).map((part, i) => i % 2 ? h('mark', { text: part }) : part);
  }
  function snippet(text, terms) {
    const lower = text.toLowerCase();
    let at = -1;
    for (const term of terms) { const i = lower.indexOf(term); if (i >= 0 && (at < 0 || i < at)) at = i; }
    if (at < 0) return text.slice(0, 180) + (text.length > 180 ? '…' : '');
    const start = Math.max(0, text.lastIndexOf(' ', Math.max(0, at - 70)));
    return (start > 0 ? '…' : '') + text.slice(start, start + 200).trim() + (start + 200 < text.length ? '…' : '');
  }
  async function runSearch() {
    const query = input.value.trim();
    if (!query) { if (current.view === 'search') current.back ? current.back() : showStart(); return; }
    try { await loadSearch(); book = await loadIndex(); } catch (error) { return fail(error); }
    if (input.value.trim() !== query) return;
    if (current.view !== 'search') { const before = current.view === 'chapter' ? { slug: current.slug, y: scrollY } : null; current.back = () => before ? openChapter(before.slug, '').then(() => scrollTo({ top: before.y, behavior: 'auto' })) : showStart(); }
    const terms = norm(query).trim().split(' ').filter(term => term.length > 1);
    const results = find(query);
    const titles = Object.fromEntries(book.chapters.map(chapter => [chapter.slug, chapter.title]));
    const list = h('ul', { class: 'm-results' }, results.map(result => h('li', null, h('button', { class: 'm-hit', type: 'button', dataset: { slug: result.c, anchor: result.a }, onclick: () => openChapter(result.c, result.a, { focus: true }) },
      h('span', { class: 'm-hit-path', text: titles[result.c] + (result.i ? ' · ' + result.i.toUpperCase() : '') }),
      h('span', { class: 'm-hit-title' }, marked(result.t, terms)),
      result.x ? h('span', { class: 'm-hit-text' }, marked(snippet(result.x, terms), terms)) : null))));
    list.addEventListener('keydown', event => {
      if (event.key !== 'ArrowDown' && event.key !== 'ArrowUp') return;
      const hits = [...list.querySelectorAll('.m-hit')], at = hits.indexOf(document.activeElement);
      event.preventDefault();
      if (event.key === 'ArrowUp' && at <= 0) return input.focus();
      hits[Math.min(hits.length - 1, at + (event.key === 'ArrowDown' ? 1 : -1))]?.focus();
    });
    const head = h('div', { class: 'm-results-head' }, h('h2', { text: 'Search' }), h('p', { role: 'status', text: results.length ? results.length + (results.length === 40 ? '+' : '') + ' places mention “' + query + '”' : 'Nothing in the manual mentions “' + query + '”' }));
    leaveMap();
    view.replaceChildren(head, results.length ? list : h('p', { class: 'm-empty', text: 'Try a shorter word, the name printed on a button, or a message shown on the screen.' }));
    window.manualArt?.stopAll();
    current.view = 'search';
    renderToc();
    scrollToView(true);
  }

  // ---------------------------------------------------------------- reading position
  let scrollQueued = false;
  function onScroll() {
    if (scrollQueued) return;
    scrollQueued = true;
    requestAnimationFrame(() => {
      scrollQueued = false;
      if (current.view !== 'chapter' || $('manual')?.classList.contains('hidden')) return;
      const article = view.querySelector('.m-chapter');
      if (!article) return;
      const box = article.getBoundingClientRect();
      const share = Math.max(0, Math.min(1, (innerHeight - box.top) / Math.max(1, box.height)));
      bar.style.transform = 'scaleX(' + share + ')';
      const slug = current.slug;
      if (share > (state.read[slug] || 0)) { const wasDone = done(slug); state.read[slug] = Math.round(share * 1000) / 1000; if (done(slug) !== wasDone) renderToc(); else updateDot(slug); renderProgress(); }
      let here = '';
      for (const heading of article.querySelectorAll('h3[data-anchor],h4[data-anchor]')) { if (!visible(heading)) continue; if (heading.getBoundingClientRect().top < 140) here = heading.dataset.anchor; else break; }
      if (state.last?.slug === slug && state.last.anchor !== here) { state.last.anchor = here; }
      for (const button of ui.toc.querySelectorAll('.m-toc-sec')) button.toggleAttribute('aria-current', false);
      const sectionAnchor = here && sectionOf(slug, here);
      const button = sectionAnchor && ui.toc.querySelector('.m-toc-sec[data-anchor="' + CSS.escape(sectionAnchor) + '"]');
      if (button) button.setAttribute('aria-current', 'location');
      save();
    });
  }
  function sectionOf(slug, anchor) {
    const chapter = book?.chapters.find(item => item.slug === slug);
    let top = '';
    for (const [level, id] of chapter?.sections || []) { if (level === 2) top = id; if (id === anchor) return top; }
    return '';
  }
  function updateDot(slug) {
    const button = ui.toc.querySelector('.m-toc-ch[data-slug="' + CSS.escape(slug) + '"]');
    button?.querySelector('.m-dot-fill')?.setAttribute('stroke-dashoffset', String(34.56 * (1 - fraction(slug))));
  }
  addEventListener('scroll', onScroll, { passive: true });

  // "/" searches the manual while its page is open.
  document.addEventListener('keydown', event => {
    if (event.key !== '/' || event.ctrlKey || event.metaKey || event.altKey || !built || $('manual').classList.contains('hidden')) return;
    if (event.target.closest('input,textarea,select,[contenteditable]') || document.querySelector('dialog[open]')) return;
    event.preventDefault();
    if (current.view === 'map') return window.manualMap?.focusSearch();
    side.classList.add('is-open'); ui.toggle.setAttribute('aria-expanded', 'true');
    input.focus();
  });
  // Any element with data-manual-open="chapter#heading" opens the manual there ("Read in the manual" links).
  document.addEventListener('click', event => {
    const link = event.target.closest('[data-manual-open]');
    if (!link) return;
    event.preventDefault();
    const [slug, anchor] = link.dataset.manualOpen.split('#');
    link.closest('dialog[open]')?.close();
    window.openManual(slug, anchor || '');
  });

  let pending = null;
  // Called by showPage('manual') in app.js.
  function shown() {
    build();
    if (pending) return;
    if (current.view === 'none') showStart(false);
    else onScroll();
  }
  window.openManual = async (chapterId, anchorId) => {
    pending = { chapterId, anchorId };
    try {
      if ($('manual').classList.contains('hidden') && typeof window.showPage === 'function') window.showPage('manual');
      build();
      if (chapterId === 'menu-map') await showMap(anchorId ? String(anchorId) : null, true);
      else if (chapterId) await openChapter(String(chapterId), anchorId ? String(anchorId) : '', { focus: true });
      else await showStart(true);
    } finally { pending = null; }
  };
  window.manualPage = Object.freeze({ shown, state: () => ({ view: current.view, slug: current.slug, iface: state.iface, map: current.view === 'map' ? window.manualMap?.state() : null }), find, setIface, showMap });
})();
