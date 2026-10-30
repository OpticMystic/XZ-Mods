// From your XZ: reports the DJ sent from the XZ that wait for their words, the reminders about them, and the sticks
// allowed to send them. Rust holds every key and makes every request; this page sees pending rows and loader IDs
// only, and draws report words as text. There is no screen picture: the screen is a set of names, said in words here.
// Design 2026-10-27 (Opus 5.5). Reminders: a Windows notification for a report that arrives while the app is open,
// a badge on Feedback, and a quiet callout at start. They stop when the DJ says so or after 7 days (remind).
'use strict';
window.xzQueue = (() => {
  const W = window.reportWizard.words, listen = window.__TAURI__?.event?.listen;
  const LIMIT = 4096, encoder = new TextEncoder(), STORE = 'xz-mods.from-xz.notified';
  let pending = [], identity = null, link = null, failure = '', started = false, startup = true, calloutOn = false, calloutText = '', toasts = 'try', lastSent = null, target = null;
  const views = new Map(), sticksView = { confirm: null, message: '' };
  // Each report is announced once, across restarts.
  let notified = new Set();
  try { notified = new Set(JSON.parse(localStorage.getItem(STORE) || '[]')); } catch {}
  const remember = id => { notified.add(id); try { localStorage.setItem(STORE, JSON.stringify([...notified].slice(-200))); } catch {} };
  const view = id => { if (!views.has(id)) views.set(id, { text: pending.find(p => p.report_id === id)?.description || '', state: 'idle', error: '', message: '', open: false, details: null, detailsError: '', preview: null }); return views.get(id); };
  const reminding = () => pending.filter(p => p.remind && !p.completion_pending);
  const onPage = () => !$('my-reports').classList.contains('hidden');
  const clean = failure => String(failure).replace(/^Error: /, '');
  const redraw = () => { if (onPage()) window.myReports?.render(); };
  function icon(id) {
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg'), use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
    svg.setAttribute('class', 'icon'); svg.setAttribute('aria-hidden', 'true'); use.setAttribute('href', '#' + id); svg.append(use);
    return svg;
  }

  // Words for what the XZ recorded.
  const TOPIC = { GOT_WORSE: ['Got worse', 'worse'], NOT_WORKING: ['Problem', 'problem'], WORK_DIFFERENTLY: ['Works differently', 'expect'], NEW_IDEA: ['Idea', 'idea'], HOW_DO_I: ['Question', 'question'] };
  const STOCK = { BROWSE: 'browse screen', TAG_LIST: 'tag list', INFO: 'track info screen', MENU: 'MENU screen', UTILITY: 'UTILITY screen', PERFORMANCE: 'play screen', SOURCE: 'source screen', SEARCH: 'search screen' };
  const DECKS = { TWO: '2 decks', FOUR: '4 decks', MIXER: 'mixer view' };
  const ALERTS = { EMERGENCY_LOOP: 'EMERGENCY LOOP', READ_ERROR: 'a read error', UNSUPPORTED_FILE: 'UNSUPPORTED', DEVICE_ERROR: 'a device error', HUB_UNSUPPORTED: 'a USB warning', UNKNOWN: 'an alert' };
  const SLOTS = { app: 'this computer', package: 'this app', xz_a: 'the XZ' };
  function screen(s) {
    if (!s) return 'Not recorded';
    const look = s.interface === 'MODERN' || s.interface === 'CLASSIC' ? s.interface + ' ' : '';
    const place = s.surface === 'MODS' ? 'MODS menu' + (s.mods_page === 'UNKNOWN' ? '' : ', ' + s.mods_page.replace(/_/g, ' ') + ' page')
      : s.surface === 'STOCK' ? STOCK[s.stock_page] || 'player screen' : look ? 'screen' : 'Screen not known';
    const shown = [s.counter_code === 'UNKNOWN' ? 'an error code' : s.counter_code === 'NONE' ? null : s.counter_code, s.alert_code === 'NONE' ? null : ALERTS[s.alert_code]].filter(Boolean);
    return [look + place, DECKS[s.deck_view], shown.length ? shown.join(' and ') + ' shown' : null].filter(Boolean).join(', ');
  }
  const day = iso => new Date(iso).toLocaleDateString('en-GB', { day: 'numeric', month: 'short', year: 'numeric' });
  function when(ms) {
    const d = new Date(ms), now = new Date(), time = d.toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit' }), before = new Date(now);
    before.setDate(now.getDate() - 1);
    return (d.toDateString() === now.toDateString() ? 'today' : d.toDateString() === before.toDateString() ? 'yesterday' : d.toLocaleDateString('en-GB', { day: 'numeric', month: 'short' })) + ' at ' + time;
  }
  function stick(id) {
    if (!id) return 'An older loader stick with no ID';
    const loader = identity?.loaders.find(l => l.loader_id === id);
    return loader ? id + ', written ' + day(loader.created_at) : id;
  }

  // State from the host: a local read, an explicit check, or the queue event after every background check.
  function apply(result, origin) {
    if (!result || result.checking) return;
    if (result.identity) identity = result.identity;
    if ('connected' in result) link = { connected: result.connected, error: result.error };
    pending = result.pending || [];
    for (const id of [...views.keys()]) if (!pending.some(p => p.report_id === id)) views.delete(id);
    const fresh = reminding().filter(p => !notified.has(p.report_id));
    // At start, and for reports that came in while the app was closed, the callout speaks for all of them.
    if (origin === 'event' && !startup) for (const p of fresh) announce(p);
    else { for (const p of fresh) remember(p.report_id); if (fresh.length || !started && reminding().length) calloutOn = true; }
    started = true;
    if (origin === 'event') startup = false;
    paint();
  }
  async function announce(p) {
    remember(p.report_id);
    // Someone looking at My reports sees it arrive; a notification would only repeat it.
    if (document.hasFocus() && onPage()) return;
    if (toasts === 'try') {
      try { if ((await invoke('support_notify_pending', { id: p.report_id, reference: p.report_reference })).shown) return; toasts = 'off'; }
      catch { return; }
    }
    // Notifications are off or unsupported: the badge and this callout carry it, without asking to turn them on.
    calloutOn = true; paint();
  }
  async function reload() {
    try {
      const [list, mine] = await Promise.all([invoke('support_pending_reports'), invoke('support_loader_identities')]);
      identity = mine; failure = ''; apply(list, 'load');
    } catch (error) { failure = clean(error); paint(); }
  }
  async function poll() {
    try { apply(await invoke('support_poll_pending'), 'poll'); } catch (error) { failure = clean(error); paint(); }
  }

  // The badge on Feedback in the rail, and the callout under the page title.
  function paint() {
    const count = reminding().length;
    for (const host of [document.querySelector('nav button[data-page="my-reports"]')]) {
      if (!host) continue;
      let badge = host.querySelector('.sr-xz-badge');
      if (!count) { badge?.remove(); continue; }
      if (!badge) { badge = el('span', { class: 'sr-xz-badge' }); host.append(badge); }
      badge.replaceChildren(el('span', { 'aria-hidden': 'true', text: String(count) }), el('span', { class: 'visually-hidden', text: ', ' + count + (count === 1 ? XZText(' report from your XZ needs') : XZText(' reports from your XZ need')) + XZText(' your words') }));
    }
    const callout = $('xz-callout'), items = reminding(), show = calloutOn && items.length > 0 && !onPage();
    callout.classList.toggle('hidden', !show);
    const text = items.length === 1 ? 'Your XZ sent a report, ' + items[0].report_reference + '. Add what happened?' : 'Your XZ sent ' + items.length + ' reports. Add what happened?';
    if (show && text !== calloutText) {
      calloutText = text;
      callout.replaceChildren(icon('i-report'), el('span', { class: 'sr-xz-callout-text', text }),
        el('span', { class: 'actions' }, plainButton(XZText('Add what happened'), () => open(reminding()[0]?.report_id), { key: 'xz-callout-open' }),
          plainButton(XZText('Remind me later'), () => { calloutOn = false; paint(); }, { text: true, key: 'xz-callout-later' }),
          plainButton(XZText('Stop reminding'), () => stop(reminding()), { text: true, key: 'xz-callout-stop' })));
    }
    if (!show) calloutText = '';
    redraw();
  }
  async function stop(items) {
    const ids = items.map(p => p.report_id);
    for (const p of items) p.remind = false;
    calloutOn = false; paint();
    for (const id of ids) { try { await invoke('support_dismiss_pending', { id }); } catch {} }
    await reload();
  }

  // Opens one report in My reports with its box ready to type in.
  function open(id) {
    calloutOn = false;
    showPage('my-reports');
    // The outline marks the report for a moment; redraws keep it until then.
    const node = pending.some(p => p.report_id === id) && document.getElementById('xz-item-' + id);
    if (!node) return;
    target = id; node.dataset.target = '';
    node.scrollIntoView({ block: 'center' });
    const box = node.querySelector('textarea');
    if (box) { box.focus({ preventScroll: true }); box.setSelectionRange(box.value.length, box.value.length); }
    setTimeout(() => { if (target !== id) return; target = null; delete document.getElementById('xz-item-' + id)?.dataset.target; }, 1600);
  }

  // From your XZ, at the top of My reports.
  function section() {
    if (!pending.length && !lastSent) return null;
    const offline = link && !link.connected && pending.length;
    return el('section', { class: 'sr-xz', 'aria-labelledby': 'sr-xz-title' },
      el('div', { class: 'sr-xz-title-row' }, el('h2', { id: 'sr-xz-title', tabindex: '-1', text: XZText('From your XZ') }),
        pending.length ? el('span', { class: 'sr-xz-count', text: pending.length + (pending.length === 1 ? XZText(' report waits') : XZText(' reports wait')) + XZText(' for your words') }) : null),
      pending.length ? el('p', { class: 'sr-xz-lede', text: XZText('You sent these from the XZ. Add what happened in your own words, then send. The reference stays the same.') }) : null,
      lastSent ? el('div', { class: 'st-note', dataset: { tone: 'ok' }, role: 'status' }, el('strong', { text: XZText('Sent. ') }),
        lastSent.reference + (lastSent.words ? XZText(' now has your words') : XZText(' is complete')) + XZText(' and is listed with your reports below.')) : null,
      offline ? el('p', { class: 'sr-xz-offline', text: XZText("This computer can't reach the report service right now. Words you send are kept and go by themselves when it can.") }) : null,
      failure && !pending.length ? el('p', { class: 'sr-xz-offline', text: XZText('Reports from your XZ could not be read. ') + failure }) : null,
      pending.length ? el('ol', { class: 'sr-xz-list', 'aria-label': 'Reports from your XZ' }, ...pending.map(p => el('li', {}, item(p)))) : null);
  }
  function item(p) {
    const id = p.report_id, v = view(id), sending = v.state === 'sending', [label, tone] = TOPIC[p.topic] || ['Report', ''];
    const facts = el('dl', { class: 'sr-xz-facts' },
      el('dt', { text: XZText('Topic') }), el('dd', { text: p.topic ? W.topic[p.topic]?.[0] || label : XZText('Not chosen on the XZ') }),
      el('dt', { text: XZText('Screen') }), el('dd', { text: screen(p.screen_state) }),
      el('dt', { text: XZText('Stick') }), el('dd', { text: stick(p.loader_id) }),
      ...(p.description ? [el('dt', { text: XZText('Typed on the XZ') }), el('dd', { class: 'sr-xz-typed', text: p.description })] : []));
    const head = el('div', { class: 'sr-xz-head' }, el('span', { class: 'sr-badge', dataset: { tone: tone || undefined }, text: label }),
      el('strong', { class: 'sr-reference', id: 'xz-ref-' + id, text: p.report_reference }),
      el('span', { class: 'sr-xz-when', text: XZText('Sent from your XZ ') + when(p.created_at) }),
      p.remind ? null : el('span', { class: 'sr-xz-quiet', text: XZText('No reminders') }));
    const body = [head, facts];
    if (p.completion_pending) {
      body.push(el('div', { class: 'st-note', dataset: { tone: 'ok' } }, el('strong', { text: XZText('Saved on this computer. ') }),
        XZText('It goes to the maintainer by itself when XZ Mods can reach the report service. You can close the app.')));
    } else {
      const box = el('textarea', { class: 'sr-box', id: 'xz-text-' + id, rows: '4', spellcheck: 'true', disabled: sending, 'aria-invalid': String(Boolean(v.error)),
        'aria-describedby': 'xz-note-' + id, placeholder: 'Say what you did and what you saw. One sentence is enough.', dataset: { key: 'xz-text-' + id } });
      box.value = v.text;
      const note = el('div', { class: 'sr-box-foot', id: 'xz-note-' + id }, ...countParts(v));
      box.oninput = () => { v.text = box.value; v.error = ''; box.setAttribute('aria-invalid', 'false'); note.replaceChildren(...countParts(v)); };
      body.push(el('label', { class: 'sr-xz-label', for: 'xz-text-' + id, text: XZText('What happened?') }), box, note, details(p, v),
        el('div', { class: 'sr-xz-foot' },
          el('span', { class: 'st-foot-left' }, p.remind ? plainButton(XZText('Stop reminding'), () => stop([p]), { text: true, key: 'xz-stop-' + id }) : null),
          plainButton(XZText('Send without comments'), () => complete(p, null), { key: 'xz-bare-' + id, disabled: sending }),
          plainButton(sending ? 'Sending...' : XZText('Send'), () => complete(p, v.text), { primary: true, key: 'xz-send-' + id, disabled: sending })));
    }
    body.push(el('p', { class: 'visually-hidden', role: 'status', text: v.message }));
    return el('article', { class: 'sr-xz-item', id: 'xz-item-' + id, 'aria-labelledby': 'xz-ref-' + id, dataset: { remind: p.remind ? '' : undefined, target: id === target ? '' : undefined } }, ...body);
  }
  function countParts(v) {
    const used = encoder.encode(v.text).length;
    return [v.error ? el('span', { class: 'sr-error', text: v.error }) : el('span'),
      used > LIMIT - 600 ? el('span', { class: 'sr-count', dataset: { near: used <= LIMIT ? '' : undefined, over: used > LIMIT ? '' : undefined }, text: used.toLocaleString('en-GB') + ' of ' + LIMIT.toLocaleString('en-GB') + ' bytes' }) : null].filter(Boolean);
  }
  // What the XZ collected, read from the report service only when opened. It went with the report from the XZ,
  // so it is shown as sent; nothing here can add to it or take it back.
  function details(p, v) {
    if (p.details_expired) return el('p', { class: 'sr-xz-expired', text: XZText('The collected details were deleted after 14 days. Your words still reach the maintainer.') });
    const d = v.details, items = d?.private?.items || [], incident = d?.incident;
    const meta = d ? items.length + (items.length === 1 ? ' source' : ' sources') + (incident ? ' · last ' + Math.max(1, Math.round((incident.before_seconds + incident.after_seconds) / 60)) + ' min' : '') : 'Sent with the report';
    const panel = el('details', { class: 'sr-details', dataset: { key: 'xz-details-' + p.report_id } }, el('summary', { dataset: { key: 'xz-summary-' + p.report_id } }, el('b', { text: XZText('Collected details') }), el('span', { class: 'sr-meta', text: meta })));
    panel.open = v.open;
    panel.ontoggle = () => { v.open = panel.open; if (panel.open && !v.details) readDetails(p); };
    const body = el('div', { class: 'sr-detail-body' });
    if (v.detailsError) body.append(el('p', { class: 'fine', text: v.detailsError }), el('div', { class: 'actions' }, plainButton(XZText('Try again'), () => readDetails(p), { key: 'xz-details-again-' + p.report_id })));
    else if (!d) body.append(el('p', { class: 'fine', text: XZText('Reading the details...') }));
    else {
      body.append(el('p', { class: 'fine', text: XZText('The XZ collected these when you pressed Send, already cleaned: no track names, file paths or serial numbers. They went with the report and are deleted 14 days after it was sent.') }));
      if (!items.length) body.append(el('p', { class: 'fine', text: XZText('The XZ sent no logs with this report, only the screen it was on.') }));
      else body.append(el('ul', { class: 'sr-items sr-xz-items' }, ...items.map(entry => {
        const open = v.preview === entry.item_id, lines = (entry.facts || []).length;
        return el('li', { class: 'sr-item' }, el('span', { class: 'sr-xz-source', text: (W.sources[entry.source_id] || entry.source_id.replace(/_/g, ' ')) + ' · ' + (SLOTS[entry.source_slot] || 'USB ' + entry.source_slot.slice(-1).toUpperCase()) }),
          el('small', { text: lines + (lines === 1 ? ' line' : ' lines') }),
          plainButton(open ? XZText('Hide') : XZText('Show'), () => { v.preview = open ? null : entry.item_id; redraw(); }, { text: true, key: 'xz-show-' + entry.item_id }),
          open ? el('pre', { class: 'sr-preview', tabindex: '0', 'aria-label': 'Exact content sent', text: JSON.stringify(entry, null, 1) }) : null);
      })));
    }
    panel.append(body);
    return panel;
  }
  async function readDetails(p) {
    const v = view(p.report_id);
    v.detailsError = ''; redraw();
    try { v.details = await invoke('support_pending_details', { id: p.report_id }); }
    catch { v.detailsError = "The details couldn't be read right now. Check the internet connection, then try again."; }
    redraw();
  }
  async function complete(p, comments) {
    const id = p.report_id, v = view(id);
    v.error = '';
    if (comments !== null && !comments.trim()) v.error = 'Write a few words first, or choose Send without comments.';
    else if (comments !== null && encoder.encode(comments).length > LIMIT) v.error = 'This is longer than ' + LIMIT.toLocaleString('en-GB') + ' bytes. Shorten it a little.';
    if (v.error) { v.message = v.error; redraw(); document.getElementById('xz-text-' + id)?.focus(); return; }
    v.state = 'sending'; v.message = 'Sending...'; redraw();
    try { apply(await invoke('support_complete_pending', { id, comments }), 'poll'); }
    catch (error) { v.state = 'idle'; v.error = clean(error); v.message = v.error; redraw(); document.getElementById('xz-text-' + id)?.focus(); return; }
    await reload();
    if (!pending.some(item => item.report_id === id)) {
      lastSent = { reference: p.report_reference, words: comments !== null };
      await window.myReports?.refresh();
      document.getElementById('sr-xz-title')?.focus({ preventScroll: true });
    } else if (views.has(id)) {
      const still = view(id);
      still.state = 'idle'; still.message = 'Saved on this computer. It goes by itself when XZ Mods can reach the report service.';
      redraw();
    }
  }

  // Your XZ sticks: which loaders may send reports to this computer, and Revoke.
  function sticks() {
    const loaders = [...(identity?.loaders || [])].sort((a, b) => Number(a.revoked) - Number(b.revoked) || String(b.created_at).localeCompare(String(a.created_at)));
    const parts = [el('h3', { class: 'sr-section-title', id: 'xz-sticks-title', tabindex: '-1', text: XZText('Your XZ sticks') })];
    if (!identity) parts.push(el('p', { class: 'fine', text: failure ? XZText('Your XZ sticks could not be read. ') + failure : XZText('Reading your XZ sticks...') }));
    else if (!loaders.length) parts.push(el('div', { class: 'st-note sr-xz-sticks-setup', dataset: { tone: 'setup' } }, el('strong', { text: XZText('No XZ sticks yet. ') }),
      XZText('Write XZ Mods to a USB stick and the XZ can send reports from it straight to this app.'),
      el('div', { class: 'actions' }, plainButton(XZText('Set up a USB'), () => showPage('setup'), { primary: true, key: 'xz-setup-usb' }))));
    else {
      parts.push(el('p', { class: 'sr-xz-sticks-lede', text: XZText('These sticks can send reports from the XZ to this computer. Revoke one you lost or gave away. Writing XZ Mods to it again gives it a new key.') }),
        el('ul', { class: 'sr-loaders', 'aria-labelledby': 'xz-sticks-title' }, ...loaders.map(loaderRow)));
      if (!identity.registered) parts.push(el('p', { class: 'fine', text: XZText('This computer registers with the report service the next time it is online.') }));
    }
    parts.push(el('p', { class: 'fine sr-xz-sticks-status', role: 'status', text: sticksView.message }));
    return el('section', { class: 'sr-xz-sticks', 'aria-labelledby': 'xz-sticks-title' }, ...parts);
  }
  function loaderRow(l) {
    const [tone, state] = l.revoked ? ['off', 'Revoked'] : l.revocation_pending ? ['warn', 'Revokes when online'] : l.write_pending ? ['warn', 'Plug it in to finish']
      : l.registration_pending ? ['warn', 'Registers when online'] : ['good', 'Can send reports'];
    const confirming = sticksView.confirm === l.loader_id, done = l.revoked || l.revocation_pending;
    return el('li', { class: 'sr-loader', dataset: { state: tone } }, icon('i-usb'),
      el('span', { class: 'sr-loader-name' }, el('strong', { class: 'sr-reference', text: l.loader_id }), el('small', { text: 'Written ' + day(l.created_at) })),
      el('span', { class: 'sr-tag', dataset: { tone }, text: state }),
      done ? el('span') : plainButton(XZText('Revoke'), () => { sticksView.confirm = l.loader_id; sticksView.message = ''; redraw(); document.querySelector('[data-key="xz-revoke-no"]')?.focus(); }, { key: 'xz-revoke-' + l.loader_id }),
      confirming ? el('div', { class: 'sr-confirm' }, 'Revoke ' + l.loader_id + XZText('? The XZ can no longer send reports from this stick. Writing XZ Mods to it again gives it a new key.'),
        el('div', { class: 'actions' }, plainButton(XZText('Revoke this stick'), () => revoke(l), { primary: true, key: 'xz-revoke-yes' }),
          plainButton(XZText('Keep it'), () => { sticksView.confirm = null; redraw(); document.querySelector('[data-key="xz-revoke-' + CSS.escape(l.loader_id) + '"]')?.focus(); }, { key: 'xz-revoke-no' }))) : null);
  }
  async function revoke(l) {
    sticksView.confirm = null; l.revocation_pending = true; redraw();
    try {
      const result = await invoke('support_revoke_loader', { loaderId: l.loader_id });
      apply(result, 'poll');
      if (result.checking || !result.connected) await reload();
      const now = identity?.loaders.find(x => x.loader_id === l.loader_id);
      sticksView.message = now?.revoked ? l.loader_id + ' is revoked.' : l.loader_id + ' is revoked on this computer. The report service hears of it the next time XZ Mods is online.';
    } catch (error) { sticksView.message = 'Revoke failed: ' + clean(error); await reload(); }
    redraw();
    document.getElementById('xz-sticks-title')?.focus({ preventScroll: true });
  }

  const shown = pageShown['my-reports'];
  pageShown['my-reports'] = () => { calloutOn = false; lastSent = null; shown(); paint(); if (invoke) reload(); };
  if (invoke) {
    listen?.('xz-controller-queue-changed', event => apply(event.payload, 'event'));
    listen?.('xz-controller-report-open', event => open(event.payload?.id));
    reload();
    // The launch check ends within two minutes; anything after that arrived while the app was open.
    setTimeout(() => { startup = false; }, 130000);
  }
  // count: reports waiting for words. While there are any, Send is the one primary action on My reports.
  return { has: id => pending.some(p => p.report_id === id), count: () => pending.length, section, sticks, poll, reload, open, screen, state: () => ({ pending, identity, calloutOn, toasts, startup }) };
})();
