import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, rm, readFile, writeFile, mkdir } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import { serveUi } from './csp_server.mjs';
import { killBrowser } from './kill_browser.mjs';

// Drives Feedback: "Report a problem or share an idea", My reports and the maintainer inbox in headless Edge over CDP, served
// with the app's real CSP, against a mocked host that follows the support_* commands (src-tauri/src/support*.rs) and
// the support_sources / support_collect / support_prepare_v2 contract (builder/support_reports.py). The mock checks
// the selection digest the way the collector does, so a Send that does not match what was shown fails here too.
// Run: node tools/verify_support_reports.mjs   (XZ_SUPPORT_SHOTS=<dir> saves screenshots, XZ_TEST_BROWSER overrides the browser)
const app = resolve(dirname(fileURLToPath(import.meta.url)), '..');

// The knowledge matcher, without a browser: positive and negative fixtures for similar words.
{
  const context = { console }; context.window = context; vm.createContext(context);
  for (const file of ['ui/support-kb.js', 'ui/support-knowledge.js']) vm.runInContext(await readFile(join(app, file), 'utf8'), context);
  const ids = text => [...context.supportKnowledge.match(context.XZ_SUPPORT_KB, text)].map(entry => entry.entry_id);
  const cases = [
    ['Decks 3 and 4 are silent', ['four-decks-cable-setup']],
    ['no cable found on decks 3 and 4 even with the loopback cable in usb 1', ['four-decks-cable-setup']],
    ['I get E-8307 HUBS ARE NOT SUPPORTED when I plug in the cable', ['e8307-loopback-hub']],
    ['What does SYNC 3 mean on deck 4?', ['sync-labels-decks-3-4']],
    ['deck 4 shows LIMIT next to sync', ['sync-labels-decks-3-4']],
    ['KEY SYNC does not work on deck 3', ['key-sync-cpu-limit']],
    ['RAM copy fails on deck 3', ['ram-tracks-cable']],
    ["The player won't start with my USB", ['player-will-not-start']],
    ['E-8307 USB ACCESS ERROR', []], ['sync', []], ['My waveform colours look odd', []], ['', []]
  ];
  for (const [text, expected] of cases) assert.deepEqual(ids(text), expected, 'knowledge match for ' + JSON.stringify(text));
  for (const entry of context.XZ_SUPPORT_KB.entries) {
    assert(!/[\u2014]/.test(entry.answer + entry.title), entry.entry_id + ' uses no em dash');
    assert(entry.action === null || ['OPEN_FOUR_DECK_SETUP', 'OPEN_USB_SETUP'].includes(entry.action.id), entry.entry_id + ' action is registered');
  }
  console.log('ok knowledge matcher:', cases.length, 'fixtures');
}

