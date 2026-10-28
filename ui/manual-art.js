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
    enhance(article) { for (const box of article.querySelectorAll('.mx')) mount(box); },
    stopAll, hero: heroMedia, startHero, media
  });
})();
