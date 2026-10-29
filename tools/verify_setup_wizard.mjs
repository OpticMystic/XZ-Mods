import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, rm, readFile, writeFile, mkdir } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { serveUi } from './csp_server.mjs';
import { killBrowser } from './kill_browser.mjs';

// Drives ui/index.html in headless Edge over CDP with a mocked Tauri invoke, served with the app's real CSP. No packages needed.
// Run: node tools/verify_setup_wizard.mjs   (XZ_TEST_BROWSER overrides the browser path; XZ_WIZARD_SHOTS=<dir> saves screenshots)
const app = resolve(dirname(fileURLToPath(import.meta.url)), '..');

function installMock() {
  const mode = location.hash.slice(1) || 'fresh';
  const hasLoader = ['loader', 'unknown', 'downgrade', 'nonotes', 'current'].includes(mode);
  const schema = mode === 'gamesmenu' ? { ...window.__SCHEMA__, fields: [...window.__SCHEMA__.fields, { key: 'games_menu', label: 'Games menu', group: 'extras', default: 63, min: 0, max: 63, kind: 'int', readonly: false }] } : window.__SCHEMA__;
  const defaults = Object.fromEntries(schema.fields.map(field => [field.key, field.default]));
  const OLD = '1111111aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', NEW = '2222222bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
  const kinds = ['fixed', 'new', 'new', 'fixed', 'improved', 'new', 'fixed', 'improved', 'new'];
  Object.defineProperty(window, 'MOD_CHANGES', { writable: false, value: { format: 'xz-mod-changes-ui/1',
    changes: Object.fromEntries(kinds.map((kind, index) => ['c' + (index + 1), { kind, area: ['Stems', 'Games', 'Waveform'][index % 3], text: 'Fixture change ' + (index + 1) }])),
    builds: { [OLD]: ['c1'], [NEW]: kinds.map((kind, index) => 'c' + (kinds.length - index)) } } });
  const savedCommit = { downgrade: OLD, nonotes: 'abcdef1234567' }[mode] || NEW;
  const recorded = { loader: { id: 'old', label: 'Old build', runtime_sha256: 'oldsha', source_commit: OLD.slice(0, 10) }, nonotes: { id: 'old', label: 'Old build', runtime_sha256: 'oldsha', source_commit: OLD },
    downgrade: { id: 'new', label: 'New build', runtime_sha256: 'nsha', source_commit: NEW }, current: { id: 'v1', label: 'Fixture build', runtime_sha256: 'newsha', source_commit: NEW } }[mode] || null;
  const fx = window.fx = { mode, calls: [], requests: [], saved: null, written: [], recorded, plugged: false, hold: false, profile: null,
    games: { doom: false, chex: hasLoader, freedoom: false, myhouse: false } };  // myhouse: an older backend may still list it; the UI must never offer it
  const usb = { loader: hasLoader, settings: hasLoader };
  // A USB with older settings carried theme 2 (CYBERPUNK): it reads as the standard interface with that classic theme.
  // A fresh USB has no settings file, so it reads the schema defaults: the new interface.
  fx.values = null;
  const settingsValues = () => fx.values || (hasLoader ? { ...defaults, interface: 0, classic_theme: 2 } : { ...defaults });
  // mixed: a FAT32 USB, an exFAT USB (both offered: the file system is never a reason), a read-only USB and a disk of
  // this computer that carries a loader (both left out, in one line). exfat: only the exFAT USB, which opens by
  // itself. locked: only the read-only USB. SANDISK in multi has no filesystem or writable fields, as an older
  // backend lists it, and stays choosable.
  const exfat = { volume: 'G:\\', label: 'BACKUP', removable: true, loader: false, settings: false, rekordbox: true, games: 0, filesystem: 'EXFAT', writable: true };
  const locked = { volume: 'H:\\', label: 'LOCKED', removable: true, loader: false, settings: false, rekordbox: false, games: 0, filesystem: 'FAT32', writable: false };
  const disk = { volume: 'D:\\', label: 'DATA', removable: false, loader: true, settings: false, rekordbox: false, games: 0, filesystem: 'NTFS', writable: true };
  const usbs = () => {
    if ((mode === 'nousb' && !fx.plugged) || fx.unplugged) return [];
    if (mode === 'exfat') return [exfat];
    if (mode === 'locked') return [locked];
    const one = { volume: 'E:\\', label: 'KINGSTON', removable: true, loader: usb.loader, settings: usb.settings, rekordbox: true, games: Object.values(fx.games).filter(Boolean).length, filesystem: 'FAT32', writable: true };
    return mode === 'multi' ? [one, { volume: 'F:\\', label: 'SANDISK', removable: true, loader: false, settings: false, rekordbox: false, games: 0 }] : mode === 'mixed' ? [disk, one, exfat, locked] : [one];
  };
  // A folder on this computer: Write to a folder instead… picks it, and the inspection says to copy it to a USB afterward.
  fx.folder = 'C:\\Users\\dj\\XZ USB';
  const isFolder = volume => volume === fx.folder;
  fx.volumes = () => ['C:\\', ...usbs().map(usb => usb.volume)];
  const gamesOnUsb = () => Object.entries(fx.games).map(([id, installed]) => ({ id, name: { doom: 'Doom shareware', chex: 'Chex Quest', freedoom: 'Freedoom Phase 2', myhouse: 'My House' }[id], installed, folder: 'E:\\VJTOOLS\\Games\\' + id, requires_gzdoom: id === 'myhouse', bytes: 2450688 }));
  const inspection = volume => ({ format: 'xz-mods-usb/1', volume, identity: { serial: 1 }, loader: { exists: usb.loader, bytes: 4100000, sha256: 'abc' },
    recorded_mod_version: usb.loader ? fx.recorded : null, installed_version: null, runtime_sha256: 'newsha',
    settings: { editable: true, values: settingsValues(), revision: { exists: usb.settings, bytes: 400 }, schema, path: volume + 'VJ.Tools\\XZ-Mods.cfg' }, backups: [], games: gamesOnUsb(), direct_usb_root: !isFolder(volume), requires_copy_to_usb_root: isFolder(volume), filesystem: isFolder(volume) ? 'NTFS' : volume === 'G:\\' ? 'EXFAT' : 'FAT32',
    space: { free_bytes: 29100000000, total_bytes: 32000000000 }, stems: { tracks: hasLoader ? 12 : 0, bytes: 734003200 }, rekordbox: true,
    dj_profiles: fx.profile ? { count: 1, active_name: fx.profile.dj_name } : { count: 0, active_name: null } });
  const profiles = () => ({ format: 'xz-dj-profiles/1', public_folder: fx.profile ? 'DJPROFILE' : null, revision: fx.profile ? 'r1' : null, active: fx.profile?.dj_id || null, stick_colour: '#f870f8', old_store: false, problems: [],
    summary: fx.profile ? { count: 1, active_name: fx.profile.dj_name } : { count: 0, active_name: null },
    profiles: fx.profile ? [{ source: '1.0', editable: true, active: true, folder: fx.profile.dj_name, tagline: '', aliases: [], links: [], page_url: null, note: '', logos: [], tile: null, track: null, ...fx.profile }] : [] });
  // mode 'builds': several archived builds, saved choice an older included build.
  const builds = mode === 'builds' ? [
    { id: 'v1', label: 'Included development build newsha12', runtime_sha256: 'newsha12', hardware_qualified: false, source_commit: savedCommit, archived_at: '2026-10-12T09:54:00Z' },
    { id: 'old', label: 'Included development build 98cc63d1', runtime_sha256: '98cc63d1aaaa', hardware_qualified: false, source_commit: OLD, archived_at: '2026-10-08T19:51:00Z' },
    { id: 'rel', label: '0.2.1 released', runtime_sha256: 'e47166e2aaaa', hardware_qualified: false, source_commit: OLD, archived_at: '2026-10-08T17:00:00Z' }]
    : [{ id: 'v1', label: 'Fixture build', runtime_sha256: 'newsha', hardware_qualified: false, source_commit: savedCommit, quad_supported: mode !== 'noquadany' }];
  // mode 'noquad': the saved build is a two-deck one, while the included build has the four-deck engine.
  if (mode === 'noquad') builds.push({ id: 'two', label: 'Two-deck build', runtime_sha256: 'twosha', hardware_qualified: false, source_commit: savedCommit, quad_supported: false });
  fx.selections = [];
  if(mode==='usb-only')for(const item of builds){item.network_updates_supported=false;item.network_update_reason='This build updates through USB setup.';}
  fx.selected = { id: mode === 'noversion' ? 'missing' : mode === 'builds' ? 'old' : mode === 'noquad' ? 'two' : 'v1', pinned: true, revision: 1 };
  const inventory = () => ({ format: 'xz-mod-versions/1', versions: builds, selected: { ...fx.selected }, bundled_id: 'v1' });
  const status = request => {
    switch (request.method) {
      case 'support_list': return {running:false,ok:true,result:{limit:32,reports:fx.savedReportDeleted?[]:[{report_id:'a'.repeat(32),sha256:'b'.repeat(64),stored_revision:'c'.repeat(64),title:'Earlier bridge report',readable:true,schema:'xz-support-report/1'}]}};
      case 'support_sources': return {running:false,ok:true,result:{scan_generation:'f'.repeat(32),sticks:[],other_sticks:[],excess_sticks:0,xz:'not_available'}};
      case 'support_collect': return {running:false,ok:true,result:{scan_generation:'f'.repeat(32),items:[],diary_coverage:{},bytes:0,incident:{before_seconds:600,after_seconds:300,clock_confidence:'known'}}};
      case 'support_resume': return {running:false,ok:true,result:{report_id:'a'.repeat(32),sha256:'b'.repeat(64),bytes:24,pages:2,fields:{title:'Earlier bridge report',description:'Reviewed steps',category:'bridge',include_private:true,contact:''}}};
      case 'support_preview': return {running:false,ok:true,result:{page:request.page,pages:2,sha256:'b'.repeat(64),text:request.page?'LAST REVIEWED PAGE':'FIRST REVIEWED PAGE'}};
      case 'support_remove': fx.savedReportDeleted=true;return {running:false,ok:true,result:{discarded:true}};
      case 'list_mod_versions': return { running: false, ok: true, result: inventory() };
      case 'ota_networks': return { running: false, ok: true, result: { addresses: ['169.254.168.10'], ready: true } };
      case 'discover_stem_usbs': return { running: false, ok: true, result: { drives: [] } };
      case 'select_mod_version': { fx.selected = { id: request.mod_version_id, pinned: request.pinned, revision: fx.selected.revision + 1 }; fx.selections.push(request); return { running: false, ok: true, result: inventory() }; }
      case 'discover_usbs':
        if (fx.holdQuiet) return { running: true };
        if (fx.failDiscover) { fx.failDiscover--; return { running: false, ok: false, error: 'The drive list could not be read.' }; }
        return { running: false, ok: true, result: { format: 'xz-usbs/1', usbs: usbs() } };
      case 'download_game': return { running: false, ok: true, result: { format: 'xz-game-downloaded/1', name: 'Doom shareware' } };
      case 'inspect_usb': if (fx.holdInspect) return { running: true }; return { running: false, ok: true, result: inspection(request.volume) };
      case 'inspect_mod_settings': return { running: false, ok: true, result: { format: 'xz-mod-settings/1', mod_version_id: fx.selected.id, settings_change: null } };
      case 'prepare_usb': case 'update_usb': {
        if (fx.hold) return { running: true, message: 'Writing the loader…' };
        if (fx.failWrite) return { running: false, ok: false, error: 'The USB is write-protected.', message: 'The USB is write-protected.' };
        if (!fx.written.includes(request)) fx.written.push(request);
        usb.loader = true; usb.settings = true; fx.recorded = { id: 'v1', label: 'Fixture build', runtime_sha256: 'newsha', source_commit: savedCommit };
        const games = (request.games || []).map(id => { if (id === fx.failGame) return { game: id, name: id, ok: false, error: 'Download failed' }; fx.games[id] = true; return { game: id, name: id, ok: true }; });
        return { running: false, ok: true, result: { format: request.method === 'update_usb' ? 'xz-mods-loader-updated/1' : 'xz-mods-usb-prepared/1', image: 'E:\\autoexec.bin', backup: 'E:\\VJ.Tools\\backup', game_setup: { format: 'xz-games-usb/1', games, failed_count: 0 }, inspection: inspection(request.volume) } };
      }
      case 'prepare_games_usb': { if (!fx.written.includes(request)) fx.written.push(request); const games = request.games.map(id => { fx.games[id] = true; return { game: id, name: id, ok: true }; }); return { running: false, ok: true, result: { format: 'xz-games-usb/1', games, failed_count: 0 } }; }
      case 'read_usb_settings': return { running: false, ok: true, result: { identity: { serial: 1 }, settings: inspection(request.volume).settings } };
      case 'read_dj_profiles': return { running: false, ok: true, result: profiles() };
      case 'save_dj_profile': {
        if (!fx.written.includes(request)) fx.written.push(request);
        fx.profile = { dj_id: 'djp_' + request.profile.dj_name.toLowerCase().replace(/[^a-z0-9]+/g, '_'), dj_name: request.profile.dj_name, accent: request.profile.accent };
        // This USB still had profiles from before DJ Profile 1.0, so the save moved them (builder/dj_profile.py result.moved).
        return { running: false, ok: true, result: { ...profiles(), saved: fx.profile.dj_id, moved: { profiles: 1, backup: 'C:' + '\\' + 'backup', old_kept: false, failed: [] } } };
      }
      case 'read_logo': return { running: false, ok: true, result: { path: request.path, width: 400, height: 400, bytes: 1000, format: 'png', preview: null } };
      case 'save_usb_settings': fx.saved = request; fx.values = { ...request.values }; if (!fx.written.includes(request)) fx.written.push(request); return { running: false, ok: true, result: { format: 'xz-mods-settings-saved/1', settings: { ...inspection(request.volume).settings, values: request.values } } };
    }
    throw Error('Unexpected method ' + request.method);
  };
  window.__TAURI__ = { core: { invoke: async (command, args) => {
    fx.calls.push(command);
    if (command === 'app_information') return { version: mode.startsWith('beta') ? '0.3.0-preview.12' : '0.3.0', platform: mode === 'mac' ? 'macos' : 'windows', installed: true, update_channel: mode.startsWith('beta') ? 'beta' : 'stable', release_page: mode.startsWith('beta') ? 'https://beta.xzmods.com/' : 'https://github.com/OpticMystic/XZ-Mods/releases/latest', portable_url: mode.startsWith('beta') ? 'https://github.com/OpticMystic/XZ-Mods/releases/download/v0.3.0-preview.12/XZ-Mods-Builder-preview-win64.zip' : 'https://github.com/OpticMystic/XZ-Mods/releases/latest', release_notes: { releases: [] } };
    if (command === 'check_app_update') { if (mode === 'betaoffline' && !fx.online) throw Error('Beta update service is offline'); return mode.startsWith('beta') ? { phase: 'available', channel: 'beta', latest_version: '0.3.0-preview.13', message: 'A newer XZ Mods beta is ready to install.', notes: { features: ['Beta updates'], fixes: [], changes: [] } } : { phase: 'current', message: 'Up to date' }; }
    if (command === 'support_reports') return { reports: [], limit: 200, migrated: 0 };
    if (command === 'support_check_statuses') return { checked: 0, failed: 0, updated: [], unseen: [] };
    if (command === 'open_link') { (fx.links ||= []).push(args.url); return null; }
    if (command === 'live_stream_detect') return ['169.254.168.59'];
    if (command === 'list_volumes') return fx.volumes();
    if (command === 'start_job') { fx.requests.push(args.request); return fx.requests.length; }
    if (command === 'job_status') return status(fx.requests[args.id - 1]);
    if (command === 'cancel_job') return {};
    if (command === 'pick_path') return fx.pickPath === undefined ? 'F:\\' : fx.pickPath;
    if (command === 'eject_volume') {
      (fx.ejects ||= []).push(args.volume);
      if (fx.failEject) throw 'Another program is still using E: (Windows error 5). Close any window showing the USB, then eject again.';
      if (fx.holdEject) await new Promise(done => { fx.releaseEject = done; });
      fx.unplugged = true;
      return null;
    }
    throw Error('Unexpected command ' + command);
  } } };
  fx.csp = [];
  document.addEventListener('securitypolicyviolation', event => fx.csp.push(event.violatedDirective + ' ' + (event.blockedURI || 'inline') + ' ' + (event.sourceFile || '').split('/').pop() + ':' + event.lineNumber + ':' + event.columnNumber));
  window.button = (text, root = document) => [...root.querySelectorAll('button,a.button')].find(item => item.textContent.trim() === text && item.checkVisibility() && !item.closest('nav'));
  window.tile = id => document.querySelector('.wz-tile[data-tile="' + id + '"]');
  window.game = id => document.querySelector('.page:not(.hidden) .games-row[data-id="' + id + '"]');
  window.gameBox = id => game(id)?.querySelector('.tree-check input') || null;
  window.gameSwitch = id => game(id)?.querySelector('.games-switch input') || null;
  window.plan = () => [...document.querySelectorAll('.wz-plan .wz-plan-text')].map(item => item.textContent);
  window.txt = selector => document.querySelector(selector)?.textContent.replace(/\s+/g, ' ').trim() || '';
  window.since = mark => fx.requests.slice(mark).map(request => request.method).join();
}