function installMock() {
  const hex = n => Array.from(crypto.getRandomValues(new Uint8Array(n)), b => b.toString(16).padStart(2, '0')).join('');
  const sha = async text => [...new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text)))].map(b => b.toString(16).padStart(2, '0')).join('');
  const local = (h, m) => { const d = new Date(2026, 9, 27, h, m); return d.toISOString(); };
  const fx = window.fx = { calls: [], requests: [], results: new Map(), jobs: new Map(), volumes: ['C:\\', 'E:\\'], scans: new Map(), receipts: [], drafts: [], sendMode: 'ok',
    collectHold: false, released: false, cancelled: new Set(), clipboard: [], owner: false, inboxReads: 0, changes: [], csp: [], unseen: [] };
  const sticksFor = () => {
    const sticks = [], others = [];
    if (fx.volumes.includes('E:\\')) sticks.push({ handle: hex(16), source_slot: 'usb_a', label: 'USB A', local_volume: 'E:\\', sources: { usb_session: true, usb_settings: true, native_export: false, usb_diary: true } });
    if (fx.volumes.includes('F:\\')) others.push({ label: 'USB', local_volume: 'F:\\' });
    if (fx.volumes.includes('G:\\')) sticks.push({ handle: hex(16), source_slot: 'usb_' + String.fromCharCode(97 + sticks.length), label: 'USB', local_volume: 'G:\\', sources: { usb_session: false, usb_settings: false, native_export: false, usb_diary: true } });
    return { sticks, others };
  };
  const item = (source_id, source_slot, facts, extra = {}) => ({ item_id: hex(16), source_id, source_slot, missing_reason: null, truncated: false, omission_count: 0, clock_confidence: 'known', bytes: 600 + facts * 90, sha256: hex(32), eligible: true, facts, ...extra });
  const collect = request => {
    const scan = fx.scans.get(request.scan_generation), items = [];
    const topic = request.reporter_hints?.topic, question = topic === 'HOW_DO_I', idea = topic === 'NEW_IDEA';
    for (const stick of scan.sticks) {
      items.push(item('usb_diary', stick.source_slot, 140, { truncated: true }));
      if (stick.sources.usb_session) items.push(item('usb_session', stick.source_slot, 22));
      if (stick.sources.usb_settings) items.push(item('usb_settings', stick.source_slot, 12));
    }
    items.push(item('app_events', 'app', 8), item('loader_status', 'app', 0, { missing_reason: 'missing', bytes: 180 }), item('packaged_manifest', 'package', 3, { clock_confidence: 'current' }));
    if (fx.cancelled.has(request.scan_generation)) for (const entry of items.slice(1)) Object.assign(entry, { missing_reason: 'cancelled', facts: 0, bytes: 170 });
    for (const entry of items) { entry.eligible = !idea && (!question || ['usb_settings', 'packaged_manifest'].includes(entry.source_id)); fx.content.set(entry.item_id, JSON.stringify({ item_id: entry.item_id, source_id: entry.source_id, source_slot: entry.source_slot, facts: Array.from({ length: Math.min(entry.facts, 4) }, (_, i) => ({ code: ['WAIT_LOOPBACK', 'READY_CH3_CH4', 'MUSIC_READY', 'NONE'][i], clock_basis: 'device_utc', relative_seconds: -240 + i * 30 })) }, null, 1)); }
    scan.items = items;
    const diary = Object.fromEntries(scan.sticks.map(stick => [stick.source_slot, stick.local_volume === 'E:\\' ? { state: 'present', duration_seconds: 10380, start_utc: local(21, 5), end_utc: local(23, 58), truncated: false } : { state: 'absent' }]));
    return { scan_generation: request.scan_generation, incident: { before_seconds: 600, after_seconds: 300, clock_confidence: 'known' }, category: 'bridge', kind_hint: 'BUG', items, diary_coverage: diary, bytes: items.reduce((s, i) => s + i.bytes, 0), omitted_items: 0, unfinished_sources: fx.cancelled.has(request.scan_generation) ? 5 : 0 };
  };
  fx.content = new Map();
  // From your XZ: the pending queue, loader identities and notifications. A test sets fx.preset in localStorage
  // before loading the page, the way the host holds a queue before the window opens.
  const preset = JSON.parse(localStorage.getItem('fx.preset') || '{}');
  Object.assign(fx, { pending: preset.pending || [], online: true, notifyMode: preset.notifyMode || 'shown', toasts: [], completions: [], dismissed: [], revoked: [], listeners: {}, detailsReads: 0,
    identity: preset.identity || { installation_id: 'I-4F7K-2M9Q', registered: true, loaders: [{ loader_id: 'L-7K2M-9Q4F', created_at: '2026-10-12T19:30:00.000Z', revoked: false, revocation_pending: false, registration_pending: false, write_pending: false }] } });
  // Each pending report was claimed, so its receipt is already stored; My reports must not list it twice.
  for (const p of fx.pending) fx.receipts.push({ report_id: p.report_id, state: 'accepted', report_reference: p.report_reference, item_number: null, protocol: 2, kind_hint: 'BUG', topic: p.topic, summary: p.description || '', classified_kind: null, lifecycle: 'received',
    status_text_key: 'received', status_text: 'Received privately. Track it here.', question: null, revision: 1, created_at: Math.floor(p.created_at / 1000), accepted_at: null, updated_at: null, retention_until: '2027-10-27T13:00:00.000Z', diagnostics_expires_at: '2026-11-10T13:00:00.000Z', unseen: false, checked_at: null, last_error: null, links_revoked: false });
  fx.queue = () => ({ pending: fx.pending.filter(p => p.comments_pending), connected: fx.online, identity: fx.identity, error: fx.online ? null : 'offline', usb_scan_error: null });
  fx.emit = (name, payload) => { for (const handler of fx.listeners[name] || []) handler({ event: name, payload }); };
  const queueCommand = (command, args) => {
    const find = () => { const p = fx.pending.find(x => x.report_id === args.id); if (!p) throw 'Invalid pending report ID'; return p; };
    switch (command) {
      case 'support_pending_reports': return { pending: fx.queue().pending };
      case 'support_loader_identities': return fx.identity;
      case 'support_poll_pending': return fx.queue();
      case 'support_pending_details': {
        fx.detailsReads++; const p = find();
        return { schema: 'xz-support-report/2', report_id: p.report_id, origin: 'controller', loader_id: p.loader_id, installation_id: 'I-4F7K-2M9Q', comments_pending: true, kind_hint: null, description: p.description || '', category: 'bridge',
          incident: { before_seconds: 900, after_seconds: 0, clock_confidence: 'known' }, screen_state: p.screen_state, reporter_hints: { topic: p.topic },
          private: { contact: null, contact_consent: false, items: [
            { item_id: 'c'.repeat(32), source_id: 'usb_session', source_slot: 'usb_a', parser_version: 2, capture: 'at_collection', clock_confidence: 'known', selected: true, facts: [{ code: 'WAIT_LOOPBACK', clock_basis: 'device_utc', relative_seconds: -240 }, { code: 'READY_CH3_CH4', clock_basis: 'device_utc', relative_seconds: -200 }] },
            { item_id: 'd'.repeat(32), source_id: 'usb_diary', source_slot: 'usb_a', parser_version: 2, capture: 'at_collection', clock_confidence: 'known', selected: true, facts: [{ code: 'MUSIC_READY', clock_basis: 'device_utc', relative_seconds: -600 }] }] } };
      }
      case 'support_complete_pending': {
        const p = find();
        if (args.comments !== null && (!args.comments.trim() || new TextEncoder().encode(args.comments).length > 4096)) throw 'Describe what happened in at most 4096 UTF-8 bytes';
        fx.completions.push({ id: args.id, comments: args.comments });
        if (fx.online) Object.assign(p, { comments_pending: false, remind: false, comments_state: args.comments === null ? 'completed_without_comments' : 'completed' });
        else Object.assign(p, { completion_pending: true });
        return fx.queue();
      }
      case 'support_dismiss_pending': { const p = find(); p.remind = false; fx.dismissed.push(args.id); return fx.queue(); }
      case 'support_revoke_loader': {
        const l = fx.identity.loaders.find(x => x.loader_id === args.loaderId); if (!l) throw 'Unknown loader';
        fx.revoked.push(args.loaderId); Object.assign(l, fx.online ? { revoked: true, revocation_pending: false } : { revocation_pending: true });
        return fx.queue();
      }
      case 'support_notify_pending': {
        const p = fx.pending.find(x => x.report_id === args.id);
        if (!p || !p.remind || p.report_reference !== args.reference) throw 'This report does not need a reminder';
        fx.toasts.push(args); return fx.notifyMode === 'off' ? { shown: false, reason: 'off' } : { shown: true };
      }
    }
    return undefined;
  };
  const ok = result => ({ running: false, ok: true, result });
  const fail = error => ({ running: false, ok: false, error });
  const prepare = async request => {
    if (!request.description?.trim()) return fail('Text exceeds report limit or is empty');
    const scan = request.scan_generation ? fx.scans.get(request.scan_generation) : null;
    const included = request.collect === false || !scan ? [] : scan.items.filter(i => i.eligible && !(request.omit_item_ids || []).includes(i.item_id));
    const digest = await sha(JSON.stringify(included.map(i => [i.item_id, i.sha256])));
    if (request.selection_sha256 !== digest) return fail('Included details changed; check them and send again');
    if (request.consent !== 'private-report/v2') return fail('Send needs consent to this private report');
    const id = hex(16), body = JSON.stringify({ description: request.description, items: included.map(i => i.item_id) });
    fx.lastDraft = { report_id: id, sha256: await sha(body), bytes: body.length, request, included: included.map(i => i.item_id) };
    fx.drafts.push({ report_reference: window.XZ_SUPPORT_IDENTIFIERS.formatReportReference(window.XZ_SUPPORT_IDENTIFIERS.parseReportReference('R-4F7K-2M9Q') + fx.drafts.length), report_id: id, sha256: fx.lastDraft.sha256, bytes: body.length, schema: 'xz-support-report/2', readable: true, title: request.description.slice(0, 40), topic: request.reporter_hints?.topic || null, attempted: false, saved_at: '2026-10-27T13:00:00Z', stored_revision: hex(32) });
    return ok({ report_id: id, sha256: fx.lastDraft.sha256, bytes: body.length, pages: 1, selection_sha256: digest, consent: 'private-report/v2' });
  };
  const view = (draft, extra = {}) => ({ report_id: draft.report_id, state: 'accepted', report_reference: draft.report_reference, item_number: null, protocol: 2, kind_hint: { GOT_WORSE: 'REGRESSION', NEW_IDEA: 'FEATURE', HOW_DO_I: 'QUESTION', WORK_DIFFERENTLY: 'EXPECTATION' }[draft.topic] || 'BUG', topic: draft.topic,
    summary: draft.title, classified_kind: null, lifecycle: 'received', status_text_key: 'received', status_text: 'Received privately. Track it here.', question: null, revision: 1, created_at: Math.floor(Date.now() / 1000),
    accepted_at: '2026-10-27T13:00:00.000Z', updated_at: '2026-10-27T13:00:00.000Z', retention_until: '2027-10-27T13:00:00.000Z', diagnostics_expires_at: '2026-11-10T13:00:00.000Z', unseen: false, checked_at: Math.floor(Date.now() / 1000), last_error: null, links_revoked: false, ...extra });
  const rows = () => [
    { report_id: 'a'.repeat(32), report_reference: 'R-4F7K-2M9Q', item_number: 'XZ-1042', revision: 4, kind: 'REGRESSION', severity: 'LIVE_SET_STOPPER', impact: 'RESTORES_EXPECTED', expectation: 'XZ_MODS_PREVIOUS', outcome: 'CLASSIFIED', lifecycle: 'triaged', reach_count: 6, automatic_fix_eligible: true, automation_enabled: false, created_at: 1761559200000,
      desk: { expectation: 'XZ_MODS_PREVIOUS', impact: 'RESTORES_EXPECTED', severity: 'LIVE_SET_STOPPER', affected_features: ['sync_lamp', 'master_handoff'], in_use_since: '0.5.0-preview.21', reach: { versions: ['0.5.0-preview.22.6'], interfaces: ['MODERN', 'CLASSIC'], decks: [3, 4] }, proposed_setting: null } },
    { report_id: 'b'.repeat(32), report_reference: 'R-0110-0001', item_number: 'XZ-1043', revision: 2, kind: 'EXPECTATION', severity: 'ANNOYANCE', impact: 'RESTORES_EXPECTED', expectation: 'STOCK_XZ', outcome: 'CLASSIFIED', lifecycle: 'triaged', reach_count: 3, automatic_fix_eligible: false, automation_enabled: false, created_at: 1761559300000,
      desk: { expectation: 'STOCK_XZ', impact: 'RESTORES_EXPECTED', severity: 'ANNOYANCE', affected_features: ['beat_jump_pads'], in_use_since: '0.5.0-preview.19', reach: { versions: ['0.5.0-preview.22.6'], interfaces: ['MODERN'], decks: [1, 2] }, proposed_setting: null } },
    { report_id: 'c'.repeat(32), report_reference: 'R-4F7K-2M9R', item_number: 'XZ-1044', revision: 3, kind: 'EXPECTATION', severity: 'COSMETIC', impact: 'OPT_IN_CHANGE', expectation: 'DJ_CONVENTION', outcome: 'CLASSIFIED', lifecycle: 'triaged', reach_count: 2, automatic_fix_eligible: false, automation_enabled: false, created_at: 1761559400000,
      desk: { expectation: 'DJ_CONVENTION', impact: 'OPT_IN_CHANGE', severity: 'COSMETIC', affected_features: ['sync_lamp'], in_use_since: '0.5.0-preview.20', reach: { versions: ['0.5.0-preview.22.6'], interfaces: ['MODERN'], decks: [1, 2, 3, 4] }, proposed_setting: 'sync_lamp' } },
    { report_id: 'd'.repeat(32), report_reference: 'R-4F7K-2M9S', item_number: 'XZ-1045', revision: 1, kind: 'FEATURE', severity: null, impact: 'CHANGES_DEFAULT', expectation: 'PREFERENCE', outcome: 'CLASSIFIED', lifecycle: 'triaged', reach_count: 9, automatic_fix_eligible: false, automation_enabled: false, created_at: 1761559500000,
      desk: { expectation: 'PREFERENCE', impact: 'CHANGES_DEFAULT', severity: null, affected_features: ['appearance'], in_use_since: 'unknown', reach: { versions: [], interfaces: [], decks: [] }, proposed_setting: null } },
    { report_id: 'e'.repeat(32), report_reference: 'R-4F7K-2M9T', item_number: null, revision: 1, kind: null, severity: null, impact: null, expectation: null, outcome: null, lifecycle: 'received', reach_count: 1, automatic_fix_eligible: false, automation_enabled: false, created_at: 1761559600000, desk: null }
  ];
  const status = request => {
    switch (request.method) {
      case 'list_mod_versions': return ok({ format: 'xz-mod-versions/1', versions: [{ id: 'v1', label: 'Fixture build', runtime_sha256: 'newsha', hardware_qualified: false, source_commit: 'abc', quad_supported: true }], selected: { id: 'v1', pinned: true, revision: 1 }, bundled_id: 'v1' });
      case 'discover_usbs': return ok({ format: 'xz-usbs/1', usbs: [] });
      case 'support_sources': {
        const generation = hex(16), { sticks, others } = sticksFor();
        if (request.supersedes) fx.scans.delete(request.supersedes);
        fx.scans.set(generation, { sticks, items: [] });
        return ok({ scan_generation: generation, sticks, other_sticks: others, excess_sticks: 0, xz: 'not_available' });
      }
      case 'support_collect': return ok(collect(request));
      case 'support_prepare_v2': return prepare(request);
      case 'support_source_preview': { const text = fx.content.get(request.item_id); return text ? ok({ item_id: request.item_id, page: 0, pages: 1, text }) : fail('Unknown source item'); }
      case 'support_list': return ok({ reports: fx.drafts, limit: 32 });
      case 'support_discard': case 'support_remove': fx.drafts = fx.drafts.filter(d => d.report_id !== request.report_id); return ok({ discarded: true });
      case 'support_consent': return ok({ report_id: request.report_id, consent: request.consent });
    }
    return fail('Not part of this fixture: ' + request.method);
  };
  navigator.clipboard.writeText = async text => { fx.clipboard.push(text); };
  window.__TAURI__ = { webviewWindow: { getCurrentWebviewWindow: () => ({ onDragDropEvent: async () => () => {} }) },
    event: { listen: async (name, handler) => { (fx.listeners[name] ||= []).push(handler); return () => {}; } }, core: { invoke: async (command, args = {}) => {
    fx.calls.push(command);
    if (command.startsWith('support_') && (command.includes('pending') || command.includes('loader'))) return queueCommand(command, args);
    switch (command) {
      case 'app_information': return { version: '0.5.0-preview.22.6', platform: 'windows', installed: true, release_notes: { releases: [] }, update_channel: 'beta' };
      case 'check_app_update': return { phase: 'current', message: 'XZ Mods beta is up to date.' };
      case 'list_volumes': return fx.volumes;
      case 'start_job': { fx.requests.push(args.request); const id = fx.requests.length; fx.jobs.set(id, { request: args.request, started: Date.now() }); return id; }
      case 'job_status': {
        const job = fx.jobs.get(args.id);
        if (job.request.method === 'support_collect' && fx.collectHold && !fx.cancelled.has(job.request.scan_generation)) return { running: true, message: 'Collecting' };
        if (!fx.results.has(args.id)) fx.results.set(args.id, await status(job.request));
        return fx.results.get(args.id);
      }
      case 'cancel_job': { const job = fx.jobs.get(args.id); if (job?.request.scan_generation) fx.cancelled.add(job.request.scan_generation); return null; }
      case 'support_send': {
        fx.sends = (fx.sends || 0) + 1; fx.lastSend = args;
        const draft = fx.drafts.find(d => d.report_id === args.id);
        if (!draft || draft.sha256 !== args.sha256) throw 'Stale preview or changed draft';
        if (fx.sendMode === 'fail') { if (!fx.receipts.some(r => r.report_id === args.id)) fx.receipts.push(view(draft, { state: 'unconfirmed', report_reference: null, status_text: null, lifecycle: null, last_error: 'outcome_unknown' })); throw 'OutcomeUnknown: connection failed. Check receipt before retrying.'; }
        const receipt = view(draft);
        fx.receipts = fx.receipts.filter(r => r.report_id !== args.id).concat(receipt);
        return receipt;
      }
      case 'support_cancel': return null;
      case 'support_export': return { exported: false };
      case 'support_reports': return { reports: [...fx.receipts].reverse(), limit: 200, migrated: 0 };
      case 'support_check_statuses': {
        fx.checks = (fx.checks || []).concat(args.reason);
        for (const change of fx.unseen) Object.assign(fx.receipts.find(r => r.report_id === change.report_id), change, { unseen: true });
        const updated = fx.unseen.map(c => c.report_id); fx.unseen = [];
        return { checked: fx.receipts.length, failed: 0, updated, unseen: fx.receipts.filter(r => r.unseen) };
      }
      case 'support_report_seen': { const r = fx.receipts.find(x => x.report_id === args.id); r.unseen = false; fx.seen = args.id; return r; }
      case 'support_tracking_link': return { url: 'https://xz-mods-support.will-026.workers.dev/track#id=' + args.id + '&read=' + 'f'.repeat(64) };
      case 'support_open_tracking': fx.opened = args.id; return { opened: true };
      case 'support_revoke_links': { const r = fx.receipts.find(x => x.report_id === args.id); r.links_revoked = true; return r; }
      case 'support_forget': fx.receipts = fx.receipts.filter(r => r.report_id !== args.id); return { forgotten: true };
      case 'support_owner_status': return { connected: fx.owner };
      case 'support_connect': if (args.key.length < 32) throw 'Invalid report-only owner key'; fx.owner = true; return { connected: true };
      case 'support_inbox':
        if (args.action === 'search') {
          const I=window.XZ_SUPPORT_IDENTIFIERS, ref=I.parseReportReference(args.id), item=I.parseItemNumber(args.id);
          fx.lastSearch=args.id;
          return { reports: rows().filter(r => ref!==null ? I.parseReportReference(r.report_reference)===ref : item!==null && I.parseItemNumber(r.item_number)===item), next_cursor:null, high_water:5 };
        }
        if (args.action === 'list') return { reports: rows().slice(0, 3), next_cursor: 'eyJ2IjoyfQ==', high_water: 5 };
        if (args.action === 'more') return { reports: rows().slice(3), next_cursor: null, high_water: 5 };
        if (args.action === 'changes') return { baseline: false, complete: true, changed: [{ report_id: 'c'.repeat(32), report_reference: 'R-4F7K-2M9R', item_number: 'XZ-1044', revision: 3 }] };
        if (args.action === 'read') { fx.inboxReads++; return { pages: 2 }; }
        if (args.action === 'delete') return { report_id: args.id, state: 'gone' };
        throw 'Invalid inbox action';
      case 'support_inbox_page': return { text: '{\n  "schema": "xz-support-report/2",\n  "report_id": "' + 'a'.repeat(32) + '",\n  "kind_hint": "REGRESSION",\n  "description": "SYNC lamp stays dark on deck 3 after MASTER moves",\n  "page": ' + args.page + '\n}', pages: 2 };
    }
    throw Error('Unexpected command ' + command);
  } } };
  document.addEventListener('securitypolicyviolation', event => fx.csp.push(event.violatedDirective + ' ' + (event.blockedURI || 'inline') + ' ' + (event.sourceFile || '').split('/').pop() + ':' + event.lineNumber));
  window.wiz = () => document.getElementById('report-wizard');
  window.button = (text, root = wiz()) => [...root.querySelectorAll('button')].find(item => item.textContent.trim() === text && item.checkVisibility());
  window.txt = (selector, root = document) => root.querySelector(selector)?.textContent.replace(/\s+/g, ' ').trim() || '';
  window.last = method => [...fx.requests].reverse().find(request => request.method === method);
  window.count = method => fx.requests.filter(request => request.method === method).length;
  window.type = value => { const box = wiz().querySelector('.sr-box'); box.value = value; box.dispatchEvent(new Event('input', { bubbles: true })); };
  window.digestOf = async items => [...new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(JSON.stringify(items.map(i => [i.item_id, i.sha256])))))].map(b => b.toString(16).padStart(2, '0')).join('');
}

