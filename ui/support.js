// Report a problem or share an idea. The only required input is one box. Up to three optional screens come first,
// each with "Not sure". Details are collected on this computer while the reporter types; Send freezes what has been
// collected, records consent to those exact bytes, and sends them privately. Ideas collect nothing.
// Reporting code lives here and in my-reports.js; app.js keeps only the shared job, dialog and wizard helpers.
'use strict';
window.reportWizard = (() => {
  const C = window.XZ_SUPPORT_CONTRACT, KB = window.XZ_SUPPORT_KB, K = window.supportKnowledge;
  const dialog = $('report-wizard'), connection = $('report-connection');
  // Reading report details never refuses another action: a click elsewhere waits for the bounded read instead.
  yieldingMethods.push('support_sources', 'support_collect', 'support_source_preview', 'support_list');
  const LIMIT = C.limits.description_bytes, encoder = new TextEncoder();
  const byteLength = text => encoder.encode(text).length;
  const kb = bytes => bytes < 1024 ? bytes + ' bytes' : (bytes / 1024).toFixed(bytes < 10240 ? 1 : 0) + ' KB';
  async function sha256(text) {
    const digest = await crypto.subtle.digest('SHA-256', encoder.encode(text));
    return [...new Uint8Array(digest)].map(b => b.toString(16).padStart(2, '0')).join('');
  }
  function icon(id) {
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg'), use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
    svg.setAttribute('class', 'icon'); svg.setAttribute('aria-hidden', 'true'); use.setAttribute('href', '#' + id); svg.append(use);
    return svg;
  }
  const day = iso => iso ? new Date(iso).toLocaleDateString('en-GB', { day: 'numeric', month: 'short', year: 'numeric' }) : 'Unknown';

  // Words for every reporter choice. Values come from the contract; a value without words is never offered.
  const WORDS = {
    topic: {
      GOT_WORSE: ['Something got worse', "It used to work, or worked better, and now it doesn't."],
      NOT_WORKING: ["Something isn't working", 'It stops, crashes, sounds wrong or shows something wrong.'],
      WORK_DIFFERENTLY: ['It should work differently', "It works, but not the way you'd expect."],
      NEW_IDEA: ['I have a new idea', "Something XZ Mods doesn't do at all."],
      HOW_DO_I: ['How do I...?', 'A question about using XZ Mods.']
    },
    degradation_timing: {
      AFTER_UPDATE: ['After updating XZ Mods'], DURING_SET_OVER_TIME: ['During a set, over time', 'Dropouts grow, the screen slows, the tempo drifts.'],
      AFTER_SETTING: ['After changing a setting'], SUDDENLY_DURING_SET: ['Suddenly, during a set']
    },
    problem_area: {
      SOUND: ['Sound', 'Dropout, glitch, wrong deck or silence'], SYNC_TEMPO: ['SYNC or tempo'], SCREEN_DISPLAY: ['Screen or display'],
      BUTTONS_PADS_LAMPS: ['Buttons, pads or lamps'], LOADING_BROWSING_USB: ['Loading, browsing or USB'],
      FOUR_DECKS_LOOPBACK: ['Four decks or the loopback cable'], CRASH_FREEZE: ['Crash or freeze'], OTHER: ['Something else']
    },
    expectation_baseline: {
      STOCK_XZ: ['The XZ did it this way before XZ Mods'],
      XZ_CONSISTENCY: ['Another XZ feature works this way', 'For example, decks 3 and 4 should do what decks 1 and 2 do.'],
      DJ_CONVENTION: ['Other DJ gear or software works this way', 'CDJs, rekordbox, Serato, Traktor...'],
      XZ_MODS_PREVIOUS: ['An earlier XZ Mods version did it this way'],
      PREFERENCE: ['It just feels wrong to me']
    },
    reference_feature: {
      CDJ_3000: ['CDJ-3000'], CDJ_2000NXS2: ['CDJ-2000NXS2'], XDJ_RX3: ['XDJ-RX3'], REKORDBOX: ['rekordbox'], SERATO: ['Serato'],
      TRAKTOR: ['Traktor'], DECKS_1_2: ['Decks 1 and 2'], XZ_MODS_FEATURE: ['Another XZ Mods feature']
    },
    idea_type: {
      NEW_FEATURE: ['A new feature'], FASTER_WORKFLOW: ['A faster way to do something I already do'],
      APPEARANCE: ['A new look', 'Themes, colours, layouts'], INTEGRATION: ['Working with other software or gear', 'VJ.Tools, laptops, lighting'],
      OTHER: ['Something else']
    }
  };
  for (const [field, words] of Object.entries(WORDS)) for (const value of Object.keys(words)) {
    if (!C.reporter_hints[field]?.includes(value)) console.error('Report wizard offers a value the contract does not know:', field, value);
  }
  const choices = field => C.reporter_hints[field].filter(value => WORDS[field][value]).map(value => [value, ...WORDS[field][value]]);
  const REFERENCES = { XZ_CONSISTENCY: ['DECKS_1_2', 'XZ_MODS_FEATURE'], DJ_CONVENTION: ['CDJ_3000', 'CDJ_2000NXS2', 'XDJ_RX3', 'REKORDBOX', 'SERATO', 'TRAKTOR'] };
  const SCREENS = {
    topic: { field: 'topic', title: "What's this about?", label: 'Topic', lede: 'Pick the closest one, or choose Not sure. Only the next box is required.' },
    timing: { field: 'degradation_timing', title: 'When did it get worse?', label: 'When' },
    area: { field: 'problem_area', title: 'What kind of problem?', label: 'Problem', lede: 'This decides which details XZ Mods looks at.' },
    baseline: { field: 'expectation_baseline', title: 'Compared with what?', label: 'Compared with' },
    reference: { field: 'reference_feature', title: 'Which gear or feature?', label: 'Which one', lede: 'Optional. Pick the one that works the way you expect.', chips: true },
    idea: { field: 'idea_type', title: 'What kind of idea?', label: 'Idea' },
    usb: { title: 'Plug in every USB stick that was in the XZ when it happened', label: 'USB sticks' },
    describe: { label: 'Describe' }
  };
  // The question screens for a topic, in order. Never more than three before the box with Send.
  function path() {
    const hints = rw.hints;
    switch (hints.topic) {
      case 'GOT_WORSE': return ['topic', 'timing', 'usb'];
      case 'NOT_WORKING': return ['topic', 'area'];
      case 'NEW_IDEA': return ['topic', 'idea'];
      case 'WORK_DIFFERENTLY':
        if (hints.expectation_baseline === 'XZ_MODS_PREVIOUS') return ['topic', 'baseline', 'usb'];
        return REFERENCES[hints.expectation_baseline] ? ['topic', 'baseline', 'reference'] : ['topic', 'baseline'];
      default: return ['topic'];
    }
  }
  const steps = () => [...path(), 'describe'];

  const SOURCES = {
    usb_session: 'Session log', usb_settings: 'XZ Mods settings', native_export: 'Report saved by the player', usb_diary: 'Diary',
    app_events: 'App events', loader_status: 'Four-deck status', packaged_manifest: 'App build', packaged_profile: 'Four-deck package'
  };
  const MISSING = {
    missing: 'Not found', removed: 'Stick removed while reading', budget: 'Ran out of time', cancelled: 'Stopped at Send', oversized: 'Too large to include',
    invalid: 'Could not be read', unsafe_path: 'Refused: linked folder', no_matching_schema: 'Settings format not recognised', changed: 'Changed while reading', unavailable: 'Not available'
  };

  const blankScan = () => ({ state: 'idle', token: 0, generation: null, key: null, sticks: [], others: [], excess: 0, items: [], diary: {}, bytes: 0, incident: null, error: '', digest: null, job: null, promise: null, again: false, frozen: false });
  const blank = () => ({ screen: 'topic', hints: {}, text: '', textError: '', cards: [], scan: blankScan(), omit: new Set(), detailsOpen: false, preview: null, sending: null, draft: null, receipt: null, failure: null });
  let rw = blank(), ui = {}, watch = null, matchTimer = null;
  const live = () => dialog.querySelector('.sr-live');
  const say = text => { const node = live(); if (node) node.textContent = text; };
  const idea = () => rw.hints.topic === 'NEW_IDEA';
  const hints = () => Object.fromEntries(Object.entries(rw.hints).filter(([, value]) => value));

  // Frame: the shared step strip, a focusable title, the body and one footer.
  function frame(view, labels, index) {
    const title = el('h2', { id: 'report-wizard-title', tabindex: '-1', text: view.title });
    dialog.replaceChildren(...(labels ? [stepStrip(labels, index)] : []), title, ...view.body.filter(Boolean), el('div', { class: 'st-foot' }, ...view.foot.filter(Boolean)), el('p', { class: 'visually-hidden sr-live', role: 'status', 'aria-live': 'polite' }));
    (dialog.querySelector(view.focus || '[aria-pressed=true]') || dialog.querySelector('.sr-choice,.sr-chip,.sr-box') || title).focus({ preventScroll: true });
  }
  const cancelButton = () => el('span', { class: 'st-foot-left' }, plainButton('Cancel', close, { text: true, key: 'cancel' }));
  function render() {
    const screen = rw.screen, inFlow = steps().includes(screen);
    const view = screen === 'usb' ? usbView() : screen === 'describe' ? describeView() : screen === 'sending' ? sendingView() : screen === 'done' ? doneView()
      : screen === 'failed' ? failedView() : screen === 'answered' ? answeredView() : choiceView(screen);
    frame(view, inFlow ? steps().map(name => SCREENS[name].label) : null, inFlow ? steps().indexOf(screen) : 0);
    if (screen === 'usb') startWatching(); else stopWatching();
    if (screen === 'describe') { ensureCollection(); updateAnswers(); }
  }
  function go(screen) { rw.screen = screen; render(); }
  function next() { const order = path(), index = order.indexOf(rw.screen); go(order[index + 1] || 'describe'); }
  function back() { const order = steps(), index = order.indexOf(rw.screen); if (index > 0) go(order[index - 1]); }

  function choose(field, value) {
    if (field === 'topic' && value !== rw.hints.topic) rw.hints = { topic: value };
    else rw.hints[field] = value;
    if (field === 'expectation_baseline' && !(REFERENCES[value] || []).includes(rw.hints.reference_feature)) delete rw.hints.reference_feature;
    if (idea()) discardScan();
    next();
  }
  function notSure(field) {
    if (field === 'topic') rw.hints = {};
    else delete rw.hints[field];
    if (field === 'expectation_baseline') delete rw.hints.reference_feature;
    next();
  }
  function choiceView(name) {
    const screen = SCREENS[name], field = screen.field, current = rw.hints[field];
    let options = choices(field);
    if (field === 'reference_feature') options = options.filter(([value]) => (REFERENCES[rw.hints.expectation_baseline] || []).includes(value));
    const group = screen.chips
      ? el('div', { class: 'sr-chips', role: 'group', 'aria-labelledby': 'report-wizard-title' }, ...options.map(([value, label]) =>
        el('button', { type: 'button', class: 'sr-chip', 'aria-pressed': String(current === value), dataset: { key: 'c-' + value }, onclick: () => choose(field, value), text: label })))
      : el('div', { class: 'sr-choices' + (options.length > 5 ? ' sr-two' : ''), role: 'group', 'aria-labelledby': 'report-wizard-title' }, ...options.map(([value, label, hint]) =>
        el('button', { type: 'button', class: 'sr-choice', 'aria-pressed': String(current === value), dataset: { key: 'c-' + value }, onclick: () => choose(field, value) },
          el('strong', { text: label }), hint ? el('span', { text: hint }) : null, icon('i-chevron'))));
    return {
      title: screen.title,
      body: [screen.lede ? el('p', { class: 'sr-lede', text: screen.lede }) : null, group],
      foot: [cancelButton(), name !== 'topic' ? plainButton('Back', back, { key: 'back' }) : null, plainButton('Not sure', () => notSure(field), { key: 'skip' })]
    };
  }

  // Degradation: every stick that was in the XZ, detected live, with what each one holds.
  function usbView() {
    ui.sticks = el('ul', { class: 'sr-sticks', 'aria-live': 'polite' });
    renderSticks();
    return {
      title: SCREENS.usb.title,
      body: [el('p', { class: 'sr-lede', text: 'The XZ keeps a short diary on each stick while it plays. We read only that diary, never your music. Missing sticks never stop you sending.' }), ui.sticks],
      foot: [cancelButton(), plainButton('Back', back, { key: 'back' }), plainButton('Not sure', next, { key: 'skip' }), plainButton("That's all of them", next, { primary: true, key: 'done-sticks' })],
      focus: '[data-key="done-sticks"]'
    };
  }
  const clock = iso => new Date(iso).toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit' });
  function diaryLine(coverage, s) {
    if (!coverage) return s.state === 'ready' ? ['none', 'No diary on this stick'] : ['reading', 'Reading the diary...'];
    if (coverage.state === 'absent') return ['none', 'No diary on this stick'];
    if (coverage.state !== 'present') return ['bad', 'The diary on this stick could not be read'];
    if (coverage.start_utc && coverage.end_utc) {
      const start = new Date(coverage.start_utc), end = new Date(coverage.end_utc), sameDay = start.toDateString() === end.toDateString();
      const when = sameDay ? clock(coverage.start_utc) + ' to ' + clock(coverage.end_utc)
        : start.toLocaleDateString('en-GB', { weekday: 'short' }) + ' ' + clock(coverage.start_utc) + ' to ' + end.toLocaleDateString('en-GB', { weekday: 'short' }) + ' ' + clock(coverage.end_utc);
      return ['ok', 'Diary ' + when];
    }
    const minutes = Math.round((coverage.duration_seconds || 0) / 60);
    return ['ok', 'Diary found, times unknown (' + (minutes >= 60 ? Math.floor(minutes / 60) + ' h ' + (minutes % 60) + ' min' : minutes + ' min') + ')'];
  }
  function renderSticks() {
    if (!ui.sticks?.isConnected && rw.screen !== 'usb') return;
    const s = rw.scan, rows = [];
    for (const stick of s.sticks) {
      const loader = stick.sources.usb_session || stick.sources.usb_settings, [tone, line] = diaryLine(s.diary[stick.source_slot], s);
      rows.push(el('li', { class: 'sr-stick', dataset: { diary: tone } }, icon('i-usb'), el('strong', { text: (loader ? 'USB with XZ Mods loader' : 'Music stick') + ' (' + stick.local_volume.replace(/[\\/]+$/, '') + ')' }), el('span', { text: line })));
    }
    for (const other of s.others) rows.push(el('li', { class: 'sr-stick', dataset: { diary: 'none' } }, icon('i-usb'), el('strong', { text: 'Music stick (' + other.local_volume.replace(/[\\/]+$/, '') + ')' }), el('span', { text: 'No diary on this stick' })));
    if (!rows.length) rows.push(el('li', { class: 'sr-waiting' }, s.state === 'detecting' || s.state === 'collecting' || s.state === 'idle' ? 'Looking for USB sticks...' : 'No USB sticks found yet. Plug them in now; they appear here by themselves.'));
    if (s.excess) rows.push(el('li', { class: 'st-note', dataset: { tone: 'warn' }, text: 'More than four sticks are plugged in. XZ Mods reads the first four.' }));
    ui.sticks.replaceChildren(...rows);
  }
  // Live detection: the drive list is cheap and touches no media; a change starts a fresh bounded scan.
  function startWatching() {
    if (watch) return;
    watch = { volumes: null, timer: null };
    const poll = async () => {
      if (!watch || rw.screen !== 'usb' || !dialog.open) return stopWatching();
      let volumes = '';
      try { volumes = (await invoke('list_volumes')).join('|'); } catch { volumes = '?'; }
      if (!watch) return;
      if (volumes !== watch.volumes) { watch.volumes = volumes; collect(true); }
      watch.timer = setTimeout(poll, 1500);
    };
    poll();
  }
  function stopWatching() { if (watch) clearTimeout(watch.timer); watch = null; }

  // Collection. Each scan is a new generation; results for an older generation or a closed dialog are dropped.
  async function job(request, scan) {
    await idle();
    if (scan && scan !== rw.scan) return null;
    const state = await run(request, 'Collecting report details', null, () => { if (scan) scan.job = activeJob; });
    if (scan) scan.job = null;
    return state || { ok: false, error: 'Another task is running. Your report can still be sent without details.' };
  }
  function collect(fresh = false) {
    const s = rw.scan;
    if (idea() || s.frozen || !dialog.open) return;
    if (s.state === 'detecting' || s.state === 'collecting') { s.again = true; return; }
    if (!fresh && s.state === 'ready' && s.key === JSON.stringify(hints())) return;
    s.promise = (async () => {
      const token = ++s.token, key = JSON.stringify(hints());
      s.state = 'detecting'; s.error = ''; refresh();
      const found = await job({ method: 'support_sources', ...(s.generation ? { supersedes: s.generation } : {}) }, s);
      if (!found || token !== s.token) return;
      if (!found.ok) { s.state = 'failed'; s.error = found.error || 'USB sticks could not be listed'; refresh(); return; }
      Object.assign(s, { generation: found.result.scan_generation, sticks: found.result.sticks, others: found.result.other_sticks || [], excess: found.result.excess_sticks, diary: {}, items: [] });
      s.state = 'collecting'; refresh();
      say('Collecting details from ' + (s.sticks.length ? s.sticks.length + ' USB stick' + (s.sticks.length === 1 ? '' : 's') + ' and ' : '') + 'this computer.');
      const summary = await job({ method: 'support_collect', scan_generation: s.generation, reporter_hints: hints() }, s);
      if (!summary || token !== s.token) return;
      if (!summary.ok) { s.state = 'failed'; s.error = summary.error || 'Details could not be collected'; refresh(); return; }
      Object.assign(s, { state: 'ready', key, items: summary.result.items, diary: summary.result.diary_coverage || {}, bytes: summary.result.bytes, incident: summary.result.incident });
      rw.omit = new Set([...rw.omit].filter(id => s.items.some(item => item.item_id === id)));
      await updateDigest();
      refresh();
      say('Details collected. ' + included().length + ' sources will be included.');
    })().finally(() => { if (s.again && s === rw.scan && !s.frozen) { s.again = false; collect(true); } });
    return s.promise;
  }
  function ensureCollection() { if (idea()) discardScan(); else if (rw.scan.key !== JSON.stringify(hints()) || rw.scan.state === 'idle') collect(rw.scan.state !== 'idle'); }
  async function stopCollection() {
    const s = rw.scan;
    s.again = false;
    if (s.job !== null && s.job !== 'starting' && s.job === activeJob) { try { await invoke('cancel_job', { id: s.job }); } catch {} }
    if (s.promise) await s.promise.catch(() => {});
  }
  function discardScan() { const old = rw.scan; old.token++; stopCollection(); rw.scan = { ...blankScan(), generation: old.generation, token: old.token }; rw.omit.clear(); }
  const eligible = () => rw.scan.items.filter(item => item.eligible);
  const included = () => eligible().filter(item => !rw.omit.has(item.item_id));
  // The same digest the collector checks at Send: [item_id, sha256] pairs of the included items, in order.
  async function updateDigest() { rw.scan.digest = await sha256(JSON.stringify(included().map(item => [item.item_id, item.sha256]))); }
  function refresh() { if (rw.screen === 'usb') renderSticks(); if (rw.screen === 'describe') renderDetails(); }

  function slotName(slot) {
    if (slot === 'app') return 'this computer';
    if (slot === 'package') return 'this app';
    const stick = rw.scan.sticks.find(item => item.source_slot === slot);
    return stick ? 'USB ' + slot.slice(-1).toUpperCase() + ' (' + stick.local_volume.replace(/[\\/]+$/, '') + ')' : 'USB ' + slot.slice(-1).toUpperCase();
  }

  function describeView() {
    const topic = rw.hints.topic;
    const title = topic === 'NEW_IDEA' ? "What's your idea?" : topic === 'HOW_DO_I' ? "What's your question?" : 'What happened?';
    const placeholder = { NEW_IDEA: "Describe what you'd like XZ Mods to do. One sentence is enough.", HOW_DO_I: 'Ask your question. Answers from the manual appear below as you type.',
      GOT_WORSE: 'What got worse, and since when? One sentence is enough.', WORK_DIFFERENTLY: 'What does it do now, and what did you expect?' }[topic] || 'Say what you did and what you saw. One sentence is enough.';
    const context = Object.entries(hints()).map(([field, value]) => WORDS[field]?.[value]?.[0]).filter(Boolean);
    ui.box = el('textarea', { class: 'sr-box', id: 'report-text', rows: '5', 'aria-labelledby': 'report-wizard-title', 'aria-describedby': 'report-text-note', placeholder, spellcheck: 'true' });
    ui.box.value = rw.text;
    ui.box.oninput = () => { rw.text = ui.box.value; rw.textError = ''; renderCount(); clearTimeout(matchTimer); matchTimer = setTimeout(updateAnswers, 250); };
    ui.note = el('div', { class: 'sr-box-foot', id: 'report-text-note' });
    ui.answers = el('section', { class: 'sr-answers', 'aria-live': 'polite', 'aria-label': 'Is this it?' });
    ui.details = el('div');
    renderCount(); renderDetails();
    return {
      title,
      body: [context.length ? el('p', { class: 'sr-context' }, ...context.flatMap((word, index) => [index ? el('span', { text: '·' }) : null, el('b', { text: word })]), plainButton('Change', () => go('topic'), { text: true, key: 'change' })) : null,
        ui.box, ui.note, ui.answers, ui.details,
        el('p', { class: 'sr-privacy' }, el('b', { text: 'Private. ' }), idea() ? 'Your idea goes only to the maintainer. Nothing is posted publicly.'
          : 'Your words and the included details go only to the maintainer. Details are deleted after ' + C.retention.diagnostic_days + ' days; your receipt keeps tracking longer.')],
      foot: [cancelButton(), plainButton('Back', back, { key: 'back' }), plainButton('Send privately', send, { primary: true, key: 'send' })],
      focus: '.sr-box'
    };
  }
  function renderCount() {
    if (!ui.note) return;
    const used = byteLength(rw.text), near = used > LIMIT - 600;
    ui.box.setAttribute('aria-invalid', String(Boolean(rw.textError)));
    ui.note.replaceChildren(...[rw.textError ? el('span', { class: 'sr-error', text: rw.textError }) : el('span', { text: '' }),
      near ? el('span', { class: 'sr-count', dataset: { near: used <= LIMIT ? '' : undefined, over: used > LIMIT ? '' : undefined }, text: used.toLocaleString('en-GB') + ' of ' + LIMIT.toLocaleString('en-GB') + ' bytes' }) : null].filter(Boolean));
  }
  // Is this it? Local matching only. At most three answers; each can run its registered setup action.
  const ACTIONS = {
    OPEN_FOUR_DECK_SETUP: () => $('quad-open')?.click(),
    OPEN_USB_SETUP: () => { close(); showPage('setup'); }
  };
  function updateAnswers() {
    if (!ui.answers?.isConnected) return;
    rw.cards = K.match(KB, rw.text, 3);
    ui.answers.replaceChildren(...(rw.cards.length ? [el('h3', { text: 'Is this it?' }), ...rw.cards.map(entry => el('article', { class: 'sr-answer', dataset: { entry: entry.entry_id } },
      el('strong', { text: entry.title }), el('p', { text: entry.answer }), el('small', { class: 'sr-source', text: entry.source }),
      el('div', { class: 'actions' }, entry.action && ACTIONS[entry.action.id] ? plainButton(entry.action.label, () => ACTIONS[entry.action.id](), { key: 'a-' + entry.entry_id }) : null,
        plainButton('That answers it', () => go('answered'), { text: true, key: 'ok-' + entry.entry_id }))))] : []));
  }
  function answeredView() {
    return { title: 'Glad that helped', body: [el('p', { text: 'Nothing was sent. If it happens again, report it here and it goes privately to the maintainer.' })],
      foot: [plainButton('Done', () => { rw = blank(); dialog.close(); }, { primary: true, key: 'done' })] };
  }
  function renderDetails() {
    if (!ui.details) return;
    if (idea()) { ui.details.replaceChildren(el('div', { class: 'st-note', text: "Ideas don't include logs from this computer or your USB sticks. Only your words are sent." })); return; }
    const s = rw.scan, all = eligible(), chosen = included(), bytes = chosen.reduce((sum, item) => sum + item.bytes, 0);
    const label = s.state === 'ready' ? 'Included details' : s.state === 'failed' ? "Details couldn't be collected. You can still send." : s.state === 'detecting' ? 'Looking for details to include' : 'Collecting details. You can send now.';
    const meta = s.state === 'ready' ? chosen.length + ' of ' + all.length + ' sources · ' + kb(bytes) + (s.incident ? ' · last ' + Math.round((s.incident.before_seconds + s.incident.after_seconds) / 60) + ' min' : '') : '';
    const details = el('details', { class: 'sr-details', dataset: { state: s.state } }, el('summary', {}, el('b', { text: label }), meta ? el('span', { class: 'sr-meta', text: meta }) : null));
    details.open = rw.detailsOpen;
    details.ontoggle = () => { rw.detailsOpen = details.open; };
    const body = el('div', { class: 'sr-detail-body' });
    if (s.state === 'ready') {
      body.append(el('p', { class: 'fine', text: 'Exactly what is sent, already cleaned: no track names, file paths, serial numbers or network addresses. Untick anything you would rather keep.' }),
        el('ul', { class: 'sr-items' }, ...all.map(item => itemRow(item))),
        el('p', { class: 'sr-fingerprint', text: 'Selection SHA256 ' + (s.digest || '...') + ' · Send approves exactly this selection.' }));
    } else if (s.state === 'failed') body.append(el('p', { class: 'fine', text: s.error }));
    else body.append(el('p', { class: 'fine', text: 'XZ Mods reads fixed files only, for at most ' + C.limits.scan_seconds + ' seconds. Pressing Send keeps whatever has been read by then.' }));
    body.append(el('div', { class: 'actions' }, plainButton('Collect again', () => collect(true), { key: 'recollect', disabled: s.state === 'detecting' || s.state === 'collecting' })));
    details.append(body);
    const focused = ui.details.contains(document.activeElement) ? document.activeElement.dataset.key : null;
    ui.details.replaceChildren(details);
    if (focused) ui.details.querySelector('[data-key="' + CSS.escape(focused) + '"]')?.focus({ preventScroll: true });
  }
  function itemRow(item) {
    const id = 'sr-item-' + item.item_id, omitted = rw.omit.has(item.item_id), open = rw.preview?.item === item.item_id;
    const parts = [item.missing_reason ? MISSING[item.missing_reason] || 'Not available' : (item.facts + ' line' + (item.facts === 1 ? '' : 's')), kb(item.bytes)];
    if (item.truncated) parts.push('latest part only');
    if (item.clock_confidence === 'unknown') parts.push('times unknown');
    const box = el('input', { type: 'checkbox', id, dataset: { key: 'inc-' + item.item_id } });
    box.checked = !omitted;
    box.onchange = async () => { if (box.checked) rw.omit.delete(item.item_id); else rw.omit.add(item.item_id); await updateDigest(); renderDetails(); };
    const row = el('li', { class: 'sr-item', dataset: { omitted: omitted ? '' : undefined } }, box,
      el('label', { for: id, text: (SOURCES[item.source_id] || item.source_id) + ' · ' + slotName(item.source_slot) }), el('small', { text: parts.join(' · ') }),
      plainButton(open ? 'Hide' : 'Show', () => togglePreview(item.item_id), { text: true, key: 'show-' + item.item_id }));
    if (open) {
      row.append(el('pre', { class: 'sr-preview', tabindex: '0', 'aria-label': 'Exact cleaned content', text: rw.preview.text || 'Loading...' }));
      if (rw.preview.pages > 1) row.append(el('div', { class: 'sr-preview-nav' }, plainButton('Previous', () => loadPreview(rw.preview.page - 1), { key: 'pv-prev', disabled: rw.preview.page === 0 }),
        el('span', { text: 'Page ' + (rw.preview.page + 1) + ' of ' + rw.preview.pages }), plainButton('Next', () => loadPreview(rw.preview.page + 1), { key: 'pv-next', disabled: rw.preview.page + 1 >= rw.preview.pages })));
    }
    return row;
  }
  function togglePreview(itemId) { if (rw.preview?.item === itemId) { rw.preview = null; renderDetails(); } else { rw.preview = { item: itemId, page: 0, pages: 1, text: '' }; renderDetails(); loadPreview(0); } }
  async function loadPreview(page) {
    const preview = rw.preview, generation = rw.scan.generation;
    if (!preview) return;
    const result = await job({ method: 'support_source_preview', scan_generation: generation, item_id: preview.item, page });
    if (rw.preview !== preview) return;
    if (result.ok) Object.assign(preview, { page: result.result.page, pages: result.result.pages, text: result.result.text });
    else preview.text = result.error || 'This content could not be shown.';
    renderDetails();
  }

  // Send: freeze the details read so far, seal the exact bytes with consent, upload privately.
  async function send() {
    const text = rw.text;
    if (!text.trim()) { rw.textError = 'Write a few words first. One sentence is enough.'; renderCount(); ui.box.focus(); say(rw.textError); return; }
    if (byteLength(text) > LIMIT) { rw.textError = 'This is longer than ' + LIMIT.toLocaleString('en-GB') + ' bytes. Shorten it a little.'; renderCount(); ui.box.focus(); say(rw.textError); return; }
    rw.sending = { stage: 0, cancelled: false }; rw.failure = null; go('sending');
    const s = rw.scan;
    if (s.state === 'detecting' || s.state === 'collecting') { await stopCollection(); }
    s.frozen = true; stopWatching();
    const usable = !idea() && s.state === 'ready';
    if (usable) await updateDigest();
    const digest = usable ? s.digest : await sha256('[]');
    if (rw.sending.cancelled) return;
    rw.sending.stage = 1; render();
    const prepared = await job({ method: 'support_prepare_v2', description: text, reporter_hints: hints(), selection_sha256: digest, consent: C.consent,
      ...(usable ? { scan_generation: s.generation, omit_item_ids: [...rw.omit] } : { collect: false }) });
    if (rw.sending.cancelled) {
      // Cancelled while sealing: the sealed copy is removed, so nothing is left that could be sent.
      if (prepared.ok) job({ method: 'support_discard', report_id: prepared.result.report_id, sha256: prepared.result.sha256 });
      return;
    }
    if (!prepared.ok) { rw.failure = { kind: 'prepare', message: prepared.error || 'The report could not be prepared.' }; go('failed'); return; }
    rw.draft = { report_id: prepared.result.report_id, sha256: prepared.result.sha256, bytes: prepared.result.bytes };
    rw.sending.stage = 2; render();
    await upload();
  }
  async function upload() {
    try {
      rw.receipt = await invoke('support_send', { id: rw.draft.report_id, sha256: rw.draft.sha256 });
      go('done'); say('Received privately.');
    } catch (error) {
      rw.failure = { kind: 'send', message: String(error) }; go('failed');
    }
    window.myReports?.refresh();
  }
  async function cancelSend() {
    if (!rw.sending) return;
    rw.sending.cancelled = true;
    if (rw.sending.stage < 2) { await stopCollection(); rw.scan.frozen = false; rw.sending = null; go('describe'); say('Sending cancelled. Nothing was sent.'); return; }
    try { await invoke('support_cancel'); } catch {}
  }
  function sendingView() {
    const stage = rw.sending?.stage ?? 0;
    return {
      title: 'Sending privately',
      body: [stageList([['Keeping the details read so far', ''], ['Sealing your report', ''], ['Sending to the maintainer', '']], stage, null),
        el('p', { text: stage < 2 ? 'Cancel now and nothing is sent.' : 'If you cancel now, the report may still arrive. My reports checks for you.' })],
      foot: [plainButton('Cancel', cancelSend, { key: 'cancel-send' })],
      focus: '[data-key="cancel-send"]'
    };
  }
  function doneView() {
    const r = rw.receipt;
    return {
      title: idea() ? 'Idea received' : 'Report received',
      body: [el('div', { class: 'sr-receipt' }, icon('i-check'), el('strong', { text: 'Your receipt' }), el('p', { text: r.status_text || 'Received privately. Track it here.' }),
        el('dl', {}, el('dt', { text: 'Receipt' }), el('dd', { text: 'Report ' + r.report_id.slice(0, 8) }),
          ...(idea() ? [] : [el('dt', { text: 'Details deleted' }), el('dd', { text: day(r.diagnostics_expires_at) })]),
          el('dt', { text: 'Tracking until' }), el('dd', { text: r.retention_until ? day(r.retention_until) + ', or longer while it is open' : 'While it is open' }))),
      el('p', { text: 'Your receipt is kept on this computer, protected by Windows. XZ Mods checks for news when it starts and shows a notice here when the status changes.' })],
      foot: [plainButton('Track my report', () => { const id = r.report_id; rw = blank(); dialog.close(); window.myReports?.show(id); }, { key: 'track' }),
        plainButton('Done', () => { rw = blank(); dialog.close(); }, { primary: true, key: 'done' })],
      focus: '[data-key="done"]'
    };
  }
  const REFUSALS = { quota: 'The report service has had too many reports from this network today. Try again tomorrow.', rate_identity_unavailable: 'The report service could not check this connection. Try again later.',
    too_large: 'This report is too large to send. Untick some details and send again.', conflict: 'A different report already uses this receipt. Start a new report.', retained_report_id: 'A different report already uses this receipt. Start a new report.' };
  function failedView() {
    const f = rw.failure;
    if (f.kind === 'prepare') return {
      title: "Your report wasn't prepared", body: [el('div', { class: 'st-fail' }, el('strong', { text: 'Nothing was sent' }), el('p', { text: f.message }))],
      foot: [cancelButton(), plainButton('Back to my report', () => { rw.scan.frozen = false; go('describe'); }, { key: 'back' }), plainButton('Try again', send, { primary: true, key: 'retry' })]
    };
    const code = /\(([a-z_]+)\)/.exec(f.message)?.[1];
    return {
      title: 'Not confirmed yet',
      body: [el('p', { text: "Your report is saved on this computer and nothing is lost. We couldn't confirm that it reached the maintainer." }),
        el('div', { class: 'st-note', dataset: { tone: 'warn' } }, el('strong', { text: REFUSALS[code] ? 'The report service refused it' : 'No confirmation' }), ' ', REFUSALS[code] || 'The connection failed or was cancelled. Sending again uses the same report, so it never arrives twice.'),
        el('p', { class: 'fine', text: 'It is listed in My reports as Not confirmed.' })],
      foot: [el('span', { class: 'st-foot-left' }, plainButton('Close', () => { rw = blank(); dialog.close(); }, { text: true, key: 'close' })),
        plainButton('Save to a file', exportDraft, { key: 'export' }), plainButton('Set up reporting connection', () => openConnection(rw.draft, receipt => { rw.receipt = receipt; go('done'); }), { primary: true, key: 'setup' })],
      focus: '[data-key="setup"]'
    };
  }
  async function exportDraft() {
    try { const result = await invoke('support_export', { id: rw.draft.report_id, sha256: rw.draft.sha256 }); say(result.exported ? 'Saved to a file. Nothing was sent.' : 'Not saved.'); }
    catch (error) { say(String(error)); }
  }

  // Setup flow for a send that was not confirmed: detect the connection, check the clock, send the same report again.
  let conn = null;
  function openConnection(target, onSent) {
    conn = { target, onSent, state: 'idle', message: '' };
    renderConnection();
    if (!connection.open) connection.showModal();
  }
  function renderConnection() {
    const online = navigator.onLine, now = new Date();
    const status = conn.state === 'sending' ? 'Sending the same report again...' : conn.message;
    connection.replaceChildren(stepStrip(['Connection', 'Clock', 'Send again'], conn.state === 'sent' ? 3 : 2, conn.state === 'failed' ? 'bad' : 'now'),
      el('h2', { id: 'report-connection-title', tabindex: '-1', text: 'Set up reporting connection' }),
      el('h3', { text: '1. Connect to the internet' }),
      el('div', { class: 'st-note', dataset: { tone: online ? 'ok' : 'warn' } }, el('strong', { text: online ? 'Connected. ' : 'Offline. ' }), online ? 'This computer has a network connection.' : 'Connect to Wi-Fi or a network cable, then send again.'),
      el('h3', { text: '2. Check the date and time' }),
      el('p', { text: 'This computer says it is ' + now.toLocaleDateString('en-GB', { day: 'numeric', month: 'long', year: 'numeric' }) + ', ' + now.toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit' }) + '. Secure connections fail when the date is wrong; correct it in the Windows clock settings if needed.' }),
      el('h3', { text: '3. Send again' }),
      el('p', { text: 'XZ Mods sends the same report again. If the first attempt arrived, you get its receipt and nothing arrives twice.' }),
      el('p', { role: 'status', class: conn.state === 'failed' ? 'sr-error' : '', text: status }),
      el('div', { class: 'st-foot' }, plainButton('Close', () => connection.close(), { key: 'conn-close' }),
        plainButton(conn.state === 'sending' ? 'Sending...' : 'Send again', sendAgain, { primary: true, key: 'conn-send', disabled: conn.state === 'sending' })));
    (connection.querySelector('[data-key="conn-send"]:not(:disabled)') || connection.querySelector('h2')).focus({ preventScroll: true });
  }
  async function sendAgain() {
    conn.state = 'sending'; renderConnection();
    try {
      const receipt = await invoke('support_send', { id: conn.target.report_id, sha256: conn.target.sha256 });
      conn.state = 'sent'; connection.close(); conn.onSent?.(receipt);
    } catch (error) {
      const code = /\(([a-z_]+)\)/.exec(String(error))?.[1];
      conn.state = 'failed'; conn.message = REFUSALS[code] || (navigator.onLine ? 'Still not confirmed. Check the date and time, then try again in a minute.' : 'This computer is still offline.');
      renderConnection();
    }
    window.myReports?.refresh();
  }
  window.addEventListener('online', () => { if (connection.open && conn) renderConnection(); });
  window.addEventListener('offline', () => { if (connection.open && conn) renderConnection(); });

  function open(prefill = {}) {
    // Words typed before a close come back; with nothing typed, a new report starts at the first question.
    if (['done', 'failed', 'answered'].includes(rw.screen) || (rw.screen !== 'sending' && !rw.text.trim())) rw = blank();
    if (prefill.text !== undefined) { rw = blank(); rw.text = prefill.text; rw.hints = prefill.hints || {}; rw.screen = 'describe'; }
    if (!dialog.open) dialog.showModal();
    render();
  }
  // Closing stops every read. The words and choices stay until Done, so an accidental close loses nothing.
  function close() {
    rw.scan.token++; stopCollection(); stopWatching();
    rw.scan = { ...blankScan(), generation: rw.scan.generation, token: rw.scan.token };
    if (rw.screen === 'sending') return;
    if (dialog.open) dialog.close();
  }
  dialog.addEventListener('cancel', event => { event.preventDefault(); if (rw.screen !== 'sending') close(); });
  dialog.addEventListener('close', () => { stopWatching(); });
  $('support-open').onclick = () => open();
  $('report-tool-row').onclick = () => open();
  return { open, openConnection, state: () => rw, watching: () => watch && { volumes: watch.volumes, timer: Boolean(watch.timer) } };
})();