const schema = JSON.parse(await readFile(resolve(app, process.env.XZ_SETTINGS_SCHEMA || 'source/xdj-xz-toolkit/mods/settings-schema.json'), 'utf8'));
const server = await serveUi(app);
const profile = await mkdtemp(join(tmpdir(), 'xz-wizard-'));
const child = spawn(process.env.XZ_TEST_BROWSER || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  ['--headless=new', '--disable-gpu', '--no-first-run', '--remote-debugging-port=0', '--user-data-dir=' + profile, 'about:blank'], { stdio: 'ignore', windowsHide: true });
let socket, base, passed = 0;
try {
  for (const file of ['ui/index.html', 'ui/app.js', 'ui/setup.js', 'ui/dj-profile.js']) {
    const text = await readFile(join(app, file), 'utf8');
    assert(!/\bPC\b/.test(text.replace(/\/\/[^\n]*/g, '')), file + ' says "PC" where a Mac reads it too; say "this computer"');
    assert(!/File Explorer|\bFinder\b/.test(text), file + ' names a file manager in fixed copy');
  }
  passed++; console.log('ok', 'fixed copy says "this computer" and names no file manager');
  // The four-deck port names come from the firmware's one definition; every About link must be one the host opens.
  const loopbackPort = (await readFile(join(app, 'source/xdj-xz-toolkit/mods/ui/loopback_port.h'), 'utf8')).match(/#define XZ_LOOPBACK_PORT "([^"]+)"/)[1];
  const hostLinks = new Set([...(await readFile(join(app, 'src-tauri/src/main.rs'), 'utf8')).match(/const LINKS:[^;]*;/)[0].matchAll(/"([^"]+)"/g)].map(match => match[1]));
  const aboutLinks = ['https://github.com/OpticMystic/XZ-Mods', 'https://github.com/OpticMystic/XZ-Mods/issues', 'https://github.com/OpticMystic/XZ-Mods/releases', 'https://xzmods.com', 'https://vj.tools', 'https://cash.app/$FireAtWill42'];
  for (const url of aboutLinks) assert(hostLinks.has(url), url + ' is not in LINKS in src-tauri/src/main.rs, so open_link would refuse it');
  passed++; console.log('ok', 'every About link is in the host link allowlist');
  for (let attempt = 0; attempt < 100 && !base; attempt++) {
    try { base = 'http://127.0.0.1:' + (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; }
    catch { await new Promise(done => setTimeout(done, 100)); }
  }
  assert(base, 'Browser did not start');
  const target = (await (await fetch(base + '/json/list', { signal: AbortSignal.timeout(5000) })).json()).find(item => item.type === 'page');
  socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((done, fail) => { socket.onopen = done; socket.onerror = () => fail(Error('CDP connection failed')); });
  let sequence = 0;
  const errors = [];
  const pending = new Map();
  socket.onmessage = event => { const data = JSON.parse(event.data), task = pending.get(data.id); if (!task) return; pending.delete(data.id); data.error ? task.reject(Error(JSON.stringify(data.error))) : task.resolve(data.result); };
  const send = (method, params = {}) => new Promise((resolve, reject) => { const id = ++sequence; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); setTimeout(() => reject(Error('CDP timed out: ' + method)), 10000); });
  const evaluate = async expression => {
    const value = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    if (value.exceptionDetails) throw Error(expression.slice(0, 120) + ': ' + (value.exceptionDetails.exception?.description || value.exceptionDetails.text));
    return value.result.value;
  };
  const context = () => evaluate(`'view: '+setupWizard.state.view+' step: '+setupWizard.state.step+' job: '+activeJob+' requests: '+fx.requests.map(r=>r.method).join(',')+'\\n  wiz: '+txt('#wiz').slice(0,300)`);
  const waitFor = async (expression, label, tries = 200) => {
    for (let attempt = 0; attempt < tries; attempt++) { if (await evaluate(expression)) return; await new Promise(done => setTimeout(done, 25)); }
    throw Error('Timed out: ' + label + '\n  ' + await context() + '\n  page errors: ' + errors.join(' / '));
  };
  const check = async (expression, label) => {
    if (!await evaluate(expression)) throw Error('Failed: ' + label + '\n  ' + await context() + '\n  page errors: ' + errors.join(' / '));
    passed++; console.log('ok', label);
  };
  await send('Runtime.enable');
  socket.addEventListener('message', event => { const data = JSON.parse(event.data); if (data.method === 'Runtime.exceptionThrown') errors.push((data.params.exceptionDetails.exception?.description || data.params.exceptionDetails.text)+' @ '+JSON.stringify(data.params.exceptionDetails.stackTrace?.callFrames?.slice(0,4).map(f=>f.functionName+':'+f.url.split('/').pop()+':'+f.lineNumber))); });
  await send('Page.enable');
  await send('Page.addScriptToEvaluateOnNewDocument', { source: 'window.__SCHEMA__=' + JSON.stringify(schema) + ';(' + installMock + ')();' });
  const size = (width, height) => send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
  await size(1100, 760);
  const cspClean = async () => {
    const violations = await evaluate(`window.fx?.csp||[]`);
    assert.deepEqual(violations.slice(0, 5), [], 'CSP violations (' + violations.length + ')');
  };
  const open = async mode => {
    await cspClean();
    assert.deepEqual(errors, [], 'no page errors: ' + errors.join(' / '));
    await send('Page.navigate', { url: 'about:blank' });
    await new Promise(done => setTimeout(done, 50));
    await send('Page.navigate', { url: server.url + 'index.html#' + mode });
    await waitFor(`document.readyState==='complete'&&window.setupWizard&&modInventory!==null`, 'page loaded (' + mode + ')');
  };
  const click = async (text, root = 'document') => { assert(await evaluate(`Boolean(button(${JSON.stringify(text)},${root}))`), 'button "' + text + '" is visible\n  ' + await context()); await evaluate(`button(${JSON.stringify(text)},${root}).click()`); };
  const idle = () => waitFor(`activeJob===null`, 'no job running');
  const view = async (name, step) => waitFor(`setupWizard.state.view===${JSON.stringify(name)}&&${step ? `setupWizard.state.step===${JSON.stringify(step)}` : 'true'}&&activeJob===null`, 'view ' + name + (step ? ' / ' + step : ''));
  const shots = process.env.XZ_WIZARD_SHOTS; if (shots) await mkdir(shots, { recursive: true });
  // XZ_WIZARD_SIZES=760x520,1280x680 picks the window sizes each shot is taken at (default 1100x760 and 1440x900).
  const shotSizes = (process.env.XZ_WIZARD_SIZES || '1100x760,1440x900').split(',').map(item => item.split('x').map(Number));
  const shot = async name => {
    if (!shots) return;
    for (const [width, height] of shotSizes) {
      await size(width, height);
      await new Promise(done => setTimeout(done, 150));
      const image = await send('Page.captureScreenshot', { format: 'png' });
      await writeFile(join(shots, name + '-' + width + (process.env.XZ_WIZARD_SIZES ? 'x' + height : '') + '.png'), Buffer.from(image.data, 'base64'));
    }
    await size(1100, 760);
  };
  const txtQuad = `txt('.wz-quad-prereq').includes('paired XDJ-XZ 1.26 quad beta resources')&&txt('.wz-quad-status').includes('not checked')`;
  const fits = `document.documentElement.scrollWidth<=innerWidth`;
  // Keys go through the browser's input pipeline, so focus, default actions and the app's handlers all run.
  const KEYS = { Escape: 27, Enter: 13 };
  const press = async key => { for (const type of ['keyDown', 'keyUp']) await send('Input.dispatchKeyEvent', { type, key, code: key, windowsVirtualKeyCode: KEYS[key], nativeVirtualKeyCode: KEYS[key], ...(key === 'Enter' && type === 'keyDown' ? { text: '\r' } : {}) }); };
  // The bar every step ends in: Back first, on the left of everything else; the main action last on the right.
  const bar = (next, primary = true) => `(()=>{const bar=document.querySelector('.wz-flow>.wz-foot'),back=document.getElementById('wz-back'),items=[...(bar?.querySelectorAll('.wz-foot-next button')||[])];
    return Boolean(bar&&back)&&bar.firstElementChild===back&&back.textContent.trim()==='Back'&&items.length>0&&items.every(b=>b.getBoundingClientRect().left>back.getBoundingClientRect().right)
      &&items.at(-1).textContent.trim()===${JSON.stringify(next)}&&items.at(-1).classList.contains(${JSON.stringify(primary ? 'primary' : 'secondary')});})()`;
  // The bar is in view: inside the window even when the step is taller than it.
  const barInView = `(()=>{const r=document.querySelector('.wz-foot')?.getBoundingClientRect();return Boolean(r)&&r.top>=0&&r.bottom<=innerHeight+0.5;})()`;
  // Nothing is cut off at the right or forces a sideways scroll, and the rail's foot is on screen.
  const clipped = `(()=>{if(document.documentElement.scrollWidth>innerWidth)return 'page is '+document.documentElement.scrollWidth+' wide';
    const open=document.querySelector('dialog[open]'),scope=open?[open]:[document.querySelector('main'),document.querySelector('aside')];
    const bad=scope.flatMap(root=>[...root.querySelectorAll('button,a.button,input,select,h1,h2,h3,p')]).filter(e=>e.checkVisibility()&&!e.closest('.table-scroll,.st-table,.visually-hidden')).filter(e=>{const r=e.getBoundingClientRect();return r.width>0&&(r.left<-0.5||r.right>innerWidth+0.5);});
    const rail=document.querySelector('nav button.nav-feedback').getBoundingClientRect();
    if(rail.bottom>innerHeight+0.5&&getComputedStyle(document.querySelector('aside')).position==='sticky')return 'Feedback is below the window';
    return bad.slice(0,3).map(e=>(e.id||e.className||e.tagName)+' "'+e.textContent.trim().slice(0,30)+'" '+Math.round(e.getBoundingClientRect().right)).join(' | ');})()`;
  const capture = async name => {
    if (!shots) return;
    await new Promise(done => setTimeout(done, 120));
    const [width, height] = await evaluate(`[innerWidth,innerHeight]`);
    const image = await send('Page.captureScreenshot', { format: 'png' });
    await writeFile(join(shots, name + '-' + width + 'x' + height + '.png'), Buffer.from(image.data, 'base64'));
  };

  await open('fresh');
  await check(`[...document.querySelectorAll('nav button')].map(b=>b.dataset.page).join()==='setup,stems,dj-profile,updates,tools,manual,licenses,my-reports'&&[...document.querySelectorAll('nav button:not(.nav-feedback)')].map(b=>b.textContent.trim()).join()==='USB,Stems,DJ Profile,Updates,Tools,Manual,About'&&txt('nav .nav-feedback-text>span')==='Feedback'&&document.querySelectorAll('nav button svg.icon use').length===8&&new Set([...document.querySelectorAll('nav button svg.icon use')].map(u=>u.getAttribute('href'))).size===8`, 'nav is USB, Stems, DJ Profile, Updates, Tools, Manual, About, then Feedback at the foot of the rail, each with its own icon');
  await check(`document.querySelector('nav button.selected').dataset.page==='setup'&&txt('#page-title')==='USB'&&!document.querySelector('header .eyebrow')&&getComputedStyle(document.getElementById('page-title')).fontSize==='20px'`, 'USB is the home page under a compact one-line header');
  await view('device');
  await check(`fx.requests.slice(0,3).map(r=>r.method).join()==='list_mod_versions,discover_usbs,inspect_usb'&&fx.calls.includes('list_volumes')&&!document.getElementById('job-panel').checkVisibility()`, 'a single USB opens by itself: no Find button, no job panel flash');
  await check(`txt('.wz-drive h2')==='KINGSTON'&&txt('.wz-drive .wz-mono')==='E:\\\\'&&txt('.wz-drive-meta')==='FAT32 · 29.1 GB free of 32 GB'&&Boolean(button('Change USB'))`, 'device header: label, volume, filesystem and free space, Change USB');
  await check(`document.querySelector('.wz-status').dataset.tone==='add'&&txt('.wz-status h3')==='XZ Mods is not on this USB yet'&&txt('.wz-status p')==='Adds the loader, free games and your DJ profile. Music and playlists stay as they are.'&&document.querySelectorAll('#wiz button.primary').length===1&&Boolean(button('Install XZ Mods'))`, 'fresh USB: one status and one primary button, Install XZ Mods');
  await check(`txt('.wz-tile[data-tile=profile] .wz-tile-value')==='Not set'&&txt('.wz-tile[data-tile=profile] .wz-tile-action')==='Add'&&txt('.wz-tile[data-tile=games] .wz-tile-value')==='None'&&txt('.wz-tile[data-tile=stems] .wz-tile-value')==='None yet'&&txt('.wz-tile[data-tile=stems] .wz-tile-action')==='Prepare'`, 'tiles: DJ profile not set, no games, no stems yet');
  await check(`txt('.wz-build-line')==='Build: Fixture build · Change'&&!document.querySelector('#wiz input[type=checkbox],#wiz input[type=radio],#wiz select')&&!document.querySelector('.wz-files').open`, 'one quiet build line; no tick boxes, radios or build chooser on the main path; files folded away');
  await shot('02-device-fresh');
  await check(`setupWizard.loopbackPort===${JSON.stringify(loopbackPort)}&&setupWizard.musicPort==='the other top port'`, 'the four-deck ports are the firmware\'s: cable in ' + loopbackPort + ', music in the other top port');
  await check(`(()=>{const b=document.getElementById('wz-fourdeck-open');return Boolean(b)&&!b.classList.contains('primary')&&txt('#wz-fourdeck-open')==='Four decksExperimentalPlay decks 3 and 4 through one loopback cable. See how it works.'&&!button('Set up four decks')&&!document.querySelector('.wz-quad-entry')&&Boolean(document.querySelector('.wz-build-line').compareDocumentPosition(b)&Node.DOCUMENT_POSITION_FOLLOWING);})()`, 'dashboard: four decks is one calm entry under the build line, not a setup step or a second primary button');
  await evaluate(`window.__manual=[];window.openManual=(chapter,anchor)=>window.__manual.push([chapter,anchor])`);
  await evaluate(`document.getElementById('wz-fourdeck-open').click()`);
  await check(`JSON.stringify(window.__manual)==='[["four-decks",null]]'&&setupWizard.state.view==='device'`, 'the entry opens the manual at Four decks');
  await evaluate(`window.openManual=undefined;document.getElementById('wz-fourdeck-open').click()`);
  await view('flow', 'quad');
  await check(`txt('.wz-flow-title h2')==='Four decks'&&!document.querySelector('.wz-flow-progress')&&!document.querySelector('.wz-dots')&&`+txtQuad+`&&txt('.wz-quad-art figcaption').startsWith('Rear USB-B → top USB 1 or 2 · Music in the other top port')`, 'without the manual the entry opens the cable guide instead: one page, no step count');
  await check(bar('Done'), 'the cable guide: Back, then Done');
  await click('Done');
  await view('device');
  await check(`document.activeElement?.id==='wz-fourdeck-open'`, 'Done returns to the dashboard with the entry focused');
  await evaluate(`document.querySelector('.wz-files summary').click()`);
  await check(`document.querySelector('.wz-files .tree')&&!document.querySelector('.wz-files .tree input')&&document.querySelector('.tree-row[data-id=loader]').dataset.state==='missing'&&!document.querySelector('.tree-row[data-id=game-myhouse]')&&!txt('#wiz').includes('My House')`, 'Show every file opens the read-only tree, without My House');
  await check(`(()=>{const rows=['drive','loader','settings','game-doom'].map(id=>document.querySelector('.tree-row[data-id="'+id+'"]'));return rows.every(r=>getComputedStyle(r).gridTemplateColumns.split(' ').length===4)&&getComputedStyle(rows[3]).gridTemplateColumns.startsWith('44px');})()`, 'tree rows keep the depth indent under the CSP (CSSOM --depth)');
  await check(fits, 'device view fits without horizontal scroll');
  await evaluate(`document.querySelector('.wz-files summary').click()`);

  await click('Install XZ Mods');
  await view('flow', 'profile');
  await waitFor(`document.querySelector('#wz-djp-slot #djp-name')`, 'DJ profile editor in the flow');
  await check(`txt('.wz-flow-title h2')==='Install XZ Mods'&&txt('.wz-flow-count')==='Step 1 of 6'&&document.querySelectorAll('.wz-dots li').length===6&&document.querySelector('.wz-dots li[data-state=current]')===document.querySelector('.wz-dots li')&&Boolean(button('Cancel'))`, 'install flow: compact header, Step 1 of 6 with six dots (no four-deck step), Cancel');
  await check(`Boolean(button('Skip for now'))&&button('Skip for now').classList.contains('secondary')&&Boolean(button('Back'))&&fx.requests.filter(r=>r.method==='read_dj_profiles').length===1`, 'a USB without a profile starts on the DJ profile step: Skip for now, Back');
  await shot('05-flow-profile');
  await check(bar('Skip for now', false), 'every step ends in one bar: Back on the left, the way forward on the right');
  await evaluate(`document.querySelector('#wz-djp-slot #djp-name').focus()`);
  await press('Escape');
  await new Promise(done => setTimeout(done, 100));
  await check(`setupWizard.state.view==='flow'&&setupWizard.state.step==='profile'`, 'Escape while typing a name does not leave the step');
  await click('Back');
  await view('device');
  await check(`txt('.wz-drive h2')==='KINGSTON'&&djProfilePage.pending()===null`, 'Back on the first step returns to the USB dashboard');
  await click('Install XZ Mods');
  await view('flow', 'profile');
  await waitFor(`Boolean(button('Skip for now'))`, 'profile step ready again');
  await click('Skip for now');
  await view('flow', 'look');
  await check(bar('Next'), 'screen step: Back, then Next');
  await check(`txt('.wz-flow-count')==='Step 2 of 6'&&txt('.wz-flow-body h3')==='Choose the screen the XZ shows'&&document.querySelectorAll('.wz-look-card').length===2&&document.querySelector('.wz-look-card[data-look=new] input').checked&&!document.querySelector('.wz-look-card[data-look=standard] input').checked&&document.querySelector('.wz-look-step').dataset.choice==='new'&&Boolean(button('Cancel'))`, 'look step after the profile: the new interface is selected by default on a fresh USB');
  await check(`txt('.wz-look-card[data-look=new] .wz-look-title')==='New interfaceBeta'&&txt('.wz-look-card[data-look=standard] .wz-look-title')==='Standard XDJ-XZ interface'`, 'cards: New interface (Beta) and Standard XDJ-XZ interface');
  await check(`(()=>{const v=document.querySelector('.wz-look-card[data-look=new] video');return Boolean(v)&&v.muted&&v.loop&&v.autoplay&&v.playsInline&&v.getAttribute('src')==='assets/wizard/new-interface.mp4'&&v.getAttribute('aria-label').length>40;})()&&document.querySelector('.wz-look-card[data-look=standard] img').alt.length>20`, 'the chosen card plays the new interface muted and looping in place; the other shows a still with alt text');
  await waitFor(`(()=>{const v=document.querySelector('.wz-look-card[data-look=new] video');return Boolean(v)&&v.readyState>=2&&v.videoWidth===800&&v.videoHeight===480;})()`, 'the new-interface video decodes under the app CSP');
  await check(`(()=>{const c=[...document.querySelectorAll('.wz-look-card')].map(e=>e.getBoundingClientRect().width);return c[0]>c[1]*1.4;})()`, 'the selected card is the large one');
  await check(`txt('.wz-later').includes('Press the button directly above LINK to switch between MODERN and CLASSIC, any time.')&&txt('.wz-later').includes("default job. MODS > PLAY can give it another.")&&txt('.wz-later').includes('Or use the menu: press MENU, tap SCREEN, then tap CLASSIC under INTERFACE.')&&(()=>{const v=[...document.querySelectorAll('.wz-later video')];return v.length===2&&v[0].getAttribute('src')==='assets/manual/nav/physical-rekordbox.mp4'&&v[1].getAttribute('src')==='assets/manual/nav/interface-choice.mp4'&&v.every(x=>x.muted&&x.loop&&x.getAttribute('aria-label').length>40);})()`, 'switching later leads with the button above LINK and its navigation video, then the menu route with its own');
  await waitFor(`[...document.querySelectorAll('.wz-later video')].every(v=>v.readyState>=2&&v.videoWidth===1280&&v.videoHeight===480)`, 'both navigation videos decode under the app CSP')
  await waitFor(`[...document.querySelectorAll('.wz-look-step img')].every(i=>i.complete&&i.naturalWidth>0&&i.alt)`, 'stills and switch-later graphics load, each with alt text');
  await check(`!/\\b(CDJ|Pioneer|rekordbox|AlphaTheta)\\b/i.test(txt('.wz-look-step')+[...document.querySelectorAll('.wz-look-step [alt],.wz-look-step [aria-label]')].map(e=>e.getAttribute('alt')||e.getAttribute('aria-label')).join(' '))`, 'the step names no other brand');
  await check(fits, 'look step fits without horizontal scroll');
  await shot('05b-flow-look-new');
  await evaluate(`document.querySelector('.wz-look-card[data-look=new] input').focus()`);
  for (const type of ['keyDown', 'keyUp']) await send('Input.dispatchKeyEvent', { type, key: 'ArrowRight', code: 'ArrowRight', windowsVirtualKeyCode: 39, nativeVirtualKeyCode: 39 });
  await waitFor(`document.querySelector('.wz-look-step').dataset.choice==='standard'`, 'the arrow key picks the standard interface');
  await check(`setupWizard.state.settings.interface===0&&document.activeElement===document.querySelector('.wz-look-card[data-look=standard] input')&&getComputedStyle(document.querySelector('.wz-look-card[data-look=standard]')).outlineStyle==='solid'&&txt('.wz-later').includes('then tap MODERN under INTERFACE.')`, 'keyboard: arrows move the choice, the focused card shows a ring, the switch-later graphic follows the choice');
  await new Promise(done => setTimeout(done, 700));
  await check(`(()=>{const c=[...document.querySelectorAll('.wz-look-card')].map(e=>e.getBoundingClientRect().width);return c[1]>c[0]*1.4;})()&&!document.querySelector('.wz-look-card[data-look=new] video')&&document.querySelector('.wz-look-card[data-look=new] img').getAttribute('src')==='assets/wizard/new-interface-poster.webp'`, 'the large frame moves to the standard card; the new interface stops and shows its poster');
  await shot('05c-flow-look-standard');
  await press('Escape');
  await view('flow', 'profile');
  passed++; console.log('ok', 'Escape is Back: from the screen step it goes back to the profile step');
  await waitFor(`Boolean(button('Skip for now'))`, 'profile step ready');
  await click('Skip for now');
  await view('flow', 'look');
  await check(`document.querySelector('.wz-look-card[data-look=standard] input').checked`, 'Back and forward keep the choice');
  await evaluate(`document.querySelector('.wz-look-card[data-look=new] input').click()`);
  await check(`setupWizard.state.settings.interface===1&&document.querySelector('.wz-look-step').dataset.choice==='new'`, 'a click picks the new interface again');
  await click('Next');
  // Four decks is no longer a step of the guide: the screen step leads straight to the checklist.
  await view('flow', 'review');
  await check(`plan().join('|')==='Add the XZ Mods loader (Fixture build)|Add 3 free games: Doom shareware, Chex Quest and Freedoom Phase 2|Use the new interface on the XZ|Create the settings file'&&txt('.wz-safe')==='Music, playlists and prepared stems are never touched.'&&txt('.wz-flow-count')==='Step 3 of 6'`, 'review: a plain checklist of what will happen, then the reassurance line');
  await check(bar('Write to USB'), 'review: Back, then Write to USB last in the bar');
  await check(`!document.querySelector('.wz-games-change .games')&&Boolean(button('Change games'))&&!button('Write to USB').disabled`, 'games stay folded behind Change games');
  await shot('06-flow-review');
  await click('Change games');
  await check(`(()=>{const rows=[...document.querySelectorAll('.wz-games-change .games-group[data-kind=download] .games-row')];return rows.map(r=>r.dataset.id).join()==='doom,chex,freedoom'&&rows.every(r=>r.querySelector('.tree-check input').checked);})()&&txt('.wz-games-change .games-builtin-line').startsWith('Beat Arcade, Wave Rider')`, 'Change games opens the chooser in place: three downloads ticked, built-ins in one line');
  await evaluate(`gameBox('chex').click()`);
  await check(`setupWizard.newGames().join()==='doom,freedoom'&&plan()[1]==='Add 2 free games: Doom shareware and Freedoom Phase 2'&&Boolean(document.querySelector('.wz-games-change .games'))`, 'unticking Chex Quest updates the checklist and keeps the chooser open');
  await check(fits, 'review with the games chooser fits without horizontal scroll');
  await shot('06b-flow-review-games');
  await evaluate(`appUpdating=true`);
  await click('Write to USB');
  await check(`setupWizard.state.step==='review'&&txt('#job-message').includes('updating itself')&&document.querySelector('.wz-flow').contains(document.getElementById('job-panel'))&&!fx.requests.some(r=>r.method==='prepare_usb')`, 'Write during an app update says why and stays on the review instead of hanging');
  await evaluate(`appUpdating=false;setupWizard.chooseUsb('E:\\\\')`);
  await idle();
  await check(`setupWizard.state.view==='flow'&&setupWizard.state.step==='review'&&fx.requests.at(-1).method==='read_dj_profiles'`, 'a queued USB choice never pulls the DJ out of the flow');
  await evaluate(`fx.failWrite=true`);
  await click('Write to USB');
  await waitFor(`setupWizard.state.step==='review'&&activeJob===null&&txt('#job-message').includes('write-protected')`, 'failed write back on the review');
  await check(`document.querySelector('.wz-flow').contains(document.getElementById('job-panel'))&&document.getElementById('job-panel').classList.contains('is-error')&&!button('Write to USB').disabled&&fx.written.length===0`, 'a failed write returns to the review with the error beside it, ready to try again');
  await evaluate(`fx.failWrite=false;fx.hold=true;window.__mark=fx.requests.length`);
  await click('Write to USB');
  await waitFor(`setupWizard.state.step==='writing'&&fx.requests.at(-1)?.method==='prepare_usb'&&document.getElementById('job-panel').checkVisibility()`, 'writing step with the job panel');
  await check(`document.querySelector('.wz-flow').contains(document.getElementById('job-panel'))&&txt('#job-title')==='Adding the XZ Mods loader'&&!button('Write to USB')&&!document.querySelector('.wz-cancel')&&!document.querySelector('.wz-foot')&&txt('.wz-flow-count')==='Step 4 of 6'`, 'writing: the job panel shows inside the flow; no Write, Cancel or Back to press');
  await shot('07-flow-writing');
  await check(`(()=>{const s=document.querySelector('.wz-flow[data-step=writing] .wz-fourdeck');return Boolean(s)&&s.dataset.where==='writing'&&txt('.wz-fourdeck .wz-eyebrow')==='While you wait'&&[...s.querySelectorAll('.wz-fourdeck-steps strong')].map(e=>e.textContent).join('|')==='Cable from the rear USB-B socket to top USB 1 or 2|Music in the other top port|CH3 and CH4 set to USB'&&!s.querySelector('button.primary')&&!document.getElementById('wz-fourdeck-how')&&s.querySelector('video')?.getAttribute('src')==='assets/wizard/loopback-cable.mp4'&&Boolean(document.getElementById('job-panel').compareDocumentPosition(s)&Node.DOCUMENT_POSITION_FOLLOWING);})()`, 'writing: four decks is explained under the progress, as something to read; without the manual there is no button that would leave the write');
  await evaluate(`window.openManual=()=>{};setupWizard.render()`);
  await check(`Boolean(document.getElementById('wz-fourdeck-how'))&&setupWizard.state.step==='writing'`, 'writing: with the manual in this host, How four decks work is offered');
  await evaluate(`document.querySelector('.wz-fourdeck').scrollIntoView({block:'start'})`);
  await shot('07b-flow-writing-fourdecks');
  await evaluate(`window.openManual=undefined;setupWizard.render();scrollTo(0,0)`);
  await evaluate(`fx.hold=false`);
  await view('flow', 'done');
  await check(`since(window.__mark)==='inspect_mod_settings,prepare_usb,read_usb_settings,save_usb_settings,inspect_usb'&&fx.written.length===2&&fx.written[0].method==='prepare_usb'&&fx.written[0].volume==='E:\\\\'&&fx.written[0].experimental===true&&fx.written[0].mod_version_id==='v1'&&fx.written[0].pin_mod_version===true&&JSON.stringify(fx.written[0].games)==='["doom","freedoom"]'`, 'Write: settings check, prepare_usb with the chosen games, the settings file, then a fresh inspection');
  await check(`fx.written[1].method==='save_usb_settings'&&fx.saved.values.interface===1&&fx.saved.values.classic_theme===0&&!('theme' in fx.saved.values)`, 'the new interface is written as interface=1, the one setting MODS > APPEARANCE > INTERFACE reads');
  await check(`txt('.wz-done-hero h3')==='Your USB is ready'&&txt('.wz-done').includes('Loader written')&&txt('.wz-done').includes('Games on USB: doom, freedoom.')&&txt('.wz-next').includes('press MENU')&&Boolean(button('Done'))&&!document.getElementById('job-panel').checkVisibility()`, 'done: success mark, result lines, the On the XZ steps, Done');
  await check(`!document.getElementById('wz-back')&&Boolean(document.querySelector('.wz-flow>.wz-foot #wz-done'))`, 'done: no Back, a write cannot be undone; Done is in the bar');
  await check(`(()=>{const s=document.querySelector('.wz-flow[data-step=done] .wz-fourdeck'),next=document.querySelector('.wz-next');return Boolean(s&&next)&&Boolean(next.compareDocumentPosition(s)&Node.DOCUMENT_POSITION_FOLLOWING)&&txt('.wz-fourdeck h4')==='Four decksExperimental'&&s.querySelectorAll('.wz-fourdeck-steps li').length===3&&s.querySelectorAll('button.primary').length===1&&button('How four decks work').classList.contains('primary')&&!s.querySelector('.wz-fourdeck-setup')&&!document.querySelector('.wz-quad-entry')&&!txt('#wiz').includes('Quad standalone mixer');})()`, 'done: after On the XZ, a Four decks section with the cable picture, three steps and one primary button, How four decks work');
  await check(`txt('.wz-next').includes('Press the button above LINK to switch between the CLASSIC and MODERN screens. MODS > PLAY can give it another job.')&&!/rekordbox/i.test(txt('.wz-next'))&&document.querySelector('.wz-next .wz-done-nav video')?.getAttribute('src')==='assets/manual/nav/physical-rekordbox.mp4'&&!txt('.wz-next').includes('next to LINK')`, 'done: On the XZ names the button above LINK, with its navigation video and no maker name');
  await check(`!(fx.links||[]).length&&Boolean(button('Done'))&&Boolean(button('Support XZ Mods (optional)'))`, 'support is optional and opens no links on completion');
  await evaluate('window.supportWriteCount=fx.written.length');
  await click('Support XZ Mods (optional)'); await view('flow','support');
  await check(`txt('.wz-support').includes('Every feature stays available')&&document.activeElement.closest('.wz-flow-body')!==null&&!document.querySelector('a[href*="sponsors/"]')&&!(fx.links||[]).length`, 'Support has focus, no paywall and no unavailable Sponsors link');
  await shot('support-final');
  await evaluate(`button('Support on Cash App').focus()`);
  for (const type of ['keyDown','keyUp']) await send('Input.dispatchKeyEvent',{type,key:'Enter',code:'Enter',windowsVirtualKeyCode:13,nativeVirtualKeyCode:13});
  await waitFor(`fx.links?.length===1`, 'explicit support link');
  await check(`fx.links[0]==='https://cash.app/$FireAtWill42'&&fx.written.length===supportWriteCount`, 'keyboard opens only the exact Cash App URL without another USB write');
  await check(bar('Finish'), 'support: Back, then Finish');
  await click('Back'); await view('flow','done');

  await shot('08-flow-done');
  await evaluate(`document.querySelector('.wz-fourdeck').scrollIntoView({block:'start'})`);
  await shot('08b-done-fourdecks');
  await evaluate(`scrollTo(0,0);window.__manual=[];window.openManual=(chapter,anchor)=>window.__manual.push([chapter,anchor])`);
  await click('How four decks work');
  await check(`JSON.stringify(window.__manual)==='[["four-decks",null]]'&&setupWizard.state.step==='done'`, 'How four decks work opens the manual at Four decks and leaves the done screen as it is');
  await evaluate(`window.openManual=undefined`);
  await click('How four decks work');
  await view('flow', 'quad');
  await check(txtQuad, 'without the manual it opens the cable guide, which keeps the paired-loader prerequisite');
  await check(bar('Done')+`&&document.documentElement.scrollHeight>innerHeight+200&&`+barInView, 'the cable guide is taller than the window and its bar stays in view at the bottom');
  // The cable guide's picture is the deck's own explainer, a looping video under the app's CSP.
  const quadVideo = `document.querySelector('.wz-quad-art video')`;
  await check(`(()=>{const v=${quadVideo};return Boolean(v)&&v.muted&&v.loop&&v.autoplay&&v.playsInline&&!v.controls&&v.getAttribute('src')==='assets/wizard/loopback-cable.mp4'&&v.getAttribute('poster')==='assets/wizard/loopback-cable-poster.webp'&&v.getAttribute('aria-label')==='One USB-B to USB-A cable loops back into the same XDJ-XZ'&&txt('#wz-quad-desc').includes('square USB-B plug with beveled top corners')&&!document.querySelector('.wz-quad-art svg');})()`, 'quad guide: the cable explainer plays muted, looping, without controls, with a poster and a text description');
  await waitFor(`(()=>{const v=${quadVideo};return Boolean(v)&&v.readyState>=2&&v.videoWidth===1280&&v.videoHeight===400&&Math.abs(v.duration-13)<0.05;})()`, 'the cable video decodes under the app CSP (13 s, 1280 x 400)');
  await evaluate(`(()=>{const v=${quadVideo};v.currentTime=v.duration-0.3;v.play();})()`);
  await waitFor(`(()=>{const v=${quadVideo};return !v.paused&&v.currentTime>0&&v.currentTime<3;})()`, 'the cable video loops: past its end it plays from the start again');
  await shot('05d-flow-quad');
  await click('Pause cable animation');
  await check(`${quadVideo}.paused&&document.querySelector('.wz-quad-art').dataset.paused==='1'&&button('Play cable animation').getAttribute('aria-pressed')==='true'`, 'Pause stops the cable picture where it is');
  await click('Play cable animation');
  await waitFor(`!${quadVideo}.paused&&!document.querySelector('.wz-quad-art').dataset.paused`, 'Play starts it again');
  await send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'reduce' }] });
  await evaluate(`setupWizard.render()`);
  await check(`!document.querySelector('.wz-quad-art video')&&document.querySelector('.wz-quad-art img').getAttribute('src')==='assets/wizard/loopback-cable-poster.webp'&&document.querySelector('.wz-quad-art img').alt.includes('rear USB-B socket')&&!button('Pause cable animation')`, 'reduced motion: the guide shows the still with its description and no motion control');
  await shot('05e-flow-quad-reduced-motion');
  await send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'no-preference' }] });
  await click('Done');
  await view('flow', 'done');
  await check(`document.activeElement?.id==='wz-fourdeck-how'`, 'Done in the cable guide returns to the done screen with How four decks work focused');
  await click('Done');
  await view('device');
  await check(`document.querySelector('.wz-status').dataset.tone==='keep'&&txt('.wz-status h3')==='Up to date'&&txt('.wz-status p')==='Fixture build is on this USB.'&&!document.querySelector('#wiz button.primary')&&txt('.wz-tile[data-tile=games] .wz-tile-value')==='2 games on USB'`, 'Done returns to the re-inspected USB: up to date, no write button, two games');

  await click('Change USB');
  await view('find');
  await new Promise(done => setTimeout(done, 2600));
  await check(`setupWizard.state.view==='find'&&document.querySelectorAll('#wiz .wz-usb[data-volume]').length===1&&txt('.wz-find h2')==='Your XZ USB'&&txt('#wiz .wz-usb[data-volume]').includes('KINGSTON')&&txt('#wiz .wz-usb[data-volume]').includes('XZ Mods installed')`, 'Change USB goes back to the list and does not reopen the same USB by itself');
  await evaluate(`fx.unplugged=true`);
  await waitFor(`setupWizard.state.usbs?.length===0&&activeJob===null`, 'unplugged', 400);
  await evaluate(`fx.unplugged=false`);
  await view('device');
  await check(`txt('.wz-drive h2')==='KINGSTON'`, 'unplugging and plugging the USB back in opens it by itself again');
  await check(`button('Change USB').classList.contains('secondary')&&document.getElementById('wz-folder')===null`, 'Change USB is a button, not a small link');
  await evaluate(`document.activeElement?.blur()`);
  await press('Escape');
  await view('find');
  await new Promise(done => setTimeout(done, 2600));
  await check(`setupWizard.state.view==='find'&&document.activeElement?.dataset.volume==='E:\\\\'&&document.querySelector('#wiz .wz-usb-list>button:last-child').id==='wz-folder'&&txt('#wz-folder')==='Write to a folder instead…Copy it to the top of a USB stick afterward.'`, 'Escape on the dashboard goes back to the USB list; going back holds, the USB keeps focus, and the last choice is Write to a folder instead…');
  await shot('01d-find-after-back');
  await press('Enter');
  await view('device');
  await check(`txt('.wz-drive h2')==='KINGSTON'`, 'Enter on the focused card opens the USB again');

  await open('nousb');
  await waitFor(`setupWizard.state.usbs!==null&&activeJob===null&&!setupWizard.state.finding`, 'first look finished');
  await check(`setupWizard.state.view==='find'&&document.querySelector('.wz-empty .wz-art svg')&&txt('.wz-find h2')==='Plug in your XZ USB'&&txt('.wz-find .wz-lead')==='Any USB stick works. XZ Mods finds it as soon as it is connected. It never formats a drive or touches your music.'`, 'no USB: illustration, Plug in your XZ USB, one plain line');
  await check(`!document.querySelector('#wiz button.primary')&&!button('Find my USB')&&Boolean(button('Look again'))&&Boolean(button('Write to a folder instead…'))&&button('Look again').classList.contains('text-button')`, 'no USB: no primary Find button, only quiet Look again and Choose a folder instead');
  await shot('00-find-empty');
  await evaluate(`window.__mark=fx.requests.length;fx.failDiscover=1;fx.plugged=true`);
  await waitFor(`setupWizard.state.view==='device'&&activeJob===null`, 'plugged-in USB opened', 400);
  await check(`since(window.__mark)==='discover_usbs,discover_usbs,inspect_usb'&&txt('.wz-drive h2')==='KINGSTON'`, 'plugging in the USB opens it by itself; a discover that failed is retried on the next poll');

  await open('multi');
  await waitFor(`document.querySelectorAll('#wiz .wz-usb[data-volume]').length===2&&activeJob===null`, 'two USB cards');
  await check(`setupWizard.state.view==='find'&&txt('.wz-find h2')==='Choose your XZ USB'&&txt('#wiz .wz-usb:nth-child(2)').includes('No XZ Mods yet')&&!txt('#wiz .wz-usb[data-volume]').includes('game')`, 'several USBs: cards to pick from, with simple chips');
  await shot('01-find-several');
  await evaluate(`fx.holdQuiet=true;window.__mark=fx.requests.length;document.getElementById('wz-look').click()`);
  await waitFor(`activeJob!==null&&setupWizard.state.finding`, 'a slow discover holds the job slot');
  await evaluate(`document.querySelectorAll('#wiz .wz-usb[data-volume]')[1].click();showPage('games');document.getElementById('download-doom').click();showPage('setup')`);
  await new Promise(done => setTimeout(done, 400));
  await evaluate(`fx.holdQuiet=false`);
  await view('device');
  await waitFor(`fx.requests.some(r=>r.method==='download_game')&&activeJob===null`, 'download ran after the discover');
  await check(`since(window.__mark).split(',').sort().join()==='discover_usbs,download_game,inspect_usb'&&txt('.wz-drive h2')==='SANDISK'&&fx.requests.find(r=>r.method==='inspect_usb'&&fx.requests.indexOf(r)>=window.__mark).volume==='F:\\\\'`, 'clicks made while a quiet discover runs wait for it and then all run: the card opens that USB, the download starts');

  await open('multi');
  await waitFor(`document.querySelectorAll('#wiz .wz-usb[data-volume]').length===2&&activeJob===null`, 'two USB cards again');
  await evaluate(`fx.holdQuiet=true;document.getElementById('wz-look').click()`);
  await waitFor(`activeJob!==null&&setupWizard.state.finding`, 'a slow discover holds the job slot again');
  await evaluate(`window.__mark=fx.requests.length;document.querySelectorAll('#wiz .wz-usb[data-volume]')[0].click()`);
  await check(`(()=>{const cards=document.querySelectorAll('#wiz .wz-usb[data-volume]');return cards[0].dataset.state==='opening'&&cards[0].getAttribute('aria-busy')==='true'&&txt('#wiz .wz-opening')==='Reading the USB…'&&Boolean(cards[0].querySelector('.wz-spinner'))&&cards[1].disabled&&cards[1].dataset.state==='waiting';})()`, 'a click during a background scan shows Reading the USB with a spinner on that card at once; the other card waits');
  await evaluate(`fx.holdInspect=true;fx.holdQuiet=false`);
  await waitFor(`fx.requests.slice(window.__mark).some(r=>r.method==='inspect_usb')`, 'the read starts after the scan');
  await check(`setupWizard.state.view==='find'&&document.querySelectorAll('#wiz .wz-usb[data-volume]')[0].dataset.state==='opening'`, 'the card keeps showing Reading the USB through a slow read');
  await shot('01b-find-opening');
  await evaluate(`document.querySelectorAll('#wiz .wz-usb[data-volume]')[0].click();document.querySelectorAll('#wiz .wz-usb[data-volume]')[1].click();fx.holdInspect=false`);
  await view('device');
  await check(`txt('.wz-drive h2')==='KINGSTON'&&fx.requests.slice(window.__mark).filter(r=>r.method==='inspect_usb').length===1&&setupWizard.state.opening===null`, 'repeat clicks while a USB opens read it once, and the clicked USB is the one that opens');

  // The USB list offers every USB it can write to, whatever the file system, and the folder choice last.
  await open('mixed');
  await waitFor(`document.querySelectorAll('#wiz .wz-usb[data-volume]').length===2&&activeJob===null&&!setupWizard.state.finding`, 'two usable USBs listed');
  await check(`[...document.querySelectorAll('#wiz .wz-usb[data-volume]')].map(b=>b.querySelector('strong').textContent+' '+b.querySelector('small').textContent).join('|')==='KINGSTON E:\\\\ · FAT32|BACKUP G:\\\\ · exFAT'&&!fx.requests.some(r=>r.method==='inspect_usb')`, 'FAT32 and exFAT USBs are both offered; the file system is plain information');
  await check(`txt('#wz-skipped')==='2 drives not shown: DATA (D:\\\\) is a drive of this computer and LOCKED (H:\\\\) is read-only. XZ Mods needs a USB drive it can write to.'`, 'a read-only USB and a disk of this computer are left out, named in one line with the reason');
  await check(`document.querySelector('#wiz .wz-usb-list>button:last-child').id==='wz-folder'&&txt('#wz-folder')==='Write to a folder instead…Copy it to the top of a USB stick afterward.'`, 'the last choice in the list is Write to a folder instead…');
  await shot('01c-find-mixed');
  await evaluate(`document.querySelectorAll('#wiz .wz-usb[data-volume]')[1].click()`);
  await view('device');
  await check(`txt('.wz-drive h2')==='BACKUP'&&txt('.wz-drive-meta').startsWith('exFAT · ')&&Boolean(button('Install XZ Mods'))`, 'an exFAT USB opens like any other and offers Install XZ Mods');
  await open('exfat');
  await view('device');
  await check(`txt('.wz-drive h2')==='BACKUP'`, 'a single exFAT USB opens by itself');
  await open('locked');
  await waitFor(`setupWizard.state.usbs!==null&&activeJob===null&&!setupWizard.state.finding`, 'first look finished (read-only USB)');
  await check(`setupWizard.state.view==='find'&&txt('.wz-find h2')==='Plug in your XZ USB'&&txt('#wz-skipped')==='1 drive not shown: LOCKED (H:\\\\) is read-only. XZ Mods needs a USB drive it can write to.'&&Boolean(button('Write to a folder instead…'))&&!fx.requests.some(r=>r.method==='inspect_usb')`, 'only a read-only USB: it never opens, one line says why, and a folder is still offered');
  await shot('00b-find-locked');

  // A folder, then back to the list: the one USB does not open by itself, and one click switches to it.
  await open('fresh');
  await view('device');
  await click('Change USB');
  await view('find');
  await evaluate(`fx.pickPath=fx.folder;document.getElementById('wz-folder').click()`);
  await view('device');
  await check(`setupWizard.state.usb===fx.folder&&txt('.wz-drive h2')===fx.folder&&document.querySelector('.wz-drive-icon use').getAttribute('href')==='#i-folder'&&txt('#wiz .wz-device>.notice').startsWith('This is a folder, not a USB.')`, 'Write to a folder instead… opens the folder, with a folder icon and the line to copy it to a USB');
  await click('Install XZ Mods');
  await view('flow', 'profile');
  await waitFor(`Boolean(button('Skip for now'))`, 'profile step ready (folder)');
  await check(bar('Skip for now', false), 'a folder guide has the same bar');
  await click('Cancel');
  await view('find');
  await new Promise(done => setTimeout(done, 2600));
  await check(`setupWizard.state.view==='find'&&!setupWizard.state.usb&&document.activeElement===document.getElementById('wz-folder')&&document.querySelectorAll('#wiz .wz-usb[data-volume]').length===1`, 'Cancel from a folder lands on the USB list; the one USB does not open by itself and the folder choice keeps focus');
  await evaluate(`document.querySelector('#wiz .wz-usb[data-volume]').click()`);
  await view('device');
  await check(`txt('.wz-drive h2')==='KINGSTON'`, 'one click on the USB card switches from the folder to the USB');
  await evaluate(`fx.pickPath=undefined`);

  await open('fresh');
  await view('device');
  await click('Install XZ Mods');
  await view('flow', 'profile');
  await waitFor(`Boolean(button('Skip for now'))`, 'profile step ready');
  await click('Skip for now');
  await view('flow', 'look');
  await click('Next');
  await view('flow', 'review');
  await check(`(()=>{const box=document.getElementById('wz-eject-auto');return box&&!box.checked&&txt('.wz-eject-auto').startsWith('Eject the USB when writing finishes')&&(document.querySelector('.wz-eject-auto').compareDocumentPosition(document.getElementById('wz-write'))&Node.DOCUMENT_POSITION_FOLLOWING);})()`, 'the review offers Eject the USB when writing finishes, unticked by default, just above Write to USB');
  await click('Write to USB');
  await view('flow', 'done');
  await check(`button('Eject USB')?.classList.contains('primary')&&button('Done').classList.contains('secondary')&&!(fx.ejects||[]).length&&!document.getElementById('job-panel').checkVisibility()`, 'done offers Eject USB as the main action; with the box unticked nothing ejects by itself');
  await shot('09-done-eject-offer');
  await evaluate(`fx.failEject=true`);
  await click('Eject USB');
  await waitFor(`setupWizard.state.eject?.state==='error'`, 'refused eject');
  await check(`txt('.wz-eject-error').startsWith('The USB was not ejected. Another program is still using E:')&&Boolean(button('Try ejecting again'))&&setupWizard.state.view==='flow'`, 'a refused eject says why in plain words and offers Try ejecting again');
  await evaluate(`fx.failEject=false;fx.holdEject=true`);
  await click('Try ejecting again');
  await waitFor(`setupWizard.state.eject?.state==='ejecting'`, 'ejecting');
  await check(`button('Ejecting…')?.disabled&&Boolean(button('Ejecting…').querySelector('.wz-spinner'))&&button('Done').disabled`, 'while ejecting the button reads Ejecting with a spinner, and Done waits');
  await evaluate(`fx.releaseEject()`);
  await waitFor(`setupWizard.state.eject?.state==='done'`, 'ejected');
  await check(`fx.ejects.join('|')==='E:\\\\|E:\\\\'&&txt('.wz-ejected')==='USB ejected. You can unplug it now.'&&!button('Eject USB')&&button('Done').classList.contains('primary')&&txt('.wz-next li').startsWith('Unplug the USB')&&!document.getElementById('wz-eject-auto')`, 'after the eject: USB ejected line, the XZ steps say unplug, Done is the main action');
  await check(fits, 'done with the eject outcome fits without horizontal scroll');
  await shot('10-done-ejected');
  await click('Done');
  await waitFor(`setupWizard.state.view==='find'&&activeJob===null&&setupWizard.state.usbs!==null&&!setupWizard.state.finding`, 'back to finding a USB');
  await check(`txt('.wz-find h2')==='Plug in your XZ USB'&&!setupWizard.state.usb`, 'Done after an eject goes back to finding a USB, because that one is gone');

  await open('fresh');
  await view('device');
  await click('Install XZ Mods');
  await view('flow', 'profile');
  await waitFor(`Boolean(button('Skip for now'))`, 'profile step ready');
  await click('Skip for now');
  await view('flow', 'look');
  await click('Next');
  await view('flow', 'review');
  await evaluate(`document.getElementById('wz-eject-auto').click()`);
  await check(`localStorage.getItem('xz-eject-after-write')==='1'&&document.getElementById('wz-eject-auto').checked`, 'ticking Eject the USB when writing finishes is remembered');
  await click('Write to USB');
  await waitFor(`setupWizard.state.step==='done'&&setupWizard.state.eject?.state==='done'&&activeJob===null`, 'automatic eject');
  await check(`fx.ejects?.length===1&&fx.ejects[0]==='E:\\\\'&&txt('.wz-ejected')==='USB ejected. You can unplug it now.'&&!button('Eject USB')`, 'with the box ticked the USB ejects by itself once writing finishes');

  await open('fresh');
  await view('device');
  await click('Install XZ Mods');
  await view('flow', 'profile');
  await waitFor(`Boolean(button('Skip for now'))`, 'profile step ready');
  await click('Skip for now');
  await view('flow', 'look');
  await click('Next');
  await view('flow', 'review');
  await check(`document.getElementById('wz-eject-auto').checked`, 'the remembered choice is ticked on the next write');
  await evaluate(`fx.failGame='doom'`);
  await click('Write to USB');
  await view('flow', 'done');
  await check(`!(fx.ejects||[]).length&&txt('.wz-done').includes('The USB was not ejected, so you can retry the failed games first.')&&Boolean(button('Retry the failed games'))&&!button('Eject USB')`, 'a failed game download keeps the USB plugged in for the retry and says so');
  await evaluate(`localStorage.removeItem('xz-eject-after-write')`);

  await open('loader');
  await view('device');
  await check(`document.querySelector('.wz-status').dataset.tone==='update'&&txt('.wz-status h3')==='Update available'&&txt('.wz-status p').startsWith('This USB has Old build.')&&Boolean(button('Update USB'))&&!document.querySelector('.wz-changes').open&&txt('.wz-changes-count')==='8 changes'&&document.querySelector('.wz-changes-see').checkVisibility()&&!document.querySelector('.wz-changes-hide').checkVisibility()`, 'older loader: Update available with the change list folded to one summary line');
  await check(`(document.getElementById('wz-primary').compareDocumentPosition(document.querySelector('.wz-changes'))&Node.DOCUMENT_POSITION_FOLLOWING)&&document.getElementById('wz-primary').getBoundingClientRect().bottom<=innerHeight`, 'Update USB sits under the status line, above the change list, inside the first screen at 1100x760');
  await shot('03-device-update');
  await check(`txt('.wz-tile[data-tile=games] .wz-tile-value')==='1 game on USB'&&txt('.wz-tile[data-tile=stems] .wz-tile-value')==='12 tracks prepared'`, 'tiles count the game and the prepared tracks');
  await evaluate(`document.querySelector('.wz-changes summary').click()`);
  await check(`[...document.querySelectorAll('.wz-changes .mu-group h5')].map(h=>h.textContent).join()==='New,Improved,Fixed'&&document.querySelectorAll('.wz-changes .mu-item').length===6&&Boolean(button('Show all 8 changes'))&&txt('.wz-changes .mu-head small')==='Old build → Fixture build'`, 'the change list groups New, Improved, Fixed and names both builds');
  await click('Show all 8 changes');
  await check(`document.querySelectorAll('.wz-changes .mu-item').length===8&&document.querySelector('.wz-changes').open`, 'Show all expands in place and the list stays open');
  await check(fits, 'update view fits without horizontal scroll');
  await shot('03b-device-update-changes');
  await click('Update USB');
  await view('flow', 'look');
  await check(`txt('.wz-flow-title h2')==='Update USB'&&txt('.wz-flow-count')==='Step 1 of 5'&&document.querySelector('.wz-look-card[data-look=standard] input').checked&&!document.querySelector('.wz-look-card[data-look=new] input').checked&&!document.querySelector('.wz-look-card[data-look=new] video')`, 're-entry: a USB whose settings say the standard interface opens the step on Standard, not on the default');
  await click('Next');
  await view('flow', 'review');
  await check(`txt('.wz-flow-count')==='Step 2 of 5'&&plan().join('|')==='Back up and update the loader'&&txt('.wz-plan small')==='Old build → Fixture build'`, 'update flow: no profile step; keeping the saved screen adds nothing to the review');
  await evaluate(`window.__mark=fx.requests.length`);
  await click('Write to USB');
  await view('flow', 'done');
  await check(`since(window.__mark)==='inspect_mod_settings,update_usb,inspect_usb'&&fx.written.length===1&&fx.written[0].method==='update_usb'&&JSON.stringify(fx.written[0].expected_identity)==='{"serial":1}'&&fx.written[0].expected_loader.sha256==='abc'&&JSON.stringify(fx.written[0].games)==='[]'`, 'update writes update_usb with the inspected identity and loader, and adds no games');
  await check(`txt('.wz-done').includes('Loader updated. The previous loader is backed up on the USB.')&&!fx.saved`, 'done says the old loader is backed up; the unchanged screen choice writes nothing');
  await click('Done');
  await view('device');
  await evaluate(`setupWizard.startFlow('update')`);
  await view('flow', 'look');
  await evaluate(`document.querySelector('.wz-look-card[data-look=new] input').click()`);
  await click('Next');
  await view('flow', 'review');
  await check(`plan().includes('Use the new interface on the XZ')`, 'switching an existing USB to the new interface is listed in the review');
  await evaluate(`window.__mark=fx.requests.length`);
  await click('Write to USB');
  await view('flow', 'done');
  await check(`since(window.__mark)==='read_usb_settings,save_usb_settings,inspect_usb'&&fx.saved.values.interface===1&&fx.saved.values.classic_theme===2`, 'the new interface writes interface=1 and keeps the classic theme the USB had');
  await click('Done');
  await view('device');
  await evaluate(`setupWizard.startFlow('update')`);
  await view('flow', 'look');
  await check(`document.querySelector('.wz-look-card[data-look=new] input').checked&&document.querySelector('.wz-look-step').dataset.choice==='new'`, 're-entry after saving shows the saved new interface');
  await evaluate(`document.querySelector('.wz-look-card[data-look=standard] input').click()`);
  await click('Next');
  await view('flow', 'review');
  await evaluate(`window.__mark=fx.requests.length`);
  await click('Write to USB');
  await view('flow', 'done');
  await check(`fx.saved.values.interface===0&&fx.saved.values.classic_theme===2&&setupWizard.state.inspection.settings.values.interface===0`, 'the standard interface writes interface=0 and leaves the classic theme as it was');

  await open('fresh');
  await view('device');
  await click('Install XZ Mods');
  await view('flow', 'profile');
  await waitFor(`Boolean(button('Skip for now'))`, 'profile step ready');
  await click('Skip for now');
  await view('flow', 'look');
  await evaluate(`document.querySelector('.wz-look-card[data-look=standard] input').click()`);
  await click('Next');
  await view('flow', 'review');
  await check(`plan().includes('Use the standard XDJ-XZ interface on the XZ')&&!plan().includes('Create the settings file')`, 'a fresh USB with the standard interface: the review says so');
  await click('Write to USB');
  await view('flow', 'done');
  await check(`fx.saved.values.interface===0&&fx.saved.values.classic_theme===0&&txt('.wz-done').includes('Settings saved to the USB and read back.')`, 'the standard interface on a fresh USB writes interface=0 with the default classic theme');

  await open('loader');
  await view('device');
  await evaluate(`tile('games').click()`);
  await view('flow', 'review');
  await check(`txt('.wz-flow-title h2')==='Add games'&&Boolean(document.querySelector('.wz-games-change .games'))&&!gameBox('chex')&&txt('.wz-games-change .games-row[data-id=chex] .tree-badge')==='On USB'&&button('Write to USB').disabled&&txt('.wz-flow-body .notice').startsWith('Nothing to write yet')`, 'Games tile: Add games opens on the review with the chooser; nothing to write until a game is ticked');
  await click('Add all free games');
  await check(`plan().join('|')==='Add 2 free games: Doom shareware and Freedoom Phase 2'&&!button('Write to USB').disabled`, 'Add all plans the two missing games and nothing else');
  await evaluate(`window.__mark=fx.requests.length`);
  await click('Write to USB');
  await view('flow', 'done');
  await check(`since(window.__mark)==='prepare_games_usb,inspect_usb'&&fx.written.length===1&&fx.written[0].method==='prepare_games_usb'&&JSON.stringify(fx.written[0].games)==='["doom","freedoom"]'`, 'a games-only write copies only the games and leaves the loader alone');

  await open('current');
  await view('device');
  await check(`document.querySelector('.wz-status').dataset.tone==='keep'&&txt('.wz-status h3')==='Up to date'&&!document.querySelector('#wiz button.primary')`, 'up to date: a green status and no write button');
  await shot('04-device-current');
  await evaluate(`tile('profile').click()`);
  await view('flow', 'profile');
  await waitFor(`document.querySelector('#wz-djp-slot #djp-name')`, 'DJ profile editor');
  await check(`txt('.wz-flow-title h2')==='Save DJ profile'&&txt('.wz-flow-count')==='Step 1 of 4'`, 'DJ profile tile: Save DJ profile flow, even though the loader is up to date');
  await click('Cancel');
  await view('find');
  await new Promise(done => setTimeout(done, 2600));
  await check(`setupWizard.state.view==='find'&&djProfilePage.pending()===null&&document.activeElement?.dataset.volume==='E:\\\\'&&Boolean(document.getElementById('wz-folder'))`, 'Cancel leaves the guide for the USB list and drops the draft; the USB it left keeps focus and does not reopen by itself');
  await evaluate(`document.querySelector('#wiz .wz-usb[data-volume]').click()`);
  await view('device');
  await check(`document.querySelector('.wz-status').dataset.tone==='keep'`, 'one click on its card returns to the same USB');
  await evaluate(`tile('profile').click()`);
  await waitFor(`document.querySelector('#wz-djp-slot #djp-name')`, 'DJ profile editor again');
  await evaluate(`const f=document.querySelector('#wz-djp-slot #djp-name');f.value='DJ Test';f.dispatchEvent(new Event('input',{bubbles:true}))`);
  await check(`Boolean(button('Next'))&&button('Next').classList.contains('primary')&&!button('Skip for now')`, 'a name turns Skip for now into Next');
  await evaluate(`djProfilePage.addLogoPaths(['C:\\\\Logos\\\\test.png'])`);
  await waitFor(`djProfilePage.state.draft.logos.length===1&&activeJob===null`, 'the draft has a logo');
  await click('Next');
  await view('flow', 'review');
  await check(`plan().join('|')==='Save DJ profile DJ Test'`, 'the review lists only the profile');
  await evaluate(`window.__mark=fx.requests.length`);
  await click('Write to USB');
  await view('flow', 'done');
  await check(`since(window.__mark)==='save_dj_profile,inspect_usb'&&fx.written.length===1&&fx.written[0].method==='save_dj_profile'&&fx.written[0].profile.dj_name==='DJ Test'&&fx.written[0].volume==='E:\\\\'`, 'a DJ-profile-only write saves the profile and nothing else');
  await check(`txt('.wz-done').includes('DJ profile DJ Test saved to the USB.')&&!document.querySelector('.wz-next')&&Boolean(button('Make the profile track'))`, 'done lists the profile; the loader steps only show after a loader write');
  await check(`txt('.wz-done').includes('Moved your profiles to the new DJ Profile layout. A copy of the old files is in C:\\\\backup.')`, 'done says once that the profiles moved, and where the copy is');
  await click('Done');
  await view('device');
  await check(`txt('.wz-tile[data-tile=profile] .wz-tile-value')==='DJ Test'&&txt('.wz-tile[data-tile=profile] .wz-tile-action')==='Edit'`, 'the DJ profile tile shows the saved name');

  await open('fresh');
  await view('device');
  await evaluate(`fx.failGame='chex'`);
  await click('Install XZ Mods'); await view('flow', 'profile'); await click('Skip for now'); await view('flow', 'look'); await click('Next'); await view('flow', 'review');
  await click('Write to USB');
  await view('flow', 'done');
  await check(`txt('.wz-done-hero h3')==='Finished, with something to check'&&txt('.wz-done').includes('chex did not download: Download failed')&&Boolean(button('Retry the failed games'))`, 'a failed game download ends on Finished, with something to check and a Retry button');
  await evaluate(`fx.failGame=null;window.__mark=fx.requests.length`);
  await click('Retry the failed games');
  await waitFor(`activeJob===null&&since(window.__mark)==='prepare_games_usb,inspect_usb'`, 'retry ran');
  await check(`JSON.stringify(fx.written.at(-1).games)==='["chex"]'&&setupWizard.state.inspection.games.find(g=>g.id==='chex').installed&&txt('.wz-done').includes('Games on USB: chex.')`, 'Retry copies only the failed game and reads the USB again');

  await open('noversion');
  await view('device');
  await check(`document.querySelector('.wz-status').dataset.tone==='setup'&&txt('.wz-status p')==='Choose which XZ Mods build goes on this USB.'&&document.querySelectorAll('#wiz button.primary').length===1&&Boolean(button('Use the newest build'))`, 'no saved build: one button sets it up');
  await shot('10-device-noversion');
  await click('Use the newest build');
  await waitFor(`fx.selections.length===1&&activeJob===null&&document.querySelector('.wz-status').dataset.tone==='add'`, 'build saved');
  await check(`fx.selections[0].mod_version_id==='v1'&&fx.selections[0].pinned===true&&Boolean(button('Install XZ Mods'))`, 'Use the newest build saves it in place and offers Install XZ Mods');

  // The written build has no four-deck engine: a setup state with one button into the build choice.
  await open('noquad');
  await view('device');
  await click('Install XZ Mods'); await view('flow', 'profile'); await click('Skip for now'); await view('flow', 'look'); await click('Next'); await view('flow', 'review');
  await click('Write to USB');
  await view('flow', 'done');
  await check(`txt('.wz-fourdeck-setup p')==='The build on this USB has no four-deck engine. Choose the four-deck build, then update this USB.'&&document.querySelectorAll('.wz-fourdeck button.primary').length===1&&button('Choose the four-deck build').classList.contains('primary')&&button('How four decks work').classList.contains('text-button')`, 'no four-deck engine in the written build: one primary button, Choose the four-deck build');
  await shot('08c-done-fourdecks-noengine');
  await click('Choose the four-deck build');
  await waitFor(`document.getElementById('quad-setup').open`, 'the four-deck build dialog opens');
  await check(`!document.getElementById('quad-use').disabled`, 'the dialog offers the included four-deck build');
  await evaluate(`document.getElementById('quad-close').click()`);
  await open('noquadany');
  await view('device');
  await click('Install XZ Mods'); await view('flow', 'profile'); await click('Skip for now'); await view('flow', 'look'); await click('Next'); await view('flow', 'review');
  await click('Write to USB');
  await view('flow', 'done');
  await check(`txt('.wz-fourdeck-setup p')==='The build on this USB has no four-deck engine, and this app includes none.'&&!button('Choose the four-deck build')&&button('How four decks work').classList.contains('primary')`, 'no four-deck build anywhere: one sentence says so, and no button leads to a dead end');

  await open('builds');
  await view('device');
  await waitFor(`fx.selections.length===1&&activeJob===null&&document.querySelector('.wz-build-note')`, 'older included build follows the newest');
  await check(`fx.selections[0].mod_version_id==='v1'&&txt('.wz-build-note')==='Switched to the newest build, which came with this app.'&&txt('.wz-build-name')==='Development build newsha12'`, 'a saved older included build follows the newest build, said in the build line');
  await click('Change');
  await check(`txt('#page-title')==='Updates'&&!document.getElementById('advanced').classList.contains('hidden')&&document.querySelector('nav button.selected').dataset.page==='updates'&&document.getElementById('tab-advanced').getAttribute('aria-selected')==='true'&&!document.getElementById('back-to-tools').checkVisibility()`, 'Change in the build line opens Updates › XZ Mods version');

  await open('unknown');
  await view('device');
  await check(`txt('.wz-status p').startsWith('The XZ Mods version on this USB is not known.')&&txt('.wz-changes-count')==='9 changes'`, 'unknown loader version: said plainly, every change in the target build listed');
  await evaluate(`document.querySelector('.wz-changes summary').click()`);
  await check(`txt('.wz-changes .mu-note').includes('is not known')`, 'the change list explains the unknown version');
  await open('downgrade');
  await view('device');
  await evaluate(`document.querySelector('.wz-changes summary').click()`);
  await check(`txt('.mu-group[data-group=removed] h5')==='Not in Fixture build'&&document.querySelectorAll('.mu-group[data-group=removed] .mu-item').length===8`, 'downgrade: Not in Fixture build lists the changes the DJ loses');
  await open('nonotes');
  await view('device');
  await check(`txt('.wz-status .mu-quiet')==='Notes for Fixture build are not available yet.'&&!document.querySelector('.wz-changes')`, 'target build without notes: one quiet line');

  await open('gamesmenu');
  await view('device');
  await evaluate(`tile('games').click()`);
  await view('flow', 'review');
  await check(`txt('.wz-flow-title h2')==='Install XZ Mods'&&[...document.querySelectorAll('.wz-games-change .games-group[data-kind=builtin] .games-switch input')].map(i=>i.closest('.games-row').dataset.id).join()==='beat-arcade,wave-rider,rhythm-lab,static-surfer'`, 'Games on a fresh USB joins the install; games_menu in the schema gives built-ins a menu switch');
  await click('Back');
  await view('device');
  passed++; console.log('ok', 'a guide opened from the Games tile goes Back to the dashboard, not to steps it skipped');
  await evaluate(`tile('games').click()`);
  await view('flow', 'review');
  await evaluate(`gameSwitch('beat-arcade').click()`);
  await check(`setupWizard.state.settings.games_menu===62&&setupWizard.settingsChanges().join()==='games_menu'&&plan().includes('Change which games show in the GAMES menu')`, 'switching off Beat Arcade is a settings change the review lists');

  await evaluate(`document.querySelector('nav button[data-page=tools]').click()`);
  await check(`txt('#page-title')==='Tools'&&[...document.querySelectorAll('#tools .tool-row strong')].map(s=>s.textContent).join('|')==='Repair library|Game downloads|XZ Screen'&&[...document.querySelectorAll('#tools .tool-group-title')].map(h=>h.textContent).join('|')==='Your USB|Your XZ'&&document.querySelectorAll('#tools .tool-row .tool-chevron').length===3&&!document.getElementById('back-to-tools').checkVisibility()&&!document.getElementById('updates-tabs').checkVisibility()`, 'Tools is three tools with chevrons in two groups: Your USB, Your XZ');
  await shot('09-tools');
  for (const [page, title] of [['library-repair', 'Repair library'], ['games', 'Game downloads'], ['live-stream', 'XZ Screen']]) {
    await evaluate(`document.querySelector('nav button[data-page=tools]').click();document.querySelector('.tool-row[data-open="${page}"]').click()`);
    await check(`!document.getElementById('${page}').classList.contains('hidden')&&txt('#page-title')==='${title}'&&document.querySelector('nav button.selected').dataset.page==='tools'&&document.getElementById('back-to-tools').checkVisibility()`, 'Tools › ' + title + ' opens with a back link and Tools selected');
  }
  await check(`(()=>{const e=document.querySelector('#live-stream .live-frame'),f=e.getBoundingClientRect(),c=getComputedStyle(e.parentElement),room=e.parentElement.clientWidth-parseFloat(c.paddingLeft)-parseFloat(c.paddingRight);return f.width>0&&Math.abs(f.width-Math.min(800,room))<1;})()`, 'XZ Screen under Tools keeps its full-size view: the whole panel width, up to 800');
  await evaluate(`document.getElementById('back-to-tools').click()`);
  await check(`!document.getElementById('tools').classList.contains('hidden')&&txt('#page-title')==='Tools'`, 'the back link returns to Tools');
  await waitFor(`activeJob===null`, 'repair drive discovery finished');
  for (const [page, title] of [['dj-profile', 'DJ Profile'], ['licenses', 'About'], ['updates', 'Updates']]) {
    await evaluate(`document.querySelector('nav button[data-page="${page}"]').click()`);
    await check(`!document.getElementById('${page}').classList.contains('hidden')&&txt('#page-title')==='${title}'&&document.querySelector('nav button.selected').dataset.page==='${page}'&&!document.getElementById('back-to-tools').checkVisibility()&&!document.querySelector('#tools .tool-row[data-open="${page}"]')`, title + ' is its own rail item, with no back link and no second entry in Tools');
  }
  await check(`[...document.querySelectorAll('#updates-tabs [role=tab]')].map(t=>t.textContent.trim()+':'+t.getAttribute('aria-selected')).join('|')==='App updates:true|XZ Mods version:false|Network updates:false'&&document.getElementById('updates-tabs').checkVisibility()&&document.getElementById('tab-updates').tabIndex===0`, 'Updates opens on App updates, with tabs for XZ Mods version and Network updates');
  for (const [tab, page] of [['tab-advanced', 'advanced'], ['tab-device-updates', 'device-updates'], ['tab-updates', 'updates']]) {
    await evaluate(`document.getElementById('${tab}').click()`);
    await check(`!document.getElementById('${page}').classList.contains('hidden')&&document.getElementById('${tab}').getAttribute('aria-selected')==='true'&&document.querySelector('nav button.selected').dataset.page==='updates'&&txt('#page-title')==='Updates'`, 'the ' + tab.slice(4) + ' tab opens its page under Updates');
  }
  await waitFor(`activeJob===null`, 'network adapter detection finished');
  await evaluate(`document.getElementById('tab-updates').focus();document.getElementById('tab-updates').dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowRight',bubbles:true}))`);
  await check(`!document.getElementById('advanced').classList.contains('hidden')&&document.activeElement===document.getElementById('tab-advanced')`, 'Right arrow moves to the next Updates tab and opens it');
  await evaluate(`document.activeElement.dispatchEvent(new KeyboardEvent('keydown',{key:'End',bubbles:true}))`);
  await check(`!document.getElementById('device-updates').classList.contains('hidden')&&document.activeElement===document.getElementById('tab-device-updates')`, 'End moves to the last Updates tab');
  await waitFor(`activeJob===null`, 'network adapter detection finished again');
  await evaluate(`document.activeElement.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowRight',bubbles:true}))`);
  await check(`!document.getElementById('updates').classList.contains('hidden')&&document.activeElement===document.getElementById('tab-updates')`, 'Right arrow on the last tab wraps to App updates');
  await evaluate(`showPage('setup');renderAppUpdate({phase:'available',latest_version:'9.9.9',message:'XZ Mods 9.9.9 is available.'})`);
  await check(`document.querySelector('nav button[data-page=updates] .nav-dot')&&document.querySelector('#tab-updates .nav-dot')&&txt('nav button[data-page=updates]').endsWith(', update available')&&document.getElementById('app-update-banner').checkVisibility()`, 'a newer app puts a spoken dot on Updates in the rail and on its App updates tab');
  await evaluate(`document.getElementById('review-app-update').click()`);
  await check(`!document.getElementById('updates').classList.contains('hidden')&&document.querySelector('nav button.selected').dataset.page==='updates'`, 'Review update opens Updates › App updates');
  await shot('20-update-dot');
  await evaluate(`renderAppUpdate({phase:'current',message:'XZ Mods is up to date.'})`);
  await check(`!document.querySelector('nav .nav-dot,#updates-tabs .nav-dot')&&txt('nav button[data-page=updates]')==='Updates'`, 'the dot goes when no update is on offer');
  await evaluate(`showPage('licenses')`);
  await check(`txt('#about-title')==='XZ Mods'&&txt('.about-version')==='Version 0.3.0'&&txt('.about-maker').startsWith('Made by OpticMystic')&&txt('.about-what').split('. ').length===2&&Boolean(button('App updates and release notes'))&&Boolean(document.getElementById('open-licenses'))`, 'About: what XZ Mods is in two sentences, the version, the maker, App updates and the license notices');
  await check(`[...document.querySelectorAll('.about-projects strong')].map(e=>e.textContent).join('|')==='XZ Mods|VJ.Tools|DJ Profile'&&txt('.about-projects').includes('VJ.Tools Library, is a local-first media library and Resolume prep system')&&!/wingman|instagram|@opticmystic/i.test(txt('#licenses'))`, 'About: the maker\'s three projects, and no social handles');
  await check(`[...document.querySelectorAll('.about-notes-list strong')].map(e=>e.textContent).join('|')==='Independent.|Experimental.|Runs from the USB.|Your own firmware.'&&txt('.about-notes').includes('not affiliated with or endorsed by Pioneer DJ or AlphaTheta')`, 'About: four short disclaimers');
  await check(`[...document.querySelectorAll('#licenses a.about-link')].map(a=>a.getAttribute('href')).join('|')===${JSON.stringify(aboutLinks.join('|'))}&&[...document.querySelectorAll('#licenses a.about-link')].every(a=>a.target==='_blank'&&a.rel.includes('noopener')&&a.textContent.includes('opens in your browser'))`, 'About: links to the source, issues, releases, both sites and optional support, each saying it opens the browser');
  await evaluate(`window.__links=(fx.links||[]).length;for(const a of document.querySelectorAll('#licenses a.about-link'))a.click()`);
  await waitFor(`(fx.links||[]).length===window.__links+${aboutLinks.length}`, 'every About link asks the host to open it');
  await check(`JSON.stringify(fx.links.slice(window.__links))===${JSON.stringify(JSON.stringify(aboutLinks))}`, 'each About link opens its exact URL through open_link');
  await shot('13-about');
  await evaluate(`document.getElementById('about-feedback').click()`);
  await check(`txt('#page-title')==='Feedback'`, 'About › Feedback opens Feedback in the app');
  await evaluate(`showPage('licenses');document.getElementById('about-dj-profile').click()`);
  await check(`txt('#page-title')==='DJ Profile'&&document.querySelector('nav button.selected').dataset.page==='dj-profile'`, 'About › Make yours opens DJ Profile');
  // Pages the manual shows that have no flow of their own, drawn here so their pictures carry the current rail.
  await evaluate(`showPage('updates')`); await idle(); await shot('14-app-updates');
  await evaluate(`showPage('device-updates')`);
  await waitFor(`activeJob===null&&document.getElementById('ota-network').value==='169.254.168.10'`, 'network updates: the adapter is listed');
  // The finished detection's panel goes, as it does on any page change; the picture shows the page itself.
  await evaluate(`document.getElementById('job-panel').classList.add('hidden')`);
  await shot('15-network-updates');
  await evaluate(`showPage('library-repair')`);
  await waitFor(`activeJob===null&&txt('#repair-drive-status').startsWith('No Rekordbox USB found')`, 'repair library: no Rekordbox USB');
  await shot('16-repair-library');
  await evaluate(`showPage('live-stream')`); await shot('17-xz-screen');
  await evaluate(`document.getElementById('live-setup').click()`);
  await waitFor(`document.getElementById('live-wizard').open&&txt('#live-wizard-status')==='Found your XZ. Press Connect.'`, 'Live Streamer setup finds the XZ');
  await shot('18-xz-screen-setup');
  await evaluate(`document.getElementById('live-wizard-close').click();showPage('setup')`);
  await evaluate(`showPage('games')`);
  await check(`txt('#page-title')==='Game downloads'&&document.querySelector('#games .game-downloads #setup-games-usb')&&!document.querySelector('#advanced .game-downloads,#advanced #setup-games-usb')&&!document.getElementById('download-myhouse')&&!txt('#games').includes('My House')`, 'Tools › Game downloads holds the game downloads, and the XZ Mods version page no longer does');
  await shot('19-game-downloads');
  await evaluate(`showPage('advanced')`);
  await check(`document.getElementById('mod-version')&&document.getElementById('build')&&document.getElementById('loader-backups')&&!document.getElementById('prepare-usb')&&!document.querySelector('#advanced .intro')`, 'XZ Mods version page: build panel, loader restore and firmware file; no Prepare a new USB');
  await shot('11-tools-version');
  // The four-deck dialog shows the same cable picture: it plays while open and stops when it closes.
  await evaluate(`document.getElementById('quad-open').click()`);
  await waitFor(`(()=>{const d=document.getElementById('quad-setup'),v=document.getElementById('quad-video');return d.open&&v.readyState>=2&&!v.paused&&v.loop&&v.muted&&!v.controls&&v.videoWidth===1280&&v.getAttribute('aria-describedby')==='quad-video-desc';})()`, 'four-deck dialog: the cable explainer plays under the app CSP');
  await evaluate(`(()=>{const v=document.getElementById('quad-video');v.currentTime=v.duration-0.3;})()`);
  await waitFor(`(()=>{const v=document.getElementById('quad-video');return !v.paused&&v.currentTime>0&&v.currentTime<3;})()`, 'four-deck dialog: the cable video loops');
  await check(`txt('#quad-setup figcaption').startsWith('The cable four decks use: rear USB-B socket to top USB1.')&&document.querySelector('#quad-setup figure').getBoundingClientRect().width<=document.getElementById('quad-setup').getBoundingClientRect().width`, 'four-deck dialog: the picture fits the dialog and says plainly what the cable is');
  await shot('11b-quad-dialog');
  await evaluate(`document.getElementById('quad-close').click()`);
  await waitFor(`!document.getElementById('quad-setup').open&&document.getElementById('quad-video').paused`, 'closing the four-deck dialog stops the cable picture');

  await open('usb-only');
  await idle();
  await evaluate(`showPage('advanced')`);
  await check(`document.getElementById('mod-version-pin').disabled&&document.getElementById('mod-version-pin').checked&&txt('#mod-version-pin-note').includes('USB setup')`, 'USB-only build stays pinned with a truthful reason');
  await check(`document.getElementById('mod-version-usb-setup').checkVisibility()&&document.getElementById('mod-version-usb-setup').classList.contains('primary')`, 'USB-only state has a primary setup action');
  await evaluate(`document.getElementById('mod-version-usb-setup').click()`);
  await check(`!document.getElementById('setup').classList.contains('hidden')`, 'USB-only action opens the existing setup wizard');

  await open('beta');
  await waitFor(`fx.calls.filter(c=>c==='check_app_update').length===1&&!updateChecking`, 'one automatic beta check on launch');
  await check(`document.documentElement.dataset.updateChannel==='beta'&&txt('#update-channel-note').includes('automatically at launch')&&document.getElementById('app-update-banner').checkVisibility()&&txt('#app-update-banner-text').includes('0.3.0-preview.13')`, 'beta startup shows the update banner without opening Tools');
  await check(`document.getElementById('app-downloads').href==='https://beta.xzmods.com/'&&document.getElementById('portable-downloads').href.includes('/v0.3.0-preview.12/')`, 'beta download links never point at stable latest');
  await evaluate(`showPage('updates');document.getElementById('check-app-update').click()`);
  await waitFor(`fx.calls.filter(c=>c==='check_app_update').length===2&&!updateChecking`, 'manual beta recheck');
  await open('betaoffline');
  await waitFor(`fx.calls.includes('check_app_update')&&!updateChecking`, 'offline startup check settles');
  await evaluate(`showPage('updates')`);
  await check(`txt('#update-status').includes('offline')&&!document.getElementById('check-app-update').disabled&&activeJob===null`, 'offline startup stays usable and permits retry');
  await evaluate(`fx.online=true;document.getElementById('check-app-update').click()`);
  await waitFor(`fx.calls.filter(c=>c==='check_app_update').length===2&&!updateChecking`, 'offline beta retry succeeds');
  await check(`document.getElementById('app-update-banner').checkVisibility()`, 'successful retry exposes the beta update');

  await open('mac');
  await idle();
  await evaluate(`showPage('updates')`);
  await check(`!document.getElementById('check-app-update').checkVisibility()&&!document.getElementById('install-app-update').checkVisibility()&&Boolean(button('Open downloads page'))&&button('Open downloads page').classList.contains('primary')&&txt('#updates .mac-only p')==='Mac versions are downloaded from the release page.'&&!fx.calls.includes('check_app_update')`, 'Mac: App updates says where Mac versions come from, with one Open downloads page button and no update controls');
  await shot('12-mac-updates');
  await check(`document.querySelector('nav button.selected').dataset.page==='updates'&&document.getElementById('tab-updates').getAttribute('aria-selected')==='true'&&!document.querySelector('nav .nav-dot')`, 'Mac: Updates in the rail opens App updates, with no update dot');


  await open('fresh');
  await evaluate("document.querySelector('nav button[data-page=\"my-reports\"]').click()");
  await waitFor("document.querySelectorAll('.sr-drafts .sr-report').length===1",'saved report inventory');
  await check("txt('.sr-drafts strong')==='Earlier bridge report'&&txt('.sr-drafts .sr-badge')==='Older form'&&Boolean(button('Use this text'))&&!document.getElementById('support-consent')&&!fx.requests.some(r=>r.method==='support_preview')",'a report saved by the old form is listed in My reports and never sent by the old page-by-page flow');
  await click('Use this text');
  await waitFor("document.getElementById('report-wizard').open&&reportWizard.state().screen==='describe'",'old words open the new wizard');
  await check("document.querySelector('#report-wizard .sr-box').value.startsWith('Earlier bridge report')",'its words seed a new one-box report');
  await evaluate("reportWizard.state().text='';document.getElementById('report-wizard').dispatchEvent(new Event('cancel'))");
  await idle();
  await click('Discard');
  await waitFor("document.querySelectorAll('.sr-drafts .sr-report').length===0&&![...document.querySelectorAll('.sr-section-title')].some(h=>h.textContent==='Saved, not sent')",'discarded local report');
  await check("fx.requests.filter(r=>r.method==='support_remove').length===1&&fx.requests.find(r=>r.method==='support_remove').stored_revision==='c'.repeat(64)",'discarding names the exact stored copy');
  await shot('saved-reports-empty');


  // Layout sweep: from the smallest window the app allows (760 x 520) through a 1366 x 768 screen at 125 % and a
  // 1280 x 720 one, to a large window. Nothing is cut off at the right or scrolls sideways, the rail's foot is on
  // screen, and a guide step's bar is in view.
  await open('fresh');
  await view('device');
  for (const [width, height] of [[760, 520], [1092, 582], [1280, 680], [1920, 1040]]) {
    await size(width, height);
    const at = ' at ' + width + 'x' + height;
    const sweep = async (label, name) => { await new Promise(done => setTimeout(done, 150)); const bad = await evaluate(clipped); assert.equal(bad, '', label + at + ': ' + bad); if (name) await capture('sweep-' + name); };
    await evaluate(`showPage('setup')`); await view('device');
    await sweep('USB dashboard', 'device');
    await evaluate(`setupWizard.startFlow('install')`);
    for (const step of ['profile', 'look', 'review']) {
      await evaluate(`setupWizard.goto(${JSON.stringify(step)})`); await view('flow', step);
      await sweep('guide step ' + step, 'flow-' + step);
      assert(await evaluate(barInView), 'the bar is in view on ' + step + at);
    }
    await evaluate(`window.openManual=undefined;setupWizard.openFourDecks()`); await view('flow', 'quad');
    await sweep('cable guide', 'flow-quad');
    await click('Done'); await view('flow', 'review');
    await click('Cancel'); await view('find');
    await sweep('USB list', 'find');
    await evaluate(`setupWizard.chooseUsb('E:\\\\')`); await view('device');
    for (const page of ['tools', 'advanced', 'dj-profile', 'library-repair', 'games', 'device-updates', 'updates', 'licenses', 'live-stream', 'my-reports', 'manual']) {
      await evaluate(`showPage(${JSON.stringify(page)})`); await idle();
      await sweep('page ' + page, page);
    }
    await evaluate(`showPage('advanced');document.getElementById('quad-open').click()`);
    await sweep('four-deck dialog', 'quad-dialog');
    await evaluate(`document.getElementById('quad-close').click();showPage('setup')`);
  }
  await size(1100, 760);
  passed++; console.log('ok', 'every page fits from 760 x 520 to 1920 x 1040: no sideways scroll, nothing cut off, the rail foot and the guide bar in view');
  await cspClean();
  assert.deepEqual(errors, [], 'no page errors: ' + errors.join(' / '));
  passed++; console.log('ok', 'no CSP violations and no page errors');
  await check(`(()=>{const probe=document.createElement('div');probe.setAttribute('style','color:rgb(255, 0, 0)');document.body.append(probe);const blocked=getComputedStyle(probe).color!=='rgb(255, 0, 0)';probe.remove();return location.protocol==='http:'&&blocked;})()`, 'the page runs under the app CSP: a style attribute is blocked, as in the installed app');
  console.log('\n' + passed + ' checks passed');
} finally {
  socket?.close();
  await server.close();
  killBrowser(child, profile);
  await new Promise(done => setTimeout(done, 300));
  await rm(profile, { recursive: true, force: true }).catch(() => {});
}