const server = await serveUi(app);
const profile = await mkdtemp(join(tmpdir(), 'xz-support-'));
const child = spawn(process.env.XZ_TEST_BROWSER || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  ['--headless=new', '--disable-gpu', '--no-first-run', '--remote-debugging-port=0', '--user-data-dir=' + profile, 'about:blank'], { stdio: 'ignore', windowsHide: true });
let socket, base, passed = 0;
try {
  for (let attempt = 0; attempt < 100 && !base; attempt++) {
    try { base = 'http://127.0.0.1:' + (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; }
    catch { await new Promise(done => setTimeout(done, 100)); }
  }
  assert(base, 'Browser did not start');
  const target = (await (await fetch(base + '/json/list', { signal: AbortSignal.timeout(5000) })).json()).find(item => item.type === 'page');
  socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((done, fail) => { socket.onopen = done; socket.onerror = () => fail(Error('CDP connection failed')); });
  let sequence = 0;
  const errors = [], pending = new Map();
  socket.onmessage = event => { const data = JSON.parse(event.data), task = pending.get(data.id); if (!task) return; pending.delete(data.id); data.error ? task.reject(Error(JSON.stringify(data.error))) : task.resolve(data.result); };
  const send = (method, params = {}) => new Promise((resolve, reject) => { const id = ++sequence; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); setTimeout(() => reject(Error('CDP timed out: ' + method)), 10000); });
  const evaluate = async expression => {
    const value = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    if (value.exceptionDetails) throw Error(expression.slice(0, 120) + ': ' + (value.exceptionDetails.exception?.description || value.exceptionDetails.text));
    return value.result.value;
  };
  const context = () => evaluate(`'screen: '+reportWizard.state().screen+' scan: '+reportWizard.state().scan.state+' job: '+activeJob+' requests: '+fx.requests.map(r=>r.method).join(',')+'\\n  watch: '+JSON.stringify(reportWizard.watching())+' volumes: '+fx.volumes.join()+'\\n  checks: '+JSON.stringify(fx.checks)+' calls: '+fx.calls.slice(-12).join(',')+'\\n  dialog: '+txt('#report-wizard').slice(0,300)`);
  const waitFor = async (expression, label, seconds = 6) => {
    for (let attempt = 0; attempt < seconds * 40; attempt++) { if (await evaluate(expression)) return; await new Promise(done => setTimeout(done, 25)); }
    throw Error('Timed out: ' + label + '\n  ' + await context() + '\n  page errors: ' + errors.join(' / '));
  };
  const check = async (expression, label) => {
    if (!await evaluate(expression)) throw Error('Failed: ' + label + '\n  ' + await context() + '\n  page errors: ' + errors.join(' / '));
    passed++; console.log('ok', label);
  };
  await send('Runtime.enable');
  socket.addEventListener('message', event => { const data = JSON.parse(event.data); if (data.method === 'Runtime.exceptionThrown') errors.push((data.params.exceptionDetails.exception?.description || data.params.exceptionDetails.text)); });
  await send('Page.enable');
  await send('Page.addScriptToEvaluateOnNewDocument', { source: '(' + installMock + ')();' });
  const size = (width, height) => send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
  await size(1100, 760);
  const shots = process.env.XZ_SUPPORT_SHOTS; if (shots) await mkdir(shots, { recursive: true });
  // Each shot can record the boxes of named elements, so manual callouts sit beside what they name.
  const boxes = {};
  const shot = async (name, marks = {}, tall = false) => {
    if (!shots) return;
    await new Promise(done => setTimeout(done, 200));
    // The clip is in page coordinates, so a scrolled page is captured where it is scrolled to.
    const [left, top] = await evaluate(`[scrollX,scrollY]`);
    // tall: the whole page, for a page the manual shows in one picture.
    const height = tall ? await evaluate(`document.documentElement.scrollHeight`) : 760;
    if (tall) { await size(1100, height); await new Promise(done => setTimeout(done, 200)); }
    const image = await send('Page.captureScreenshot', { format: 'png', clip: { x: left, y: top, width: 1100, height, scale: 1 } });
    await writeFile(join(shots, name + '.png'), Buffer.from(image.data, 'base64'));
    boxes[name] = await evaluate(`(()=>{const out={};for(const [key,selector] of Object.entries(${JSON.stringify(marks)})){const item=[...document.querySelectorAll(selector)].find(e=>e.checkVisibility());if(item){const range=document.createRange();range.selectNodeContents(item);const r=['BUTTON','INPUT','PRE','TEXTAREA','DL'].includes(item.tagName)?item.getBoundingClientRect():range.getBoundingClientRect();out[key]=[Math.round(r.left),Math.round(r.top),Math.round(r.right),Math.round(r.bottom)];}}return out;})()`);
    await writeFile(join(shots, 'boxes.json'), JSON.stringify(boxes, null, 1));
    if (tall) await size(1100, 760);
  };
  const idle = () => waitFor(`activeJob===null`, 'no job running');
  const click = async (text, root = 'wiz()') => { assert(await evaluate(`Boolean(button(${JSON.stringify(text)},${root}))`), 'button "' + text + '" is visible\n  ' + await context()); await evaluate(`button(${JSON.stringify(text)},${root}).click()`); };
  const choice = async value => { assert(await evaluate(`Boolean(wiz().querySelector('[data-key="c-${value}"]'))`), 'choice ' + value + '\n  ' + await context()); await evaluate(`wiz().querySelector('[data-key="c-${value}"]').click()`); };
  const open = async () => {
    await send('Page.navigate', { url: 'about:blank' });
    await new Promise(done => setTimeout(done, 50));
    await send('Page.navigate', { url: server.url + 'index.html' });
    await waitFor(`document.readyState==='complete'&&window.reportWizard&&window.myReports&&modInventory!==null`, 'page loaded');
    await idle();
    errors.length = 0;
  };
  const feedback = () => evaluate(`document.querySelector('nav button[data-page="my-reports"]').click()`);
  const startReport = async () => { await feedback(); await evaluate(`document.getElementById('support-open').click()`); await waitFor(`wiz().open&&reportWizard.state().screen==='topic'`, 'wizard open'); };

  await open();
  await check(`[...document.querySelectorAll('nav button')].map(b=>b.dataset.page).join()==='setup,stems,live-stream,tools,manual,my-reports'&&txt('nav .nav-feedback-text>span')==='Feedback'&&document.querySelector('nav button.nav-feedback use').getAttribute('href')==='#i-report'&&!document.querySelector('#tools [data-open="my-reports"]')&&!document.querySelector('#licenses [id^=support],#licenses .sr-start-action')&&document.getElementById('about-feedback')?.textContent.includes('Feedback')`, 'Feedback has its own place in the rail, with its glyph; Tools and About hold no report entries, and About links to Feedback');
  await evaluate(`showPage('tools')`);
  await shot('00-tools', Object.fromEntries([...['dj-profile', 'library-repair', 'device-updates', 'advanced', 'updates', 'licenses'].map(id => [id, '.tool-row[data-open="' + id + '"] strong']), ['feedback', 'nav button.nav-feedback']]));
  await evaluate(`showPage('licenses')`);
  await shot('00-about', { version: '.about-version', maker: '.about-maker strong', updates: '#about-updates', projects: '#about-maker-title', links: '#about-links-title', feedback: '#about-feedback strong', support: '.about-link[href^="https://cash.app"] strong', notes: '#about-notes-title', licenses: '#open-licenses' }, true);
  await check(`!document.getElementById('support-wizard')&&!document.getElementById('support-consent')&&!document.querySelector('[id^=support-preview]')`, 'the old form, its page-by-page preview and consent checkbox are gone');
  await waitFor(`(fx.checks||[]).includes('launch')`, 'status check at launch');
  await evaluate(`document.getElementById('check-app-update').click()`);
  await waitFor(`(fx.checks||[]).includes('update')`, 'status check after the update check');
  await check(`fx.checks.filter(c=>c==='update').length>=1&&fx.checks.includes('launch')&&!fx.requests.some(r=>r.method==='support_list')&&yieldingMethods.includes('support_collect')`, 'My reports checks status at launch and on the app update check, without holding the job slot');
  await feedback();
  await check(`txt('#page-title')==='Feedback'&&document.querySelector('nav button.selected').dataset.page==='my-reports'&&!document.getElementById('back-to-tools').checkVisibility()&&[...document.querySelectorAll('.sr-start-action strong')].map(s=>s.textContent).join('|')==='Report a problem|Request a feature|Ask a question'&&txt('#my-reports .sr-page-head h2')==='My reports'&&document.getElementById('support-owner-open').checkVisibility()`, 'Feedback opens from the rail: Report a problem, Request a feature, Ask a question, then My reports and the maintainer inbox');
  await shot('00-feedback', { rail: 'nav button.nav-feedback', art: '.sr-start-art', problem: '#support-open strong', idea: '#support-idea-open strong', question: '#support-question-open strong', mine: '#my-reports .sr-page-head h2', check: '[data-key="check-now"]', inbox: '#support-owner-open' });
  await startReport();
  await check(`[...wiz().querySelectorAll('.sr-choice')].map(b=>b.dataset.key.slice(2)).join()===XZ_SUPPORT_CONTRACT.reporter_hints.topic.join()&&document.activeElement.classList.contains('sr-choice')`, 'screen 1 offers the five topics in contract order, focus on the first');
  await check(`txt('#report-wizard .st-steps')==='1 Topic2 Describe'&&Boolean(button('Not sure'))&&Boolean(button('Cancel'))`, 'every screen has Not sure; the step strip shows where the box is');
  await shot('01-topic', { steps: '#report-wizard .st-steps', worse: '[data-key="c-GOT_WORSE"] strong', broken: '[data-key="c-NOT_WORKING"] strong', differently: '[data-key="c-WORK_DIFFERENTLY"] strong', idea: '[data-key="c-NEW_IDEA"] strong', question: '[data-key="c-HOW_DO_I"] strong', skip: '[data-key="skip"]' });
  await click('Not sure');
  await waitFor(`reportWizard.state().screen==='describe'`, 'Not sure skips to the box');
  await check(`txt('#report-wizard-title')==='What happened?'&&document.activeElement.classList.contains('sr-box')`, 'Not sure on the topic goes straight to What happened? with the box focused');
  await waitFor(`reportWizard.state().scan.state==='ready'`, 'collection finished');
  await check(`count('support_sources')===1&&count('support_collect')===1&&!wiz().querySelector('.sr-details').open`, 'collection starts by itself and the details panel stays closed');
  await click('Send privately');
  await check(`txt('.sr-box-foot .sr-error')==='Write a few words first. One sentence is enough.'&&count('support_prepare_v2')===0&&document.activeElement.classList.contains('sr-box')`, 'the one box is the only requirement; an empty Send explains and sends nothing');
  await evaluate(`wiz().dispatchEvent(new Event('cancel'))`);
  await check(`!wiz().open`, 'Escape closes the wizard while not sending');

  // Problem path with Is this it? and the details panel.
  await startReport();
  await check(`reportWizard.state().screen==='topic'`, 'reopening keeps the reporter where they were, nothing collected after closing');
  await choice('NOT_WORKING');
  await waitFor(`reportWizard.state().screen==='area'`, 'problem screen');
  await check(`wiz().querySelectorAll('.sr-choice').length===8&&wiz().querySelector('.sr-choices').classList.contains('sr-two')&&txt('#report-wizard-title')==='What kind of problem?'`, 'problem path: eight problem areas');
  await shot('02-problem-area');
  await choice('FOUR_DECKS_LOOPBACK');
  await waitFor(`reportWizard.state().screen==='describe'&&reportWizard.state().scan.state==='ready'`, 'describe with details');
  await check(`last('support_collect').reporter_hints.topic==='NOT_WORKING'&&last('support_collect').reporter_hints.problem_area==='FOUR_DECKS_LOOPBACK'&&[...wiz().querySelectorAll('.sr-context b')].map(b=>b.textContent).join('|')==="Something isn't working|Four decks or the loopback cable"&&Boolean(button('Change'))`, 'choices go to collection as hints and show above the box with Change');
  await evaluate(`type('Decks 3 and 4 are silent after I plug in the cable')`);
  await waitFor(`wiz().querySelectorAll('.sr-answer').length===1`, 'Is this it? card');
  await check(`txt('.sr-answers h3')==='Is this it?'&&txt('.sr-answer strong')==='Decks 3 and 4 need the loopback cable'&&Boolean(button('Set up four decks'))&&Boolean(button('That answers it'))`, 'Is this it? shows the direct answer with its setup action');
  await shot('03-is-this-it', { context: '.sr-context', change: '[data-key="change"]', box: '.sr-box', card: '.sr-answer strong', action: '.sr-answer .actions button', answers: '[data-key^="ok-"]', details: '.sr-details summary b', meta: '.sr-details summary .sr-meta', send: '[data-key="send"]', privacy: '.sr-privacy' });
  await click('Set up four decks');
  await check(`document.getElementById('quad-setup').open`, 'the answer action opens the four-deck setup');
  await evaluate(`document.getElementById('quad-setup').close()`);
  await check(`wiz().open&&wiz().querySelector('.sr-box').value.startsWith('Decks 3 and 4')`, 'the wizard and the typed words stay');
  await evaluate(`wiz().querySelector('.sr-details summary').click()`);
  await check(`wiz().querySelector('.sr-details').open&&wiz().querySelectorAll('.sr-item').length===6&&txt('.sr-details summary .sr-meta').startsWith('6 of 6 sources')`, 'expanding shows every source with Include');
  const first = await evaluate(`reportWizard.state().scan.items[1].item_id`);
  await evaluate(`wiz().querySelector('[data-key="show-${first}"]').click()`);
  await waitFor(`txt('.sr-preview').includes('"source_id": "usb_session"')`, 'exact content shown');
  await check(`txt('.sr-preview').includes('WAIT_LOOPBACK')&&last('support_source_preview').item_id===${JSON.stringify(first)}`, 'Show displays the exact cleaned content of that source');
  const before = await evaluate(`reportWizard.state().scan.digest`);
  await evaluate(`wiz().querySelector('[data-key="inc-${first}"]').click()`);
  await waitFor(`reportWizard.state().scan.digest!==${JSON.stringify(before)}`, 'digest updates');
  await check(`txt('.sr-details summary .sr-meta').startsWith('5 of 6 sources')&&txt('.sr-fingerprint').includes(reportWizard.state().scan.digest)`, 'unticking a source updates the count and the displayed selection SHA256');
  await evaluate(`wiz().querySelector('.sr-details').scrollIntoView({block:'start'})`);
  await shot('04-details-expanded', { summary: '.sr-details summary b', meta: '.sr-details summary .sr-meta', tick: '.sr-item input', name: '.sr-item label', show: '.sr-item .text-button', preview: '.sr-preview', omitted: '.sr-item[data-omitted] label' });
  await click('Send privately');
  await waitFor(`reportWizard.state().screen==='done'`, 'sent');
  await check(`(async()=>{const r=last('support_prepare_v2');const s=fx.scans.get(r.scan_generation);const kept=s.items.filter(i=>i.eligible&&!r.omit_item_ids.includes(i.item_id));return r.consent==='private-report/v2'&&r.omit_item_ids.join()===${JSON.stringify(first)}&&r.selection_sha256===await digestOf(kept)&&r.description.startsWith('Decks 3 and 4')&&r.collect===undefined;})()`, 'Send freezes the shown selection: omitted source out, selection SHA256 and consent bound');
  await check(`fx.lastSend.id===fx.lastDraft.report_id&&fx.lastSend.sha256===fx.lastDraft.sha256&&fx.sends===1`, 'Send uploads exactly the sealed draft by its ID and SHA256, once');
  await check(`txt('.sr-receipt strong')==='Your reference R-4F7K-2M9Q'&&txt('.sr-receipt dd').startsWith('R-4F7K-2M9Q')&&Boolean(button('Track my report'))&&button('Done').classList.contains('primary')`, 'success shows the receipt, Track my report and Done');
  await shot('05-received', { receipt: '.sr-receipt strong', status: '.sr-receipt p', id: '.sr-receipt dd', track: '[data-key="track"]', done: '[data-key="done"]' });
  await click('Copy');
  await check(`fx.clipboard.at(-1)==='R-4F7K-2M9Q'&&button('Copied')`, 'Copy copies only the public reference');
  await evaluate(`fx.clipboard=[]`);
  await click('Done');
  await check(`!wiz().open&&window.myReports.state().reports.length===1`, 'Done closes; the receipt is in My reports');

  // Degradation: live USB detection with per-stick diaries.
  await startReport();
  await choice('GOT_WORSE');
  await waitFor(`reportWizard.state().screen==='timing'`, 'timing');
  await check(`wiz().querySelectorAll('.sr-choice').length===4&&txt('#report-wizard .st-steps')==='1 Topic2 When3 USB sticks4 Describe'`, 'got worse: when did it get worse, then USB sticks, then the box (three screens before Send)');
  await shot('06-when-worse');
  await choice('DURING_SET_OVER_TIME');
  await waitFor(`reportWizard.state().screen==='usb'&&wiz().querySelectorAll('.sr-stick').length===1&&reportWizard.state().scan.state==='ready'`, 'one stick found', 20);
  await check(`txt('.sr-stick strong')==='USB with XZ Mods loader (E:)'&&txt('.sr-stick span')==='Diary 21:05 to 23:58'&&button("That's all of them").classList.contains('primary')`, "each stick lists its diary range; That's all of them is the main action");
  await evaluate(`fx.volumes=['C:\\\\','E:\\\\','F:\\\\']`);
  await waitFor(`wiz().querySelectorAll('.sr-stick').length===2&&reportWizard.state().scan.state==='ready'`, 'second stick detected live', 20);
  await check(`txt('.sr-stick:nth-child(2) strong')==='Music stick (F:)'&&txt('.sr-stick:nth-child(2) span')==='No diary on this stick'&&last('support_sources').supersedes`, 'a stick plugged in now appears by itself, says when it has no diary, and replaces the old scan');
  await shot('07-usb-sticks', { title: '#report-wizard-title', loader: '.sr-stick:nth-child(1) strong', diary: '.sr-stick:nth-child(1) span', music: '.sr-stick:nth-child(2) strong', none: '.sr-stick:nth-child(2) span', all: '[data-key="done-sticks"]', skip: '[data-key="skip"]' });
  await click("That's all of them");
  await waitFor(`reportWizard.state().screen==='describe'`, 'describe');
  await check(`count('support_collect')===4&&last('support_collect').reporter_hints.degradation_timing==='DURING_SET_OVER_TIME'`, 'the box reuses the sticks scan instead of collecting again');
  // Send while a slow collection runs: the read so far is kept, the rest is stopped.
  await evaluate(`fx.collectHold=true;wiz().querySelector('[data-key="recollect"]').click()`);
  await waitFor(`reportWizard.state().scan.state==='collecting'`, 'slow collection running');
  await check(`txt('.sr-details summary b')==='Collecting details. You can send now.'`, 'Send stays available while collecting');
  await evaluate(`type('Dropouts get longer after an hour on deck 3')`);
  await click('Send privately');
  await waitFor(`reportWizard.state().screen==='done'`, 'sent while collecting');
  await check(`(()=>{const r=last('support_prepare_v2');return fx.cancelled.has(r.scan_generation)&&r.scan_generation===last('support_collect').scan_generation&&!r.collect;})()`, 'Send stops the unfinished reads and freezes what had been read');
  await evaluate(`fx.collectHold=false`);
  await click('Done');

  // Expectation path with the reference gear.
  await startReport();
  await choice('WORK_DIFFERENTLY');
  await waitFor(`reportWizard.state().screen==='baseline'`, 'baseline');
  await check(`wiz().querySelectorAll('.sr-choice').length===5&&!wiz().querySelector('[data-key="c-NONE"]')`, 'compared with what: the five reporter baselines');
  await shot('08-compared-with');
  await choice('DJ_CONVENTION');
  await waitFor(`reportWizard.state().screen==='reference'`, 'reference');
  await check(`[...wiz().querySelectorAll('.sr-chip')].map(b=>b.textContent).join()==='CDJ-3000,CDJ-2000NXS2,XDJ-RX3,rekordbox,Serato,Traktor'&&txt('#report-wizard .st-steps')==='1 Topic2 Compared with3 Which one4 Describe'`, 'which gear: the DJ gear and software suggestions');
  await shot('09-which-gear');
  await choice('CDJ_3000');
  await waitFor(`reportWizard.state().screen==='describe'`, 'describe');
  await click('Back');
  await click('Back');
  await choice('XZ_CONSISTENCY');
  await waitFor(`reportWizard.state().screen==='reference'`, 'reference again');
  await check(`[...wiz().querySelectorAll('.sr-chip')].map(b=>b.textContent).join()==='Decks 1 and 2,Another XZ Mods feature'&&!reportWizard.state().hints.reference_feature`, 'changing the baseline changes the suggestions and drops the old pick');
  await click('Not sure');
  await click('Cancel');

  // Idea: no logs, and the question path answers SYNC and KEY SYNC differently.
  await feedback();
  await evaluate(`document.getElementById('support-idea-open').click()`);
  await waitFor(`wiz().open&&reportWizard.state().screen==='idea'`, 'idea type');
  await check(`reportWizard.state().hints.topic==='NEW_IDEA'&&txt('#report-wizard .st-steps li[data-s=done]').includes('Topic')`, 'Request a feature skips the topic question: it opens on the kind of idea');
  await shot('10-idea-type');
  await choice('APPEARANCE');
  await waitFor(`reportWizard.state().screen==='describe'`, 'idea box');
  const collects = await evaluate(`count('support_collect')`);
  await check(`txt('#report-wizard-title')==="What's your idea?"&&txt('#report-wizard .st-note')==="Ideas don't include logs from this computer or your USB sticks. Only your words are sent."`, 'ideas say plainly that no logs are collected');
  await evaluate(`type('Let me pick the waveform colours per deck')`);
  await shot('11-idea-box');
  await click('Send privately');
  await waitFor(`reportWizard.state().screen==='done'`, 'idea sent');
  await check(`count('support_collect')===${collects}&&last('support_prepare_v2').collect===false&&last('support_prepare_v2').reporter_hints.idea_type==='APPEARANCE'&&txt('#report-wizard-title')==='Idea received'`, 'an idea collects nothing and sends only the words');
  await click('Done');
  await feedback();
  await evaluate(`document.getElementById('support-question-open').click()`);
  await waitFor(`wiz().open&&reportWizard.state().screen==='describe'&&txt('#report-wizard-title')==="What's your question?"`, 'Ask a question opens straight on the question box');
  await evaluate(`type('KEY SYNC does not work on deck 3')`);
  await waitFor(`wiz().querySelectorAll('.sr-answer').length===1`, 'question answer');
  await check(`txt('.sr-answer strong')==='KEY SYNC and KEY CPU LIMIT'`, 'KEY SYNC gets the KEY SYNC answer, not the SYNC labels');
  await evaluate(`type('What does SYNC 3 mean on deck 4?')`);
  await waitFor(`txt('.sr-answer strong')==='What SYNC 3, BPM 1, LIMIT and EXT MASTER mean'`, 'SYNC labels answer');
  await shot('12-question-answer');
  await click('That answers it');
  await check(`txt('#report-wizard-title')==='Glad that helped'&&count('support_prepare_v2')===3`, 'That answers it ends without sending');
  await click('Done');

  // A send that cannot be confirmed leads into the connection setup and retries the same report.
  await evaluate(`fx.sendMode='fail'`);
  await startReport();
  await click('Not sure');
  await evaluate(`type('The screen froze when I loaded a track')`);
  await waitFor(`reportWizard.state().scan.state==='ready'`, 'ready');
  await click('Send privately');
  await waitFor(`reportWizard.state().screen==='failed'`, 'failed');
  await check(`txt('#report-wizard-title')==='Not confirmed yet'&&button('Set up reporting connection').classList.contains('primary')&&wiz().querySelectorAll('.st-foot .primary').length===1`, 'not confirmed: one primary button into the reporting connection setup');
  await shot('13-not-confirmed');
  const failedId = await evaluate(`fx.lastDraft.report_id`);
  await click('Set up reporting connection');
  await check(`document.getElementById('report-connection').open&&txt('#report-connection .st-steps')==='1 Connection2 Clock3 Send again'`, 'the setup walks connection, clock and send again');
  await shot('14-connection-setup', { online: '#report-connection .st-note', clock: '#report-connection h3:nth-of-type(2)', again: '#report-connection [data-key="conn-send"]' });
  await evaluate(`fx.sendMode='ok'`);
  await click('Send again', `document.getElementById('report-connection')`);
  await waitFor(`reportWizard.state().screen==='done'`, 'sent after setup');
  await check(`fx.lastSend.id===${JSON.stringify(failedId)}&&fx.requests.filter(r=>r.method==='support_prepare_v2').length===4`, 'Send again uploads the same report ID and SHA256; no second report is prepared');
  await click('Done');

  // My reports, the status notice and the tracking view.
  const tracked = await evaluate(`fx.receipts[0].report_id`);
  await evaluate(`fx.unseen=[{report_id:${JSON.stringify(tracked)},item_number:'XZ-1042',lifecycle:'fix_in_progress',status_text_key:'fix_in_progress',status_text:'A fix is in progress.',revision:3,classified_kind:'BUG',updated_at:'2026-10-29T09:00:00.000Z'}];myReports.check('update')`);
  await waitFor(`!document.getElementById('report-notice').classList.contains('hidden')`, 'notice');
  await check(`txt('#report-notice-text')==='Update on your report: A fix is in progress.'`, 'a status change shows a notice in plain words');
  await evaluate(`showPage('updates');window.scrollTo(0,0)`);
  await shot('15-status-notice', { notice: '#report-notice-text', view: '#report-notice-view' });
  await evaluate(`showPage('my-reports')`);
  await waitFor(`document.querySelectorAll('.sr-reports .sr-report').length===4`, 'my reports list');
  await check(`txt('.sr-report[data-unseen] .sr-status')==='New: A fix is in progress.'&&[...document.querySelectorAll('.sr-reports .sr-badge')].map(b=>b.textContent).includes('Idea')`, 'My reports shows each report with its kind and status in plain words');
  await check(`txt('.sr-report[data-unseen] .sr-reference')==='R-4F7K-2M9Q'&&txt('.sr-report[data-unseen] .sr-item-number')==='Part of XZ-1042'`, 'My reports shows the report reference and assigned item');
  await evaluate(`document.querySelector('#my-reports .sr-page-head').scrollIntoView({block:'start'});scrollBy(0,-20)`);
  await shot('16-my-reports', { notice: '#report-notice-text', view: '#report-notice-view', kind: '.sr-reports .sr-badge', words: '.sr-reports .sr-report strong', status: '.sr-report[data-unseen] .sr-status', sent: '.sr-reports .sr-when', check: '[data-key="check-now"]', mine: '#my-reports .sr-page-head h2', item: '.sr-report[data-unseen] .sr-item-number' });
  await evaluate(`scrollTo(0,0)`);
  await size(700,760);
  await check(`document.querySelector('.sr-report[data-unseen] .sr-reference').checkVisibility()&&document.querySelector('.sr-report[data-unseen] .sr-item-number').checkVisibility()`, 'narrow My reports rows keep both identifiers visible');
  await size(1100,760);
  await evaluate(`document.getElementById('report-notice-view').click()`);
  await waitFor(`document.getElementById('report-track').open`, 'tracking view');
  await waitFor(`fx.seen===${JSON.stringify(tracked)}&&document.getElementById('report-notice').classList.contains('hidden')`, 'seen');
  await check(`txt('#report-track .sr-track-status')==='A fix is in progress.'&&txt('#report-track .st-steps').startsWith('1 Received2 Confirmed3 Fix in progress')`, 'the tracking view shows where the report is, read-only');
  await check(`txt('#report-track h2').includes('R-4F7K-2M9Q')&&txt('#report-track .sr-item-number')==='Part of XZ-1042'`, 'tracking shows both identifiers');
  await shot('17-tracking', { steps: '#report-track .st-steps', title: '#report-track h2', words: '#report-track .sr-track-words', status: '#report-track .sr-track-status', dates: '#report-track .sr-dates', more: '#report-track .sr-more summary', check: '#report-track [data-key="check-now"]' });
  await evaluate(`document.querySelector('#report-track .sr-more summary').click()`);
  await click('Copy status link', `document.getElementById('report-track')`);
  await waitFor(`fx.clipboard.length===1`, 'copied');
  await check(`/^https:\\/\\/xz-mods-support\\.will-026\\.workers\\.dev\\/track#id=[a-f0-9]{32}&read=[a-f0-9]{64}$/.test(fx.clipboard[0])&&!fx.clipboard[0].includes('?')`, 'the status link carries a read-only capability in the fragment, never a query');
  await click('Forget this report', `document.getElementById('report-track')`);
  await check(`Boolean(document.querySelector('#report-track .sr-confirm'))&&fx.receipts.length===4`, 'forgetting asks first');
  await evaluate(`button('Keep it',document.getElementById('report-track')).click();document.getElementById('report-track').close()`);

  // Maintainer inbox: setup, then the taxonomy columns in priority order.
  await feedback();
  await evaluate(`document.getElementById('support-owner-open').click()`);
  await waitFor(`document.getElementById('support-inbox').open&&Boolean(document.getElementById('support-owner-key'))`, 'inbox setup');
  await check(`document.querySelectorAll('#support-inbox .primary').length===1&&Boolean(button('Connect and check',document.getElementById('support-inbox')))`, 'not connected: one primary button into the key setup with a live check');
  await shot('18-inbox-setup');
  await evaluate(`document.getElementById('support-owner-key').value='synthetic-owner-key-for-fixture-0000';button('Connect and check',document.getElementById('support-inbox')).click()`);
  await waitFor(`document.querySelectorAll('#support-inbox tbody tr').length===3`, 'inbox rows');
  await click('Load more', `document.getElementById('support-inbox')`);
  await waitFor(`document.querySelectorAll('#support-inbox tbody tr').length===5`, 'more rows');
  await check(`[...document.querySelectorAll('#support-inbox th')].map(t=>t.textContent).join()==='Kind,Severity,Impact,Baseline,Affected features,In use since,Reach,Status,Reference,Item'`, 'inbox columns follow the taxonomy order');
  await check(`(()=>{const c=[...document.querySelector('#support-inbox tbody tr').cells].map(td=>td.textContent);return c[0].startsWith('Regression')&&c[1]==='Live-set stopper'&&c[2]==='Restores expected'&&c[3]==='Earlier XZ Mods'&&c[4]==='SYNC lampMASTER hand-off'&&c[5]==='0.5.0-preview.21'&&c[6].startsWith('6 reports')&&c[6].includes('MODERN, CLASSIC')&&c[6].includes('decks 3, 4')&&c[7].startsWith('Triaged')&&c[7].includes('Fix rule: eligible · automation off');})()`, 'a row reads kind, severity, impact, baseline, features, in use since, reach and status');
  await check(`txt('#support-inbox tbody tr:nth-child(3) td:nth-child(3)').includes('Adds a setting: SYNC lamp')&&document.querySelector('#support-inbox tbody tr:nth-child(4)').hasAttribute('data-flag')&&txt('#support-inbox tbody tr:nth-child(4) td:nth-child(3)').includes('Never automatic')`, 'opt-in changes show their setting; changes to defaults are flagged for the owner');
  await click('Check for changes', `document.getElementById('support-inbox')`);
  await waitFor(`document.querySelectorAll('#support-inbox tr[data-changed]').length===1`, 'changed rows');
  await evaluate(`document.querySelector('#support-inbox [data-key="read-${'a'.repeat(32)}"]').click()`);
  await waitFor(`txt('#support-inbox pre').includes('SYNC lamp stays dark')`, 'report read');
  await shot('19-inbox');
  await evaluate(`document.querySelector('#support-inbox .sr-table-wrap').scrollLeft=9999`);
  await shot('20-inbox-identifiers');
  for (const [query,expected] of [[' r 4f7k - 2m9q ','R-4F7K-2M9Q'],['r oilo 0001','R-0110-0001'],[' xz - 1 042 ','R-4F7K-2M9Q']]) {
    await evaluate(`document.getElementById('support-inbox-search').value=${JSON.stringify(query)};button('Find',document.getElementById('support-inbox')).click()`);
    await waitFor(`document.querySelectorAll('#support-inbox tbody tr').length===1`, 'identifier search');
    await check(`txt('#support-inbox tbody .sr-reference')===${JSON.stringify(expected)}`, 'inbox tolerant lookup: '+query.trim());
    await click('Clear', `document.getElementById('support-inbox')`);
    await waitFor(`document.querySelectorAll('#support-inbox tbody tr').length===3`, 'clear search');
  }
  await evaluate(`document.getElementById('support-inbox-search').value='XZ-1000';button('Find',document.getElementById('support-inbox')).click()`);
  await check(`txt('#support-inbox [role=status]').includes('Enter a reference')`, 'invalid identifier gets a useful search message');
  await evaluate(`document.getElementById('support-inbox').close()`);

  assert.deepEqual(errors, [], 'no page errors before From your XZ: ' + errors.join(' / '));
  assert.deepEqual(await evaluate(`fx.csp`), [], 'CSP violations before From your XZ');

  // From your XZ: a report sent from the XZ waits in My reports for the DJ's words.
  const today = (h, m) => { const d = new Date(); d.setHours(h, m, 0, 0); return d.getTime(); };
  const pendingRow = (n, extra = {}) => ({ report_id: String(n).repeat(32), report_reference: 'R-7Q2M-4F8' + 'KMNPR'[n - 1], installation_id: 'I-4F7K-2M9Q', loader_id: 'L-7K2M-9Q4F', origin: 'controller',
    comments_state: 'pending_comments', comments_pending: true, remind: true, created_at: today(21, 14 + n), reminders_until: Date.now() + 6 * 86400000, description: '', topic: 'NOT_WORKING',
    screen_state: { surface: 'STOCK', stock_page: 'PERFORMANCE', mods_page: 'UNKNOWN', interface: 'MODERN', deck_view: 'FOUR', alert_code: 'NONE', counter_code: 'E-8307' }, details_expired: false, completion_pending: false, ...extra });
  const preset = async value => { await evaluate(`localStorage.setItem('fx.preset',${JSON.stringify(JSON.stringify(value))});localStorage.removeItem('xz-mods.from-xz.notified')`); await open(); };
  const item = id => `document.getElementById('xz-item-${id}')`;
  const typeInto = (id, text) => evaluate(`(()=>{const box=document.getElementById('xz-text-${id}');box.value=${JSON.stringify(text)};box.dispatchEvent(new Event('input',{bubbles:true}));})()`);
  const waiting = pendingRow(1, { description: 'Deck 3 went silent after the cable' }), id1 = waiting.report_id;
  await preset({ pending: [waiting] });
  await waitFor(`!document.getElementById('xz-callout').classList.contains('hidden')`, 'start callout');
  await check(`txt('#xz-callout .sr-xz-callout-text')==='Your XZ sent a report, R-7Q2M-4F8K. Add what happened?'&&fx.toasts.length===0&&!document.querySelector('dialog[open]')&&!document.getElementById('xz-callout').contains(document.activeElement)`, 'start with a pending report: a quiet callout names it, no notification, no dialog, focus left alone');
  await check(`txt('nav button[data-page="my-reports"] .sr-xz-badge')==='1, 1 report from your XZ needs your words'&&document.querySelectorAll('.sr-xz-badge').length===1`, 'Feedback in the rail carries one badge with a spoken count');
  await shot('21-xz-callout', { callout: '#xz-callout .sr-xz-callout-text', open: '[data-key="xz-callout-open"]', later: '[data-key="xz-callout-later"]', stop: '[data-key="xz-callout-stop"]', badge: 'nav button[data-page="my-reports"] .sr-xz-badge' });
  await click('Add what happened', `document.getElementById('xz-callout')`);
  await waitFor(`!document.getElementById('my-reports').classList.contains('hidden')&&document.activeElement?.id==='xz-text-${id1}'`, 'item opened with its box focused');
  await check(`document.activeElement.value==='Deck 3 went silent after the cable'&&document.activeElement.selectionStart===document.activeElement.value.length&&document.getElementById('xz-callout').classList.contains('hidden')`, 'Add what happened opens the report with the XZ text in the box, ready to type');
  await check(`(()=>{const it=${item(id1)};const facts=[...it.querySelectorAll('dd')].map(d=>d.textContent);return txt('.sr-xz-head .sr-reference',it)==='R-7Q2M-4F8K'&&txt('.sr-xz-when',it)==='Sent from your XZ today at 21:15'&&facts.join('|')==="Something isn't working|MODERN play screen, 4 decks, E-8307 shown|L-7K2M-9Q4F, written 12 Oct 2026|Deck 3 went silent after the cable";})()`, 'each report shows reference, time, topic, screen in words, stick and what was typed on the XZ');
  await check(`(()=>{const it=${item(id1)};return it.getAttribute('aria-labelledby')==='xz-ref-${id1}'&&txt('label[for="xz-text-${id1}"]')==='What happened?'&&it.querySelectorAll('.primary').length===1&&button('Send',it).classList.contains('primary')&&Boolean(button('Send without comments',it))&&!it.querySelector('.sr-details').open&&fx.detailsReads===0;})()`, 'one labelled box, one primary Send, Send without comments, details closed and not read yet');
  await check(`![...document.querySelectorAll('.sr-reports .sr-reference')].some(r=>r.textContent==='R-7Q2M-4F8K')&&document.querySelectorAll('.sr-xz-item').length===1`, 'a waiting report is listed once, under From your XZ');
  await evaluate(`document.querySelector('.sr-xz').scrollIntoView({block:'start'});scrollBy(0,-20)`);
  await waitFor(`!${item(id1)}.hasAttribute('data-target')`, 'the opening outline fades', 4);
  await shot('22-xz-report', { title: '#sr-xz-title', reference: '.sr-xz-head .sr-reference', when: '.sr-xz-when', screen: '.sr-xz-facts dd:nth-of-type(2)', stick: '.sr-xz-facts dd:nth-of-type(3)', typed: '.sr-xz-typed', box: '.sr-xz .sr-box', details: '.sr-xz .sr-details summary b', stop: '[data-key^="xz-stop-"]', bare: '[data-key^="xz-bare-"]', send: '[data-key^="xz-send-"]' });
  await evaluate(`${item(id1)}.querySelector('.sr-details summary').click()`);
  await waitFor(`${item(id1)}.querySelectorAll('.sr-xz-items .sr-item').length===2`, 'details read');
  await check(`fx.detailsReads===1&&[...${item(id1)}.querySelectorAll('.sr-xz-source')].map(s=>s.textContent).join('|')==='Session log · USB A|Diary · USB A'&&txt('.sr-details .sr-meta',${item(id1)})==='2 sources · last 15 min'`, 'opening Collected details reads them once and names each source in words');
  await evaluate(`(()=>{const b=button('Show',${item(id1)});b.focus();b.click();})()`);
  await waitFor(`txt('.sr-preview',${item(id1)}).includes('WAIT_LOOPBACK')`, 'exact content');
  await check(`document.activeElement.dataset.key==='xz-show-${'c'.repeat(32)}'&&txt('.sr-preview',${item(id1)}).includes('"source_id": "usb_session"')`, 'Show displays exactly what the XZ sent, and focus stays on the button');
  await evaluate(`${item(id1)}.querySelector('.sr-details').scrollIntoView({block:'center'})`);
  await shot('23-xz-details', { summary: '#xz-item-' + id1 + ' .sr-details summary b', source: '#xz-item-' + id1 + ' .sr-xz-source', preview: '#xz-item-' + id1 + ' .sr-preview' });
  await typeInto(id1, '   ');
  await evaluate(`button('Send',${item(id1)}).click()`);
  await check(`txt('#xz-note-${id1} .sr-error')==='Write a few words first, or choose Send without comments.'&&fx.completions.length===0&&document.activeElement.id==='xz-text-${id1}'&&document.activeElement.getAttribute('aria-invalid')==='true'`, 'an empty box explains itself and sends nothing');
  await typeInto(id1, 'Deck 3 went silent after the cable. I had just plugged it into USB 1.');
  await evaluate(`button('Send',${item(id1)}).click()`);
  await waitFor(`fx.completions.length===1&&!${item(id1)}`, 'completed');
  await waitFor(`[...document.querySelectorAll('.sr-reports .sr-reference')].some(r=>r.textContent==='R-7Q2M-4F8K')`, 'listed with sent reports');
  await check(`fx.completions[0].id==='${id1}'&&fx.completions[0].comments==='Deck 3 went silent after the cable. I had just plugged it into USB 1.'&&txt('.sr-xz .st-note')==='Sent. R-7Q2M-4F8K now has your words and is listed with your reports below.'&&document.activeElement.id==='sr-xz-title'`, 'Send completes the same report with the edited words; its reference does not change');
  await check(`!document.querySelector('.sr-xz-badge')&&document.querySelectorAll('.sr-xz-item').length===0`, 'once completed, the badge goes');
  await evaluate(`window.scrollTo(0,0)`);
  await shot('24-xz-sent', { sent: '.sr-xz .st-note', reference: '.sr-reports .sr-reference' });

  // Several reports, reminders stopped, live arrivals, notifications on and off.
  const quiet = pendingRow(2, { remind: false, loader_id: null, details_expired: true, topic: null, created_at: today(20, 2) - 86400000,
    screen_state: { surface: 'MODS', stock_page: 'UNKNOWN', mods_page: 'STEMS', interface: 'CLASSIC', deck_view: 'TWO', alert_code: 'NONE', counter_code: 'NONE' } });
  const live = pendingRow(1), id2 = quiet.report_id;
  await preset({ pending: [live, quiet], identity: { installation_id: 'I-4F7K-2M9Q', registered: true, loaders: [
    { loader_id: 'L-7K2M-9Q4F', created_at: '2026-10-12T19:30:00.000Z', revoked: false, revocation_pending: false, registration_pending: false, write_pending: false },
    { loader_id: 'L-3H8W-1D6C', created_at: '2026-10-20T09:05:00.000Z', revoked: false, revocation_pending: false, registration_pending: true, write_pending: false },
    { loader_id: 'L-0P5T-6V2X', created_at: '2026-09-02T18:00:00.000Z', revoked: true, revocation_pending: false, registration_pending: false, write_pending: false }] } });
  await waitFor(`!document.getElementById('xz-callout').classList.contains('hidden')`, 'callout');
  await check(`txt('#xz-callout .sr-xz-callout-text')==='Your XZ sent a report, R-7Q2M-4F8K. Add what happened?'&&txt('nav button[data-page="my-reports"] .sr-xz-badge [aria-hidden]')==='1'`, 'a report whose reminders ended is neither in the callout nor in the badge');
  await click('Remind me later', `document.getElementById('xz-callout')`);
  await check(`document.getElementById('xz-callout').classList.contains('hidden')&&txt('nav button[data-page="my-reports"] .sr-xz-badge [aria-hidden]')==='1'&&fx.dismissed.length===0`, 'Remind me later hides the callout for now; the badge stays and nothing is dismissed');
  await evaluate(`fx.emit('xz-controller-queue-changed',fx.queue())`);
  await check(`fx.toasts.length===0&&xzQueue.state().startup===false&&document.getElementById('xz-callout').classList.contains('hidden')`, 'the launch check brings no notification for reports already known, and Remind me later holds');
  const third = pendingRow(3, { topic: 'GOT_WORSE', description: 'Dropouts after an hour' }), id3 = third.report_id;
  await evaluate(`fx.pending.push(${JSON.stringify(third)});fx.emit('xz-controller-queue-changed',fx.queue())`);
  await waitFor(`fx.toasts.length===1`, 'notification');
  await check(`fx.toasts[0].id==='${id3}'&&fx.toasts[0].reference==='R-7Q2M-4F8N'&&txt('nav button[data-page="my-reports"] .sr-xz-badge [aria-hidden]')==='2'&&document.getElementById('xz-callout').classList.contains('hidden')`, 'a report arriving while the app is open raises one Windows notification and the badge counts it');
  await evaluate(`fx.emit('xz-controller-queue-changed',fx.queue());fx.emit('xz-controller-queue-changed',fx.queue())`);
  await new Promise(done => setTimeout(done, 150));
  await check(`fx.toasts.length===1&&JSON.parse(localStorage.getItem('xz-mods.from-xz.notified')).includes('${id3}')`, 'each report is announced once, remembered across restarts');
  await evaluate(`fx.emit('xz-controller-report-open',{id:'${id3}'})`);
  await waitFor(`!document.getElementById('my-reports').classList.contains('hidden')&&document.activeElement?.id==='xz-text-${id3}'`, 'notification opens its report');
  await check(`${item(id3)}.hasAttribute('data-target')&&document.activeElement.value==='Dropouts after an hour'`, 'clicking the notification opens that report in My reports with its box focused');
  await evaluate(`showPage('setup');fx.notifyMode='off'`);
  const fourth = pendingRow(4), fifth = pendingRow(5);
  await evaluate(`fx.pending.push(${JSON.stringify(fourth)});fx.emit('xz-controller-queue-changed',fx.queue())`);
  await waitFor(`!document.getElementById('xz-callout').classList.contains('hidden')`, 'fallback callout');
  await evaluate(`fx.pending.push(${JSON.stringify(fifth)});fx.emit('xz-controller-queue-changed',fx.queue())`);
  await waitFor(`txt('#xz-callout .sr-xz-callout-text')==='Your XZ sent 4 reports. Add what happened?'`, 'fallback callout counts');
  await check(`fx.toasts.length===2&&xzQueue.state().toasts==='off'&&txt('nav button[data-page="my-reports"] .sr-xz-badge [aria-hidden]')==='4'&&!/notification|Settings/i.test(txt('#xz-callout'))&&!document.querySelector('dialog[open]')`, 'notifications off: one try, then the badge and callout carry new reports, with no request to turn notifications on');
  await shot('25-xz-callout-several', { callout: '#xz-callout .sr-xz-callout-text' });
  await click('Stop reminding', `document.getElementById('xz-callout')`);
  await waitFor(`fx.dismissed.length===4`, 'dismissed');
  await check(`document.getElementById('xz-callout').classList.contains('hidden')&&!document.querySelector('.sr-xz-badge')&&[...fx.dismissed].sort().join()===${JSON.stringify([live, third, fourth, fifth].map(p => p.report_id).sort().join())}&&!fx.dismissed.includes('${id2}')`, 'Stop reminding dismisses every reminding report: callout and badge go');
  await evaluate(`fx.pending.push(${JSON.stringify(pendingRow(5, { report_id: '6'.repeat(32), report_reference: 'R-7Q2M-4F8T', remind: false }))});fx.emit('xz-controller-queue-changed',fx.queue())`);
  await new Promise(done => setTimeout(done, 150));
  await check(`fx.toasts.length===2&&document.getElementById('xz-callout').classList.contains('hidden')`, 'a report whose reminders stopped or ended never notifies');
  await evaluate(`showPage('my-reports')`);
  await waitFor(`document.querySelectorAll('.sr-xz-item').length===6`, 'all waiting reports listed');
  await check(`(()=>{const it=${item(id2)};const facts=[...it.querySelectorAll('dd')].map(d=>d.textContent);return txt('.sr-xz-quiet',it)==='No reminders'&&!button('Stop reminding',it)&&facts.join('|')==='Not chosen on the XZ|CLASSIC MODS menu, STEMS page, 2 decks|An older loader stick with no ID'&&txt('.sr-xz-expired',it).startsWith('The collected details were deleted after 14 days')&&txt('.sr-xz-when',it)==='Sent from your XZ yesterday at 20:02';})()`, 'reports keep their place after reminders stop; expired details and an older stick are said plainly');
  await evaluate(`button('Send without comments',${item(id2)}).click()`);
  await waitFor(`fx.completions.length===1&&!${item(id2)}`, 'completed without comments');
  await check(`fx.completions[0].comments===null&&txt('.sr-xz .st-note')==='Sent. R-7Q2M-4F8M is complete and is listed with your reports below.'`, 'Send without comments completes the report with no words');
  await evaluate(`fx.online=false`);
  await typeInto(id3, 'Dropouts after an hour, on deck 3 only');
  await evaluate(`button('Send',${item(id3)}).click()`);
  await waitFor(`txt('.st-note',${item(id3)}).startsWith('Saved on this computer.')`, 'saved offline');
  await check(`fx.completions.at(-1).comments==='Dropouts after an hour, on deck 3 only'&&!document.getElementById('xz-text-${id3}')&&txt('.sr-xz-offline').startsWith("This computer can't reach the report service")`, 'offline: the words are saved and go by themselves; the box closes so they cannot be changed');
  await evaluate(`${item(id3)}.scrollIntoView({block:'center'})`);
  await shot('26-xz-saved-offline', { saved: '#xz-item-' + id3 + ' .st-note', offline: '.sr-xz-offline' });
  await size(700, 760);
  await check(`[...${item(fourth.report_id)}.querySelectorAll('.sr-xz-foot button')].every(b=>b.checkVisibility()&&b.getBoundingClientRect().right<=document.documentElement.clientWidth)`, 'narrow window: every action stays visible');
  await size(1100, 760);

  // Your XZ sticks.
  await evaluate(`document.getElementById('xz-sticks-title').scrollIntoView({block:'start'})`);
  await check(`[...document.querySelectorAll('.sr-loader')].map(l=>txt('.sr-reference',l)+'|'+txt('small',l)+'|'+txt('.sr-tag',l)).join('/')==='L-3H8W-1D6C|Written 20 Oct 2026|Registers when online/L-7K2M-9Q4F|Written 12 Oct 2026|Can send reports/L-0P5T-6V2X|Written '+new Date('2026-09-02T18:00:00.000Z').toLocaleDateString('en-GB',{day:'numeric',month:'short',year:'numeric'})+'|Revoked'&&document.querySelectorAll('.sr-loader button').length===2`, 'Your XZ sticks lists each loader ID, when it was written and whether it can send reports');
  await shot('27-xz-sticks', { title: '#xz-sticks-title', id: '.sr-loader .sr-reference', written: '.sr-loader small', state: '.sr-loader .sr-tag', revoke: '[data-key="xz-revoke-L-7K2M-9Q4F"]' });
  await evaluate(`document.querySelector('[data-key="xz-revoke-L-7K2M-9Q4F"]').click()`);
  await check(`txt('.sr-loader .sr-confirm').startsWith('Revoke L-7K2M-9Q4F? The XZ can no longer send reports from this stick.')&&document.activeElement.dataset.key==='xz-revoke-no'&&fx.revoked.length===0`, 'Revoke asks first, with focus on Keep it');
  await shot('28-xz-revoke', { confirm: '.sr-loader .sr-confirm', yes: '[data-key="xz-revoke-yes"]', no: '[data-key="xz-revoke-no"]' });
  await click('Revoke this stick', `document.querySelector('.sr-xz-sticks')`);
  await waitFor(`fx.revoked.length===1&&txt('.sr-xz-sticks-status')!==''`, 'revoked');
  await check(`fx.revoked[0]==='L-7K2M-9Q4F'&&[...document.querySelectorAll('.sr-loader')].some(l=>txt('.sr-reference',l)==='L-7K2M-9Q4F'&&txt('.sr-tag',l)==='Revokes when online'&&!l.querySelector('button'))&&txt('.sr-xz-sticks-status')==='L-7K2M-9Q4F is revoked on this computer. The report service hears of it the next time XZ Mods is online.'&&document.activeElement.id==='xz-sticks-title'`, 'Revoke is kept on this computer and sent when online; focus returns to the list');
  await evaluate(`fx.identity.loaders=[];xzQueue.reload()`);
  await waitFor(`Boolean(document.querySelector('.sr-xz-sticks-setup'))`, 'no sticks');
  await check(`document.querySelectorAll('.sr-xz-sticks .primary').length===1&&txt('.sr-xz-sticks-setup').startsWith('No XZ sticks yet.')`, 'no sticks yet: one primary button into the USB setup');
  await evaluate(`document.getElementById('xz-sticks-title').scrollIntoView({block:'start'})`);
  await shot('29-xz-sticks-empty', { setup: '.sr-xz-sticks-setup strong', button: '[data-key="xz-setup-usb"]' });
  await click('Set up a USB', `document.querySelector('.sr-xz-sticks')`);
  await check(`!document.getElementById('setup').classList.contains('hidden')`, 'Set up a USB opens the USB setup');
  await evaluate(`localStorage.removeItem('fx.preset')`);

  assert.deepEqual(errors, [], 'no page errors: ' + errors.join(' / '));
  const violations = await evaluate(`fx.csp`);
  assert.deepEqual(violations, [], 'CSP violations');
  await check(`(()=>{const probe=document.createElement('div');probe.setAttribute('style','color:rgb(255, 0, 0)');document.body.append(probe);const blocked=getComputedStyle(probe).color!=='rgb(255, 0, 0)';probe.remove();return location.protocol==='http:'&&blocked;})()`, 'the page runs under the app CSP');
  console.log('\n' + passed + ' checks passed');
} finally {
  socket?.close();
  await server.close();
  killBrowser(child, profile);
  await new Promise(done => setTimeout(done, 300));
  await rm(profile, { recursive: true, force: true }).catch(() => {});
}
