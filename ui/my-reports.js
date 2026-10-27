// My reports, the read-only tracking view, status notices and the maintainer inbox.
// Rust holds every receipt key and makes every request; this page sees only the safe projection.
'use strict';
window.myReports = (() => {
  const C = window.XZ_SUPPORT_CONTRACT;
  const host = $('my-reports-host'), track = $('report-track'), inbox = $('support-inbox');
  titles['my-reports'] = 'My reports'; toolPages.add('my-reports');
  pageShown['my-reports'] = () => { render(); check('open', null, true); };
  const day = iso => iso ? new Date(typeof iso === 'number' ? iso * 1000 : iso).toLocaleDateString('en-GB', { day: 'numeric', month: 'short', year: 'numeric' }) : 'Unknown';
  function icon(id) {
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg'), use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
    svg.setAttribute('class', 'icon'); svg.setAttribute('aria-hidden', 'true'); use.setAttribute('href', '#' + id); svg.append(use);
    return svg;
  }
  const KINDS = { REGRESSION: ['Got worse', 'worse'], DEGRADATION: ['Gets worse in a set', 'worse'], BUG: ['Problem', 'problem'], EXPECTATION: ['Works differently', 'expect'],
    FEATURE: ['Idea', 'idea'], NOT_A_BUG: ['Answered', 'question'], QUESTION: ['Question', 'question'] };
  const TOPIC_KIND = { GOT_WORSE: 'REGRESSION', NOT_WORKING: 'BUG', WORK_DIFFERENTLY: 'EXPECTATION', NEW_IDEA: 'FEATURE', HOW_DO_I: 'QUESTION' };
  const kindOf = r => r.classified_kind || TOPIC_KIND[r.topic] || r.kind_hint || null;
  const badge = r => { const [label, tone] = KINDS[kindOf(r)] || ['Report', '']; return el('span', { class: 'sr-badge', dataset: { tone: tone || undefined }, text: label }); };
  function words(r) {
    switch (r.state) {
      case 'accepted': return r.status_text || 'Received privately.';
      case 'sending': return 'Sending...';
      case 'unconfirmed': return 'Not confirmed yet';
      case 'not_received': return "Didn't arrive. Send it again.";
      case 'expired': return 'Tracking has ended';
      default: return 'Status unknown';
    }
  }
  let reports = [], drafts = [], saved = [], loaded = false, draftsLoaded = false, loading = false, error = '';

  // Background checks read receipts from the host only. Saved drafts come from the collector, which is a job, so
  // they are read only when My reports or a report is opened; a launch check never holds the job slot.
  async function load(withDrafts = true) {
    if (loading) return;
    loading = true;
    try {
      const result = await invoke('support_reports'); reports = result.reports; error = '';
      const listed = withDrafts ? await run({ method: 'support_list' }, 'Reading saved reports', null, () => {}) : null;
      if (listed?.ok) { saved = listed.result.reports; draftsLoaded = true; }
      drafts = saved.filter(d => !reports.some(r => r.report_id === d.report_id));
    } catch (failure) { error = String(failure); }
    finally { loading = false; loaded = true; }
    render(); notice();
  }
  async function check(reason, id = null, withDrafts = false) {
    if (!invoke) return null;
    try {
      const result = await invoke('support_check_statuses', { reason, id });
      if (result.checked || !loaded || withDrafts) await load(withDrafts); else notice(result.unseen);
      return result;
    } catch { return null; }
  }

  // My reports page.
  function render() {
    if (!host) return;
    const unconfirmed = reports.filter(r => r.state !== 'accepted');
    const head = el('div', { class: 'sr-page-head' },
      el('p', { text: 'Reports and ideas sent from this computer, with where each one is now. XZ Mods checks for news when it starts and when you open this page. Your words stay on this computer.' }),
      el('div', { class: 'actions' }, plainButton('Check now', async () => { await check('refresh'); }, { key: 'check-now' }),
        plainButton('Report a problem or share an idea', () => window.reportWizard.open(), { primary: true, key: 'new-report' })));
    const parts = [head];
    if (error) parts.push(el('div', { class: 'notice', text: 'My reports could not be read. ' + error }));
    if (!reports.length && !drafts.length) parts.push(el('div', { class: 'sr-empty' }, el('strong', { text: loaded ? 'Nothing sent yet' : 'Reading My reports...' }),
      loaded ? el('span', { text: 'When you send a report or an idea, its receipt appears here so you can follow it.' }) : null));
    if (reports.length) parts.push(el('ul', { class: 'sr-reports', 'aria-label': 'Sent reports' }, ...reports.map(r => el('li', {},
      el('button', { type: 'button', class: 'sr-report', dataset: { unseen: r.unseen ? '' : undefined, key: 'r-' + r.report_id }, onclick: () => show(r.report_id) },
        badge(r), el('strong', { text: r.summary || 'Report ' + r.report_id.slice(0, 8) }), el('span', { class: 'sr-status', text: (r.unseen ? 'New: ' : '') + words(r) }),
        el('span', { class: 'sr-when', text: 'Sent ' + day(r.created_at) }), icon('i-chevron'))))));
    if (unconfirmed.length) parts.push(el('p', { class: 'fine', text: 'Not confirmed means the app could not tell whether a report arrived. Open it to check again; it is never sent twice.' }));
    if (drafts.length) {
      parts.push(el('h3', { class: 'sr-section-title', text: 'Saved, not sent' }));
      parts.push(el('ul', { class: 'sr-reports sr-drafts', 'aria-label': 'Saved reports not sent' }, ...drafts.map(d => el('li', {}, el('div', { class: 'sr-report' },
        el('span', { class: 'sr-badge', text: d.schema === 'xz-support-report/2' ? 'Not sent' : 'Older form' }),
        el('strong', { text: d.readable ? d.title : 'Unreadable saved report' }),
        el('span', { class: 'sr-status', text: d.readable ? (d.schema === 'xz-support-report/2' ? 'Saved ' + day(d.saved_at) + ' · ' + Math.max(1, Math.round(d.bytes / 1024)) + ' KB, sealed when you pressed Send' : 'Made with an older version of the report form') : 'This copy is damaged' }),
        el('div', { class: 'actions' }, ...draftActions(d)))))));
    }
    host.replaceChildren(...parts);
  }
  function draftActions(d) {
    if (d.confirming) return [plainButton('Send privately', () => sendDraft(d), { primary: true, key: 'ds-' + d.report_id }), plainButton('Keep', () => { d.confirming = false; render(); }, { key: 'dk-' + d.report_id })];
    const discard = plainButton('Discard', () => discardDraft(d), { key: 'dd-' + d.report_id });
    if (!d.readable) return [discard];
    if (d.schema === 'xz-support-report/2') return [plainButton('Send', () => { d.confirming = true; render(); host.querySelector('[data-key="ds-' + d.report_id + '"]')?.focus(); }, { key: 'dsend-' + d.report_id }), discard];
    return [plainButton('Use this text', () => reuseDraft(d), { key: 'du-' + d.report_id }), discard];
  }
  // A saved report is sent only after this explicit press, bound to its exact sealed bytes.
  async function sendDraft(d) {
    const consent = await run({ method: 'support_consent', report_id: d.report_id, sha256: d.sha256, bytes: d.bytes, consent: C.consent }, 'Sealing your report', null, () => {});
    if (!consent?.ok) { error = consent?.error || 'This saved report could not be sealed.'; render(); return; }
    try { await invoke('support_send', { id: d.report_id, sha256: d.sha256 }); await load(); show(d.report_id); }
    catch { await load(); show(d.report_id); }
  }
  async function discardDraft(d) {
    const result = await run({ method: 'support_remove', report_id: d.report_id, stored_revision: d.stored_revision }, 'Discarding a saved report', null, () => {});
    if (!result?.ok) error = result?.error || 'Discard failed. Check now and try again.';
    await load();
  }
  async function reuseDraft(d) {
    const result = await run({ method: 'support_resume', report_id: d.report_id, sha256: d.sha256 }, 'Opening a saved report', null, () => {});
    if (!result?.ok) { error = result?.error || 'This saved report could not be opened.'; render(); return; }
    const f = result.result.fields;
    window.reportWizard.open({ text: [f.title, f.description].filter(Boolean).join('\n\n').slice(0, 4000) });
  }

  // Notice for a newer status the reporter has not seen. Seen state is saved, so it shows once across restarts.
  function notice(unseen = reports.filter(r => r.unseen)) {
    const bar = $('report-notice');
    if (!bar) return;
    bar.classList.toggle('hidden', !unseen.length);
    if (!unseen.length) return;
    $('report-notice-text').textContent = unseen.length === 1 ? 'Update on your report: ' + words(unseen[0]) : unseen.length + ' of your reports have updates.';
    $('report-notice-view').onclick = () => { showPage('my-reports'); if (unseen.length === 1) show(unseen[0].report_id); };
  }

  // Read-only tracking view.
  const PROBLEM = ['Received', 'Confirmed', 'Fix in progress', 'Ready to test', 'In an update'], OTHER = ['Received', 'Looked at', 'Decided'];
  function progress(r) {
    const kind = kindOf(r), problem = !kind || ['BUG', 'REGRESSION', 'DEGRADATION'].includes(kind), labels = problem ? PROBLEM : OTHER;
    const at = { received: 0, needs_info: 0, triaged: 1, cannot_reproduce: 1, duplicate: 1, fix_in_progress: 2, fix_ready_for_testing: 3, in_test_build: 3 }[r.lifecycle];
    const index = r.lifecycle === 'released' || r.lifecycle === 'closed' ? labels.length : Math.min(at ?? 0, labels.length - 1);
    return stepStrip(labels, index, r.lifecycle === 'needs_info' || r.lifecycle === 'cannot_reproduce' ? 'bad' : 'now');
  }
  let current = null;
  async function show(id) {
    if (!reports.some(r => r.report_id === id) || !draftsLoaded) await load();
    current = { id, confirm: false, message: '' };
    renderTrack();
    if (!track.open) track.showModal();
    const r = reports.find(item => item.report_id === id);
    if (r?.unseen) { try { const seen = await invoke('support_report_seen', { id }); Object.assign(r, seen); render(); notice(); } catch {} }
  }
  function renderTrack() {
    const r = reports.find(item => item.report_id === current.id);
    if (!r) { track.close(); return; }
    const draft = saved.find(d => d.report_id === r.report_id && d.readable && d.schema === 'xz-support-report/2');
    const body = [el('p', { class: 'sr-track-words', text: r.summary || '(no summary kept)' }), el('p', { class: 'sr-track-status', text: words(r) })];
    if (r.state === 'accepted' && r.question) body.push(el('div', { class: 'sr-question' }, el('strong', { text: r.question }),
      'Replies from the app come in a later update. For now, answer in a new report that mentions this one.',
      el('div', { class: 'actions' }, plainButton('Answer in a new report', () => { track.close(); window.reportWizard.open({ text: 'About report ' + r.report_id.slice(0, 8) + ': ' }); }, { key: 'answer' }))));
    if (r.state === 'unconfirmed' || r.state === 'not_received' || r.state === 'sending') {
      body.push(el('div', { class: 'st-note', dataset: { tone: 'warn' } }, el('strong', { text: r.state === 'not_received' ? 'This report did not arrive. ' : 'Not confirmed yet. ' }),
        draft ? 'Set up the reporting connection and send the same report again. If it already arrived, you get its receipt and nothing arrives twice.'
          : 'Check again to ask the report service whether it arrived.'));
    }
    body.push(el('dl', { class: 'sr-dates' }, el('dt', { text: 'Sent' }), el('dd', { text: day(r.created_at) }),
      ...(r.updated_at ? [el('dt', { text: 'Last change' }), el('dd', { text: day(r.updated_at) })] : []),
      ...(r.diagnostics_expires_at ? [el('dt', { text: 'Details deleted' }), el('dd', { text: day(r.diagnostics_expires_at) })] : []),
      ...(r.retention_until ? [el('dt', { text: 'Tracking until' }), el('dd', { text: day(r.retention_until) + ', or longer while it is open' })] : []),
      ...(r.checked_at ? [el('dt', { text: 'Last checked' }), el('dd', { text: new Date(r.checked_at * 1000).toLocaleString('en-GB', { day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit' }) })] : [])));
    const more = el('details', { class: 'sr-more' }, el('summary', { text: 'Status link and forgetting' }),
      el('p', { class: 'fine', text: 'The status link opens a read-only page with the same status. It shows no words or details from your report. Anyone with the link can read the status.' }),
      el('div', { class: 'actions' }, ...(r.state === 'accepted' && !r.links_revoked ? [plainButton('Open status page', () => act('open'), { key: 'open-link' }), plainButton('Copy status link', () => act('copy'), { key: 'copy-link' }),
        plainButton('Turn off status links', () => act('revoke'), { key: 'revoke' })] : []),
      plainButton('Forget this report', () => { current.confirm = true; renderTrack(); }, { key: 'forget' })),
      current.confirm ? el('div', { class: 'sr-confirm' }, 'Forget removes the receipt from this computer. You will no longer see its status here. The maintainer keeps the report.',
        el('div', { class: 'actions' }, plainButton('Forget this report', () => act('forget'), { primary: true, key: 'forget-yes' }), plainButton('Keep it', () => { current.confirm = false; renderTrack(); }, { key: 'forget-no' }))) : null);
    more.open = Boolean(current.more || current.confirm);
    more.ontoggle = () => { if (current) current.more = more.open; };
    const primary = r.state === 'accepted' || r.state === 'expired' ? plainButton('Close', () => track.close(), { primary: true, key: 'close' })
      : draft && r.state !== 'sending' ? plainButton('Set up reporting connection', () => window.reportWizard.openConnection({ report_id: r.report_id, sha256: draft.sha256 }, async () => { await load(); renderTrack(); }), { primary: true, key: 'setup' })
        : plainButton('Check again', () => act('check'), { primary: true, key: 'check' });
    const focused = track.contains(document.activeElement) ? document.activeElement.dataset.key : null;
    track.replaceChildren(r.state === 'accepted' ? progress(r) : el('span'),
      el('h2', { id: 'report-track-title', tabindex: '-1' }, badge(r), ' Report ' + r.report_id.slice(0, 8)), ...body, more,
      el('p', { class: 'visually-hidden', role: 'status', text: current.message }),
      el('div', { class: 'st-foot' }, el('span', { class: 'st-foot-left', text: current.message }), r.state === 'accepted' ? plainButton('Check now', () => act('check'), { key: 'check-now' }) : null,
        r.state === 'accepted' ? null : plainButton('Close', () => track.close(), { key: 'close-2' }), primary));
    (focused && track.querySelector('[data-key="' + CSS.escape(focused) + '"]') || track.querySelector('h2')).focus({ preventScroll: true });
  }
  // The view can close while a request runs; results then go to that view only and are not drawn.
  async function act(action) {
    const view = current, id = view.id;
    try {
      if (action === 'check') { const result = await invoke('support_check_statuses', { reason: 'refresh', id }); await load(); view.message = result?.updated?.length ? 'Status updated.' : 'No change since the last check.'; }
      if (action === 'open') { await invoke('support_open_tracking', { id }); view.message = 'Opened in your browser.'; }
      if (action === 'copy') { const { url } = await invoke('support_tracking_link', { id }); await navigator.clipboard.writeText(url); view.message = 'Status link copied.'; }
      if (action === 'revoke') { const updated = await invoke('support_revoke_links', { id }); Object.assign(reports.find(r => r.report_id === id), updated); view.message = 'Status links turned off. This app still checks the status.'; }
      if (action === 'forget') { await invoke('support_forget', { id }); track.close(); await load(); return; }
    } catch (failure) { view.message = String(failure).replace(/^Error: /, ''); }
    if (current === view) renderTrack();
  }
  track.addEventListener('close', () => { if (!track.open) current = null; });

  // Maintainer inbox. Owner rows only: kind, severity, impact, baseline, affected features, in use since, reach, status.
  const LABEL = {
    kinds: { REGRESSION: 'Regression', DEGRADATION: 'Degradation', BUG: 'Bug', EXPECTATION: 'Expectation', FEATURE: 'Feature', NOT_A_BUG: 'Not a bug', QUESTION: 'Question' },
    severities: { LIVE_SET_STOPPER: 'Live-set stopper', WORKAROUND_EXISTS: 'Workaround exists', ANNOYANCE: 'Annoyance', COSMETIC: 'Cosmetic' },
    impacts: { ADDITIVE: 'Additive', RESTORES_EXPECTED: 'Restores expected', OPT_IN_CHANGE: 'Opt-in change', CHANGES_DEFAULT: 'Changes default', BREAKS: 'Breaks' },
    expectations: { STOCK_XZ: 'Stock XZ', XZ_CONSISTENCY: 'XZ consistency', DJ_CONVENTION: 'DJ convention', XZ_MODS_PREVIOUS: 'Earlier XZ Mods', PREFERENCE: 'Preference', NONE: 'None' },
    features: { audio: 'Audio', stems: 'Stems', sync_tempo: 'SYNC tempo', sync_lamp: 'SYNC lamp', master_handoff: 'MASTER hand-off', beat_jump_pads: 'Beat jump pads', buttons_pads_lamps: 'Buttons, pads, lamps',
      screen_display: 'Screen', loading_browsing_usb: 'Loading and USB', four_decks: 'Four decks', loopback: 'Loopback cable', runtime_stability: 'Stability', settings: 'Settings', appearance: 'Appearance', integration: 'Integration', other: 'Other' },
    lifecycles: { received: 'Received', triaged: 'Triaged', needs_info: 'Needs info', cannot_reproduce: 'Cannot reproduce', duplicate: 'Duplicate', fix_in_progress: 'Fix in progress',
      fix_ready_for_testing: 'Ready to test', in_test_build: 'In test build', released: 'Released', closed: 'Closed' }
  };
  for (const [list, words] of Object.entries(LABEL)) {
    const values = list === 'features' ? C.affected_features : C[list];
    for (const value of values) if (!words[value]) console.error('Maintainer inbox has no words for', list, value);
  }
  const tag = (text, tone) => el('span', { class: 'sr-tag', dataset: { tone }, text });
  let box = { rows: [], cursor: null, changed: new Map(), state: 'idle', message: '', reader: null, confirmDelete: false };
  async function openInbox() {
    box = { rows: [], cursor: null, changed: new Map(), state: 'idle', message: '', reader: null, confirmDelete: false };
    if (!inbox.open) inbox.showModal();
    let connected = false;
    try { connected = (await invoke('support_owner_status')).connected; } catch {}
    if (!connected) { box.state = 'setup'; renderInbox(); return; }
    await list();
  }
  async function list(more = false) {
    box.state = 'loading'; box.message = more ? 'Reading more...' : 'Reading the inbox...'; renderInbox();
    try {
      const page = await invoke('support_inbox', { action: more ? 'more' : 'list', id: null, cursor: more ? box.cursor : null });
      const seen = new Set(box.rows.map(row => row.report_id));
      box.rows = more ? [...box.rows, ...page.reports.filter(row => !seen.has(row.report_id))] : page.reports;
      box.cursor = page.next_cursor; box.state = 'ready'; box.message = '';
    } catch (failure) {
      if (more && String(failure).includes('cursor_expired')) return list(false);
      box.state = box.rows.length ? 'ready' : 'setup'; box.message = 'Inbox unavailable: ' + String(failure).replace(/^Error: /, '');
    }
    renderInbox();
  }
  async function changes() {
    box.message = 'Checking for changes...'; renderInbox();
    try {
      const result = await invoke('support_inbox', { action: 'changes', id: null, cursor: null });
      for (const change of result.changed) box.changed.set(change.report_id, change.revision);
      box.message = result.baseline ? 'Changes are tracked from now on.' : result.changed.length ? result.changed.length + ' report' + (result.changed.length === 1 ? '' : 's') + ' changed. Refresh to see the new order.' : 'No changes since the last check.';
    } catch (failure) { box.message = 'Changes unavailable: ' + String(failure).replace(/^Error: /, ''); }
    renderInbox();
  }
  async function connect(input) {
    let key = input.value; input.value = '';
    box.message = 'Checking the key with a live inbox read...'; renderInbox();
    try { await invoke('support_connect', { key }); key = ''; await list(); }
    catch (failure) { key = ''; box.state = 'setup'; box.message = 'That key did not work: ' + String(failure).replace(/^Error: /, ''); renderInbox(); }
  }
  async function read(id, page = 0) {
    try {
      if (page === 0) { box.reader = { id, page: 0, pages: 0, text: 'Reading and verifying every page...' }; renderInbox(); const r = await invoke('support_inbox', { action: 'read', id, cursor: null }); box.reader.pages = r.pages; }
      const p = await invoke('support_inbox_page', { page }); Object.assign(box.reader, { page, pages: p.pages, text: p.text });
    } catch (failure) { box.reader = { id, page: 0, pages: 0, text: 'This report could not be read: ' + String(failure).replace(/^Error: /, '') }; }
    renderInbox();
  }
  async function remove(id) {
    try { await invoke('support_inbox', { action: 'delete', id, cursor: null }); box.reader = null; box.confirmDelete = false; box.message = 'Private copy deleted. Its tracking remains.'; }
    catch (failure) { box.message = 'Delete failed: ' + String(failure).replace(/^Error: /, ''); }
    renderInbox();
  }
  function row(r) {
    const desk = r.desk || {}, features = desk.affected_features || [], reach = desk.reach || { versions: [], interfaces: [], decks: [] };
    const flagged = r.impact === 'CHANGES_DEFAULT' || r.impact === 'BREAKS';
    return el('tr', { dataset: { changed: box.changed.has(r.report_id) ? '' : undefined, flag: flagged ? '' : undefined } },
      el('td', {}, LABEL.kinds[r.kind] || 'Unclassified', el('small', { text: r.report_id.slice(0, 8) })),
      el('td', {}, r.severity ? tag(LABEL.severities[r.severity], r.severity === 'LIVE_SET_STOPPER' ? 'stop' : undefined) : '-'),
      el('td', {}, r.impact ? tag(LABEL.impacts[r.impact], flagged ? 'flag' : r.impact === 'OPT_IN_CHANGE' ? 'warn' : 'good') : '-',
        r.impact === 'OPT_IN_CHANGE' && desk.proposed_setting ? el('small', { text: 'Adds a setting: ' + LABEL.features[desk.proposed_setting] }) : null,
        flagged ? el('small', { text: 'Owner decision. Never automatic; the desk proposes an opt-in version.' }) : null),
      el('td', { text: r.expectation ? LABEL.expectations[r.expectation] : '-' }),
      el('td', {}, ...(features.length ? features.map(f => tag(LABEL.features[f])) : ['-'])),
      el('td', { text: desk.in_use_since || '-' }),
      el('td', {}, r.reach_count + ' report' + (r.reach_count === 1 ? '' : 's'),
        el('small', { text: [reach.versions.join(', '), reach.interfaces.join(', '), reach.decks.length ? 'decks ' + reach.decks.join(', ') : ''].filter(Boolean).join(' · ') || ' ' })),
      el('td', {}, LABEL.lifecycles[r.lifecycle], box.changed.has(r.report_id) ? tag('Updated', 'warn') : null,
        r.automatic_fix_eligible ? el('small', { text: 'Fix rule: eligible' + (r.automation_enabled ? '' : ' · automation off') }) : null,
        plainButton('Read', () => read(r.report_id), { text: true, key: 'read-' + r.report_id })));
  }
  function renderInbox() {
    const parts = [el('h2', { id: 'support-inbox-title', tabindex: '-1', text: 'Maintainer inbox' })];
    if (box.state === 'setup') {
      const input = el('input', { type: 'password', id: 'support-owner-key', autocomplete: 'off', 'aria-describedby': 'support-owner-note' });
      parts.unshift(stepStrip(['Key', 'Live check'], 0, box.message ? 'bad' : 'now'));
      parts.push(el('p', { id: 'support-owner-note', text: 'For the maintainer only. Paste the report-only owner key. XZ Mods keeps it on this computer under Windows user protection, then reads the inbox to check it.' }),
        el('div', { class: 'sr-key' }, el('label', { for: 'support-owner-key', text: 'Report-only owner key' }, input),
          plainButton('Connect and check', () => connect(input), { primary: true, key: 'connect' })),
        el('p', { role: 'status', class: 'fine', text: box.message }),
        el('div', { class: 'st-foot' }, plainButton('Close', () => inbox.close(), { key: 'inbox-close' })));
      inbox.replaceChildren(...parts);
      input.onkeydown = event => { if (event.key === 'Enter') connect(input); };
      input.focus();
      return;
    }
    parts.push(el('div', { class: 'sr-inbox-tools' }, plainButton('Refresh', () => list(), { key: 'inbox-refresh', disabled: box.state === 'loading' }),
      plainButton('Check for changes', changes, { key: 'inbox-changes' }),
      el('span', { class: 'sr-meta', role: 'status', text: box.message || box.rows.length + ' report' + (box.rows.length === 1 ? '' : 's') + ' · highest priority first' })));
    parts.push(el('div', { class: 'sr-table-wrap' }, el('table', { class: 'sr-table' },
      el('colgroup', {}, ...['kind', 'sev', 'impact', 'base', 'feat', 'since', 'reach', 'status'].map(name => el('col', { class: 'c-' + name }))),
      el('thead', {}, el('tr', {}, ...['Kind', 'Severity', 'Impact', 'Baseline', 'Affected features', 'In use since', 'Reach', 'Status'].map(text => el('th', { scope: 'col', text })))),
      el('tbody', {}, ...(box.rows.length ? box.rows.map(row) : [el('tr', {}, el('td', { colspan: '8', text: box.state === 'loading' ? 'Reading...' : 'No reports.' }))])))));
    if (box.cursor) parts.push(el('div', { class: 'actions' }, plainButton('Load more', () => list(true), { key: 'inbox-more' })));
    if (box.reader) {
      const r = box.reader;
      parts.push(el('h3', { text: 'Private report ' + r.id.slice(0, 8) }), el('pre', { tabindex: '0', 'aria-label': 'Verified private report', text: r.text }),
        el('div', { class: 'actions' }, r.pages > 1 ? plainButton('Previous page', () => read(r.id, r.page - 1), { key: 'p-prev', disabled: r.page === 0 }) : null,
          r.pages > 1 ? plainButton('Next page', () => read(r.id, r.page + 1), { key: 'p-next', disabled: r.page + 1 >= r.pages }) : null,
          r.pages ? el('span', { class: 'fine', text: 'Page ' + (r.page + 1) + ' of ' + r.pages }) : null,
          box.confirmDelete ? plainButton('Delete the private copy', () => remove(r.id), { key: 'del-yes' }) : plainButton('Delete private copy', () => { box.confirmDelete = true; renderInbox(); }, { key: 'del' }),
          box.confirmDelete ? plainButton('Keep', () => { box.confirmDelete = false; renderInbox(); }, { key: 'del-no' }) : null));
    }
    parts.push(el('div', { class: 'st-foot' }, plainButton('Close', () => inbox.close(), { primary: true, key: 'inbox-close' })));
    const focused = inbox.contains(document.activeElement) ? document.activeElement.dataset.key : null;
    inbox.replaceChildren(...parts);
    (focused && inbox.querySelector('[data-key="' + CSS.escape(focused) + '"]') || inbox.querySelector('h2')).focus({ preventScroll: true });
  }

  $('my-reports-open').onclick = () => showPage('my-reports');
  $('support-owner-open').onclick = openInbox;
  if (invoke) setTimeout(() => check('launch'), 1500);
  return { refresh: load, check, show, notice, openInbox, state: () => ({ reports, drafts }) };
})();
