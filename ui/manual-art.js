// Explainer clips and the start page picture for the Manual page (ui/manual.js). Nothing here draws the player: every
// picture of the XDJ-XZ is a render of its true-scale Blender model, and the player's screen is the firmware's own
// render. ui/assets/manual/media.js lists the clips (tools/import_manual_media.py writes it from the renders), one per
// scene. The words come from the chapters: a `steps` explainer follows the numbered list after it, a `scenes`
// explainer reads its scene lines. An explainer whose clips are not rendered yet stays hidden; the chapter text around
// it says everything it would show (manual/README.md).
'use strict';
(() => {
  const reducedQuery = matchMedia('(prefers-reduced-motion: reduce)');
  const reduced = () => reducedQuery.matches;
  const media = () => window.XZ_MANUAL_MEDIA || { explainers: {}, hero: null };
  const live = new Set();

  const svg = (tag, attrs, ...kids) => { const item = document.createElementNS('http://www.w3.org/2000/svg', tag); for (const [k, v] of Object.entries(attrs)) item.setAttribute(k, v); item.append(...kids); return item; };
  const glyph = d => svg('svg', { viewBox: '0 0 16 16', 'aria-hidden': 'true' }, svg('path', { d, fill: 'currentColor' }));
  const PLAY = 'M4.5 2.5v11l9-5.5z', PAUSE = 'M4 2.5h3v11H4zM9 2.5h3v11H9z', PREV = 'M3 2.5h2v11H3zM13 2.5v11L6 8z', NEXT = 'M11 2.5h2v11h-2zM3 2.5v11L10 8z';
  const el = (tag, props, ...kids) => { const item = document.createElement(tag); for (const [k, v] of Object.entries(props || {})) { if (v === undefined || v === null || v === false) continue; if (k === 'text') item.textContent = v; else if (k.startsWith('on')) item[k] = v; else item.setAttribute(k, v === true ? '' : v); } item.append(...kids.flat().filter(Boolean)); return item; };
  const escapeHtml = text => String(text).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  // A muted clip that never shows the browser's own controls. Its poster shows until it plays.
  function clipVideo(w, h, label) {
    const video = el('video', { muted: true, playsinline: true, preload: 'none', disablepictureinpicture: true, disableremoteplayback: true, width: w, height: h, 'aria-label': label });
    video.muted = true;
    return video;
  }
  const playVideo = video => { const run = video.play(); if (run) run.catch(() => {}); };

  // ---------------------------------------------------------------- an explainer: clip, caption, controls
  function mount(box) {
    if (box.dataset.mounted) return;
    const kind = box.dataset.mx, def = media().explainers?.[kind];
    const steps = box.dataset.mode === 'steps';
    let scenes = [], list = null;
    if (steps) {
      for (let next = box.nextElementSibling; next; next = next.nextElementSibling) {
        if (next.matches('ol.steps')) { list = next; break; }
        if (next.matches('h3,h4,h5,.mx,.mf,.mg')) break;
      }
      if (list) scenes = [...list.children].map(li => ({ title: '', html: li.innerHTML }));
    } else scenes = [...box.querySelectorAll('.mx-scenes > li')].map(li => ({ title: li.dataset.title || '', html: li.querySelector('span').innerHTML }));
    // Not rendered yet, or rendered for another version of the chapter (build_manual.py check fails on that).
    if (!def?.scenes?.length || !scenes.length || def.scenes.length !== scenes.length) {
      box.hidden = true;
      box.dataset.missing = def?.scenes?.length ? 'mismatch ' + def.scenes.length + '/' + scenes.length : 'not rendered';
      return;
    }
    box.dataset.mounted = '1';
    box.dataset.scenes = scenes.length;
    if (list) list.classList.add('mx-bound');
    const label = box.getAttribute('aria-label') || 'Explainer';
    const single = scenes.length === 1;
    const video = clipVideo(def.w, def.h, label);
    const head = el('div', { class: 'mx-head' }, el('span', { class: 'mx-badge', text: 'Video' }), el('h5', { text: label }));
    const caption = el('div', { class: 'mx-cap', 'aria-live': 'off' });
    const play = el('button', { type: 'button', class: 'mx-play', 'aria-label': 'Play' }, glyph(PLAY));
    const count = el('span', { class: 'mx-count', 'aria-hidden': single ? null : 'true' });
    const controls = [play];
    let dots = null, prev = null, next = null;
    if (!single) {
      prev = el('button', { type: 'button', 'aria-label': 'Previous step' }, glyph(PREV));
      next = el('button', { type: 'button', 'aria-label': 'Next step' }, glyph(NEXT));
      dots = el('div', { class: 'mx-dots', role: 'group', 'aria-label': 'Steps' });
      scenes.forEach((scene, i) => dots.append(el('button', { type: 'button', 'data-i': i + 1, 'aria-label': 'Step ' + (i + 1) + (scene.title ? ': ' + scene.title : '') }, el('i'), el('span', { text: String(i + 1) }))));
      controls.unshift(prev); controls.push(next, dots);
    }
    box.replaceChildren(head, el('div', { class: 'mx-stage' }, video), caption, el('div', { class: 'mx-ctl' }, controls, count));

    let at = 0, playing = false, userPaused = false, timer = 0;
    // A scene lasts as long as its clip, or as long as its words take to read when that is longer.
    const reading = i => Math.min(7200, Math.max(3800, 1900 + scenes[i - 1].html.replace(/<[^>]+>/g, '').length * 40));
    const duration = i => Math.max(Math.round(def.scenes[i - 1].seconds * 1000), reading(i));
    const glyphs = () => {
      const on = single ? !video.paused : playing;
      play.replaceChildren(glyph(on ? PAUSE : PLAY));
      play.setAttribute('aria-label', on ? 'Pause' : !single && at >= scenes.length && !playing && box.dataset.played ? 'Play again' : 'Play');
      box.classList.toggle('is-playing', on);
    };
    // Show scene i: its poster at once, its clip when the explainer plays or the reader picked the scene.
    function go(i, user) {
      at = Math.max(1, Math.min(scenes.length, i));
      const clip = def.scenes[at - 1];
      if (video.getAttribute('src') !== clip.video) { video.pause(); video.poster = clip.poster; video.src = clip.video; }
      else if (!video.paused || video.ended) { video.pause(); try { video.currentTime = 0; } catch {} }
      video.loop = !!clip.loop;
      box.dataset.scene = at;
      if (playing || (user && !reduced())) playVideo(video);
      caption.setAttribute('aria-live', user ? 'polite' : 'off');
      const words = el('span'); words.innerHTML = (scene => (scene.title ? '<b>' + escapeHtml(scene.title) + '.</b> ' : '') + scene.html)(scenes[at - 1]);
      caption.replaceChildren(single ? '' : el('span', { class: 'mx-cap-n', text: String(at), 'aria-hidden': 'true' }), words);
      if (!reduced() && !single) { caption.classList.remove('mx-swap'); void caption.offsetWidth; caption.classList.add('mx-swap'); }
      if (dots) [...dots.children].forEach((dot, k) => { dot.toggleAttribute('aria-current', false); if (k + 1 === at) dot.setAttribute('aria-current', 'step'); dot.classList.toggle('is-done', k + 1 < at); });
      count.textContent = single ? Math.round(clip.seconds) + ' s' + (clip.loop ? ' loop' : '') : at + ' / ' + scenes.length;
      if (list) [...list.children].forEach((li, k) => { li.classList.toggle('is-now', k + 1 === at); li.classList.toggle('is-done', k + 1 < at); });
      box.style.setProperty('--mx-dur', duration(at) + 'ms');
      clearTimeout(timer);
      if (playing && !single) timer = setTimeout(advance, duration(at));
      glyphs();
    }
    function advance() {
      if (!box.isConnected) return stop(true);
      if (at >= scenes.length) { if (video.loop) video.pause(); stop(false); return; }
      go(at + 1, false);
    }
    function start() {
      box.dataset.played = '1';
      if (single) { playVideo(video); return; }
      playing = true;
      go(at >= scenes.length || !at ? 1 : at, false);
    }
    function stop(pause) {
      playing = false; clearTimeout(timer);
      if (pause) video.pause();
      glyphs();
    }
    video.onplay = video.onpause = glyphs;
    const user = fn => () => { userPaused = true; stop(true); fn(); };
    play.onclick = () => {
      const on = single ? !video.paused : playing;
      if (on) { userPaused = true; stop(true); } else { userPaused = false; start(); }
    };
    if (!single) {
      prev.onclick = user(() => go(at - 1, true));
      next.onclick = user(() => go(at + 1, true));
      dots.onclick = event => { const dot = event.target.closest('button'); if (dot) user(() => go(Number(dot.dataset.i), true))(); };
    }
    if (list) [...list.children].forEach((li, k) => { li.onclick = event => { if (event.target.closest('a')) return; user(() => { go(k + 1, true); box.scrollIntoView({ block: 'nearest', behavior: reduced() ? 'auto' : 'smooth' }); })(); }; });
    box.mxStop = () => { stop(true); };
    live.add(box);
    go(1, false);
    // Plays by itself once in view, unless the reader asked for reduced motion: then it shows the poster until Play.
    if (!reduced() && 'IntersectionObserver' in window) {
      const observer = new IntersectionObserver(entries => {
        for (const entry of entries) {
          if (!box.isConnected) { observer.disconnect(); return; }
          const on = single ? !video.paused : playing;
          if (entry.isIntersecting && !on && !userPaused && !box.dataset.played) start();
          else if (!entry.isIntersecting && on) { stop(true); delete box.dataset.played; }
        }
      }, { threshold: 0.55 });
      observer.observe(box);
      box.mxObserver = observer;
    }
    box.mxGo = i => go(i, true);
  }

  // ---------------------------------------------------------------- a navigation video: how to drive the player
  // build_manual.py writes a .mn box per ```nav``` block: one <li> per flow with its video, poster and the steps the
  // capture's timeline recorded (data-t: the step's start in ms). The video is the firmware's own screen beside the
  // Blender render of the control in use, with no words in it; the steps under it light up as the video reaches them,
  // and a click on a step plays from there. Without the video the steps still read as a numbered list.
  const EXPAND = 'M2 6V2h4v1.5H3.5V6zM10 2h4v4h-1.5V3.5H10zM2 10h1.5v2.5H6V14H2zM12.5 10H14v4h-4v-1.5h2.5z';
  function mountNav(box) {
    if (box.dataset.mounted) return;
    const scenes = [...box.querySelectorAll('.mn-scenes > li')].map(li => ({
      flow: li.dataset.flow, video: li.dataset.video, poster: li.dataset.poster, w: +li.dataset.w, h: +li.dataset.h, seconds: +li.dataset.seconds,
      title: li.dataset.title || '', steps: [...li.querySelectorAll('.mn-steps > li')].map(s => ({ t: +s.dataset.t, html: s.innerHTML }))
    })).filter(s => s.video && s.steps.length);
    if (!scenes.length) { box.hidden = true; box.dataset.missing = 'no video'; return; }
    box.dataset.mounted = '1';
    box.dataset.flows = scenes.map(s => s.flow).join(' ');
    const label = box.getAttribute('aria-label') || 'How to';
    const video = clipVideo(scenes[0].w, scenes[0].h, label);
    video.loop = true;
    const title = el('h5', { text: label });
    const tabs = scenes.length > 1 ? el('div', { class: 'mn-tabs', role: 'group', 'aria-label': 'Ways shown' }) : null;
    const play = el('button', { type: 'button', class: 'mn-play', 'aria-label': 'Play' }, glyph(PLAY));
    const big = document.fullscreenEnabled ? el('button', { type: 'button', class: 'mn-big', 'aria-label': 'Show larger' }, glyph(EXPAND)) : null;
    const stage = el('div', { class: 'mn-stage' }, video, el('span', { class: 'mn-bar', 'aria-hidden': 'true' }, el('i')));
    const list = el('ol', { class: 'mn-list' });
    box.replaceChildren(el('div', { class: 'mn-head' }, el('span', { class: 'mn-badge', text: 'How to' }), title, big), stage, tabs, list,
      el('div', { class: 'mn-ctl' }, play, el('span', { class: 'mn-note', text: 'The screen as the player draws it, and the control you use' })));
    let at = 0, userPaused = false, items = [];
    const bar = stage.querySelector('.mn-bar i');
    const sync = () => {
      const on = !video.paused;
      play.replaceChildren(glyph(on ? PAUSE : PLAY)); play.setAttribute('aria-label', on ? 'Pause' : 'Play');
      box.classList.toggle('is-playing', on);
    };
    // The step the video is in: the last one that has started.
    function mark() {
      const t = video.currentTime * 1000, steps = scenes[at].steps;
      let now = -1;
      steps.forEach((s, k) => { if (s.t <= t + 40) now = k; });
      items.forEach((li, k) => { li.classList.toggle('is-now', k === now); li.classList.toggle('is-done', k < now); });
      if (bar && video.duration) bar.style.transform = 'scaleX(' + Math.min(1, video.currentTime / video.duration) + ')';
    }
    function show(i, user) {
      at = i;
      const scene = scenes[at];
      video.pause(); video.poster = scene.poster; video.src = scene.video;
      title.textContent = scene.title || label;
      items = scene.steps.map((s, k) => {
        const li = el('li', { 'data-t': s.t, tabindex: '0', role: 'button', 'aria-label': 'Play from step ' + (k + 1) });
        li.innerHTML = s.html;
        li.onclick = event => { if (event.target.closest('a')) return; seek(s.t); };
        li.onkeydown = event => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); seek(s.t); } };
        return li;
      });
      list.replaceChildren(...items);
      if (tabs) [...tabs.children].forEach((b, k) => b.setAttribute('aria-pressed', String(k === at)));
      box.dataset.scene = at + 1;
      mark();
      if (user && !reduced()) playVideo(video);
    }
    function seek(t) {
      userPaused = false;
      const go = () => { try { video.currentTime = Math.max(0, t - 250) / 1000; } catch {} mark(); playVideo(video); };
      if (video.readyState >= 1) go(); else { video.preload = 'auto'; video.addEventListener('loadedmetadata', go, { once: true }); video.load(); }
    }
    if (tabs) scenes.forEach((s, k) => tabs.append(el('button', { type: 'button', 'aria-pressed': 'false', onclick: () => show(k, true) }, el('span', { text: s.title || 'Way ' + (k + 1) }))));
    video.ontimeupdate = mark;
    video.onplay = video.onpause = sync;
    video.onseeked = mark;
    stage.onclick = () => { if (video.paused) { userPaused = false; playVideo(video); } else { userPaused = true; video.pause(); } };
    play.onclick = () => { if (video.paused) { userPaused = false; playVideo(video); } else { userPaused = true; video.pause(); } };
    if (big) big.onclick = () => { if (document.fullscreenElement) document.exitFullscreen(); else stage.requestFullscreen?.().catch(() => {}); };
    // a smooth highlight while playing: timeupdate fires only a few times a second
    let raf = 0;
    const tick = () => { mark(); raf = video.paused ? 0 : requestAnimationFrame(tick); };
    video.addEventListener('play', () => { if (!raf) raf = requestAnimationFrame(tick); });
    box.mxStop = () => { video.pause(); cancelAnimationFrame(raf); raf = 0; };
    live.add(box);
    show(0, false);
    sync();
    if (!reduced() && 'IntersectionObserver' in window) {
      const observer = new IntersectionObserver(entries => {
        for (const entry of entries) {
          if (!box.isConnected) { observer.disconnect(); return; }
          if (entry.isIntersecting && video.paused && !userPaused) playVideo(video);
          else if (!entry.isIntersecting && !video.paused) video.pause();
        }
      }, { threshold: 0.55 });
      observer.observe(box);
      box.mxObserver = observer;
    }
    box.mnShow = i => show(i, true);
  }

  // ---------------------------------------------------------------- the start page picture: a render of the model
  const hero = { video: null, observer: null };
  function heroMedia() {
    const def = media().hero;
    if (!def) return null;
    const video = clipVideo(def.w, def.h, 'The XDJ-XZ');
    video.setAttribute('aria-hidden', 'true');
    video.poster = def.poster; video.src = def.video; video.loop = def.loop !== false;
    video.className = 'm-hero-video';
    const toggle = el('button', { type: 'button', class: 'm-hero-toggle', 'aria-label': 'Play the picture' }, glyph(PLAY));
    const sync = () => { const on = !video.paused; toggle.replaceChildren(glyph(on ? PAUSE : PLAY)); toggle.setAttribute('aria-label', on ? 'Pause the picture' : 'Play the picture'); };
    video.onplay = video.onpause = sync;
    toggle.onclick = () => { if (video.paused) { video.dataset.user = ''; playVideo(video); } else { video.dataset.user = 'paused'; video.pause(); } };
    hero.video = video;
    return el('div', { class: 'm-hero-media', 'data-interim': def.interim ? '1' : null }, video, toggle);
  }
  function startHero(view) {
    const video = view.querySelector('.m-hero-video');
    hero.observer?.disconnect();
    if (!video || reduced()) return;
    if (!('IntersectionObserver' in window)) return playVideo(video);
    hero.observer = new IntersectionObserver(entries => {
      for (const entry of entries) {
        if (!video.isConnected) { hero.observer.disconnect(); return; }
        if (entry.isIntersecting && video.dataset.user !== 'paused') playVideo(video);
        else if (!entry.isIntersecting) video.pause();
      }
    }, { threshold: 0.3 });
    hero.observer.observe(video);
  }

  function stopAll() {
    for (const box of live) { box.mxStop?.(); box.mxObserver?.disconnect(); }
    live.clear();
    hero.video?.pause(); hero.observer?.disconnect(); hero.video = null;
  }

  window.manualArt = Object.freeze({
    enhance(article) { for (const box of article.querySelectorAll('.mx')) mount(box); for (const box of article.querySelectorAll('.mn')) mountNav(box); },
    stopAll, hero: heroMedia, startHero, media
  });
})();
