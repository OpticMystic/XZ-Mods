/* XZ-only offline localization. Imported text is never interpreted as HTML. */
(() => {
  'use strict';
  const data = window.XZ_L10N_DATA;
  const names = { en: 'English', es: 'Español', fr: 'Français', de: 'Deutsch', pt: 'Português', it: 'Italiano', ja: '日本語', ko: '한국어', 'zh-Hans': '简体中文', 'zh-Hant': '繁體中文', nl: 'Nederlands', pl: 'Polski', tr: 'Türkçe', uk: 'Українська', ar: 'العربية' };
  if (data.catalogs['pt-BR'] && !data.catalogs.pt) data.catalogs.pt = {...data.catalogs['pt-BR'], locale: 'pt'};
  const normalize = text => text.replace(/\s+/g, ' ').trim();
  const tokens = text => (text.match(/\{[A-Za-z_]\w*\}|%[sdif]|\$\{[^}]+\}/g) || []).sort();
  const protectedTokens = text => (text.match(/(?<!\w)(?:PLAY\/PAUSE|BEAT SYNC|TAG LIST|HOT CUE|CUE|SHIFT|SYNC|MASTER|MEMORY|USB|BROWSE|INFO|MENU|BACK)(?!\w)|(?<!\w)(?:XZ Mods|XDJ-XZ|Pioneer DJ|Rekordbox|GZDoom)(?!\w)|\bE-\d{4}\b/g) || []).sort();
  const protectedValid = (expected, value) => {
    const actual = protectedTokens(value);
    return expected.every(token => actual.filter(t => t === token).length === expected.filter(t => t === token).length) && JSON.stringify(actual.filter(t => /^E-\d{4}$/.test(t))) === JSON.stringify(expected.filter(t => /^E-\d{4}$/.test(t)));
  };
  const key = 'xz-mods.localization.v1';
  let locale = 'en';
  try { locale = JSON.parse(localStorage.getItem(key))?.locale || 'en'; } catch (_) { /* default */ }
  if (locale === 'pt-BR') {
    locale = 'pt';
    try { localStorage.setItem(key, JSON.stringify({locale, provider: 'offline-json'})); } catch (_) {}
  }
  if (!names[locale]) locale = 'en';
  const originals = new Map();
  const attributes = [];
  const index = new Map();
  for (const [id, entry] of Object.entries(data.source.entries)) index.set(entry.surface + ':' + entry.source, id);
  let imported = {};
  try { imported = JSON.parse(localStorage.getItem(key + '.imports')) || {}; } catch (_) { /* default */ }
  function valid(catalog) {
    if (catalog?.locale === 'pt-BR') catalog = {...catalog, locale: 'pt'};
    if (!catalog || catalog.schema !== 1 || catalog.product !== 'xz-mods' || !data.catalogs[catalog.locale] || catalog.source_revision !== data.source.source_revision || !catalog.entries || typeof catalog.entries !== 'object' || Array.isArray(catalog.entries)) throw Error('This catalog does not match the current XZ source. Export a fresh catalog and review translations.');
    for (const [id, entry] of Object.entries(catalog.entries)) {
      const source = data.source.entries[id];
      if (!source || !entry || typeof entry.text !== 'string' || !entry.text.trim() || entry.source_sha256 !== source.source_sha256 || JSON.stringify(tokens(entry.text)) !== JSON.stringify(source.placeholders) || !protectedValid(source.protected, entry.text) || (source.immutable && entry.text !== source.source)) throw Error('Invalid translation or placeholders: ' + id);
    }
    return catalog;
  }
  for (const id of Object.keys(imported)) {
    try { const checked = valid(imported[id]); imported[checked.locale] = checked; if (id !== checked.locale) delete imported[id]; }
    catch (_) { delete imported[id]; }
  }
  function text(value, surface = 'desktop') {
    const id = index.get(surface + ':' + normalize(value));
    const translated = catalog()?.entries[id]?.text;
    return translated ? value.replace(/\S(?:[\s\S]*\S)?/, () => translated) : value;
  }
  function catalog() { const base = data.catalogs[locale], extra = imported[locale]; return extra ? {...base, ...extra, entries: {...base?.entries, ...extra.entries}} : base; }
  function capture(root, surface) {
    if (surface === 'desktop') for (const element of root.querySelectorAll('[title],[placeholder],[aria-label],[alt]')) {
      if (element.closest('svg,[data-xz-no-translate]')) continue;
      for (const name of ['title', 'placeholder', 'aria-label', 'alt']) {
        const text = element.getAttribute(name);
        const id = text && index.get(surface + ':' + normalize(text));
        if (id) attributes.push({ element, name, text, id });
      }
    }
    const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
    while (walker.nextNode()) {
      const node = walker.currentNode;
      if (node.parentElement?.closest('script,style,svg,textarea,code,pre,[data-xz-no-translate]')) continue;
      if (originals.has(node)) continue;
      const id = index.get(surface + ':' + normalize(node.data));
      if (id) originals.set(node, { id, text: node.data });
    }
  }
  function apply() {
    for (const item of attributes) {
      const current = item.element.getAttribute(item.name);
      if (current !== item.text && current !== item.last) continue;
      item.last = catalog()?.entries[item.id]?.text || item.text;
      item.element.setAttribute(item.name, item.last);
    }
    for (const [node, original] of originals) {
      if (!node.isConnected) { originals.delete(node); continue; }
      const translated = catalog()?.entries[original.id]?.text;
      // Preserve application changes to dynamic nodes; never overwrite new status values.
      if (node.data !== original.text && node.data !== original.last) { originals.delete(node); continue; }
      const next = translated ? original.text.replace(/\S(?:[\s\S]*\S)?/, () => translated) : original.text;
      if (node.data !== next) node.data = next;
      original.last = next;
    }
    document.documentElement.lang = locale;
    document.documentElement.dir = locale === 'ar' ? 'rtl' : 'ltr';
    document.dispatchEvent(new CustomEvent('xz-language-changed', { detail: { locale } }));
  }
  capture(document.body, 'desktop');
  const button = document.createElement('button');
  button.type = 'button'; button.className = 'secondary'; button.dataset.xzLocalization = '';
  button.textContent = 'Language / Idioma'; button.id = 'xz-language-setup';
  (document.querySelector('.top-version') || document.querySelector('header.topbar') || document.body).append(button);
  const dialog = document.createElement('dialog'); dialog.dataset.xzLocalization = ''; dialog.className = 'xz-language-dialog';
  dialog.innerHTML = '<h2>XZ language setup</h2><p>1. Detect: XZ offline catalogs are installed. No account or API key is needed.</p><label>2. Choose a language <select id="xz-language-choice"></select></label><p id="xz-language-coverage"></p><p>3. Apply the bundled translations, or import a reviewed XZ JSON catalog. Missing text stays in English.</p><button id="xz-language-apply">Apply and verify</button> <button id="xz-language-export">Export catalog</button><label class="xz-import">Import catalog <input id="xz-language-import" type="file" accept=".json,application/json"></label><p id="xz-language-result" role="status"></p><p>Controller: Unicode fonts and right-to-left layout are unavailable. This setup changes desktop and manual text only. Hardware screenshots retain their original labels.</p><fieldset><legend>Independent local translation provider</legend><label>Python executable <input id="xz-provider-python" data-xz-no-translate></label><label>Small100 model folder <input id="xz-provider-model" data-xz-no-translate></label><button id="xz-provider-detect">Detect resources</button><button id="xz-provider-connect">Connect and live verify</button><button id="xz-provider-install">Install pinned dependencies</button><button id="xz-provider-run">Generate all languages</button><button id="xz-provider-cancel">Cancel translation</button><pre id="xz-provider-status" role="status" data-xz-no-translate></pre></fieldset><button id="xz-language-close">Close</button>';
  document.body.append(dialog);
  const choice = dialog.querySelector('select');
  choice.dataset.xzNoTranslate = '';
  for (const [id, name] of Object.entries(names)) { const option = new Option(name, id); choice.add(option); }
  const result = dialog.querySelector('#xz-language-result');
  function coverage() {
    const base = data.catalogs[choice.value], extra = imported[choice.value];
    const selected = extra ? {...base, ...extra, entries: {...base?.entries, ...extra.entries}} : base;
    const counts = ['desktop', 'manual'].map(surface => {
      const ids = Object.keys(data.source.entries).filter(id => data.source.entries[id].surface === surface);
      return `${surface}: ${choice.value === 'en' ? ids.length : ids.filter(id => selected?.entries[id]).length}/${ids.length}`;
    });
    dialog.querySelector('#xz-language-coverage').textContent = counts.join('; ') + '. Counts cover extracted text only. Machine translations require review. Runtime values and metadata stay unchanged.';
  }
  button.onclick = () => { choice.value = locale; coverage(); dialog.showModal(); providerAction('detect'); };
  choice.onchange = coverage;
  const native = window.__TAURI__?.core?.invoke;
  const providerStatus = dialog.querySelector('#xz-provider-status');
  const providerPython = dialog.querySelector('#xz-provider-python');
  const providerModel = dialog.querySelector('#xz-provider-model');
  let providerTimer, providerJobAction;
  async function providerAction(action) {
    try {
      if (!native) throw Error('Open the installed XZ desktop app to connect a local provider.');
      if (['connect','install','run'].includes(action)) providerJobAction = action;
      const state = await native('translation_action', {action, python: providerPython.value, model: providerModel.value});
      if (action === 'detect') { providerPython.value = state.python || ''; providerModel.value = state.model || ''; }
      providerStatus.textContent = JSON.stringify(state, null, 2);
      clearTimeout(providerTimer);
      if (!state.running && state.ok && providerJobAction === 'install') {
        const saved = await native('translation_action', {action: 'detect', python: '', model: ''});
        providerPython.value = saved.python; providerModel.value = saved.model; providerJobAction = null;
      }
      if (!state.running && state.ok && providerJobAction === 'run') {
        const generated = await native('translation_action', {action: 'bundle', python: '', model: ''});
        if (generated.source.source_revision !== data.source.source_revision) throw Error('Generated source differs from this app. Install the matching app build.');
        for (const catalog of Object.values(generated.catalogs)) valid(catalog);
        data.catalogs = generated.catalogs; apply(); coverage(); providerJobAction = null;
      }
      if (state.running) providerTimer = setTimeout(() => providerAction('status'), 1000);
    } catch (error) { providerStatus.textContent = String(error); }
  }
  for (const action of ['detect','connect','install','run','cancel']) dialog.querySelector('#xz-provider-' + action).onclick = () => providerAction(action);

  dialog.querySelector('#xz-language-close').onclick = () => dialog.close();
  dialog.querySelector('#xz-language-apply').onclick = () => {
    locale = choice.value; apply();
    try { localStorage.setItem(key, JSON.stringify({ locale, provider: 'offline-json' })); result.textContent = '4. Applied. Check the navigation and manual text. Language choice saved for XZ only.'; }
    catch (_) { result.textContent = 'Applied for this session. Storage is unavailable; language choice could not be saved.'; }
  };
  dialog.querySelector('#xz-language-export').onclick = () => {
    if (choice.value === 'en') { result.textContent = 'Choose a translation language first.'; return; }
    const base = data.catalogs[choice.value], extra = imported[choice.value];
    const selected = extra ? {...base, ...extra, entries: {...base?.entries, ...extra.entries}} : base;
    const out = { ...selected, source_entries: data.source.entries };
    const url = URL.createObjectURL(new Blob([JSON.stringify(out, null, 2)], { type: 'application/json' }));
    const link = document.createElement('a'); link.href = url; link.download = 'xz-' + choice.value + '.json'; link.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
  };
  dialog.querySelector('#xz-language-import').onchange = async event => {
    try {
      const file = event.target.files[0]; if (!file) return;
      if (file.size > 32 * 1024 * 1024) throw Error('Catalog is larger than 32 MB.');
      const value = valid(JSON.parse(await file.text()));
      const corrections = Object.fromEntries(Object.entries(value.entries).filter(([id, entry]) => entry.text !== data.catalogs[value.locale]?.entries[id]?.text));
      const next = { ...imported, [value.locale]: {...value, entries: corrections} };
      localStorage.setItem(key + '.imports', JSON.stringify(next)); imported = next;
      choice.value = value.locale; coverage(); result.textContent = 'Catalog checked and installed. Choose Apply and verify.';
    } catch (error) { result.textContent = error.message; }
  };
  // Manual HTML is generated from checked source. Capture its newly loaded chapters only.
  const manual = document.querySelector('#manual');
  if (manual) new MutationObserver(records => {
    if (!records.some(record => record.addedNodes.length)) return;
    capture(manual, 'manual'); apply();
  }).observe(manual, { childList: true, subtree: true });
  capture(dialog, 'desktop'); capture(button, 'desktop');
  apply();
  window.XZText = value => text(value);
  window.XZTemplate = (parts, ...values) => text(parts.map((part, i) => part + (i < values.length ? '{xz' + i + '}' : '')).join('')).replace(/\{xz(\d+)\}/g, (_, i) => String(values[Number(i)]));
  window.XZLocalization = { valid, apply, text, get locale() { return locale; } };
})();
