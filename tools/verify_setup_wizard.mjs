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
  const usbs = () => {
    if ((mode === 'nousb' && !fx.plugged) || fx.unplugged) return [];
    const one = { volume: 'E:\\', label: 'KINGSTON', removable: true, loader: usb.loader, settings: usb.settings, rekordbox: true, games: Object.values(fx.games).filter(Boolean).length };
    return mode === 'multi' ? [one, { volume: 'F:\\', label: 'SANDISK', removable: true, loader: false, settings: false, rekordbox: false, games: 0 }] : [one];
  };
  fx.volumes = () => ['C:\\', ...usbs().map(usb => usb.volume)];
  const gamesOnUsb = () => Object.entries(fx.games).map(([id, installed]) => ({ id, name: { doom: 'Doom shareware', chex: 'Chex Quest', freedoom: 'Freedoom Phase 2', myhouse: 'My House' }[id], installed, folder: 'E:\\VJTOOLS\\Games\\' + id, requires_gzdoom: id === 'myhouse', bytes: 2450688 }));
  const inspection = volume => ({ format: 'xz-mods-usb/1', volume, identity: { serial: 1 }, loader: { exists: usb.loader, bytes: 4100000, sha256: 'abc' },
    recorded_mod_version: usb.loader ? fx.recorded : null, installed_version: null, runtime_sha256: 'newsha',
    settings: { editable: true, values: settingsValues(), revision: { exists: usb.settings, bytes: 400 }, schema, path: volume + 'VJ.Tools\\XZ-Mods.cfg' }, backups: [], games: gamesOnUsb(), direct_usb_root: true, requires_copy_to_usb_root: false, filesystem: 'FAT32',
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
    : [{ id: 'v1', label: 'Fixture build', runtime_sha256: 'newsha', hardware_qualified: false, source_commit: savedCommit }];
  fx.selections = [];
  if(mode==='usb-only')for(const item of builds){item.network_updates_supported=false;item.network_update_reason='This build updates through USB setup.';}
  fx.selected = { id: mode === 'noversion' ? 'missing' : mode === 'builds' ? 'old' : 'v1', pinned: true, revision: 1 };
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
      case 'select_mod_version': { fx.selected = { id: request.mod_version_id, pinned: request.pinned, revision: fx.selected.revision + 1 }; fx.selections.push(request); return { running: false, ok: true, result: inventory() }; }
      case 'discover_usbs':
        if (fx.holdQuiet) return { running: true };
        if (fx.failDiscover) { fx.failDiscover--; return { running: false, ok: false, error: 'The drive list could not be read.' }; }
        return { running: false, ok: true, result: { format: 'xz-usbs/1', usbs: usbs() } };
      case 'download_game': return { running: false, ok: true, result: { format: 'xz-game-downloaded/1', name: 'Doom shareware' } };
      case 'inspect_usb': if (fx.holdInspect) return { running: true }; return { running: false, ok: true, result: inspection(request.volume) };
      case 'inspect_mod_settings': return { running: false, ok: true, result: { format: 'xz-mod-settings/1', mod_version_id: 'v1', settings_change: null } };
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
    if (command === 'list_volumes') return fx.volumes();
    if (command === 'start_job') { fx.requests.push(args.request); return fx.requests.length; }
    if (command === 'job_status') return status(fx.requests[args.id - 1]);
    if (command === 'cancel_job') return {};
    if (command === 'pick_path') return 'F:\\';
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
  const shot = async name => {
    if (!shots) return;
    for (const [width, height] of [[1100, 760], [1440, 900]]) {
      await size(width, height);
      await new Promise(done => setTimeout(done, 150));
      const image = await send('Page.captureScreenshot', { format: 'png' });
      await writeFile(join(shots, name + '-' + width + '.png'), Buffer.from(image.data, 'base64'));
    }
    await size(1100, 760);
  };
  const txtQuad = `txt('.wz-quad-prereq').includes('paired XDJ-XZ 1.26 quad beta resources')&&txt('.wz-quad-status').includes('not checked')`;
  const fits = `document.documentElement.scrollWidth<=innerWidth`;

  await open('fresh');
  await check(`[...document.querySelectorAll('nav button')].map(b=>b.dataset.page).join()==='setup,stems,live-stream,tools,my-reports'&&[...document.querySelectorAll('nav button:not(.nav-feedback)')].map(b=>b.textContent.trim()).join()==='USB,Stems,XZ Screen,Tools'&&txt('nav .nav-feedback-text>span')==='Feedback'&&document.querySelectorAll('nav button svg.icon use').length===5`, 'nav is USB, Stems, XZ Screen, Tools, then Feedback at the foot of the rail, each with an icon');
  await check(`document.querySelector('nav button.selected').dataset.page==='setup'&&txt('#page-title')==='USB'&&!document.querySelector('header .eyebrow')&&getComputedStyle(document.getElementById('page-title')).fontSize==='20px'`, 'USB is the home page under a compact one-line header');
  await view('device');
  await check(`fx.requests.slice(0,3).map(r=>r.method).join()==='list_mod_versions,discover_usbs,inspect_usb'&&fx.calls.includes('list_volumes')&&!document.getElementById('job-panel').checkVisibility()`, 'a single USB opens by itself: no Find button, no job panel flash');
  await check(`txt('.wz-drive h2')==='KINGSTON'&&txt('.wz-drive .wz-mono')==='E:\\\\'&&txt('.wz-drive-meta')==='FAT32 · 29.1 GB free of 32 GB'&&Boolean(button('Change USB'))`, 'device header: label, volume, filesystem and free space, Change USB');
  await check(`document.querySelector('.wz-status').dataset.tone==='add'&&txt('.wz-status h3')==='XZ Mods is not on this USB yet'&&txt('.wz-status p')==='Adds the loader, free games and your DJ profile. Music and playlists stay as they are.'&&document.querySelectorAll('#wiz button.primary').length===1&&Boolean(button('Install XZ Mods'))`, 'fresh USB: one status and one primary button, Install XZ Mods');
  await check(`txt('.wz-tile[data-tile=profile] .wz-tile-value')==='Not set'&&txt('.wz-tile[data-tile=profile] .wz-tile-action')==='Add'&&txt('.wz-tile[data-tile=games] .wz-tile-value')==='None'&&txt('.wz-tile[data-tile=stems] .wz-tile-value')==='None yet'&&txt('.wz-tile[data-tile=stems] .wz-tile-action')==='Prepare'`, 'tiles: DJ profile not set, no games, no stems yet');
  await check(`txt('.wz-build-line')==='Build: Fixture build · Change'&&!document.querySelector('#wiz input[type=checkbox],#wiz input[type=radio],#wiz select')&&!document.querySelector('.wz-files').open`, 'one quiet build line; no tick boxes, radios or build chooser on the main path; files folded away');
  await shot('02-device-fresh');
  await evaluate(`document.querySelector('.wz-files summary').click()`);
  await check(`document.querySelector('.wz-files .tree')&&!document.querySelector('.wz-files .tree input')&&document.querySelector('.tree-row[data-id=loader]').dataset.state==='missing'&&!document.querySelector('.tree-row[data-id=game-myhouse]')&&!txt('#wiz').includes('My House')`, 'Show every file opens the read-only tree, without My House');
  await check(`(()=>{const rows=['drive','loader','settings','game-doom'].map(id=>document.querySelector('.tree-row[data-id="'+id+'"]'));return rows.every(r=>getComputedStyle(r).gridTemplateColumns.split(' ').length===4)&&getComputedStyle(rows[3]).gridTemplateColumns.startsWith('44px');})()`, 'tree rows keep the depth indent under the CSP (CSSOM --depth)');
  await check(fits, 'device view fits without horizontal scroll');
  await evaluate(`document.querySelector('.wz-files summary').click()`);

  await click('Install XZ Mods');
  await view('flow', 'profile');
  await waitFor(`document.querySelector('#wz-djp-slot #djp-name')`, 'DJ profile editor in the flow');
  await check(`txt('.wz-flow-title h2')==='Install XZ Mods'&&txt('.wz-flow-count')==='Step 1 of 7'&&document.querySelectorAll('.wz-dots li').length===7&&document.querySelector('.wz-dots li[data-state=current]')===document.querySelector('.wz-dots li')&&Boolean(button('Cancel'))`, 'install flow: compact header, Step 1 of 7 with dots, Cancel');
  await check(`Boolean(button('Skip for now'))&&button('Skip for now').classList.contains('secondary')&&Boolean(button('Back'))&&fx.requests.filter(r=>r.method==='read_dj_profiles').length===1`, 'a USB without a profile starts on the DJ profile step: Skip for now, Back');
  await shot('05-flow-profile');
  await click('Skip for now');
  await view('flow', 'look');
  await check(`txt('.wz-flow-count')==='Step 2 of 7'&&txt('.wz-flow-body h3')==='Choose the screen the XZ shows'&&document.querySelectorAll('.wz-look-card').length===2&&document.querySelector('.wz-look-card[data-look=new] input').checked&&!document.querySelector('.wz-look-card[data-look=standard] input').checked&&document.querySelector('.wz-look-step').dataset.choice==='new'&&Boolean(button('Cancel'))`, 'look step after the profile: the new interface is selected by default on a fresh USB');
  await check(`txt('.wz-look-card[data-look=new] .wz-look-title')==='New interfaceBeta'&&txt('.wz-look-card[data-look=standard] .wz-look-title')==='Standard XDJ-XZ interface'`, 'cards: New interface (Beta) and Standard XDJ-XZ interface');
  await check(`(()=>{const v=document.querySelector('.wz-look-card[data-look=new] video');return Boolean(v)&&v.muted&&v.loop&&v.autoplay&&v.playsInline&&v.getAttribute('src')==='assets/wizard/new-interface.mp4'&&v.getAttribute('aria-label').length>40;})()&&document.querySelector('.wz-look-card[data-look=standard] img').alt.length>20`, 'the chosen card plays the new interface muted and looping in place; the other shows a still with alt text');
  await waitFor(`(()=>{const v=document.querySelector('.wz-look-card[data-look=new] video');return Boolean(v)&&v.readyState>=2&&v.videoWidth===800&&v.videoHeight===480;})()`, 'the new-interface video decodes under the app CSP');
  await check(`(()=>{const c=[...document.querySelectorAll('.wz-look-card')].map(e=>e.getBoundingClientRect().width);return c[0]>c[1]*1.4;})()`, 'the selected card is the large one');
  await check(`txt('.wz-later-line')==='You can switch any time on the deck: tap MODS, then APPEARANCE → INTERFACE.'&&txt('.wz-later-steps').includes('Tap MODS in the top right corner, then the APPEARANCE tab and OPEN APPEARANCE.')&&txt('.wz-later-steps').includes('Choose INTERFACE: CLASSIC.')`, 'one line on switching later, and two steps naming the deck labels');
  await waitFor(`[...document.querySelectorAll('.wz-look-step img')].every(i=>i.complete&&i.naturalWidth>0&&i.alt)`, 'stills and switch-later graphics load, each with alt text');
  await check(`!/\\b(CDJ|Pioneer|rekordbox|AlphaTheta)\\b/i.test(txt('.wz-look-step')+[...document.querySelectorAll('.wz-look-step [alt],.wz-look-step [aria-label]')].map(e=>e.getAttribute('alt')||e.getAttribute('aria-label')).join(' '))`, 'the step names no other brand');
  await check(fits, 'look step fits without horizontal scroll');
  await shot('05b-flow-look-new');
  await evaluate(`document.querySelector('.wz-look-card[data-look=new] input').focus()`);
  for (const type of ['keyDown', 'keyUp']) await send('Input.dispatchKeyEvent', { type, key: 'ArrowRight', code: 'ArrowRight', windowsVirtualKeyCode: 39, nativeVirtualKeyCode: 39 });
  await waitFor(`document.querySelector('.wz-look-step').dataset.choice==='standard'`, 'the arrow key picks the standard interface');
  await check(`setupWizard.state.settings.interface===0&&document.activeElement===document.querySelector('.wz-look-card[data-look=standard] input')&&getComputedStyle(document.querySelector('.wz-look-card[data-look=standard]')).outlineStyle==='solid'&&txt('.wz-later-steps').includes('Tap MODS in the top right corner, then APPEARANCE.')&&txt('.wz-later-steps').includes('Choose INTERFACE: MODERN.')`, 'keyboard: arrows move the choice, the focused card shows a ring, the switch-later graphic follows the choice');
  await new Promise(done => setTimeout(done, 700));
  await check(`(()=>{const c=[...document.querySelectorAll('.wz-look-card')].map(e=>e.getBoundingClientRect().width);return c[1]>c[0]*1.4;})()&&!document.querySelector('.wz-look-card[data-look=new] video')&&document.querySelector('.wz-look-card[data-look=new] img').getAttribute('src')==='assets/wizard/new-interface-poster.webp'`, 'the large frame moves to the standard card; the new interface stops and shows its poster');
  await shot('05c-flow-look-standard');
  await click('Back');
  await view('flow', 'profile');
  await waitFor(`Boolean(button('Skip for now'))`, 'profile step ready');
  await click('Skip for now');
  await view('flow', 'look');
  await check(`document.querySelector('.wz-look-card[data-look=standard] input').checked`, 'Back and forward keep the choice');
  await evaluate(`document.querySelector('.wz-look-card[data-look=new] input').click()`);
  await check(`setupWizard.state.settings.interface===1&&document.querySelector('.wz-look-step').dataset.choice==='new'`, 'a click picks the new interface again');
  await click('Next');
  await view('flow', 'quad');
  await check(txtQuad, 'quad guide retains paired-loader prerequisite');
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
  await click('Back'); await view('flow', 'look'); await click('Next'); await view('flow', 'quad');
  await check(`!document.querySelector('.wz-quad-art video')&&document.querySelector('.wz-quad-art img').getAttribute('src')==='assets/wizard/loopback-cable-poster.webp'&&document.querySelector('.wz-quad-art img').alt.includes('rear USB-B socket')&&!button('Pause cable animation')`, 'reduced motion: the guide shows the still with its description and no motion control');
  await shot('05e-flow-quad-reduced-motion');
  await send('Emulation.setEmulatedMedia', { features: [{ name: 'prefers-reduced-motion', value: 'no-preference' }] });
  await click('Skip for now');
  await view('flow', 'review');
  await check(`plan().join('|')==='Add the XZ Mods loader (Fixture build)|Add 3 free games: Doom shareware, Chex Quest and Freedoom Phase 2|Use the new interface on the XZ|Create the settings file'&&txt('.wz-safe')==='Music, playlists and prepared stems are never touched.'&&txt('.wz-flow-count')==='Step 4 of 7'`, 'review: a plain checklist of what will happen, then the reassurance line');
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
  await check(`document.querySelector('.wz-flow').contains(document.getElementById('job-panel'))&&txt('#job-title')==='Adding the XZ Mods loader'&&!button('Write to USB')&&!document.querySelector('.wz-cancel')&&txt('.wz-flow-count')==='Step 5 of 7'`, 'writing: the job panel shows inside the flow; no Write or Cancel to press twice');
  await shot('07-flow-writing');
  await evaluate(`fx.hold=false`);
  await view('flow', 'done');
  await check(`since(window.__mark)==='inspect_mod_settings,prepare_usb,read_usb_settings,save_usb_settings,inspect_usb'&&fx.written.length===2&&fx.written[0].method==='prepare_usb'&&fx.written[0].volume==='E:\\\\'&&fx.written[0].experimental===true&&fx.written[0].mod_version_id==='v1'&&fx.written[0].pin_mod_version===true&&JSON.stringify(fx.written[0].games)==='["doom","freedoom"]'`, 'Write: settings check, prepare_usb with the chosen games, the settings file, then a fresh inspection');
  await check(`fx.written[1].method==='save_usb_settings'&&fx.saved.values.interface===1&&fx.saved.values.classic_theme===0&&!('theme' in fx.saved.values)`, 'the new interface is written as interface=1, the one setting MODS > APPEARANCE > INTERFACE reads');
  await check(`txt('.wz-done-hero h3')==='Your USB is ready'&&txt('.wz-done').includes('Loader written')&&txt('.wz-done').includes('Games on USB: doom, freedoom.')&&txt('.wz-next').includes('press MENU')&&Boolean(button('Done'))&&!document.getElementById('job-panel').checkVisibility()`, 'done: success mark, result lines, the On the XZ steps, Done');
  await check(`!(fx.links||[]).length&&Boolean(button('Done'))&&Boolean(button('Support XZ Mods (optional)'))`, 'support is optional and opens no links on completion');
  await evaluate('window.supportWriteCount=fx.written.length');
  await click('Support XZ Mods (optional)'); await view('flow','support');
  await check(`txt('.wz-support').includes('Every feature stays available')&&document.activeElement.closest('.wz-flow-body')!==null&&!document.querySelector('a[href*="sponsors/"]')&&!(fx.links||[]).length`, 'Support has focus, no paywall and no unavailable Sponsors link');
  await shot('support-final');
  await evaluate(`button('Support on Cash App').focus()`);
  for (const type of ['keyDown','keyUp']) await send('Input.dispatchKeyEvent',{type,key:'Enter',code:'Enter',windowsVirtualKeyCode:13,nativeVirtualKeyCode:13});
  await waitFor(`fx.links?.length===1`, 'explicit support link');
  await check(`fx.links[0]==='https://cash.app/$FireAtWill42'&&fx.written.length===supportWriteCount`, 'keyboard opens only the exact Cash App URL without another USB write');
  await click('Back to results'); await view('flow','done');

  await shot('08-flow-done');
  await click('Done');
  await view('device');
  await check(`document.querySelector('.wz-status').dataset.tone==='keep'&&txt('.wz-status h3')==='Up to date'&&txt('.wz-status p')==='Fixture build is on this USB.'&&!document.querySelector('#wiz button.primary')&&txt('.wz-tile[data-tile=games] .wz-tile-value')==='2 games on USB'`, 'Done returns to the re-inspected USB: up to date, no write button, two games');

  await click('Change USB');
  await view('find');
  await new Promise(done => setTimeout(done, 2600));
  await check(`setupWizard.state.view==='find'&&document.querySelectorAll('#wiz .wz-usb').length===1&&txt('.wz-find h2')==='Your XZ USB'&&txt('#wiz .wz-usb').includes('KINGSTON')&&txt('#wiz .wz-usb').includes('XZ Mods installed')`, 'Change USB goes back to the list and does not reopen the same USB by itself');
  await evaluate(`fx.unplugged=true`);
  await waitFor(`setupWizard.state.usbs?.length===0&&activeJob===null`, 'unplugged', 400);
  await evaluate(`fx.unplugged=false`);
  await view('device');
  await check(`txt('.wz-drive h2')==='KINGSTON'`, 'unplugging and plugging the USB back in opens it by itself again');

  await open('nousb');
  await waitFor(`setupWizard.state.usbs!==null&&activeJob===null&&!setupWizard.state.finding`, 'first look finished');
  await check(`setupWizard.state.view==='find'&&document.querySelector('.wz-empty .wz-art svg')&&txt('.wz-find h2')==='Plug in your XZ USB'&&txt('.wz-find .wz-lead')==='Or any USB stick formatted FAT32. XZ Mods finds it as soon as it is connected. It never formats a drive or touches your music.'`, 'no USB: illustration, Plug in your XZ USB, one plain line');
  await check(`!document.querySelector('#wiz button.primary')&&!button('Find my USB')&&Boolean(button('Look again'))&&Boolean(button('Choose a folder instead'))&&button('Look again').classList.contains('text-button')`, 'no USB: no primary Find button, only quiet Look again and Choose a folder instead');
  await shot('00-find-empty');
  await evaluate(`window.__mark=fx.requests.length;fx.failDiscover=1;fx.plugged=true`);
  await waitFor(`setupWizard.state.view==='device'&&activeJob===null`, 'plugged-in USB opened', 400);
  await check(`since(window.__mark)==='discover_usbs,discover_usbs,inspect_usb'&&txt('.wz-drive h2')==='KINGSTON'`, 'plugging in the USB opens it by itself; a discover that failed is retried on the next poll');

  await open('multi');
  await waitFor(`document.querySelectorAll('#wiz .wz-usb').length===2&&activeJob===null`, 'two USB cards');
  await check(`setupWizard.state.view==='find'&&txt('.wz-find h2')==='Choose your XZ USB'&&txt('#wiz .wz-usb:nth-child(2)').includes('No XZ Mods yet')&&!txt('#wiz .wz-usb').includes('game')`, 'several USBs: cards to pick from, with simple chips');
  await shot('01-find-several');
  await evaluate(`fx.holdQuiet=true;window.__mark=fx.requests.length;document.getElementById('wz-look').click()`);
  await waitFor(`activeJob!==null&&setupWizard.state.finding`, 'a slow discover holds the job slot');
  await evaluate(`document.querySelectorAll('#wiz .wz-usb')[1].click();showPage('advanced');document.querySelector('#advanced .game-downloads').open=true;document.getElementById('download-doom').click();showPage('setup')`);
  await new Promise(done => setTimeout(done, 400));
  await evaluate(`fx.holdQuiet=false`);
  await view('device');
  await waitFor(`fx.requests.some(r=>r.method==='download_game')&&activeJob===null`, 'download ran after the discover');
  await check(`since(window.__mark).split(',').sort().join()==='discover_usbs,download_game,inspect_usb'&&txt('.wz-drive h2')==='SANDISK'&&fx.requests.find(r=>r.method==='inspect_usb'&&fx.requests.indexOf(r)>=window.__mark).volume==='F:\\\\'`, 'clicks made while a quiet discover runs wait for it and then all run: the card opens that USB, the download starts');

  await open('multi');
  await waitFor(`document.querySelectorAll('#wiz .wz-usb').length===2&&activeJob===null`, 'two USB cards again');
  await evaluate(`fx.holdQuiet=true;document.getElementById('wz-look').click()`);
  await waitFor(`activeJob!==null&&setupWizard.state.finding`, 'a slow discover holds the job slot again');
  await evaluate(`window.__mark=fx.requests.length;document.querySelectorAll('#wiz .wz-usb')[0].click()`);
  await check(`(()=>{const cards=document.querySelectorAll('#wiz .wz-usb');return cards[0].dataset.state==='opening'&&cards[0].getAttribute('aria-busy')==='true'&&txt('#wiz .wz-opening')==='Reading the USB…'&&Boolean(cards[0].querySelector('.wz-spinner'))&&cards[1].disabled&&cards[1].dataset.state==='waiting';})()`, 'a click during a background scan shows Reading the USB with a spinner on that card at once; the other card waits');
  await evaluate(`fx.holdInspect=true;fx.holdQuiet=false`);
  await waitFor(`fx.requests.slice(window.__mark).some(r=>r.method==='inspect_usb')`, 'the read starts after the scan');
  await check(`setupWizard.state.view==='find'&&document.querySelectorAll('#wiz .wz-usb')[0].dataset.state==='opening'`, 'the card keeps showing Reading the USB through a slow read');
  await shot('01b-find-opening');
  await evaluate(`document.querySelectorAll('#wiz .wz-usb')[0].click();document.querySelectorAll('#wiz .wz-usb')[1].click();fx.holdInspect=false`);
  await view('device');
  await check(`txt('.wz-drive h2')==='KINGSTON'&&fx.requests.slice(window.__mark).filter(r=>r.method==='inspect_usb').length===1&&setupWizard.state.opening===null`, 'repeat clicks while a USB opens read it once, and the clicked USB is the one that opens');

  await open('fresh');
  await view('device');
  await click('Install XZ Mods');
  await view('flow', 'profile');
  await waitFor(`Boolean(button('Skip for now'))`, 'profile step ready');
  await click('Skip for now');
  await view('flow', 'look');
  await click('Next');
  await view('flow', 'quad');
  await check(txtQuad, 'quad guide retains paired-loader prerequisite');
  await click('Skip for now');
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
  await view('flow', 'quad');
  await check(txtQuad, 'quad guide retains paired-loader prerequisite');
  await click('Skip for now');
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
  await view('flow', 'quad');
  await check(txtQuad, 'quad guide retains paired-loader prerequisite');
  await click('Skip for now');
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
  await check(`txt('.wz-flow-title h2')==='Update USB'&&txt('.wz-flow-count')==='Step 1 of 6'&&document.querySelector('.wz-look-card[data-look=standard] input').checked&&!document.querySelector('.wz-look-card[data-look=new] input').checked&&!document.querySelector('.wz-look-card[data-look=new] video')`, 're-entry: a USB whose settings say the standard interface opens the step on Standard, not on the default');
  await click('Next');
  await view('flow', 'quad');
  await check(txtQuad, 'quad guide retains paired-loader prerequisite');
  await click('Skip for now');
  await view('flow', 'review');
  await check(`txt('.wz-flow-count')==='Step 3 of 6'&&plan().join('|')==='Back up and update the loader'&&txt('.wz-plan small')==='Old build → Fixture build'`, 'update flow: no profile step; keeping the saved screen adds nothing to the review');
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
  await view('flow', 'quad');
  await check(txtQuad, 'quad guide retains paired-loader prerequisite');
  await click('Skip for now');
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
  await view('flow', 'quad');
  await check(txtQuad, 'quad guide retains paired-loader prerequisite');
  await click('Skip for now');
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
  await view('flow', 'quad');
  await check(txtQuad, 'quad guide retains paired-loader prerequisite');
  await click('Skip for now');
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
  await view('device');
  await check(`djProfilePage.pending()===null&&document.querySelector('.wz-status').dataset.tone==='keep'`, 'Cancel goes back to the USB and drops the draft');
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
  await click('Install XZ Mods'); await view('flow', 'profile'); await click('Skip for now'); await view('flow', 'look'); await click('Next'); await view('flow', 'quad'); await click('Skip for now'); await view('flow', 'review');
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

  await open('builds');
  await view('device');
  await waitFor(`fx.selections.length===1&&activeJob===null&&document.querySelector('.wz-build-note')`, 'older included build follows the newest');
  await check(`fx.selections[0].mod_version_id==='v1'&&txt('.wz-build-note')==='Switched to the newest build, which came with this app.'&&txt('.wz-build-name')==='Development build newsha12'`, 'a saved older included build follows the newest build, said in the build line');
  await click('Change');
  await check(`txt('#page-title')==='XZ Mods version'&&document.querySelector('nav button.selected').dataset.page==='tools'&&document.getElementById('back-to-tools').checkVisibility()`, 'Change in the build line opens Tools › XZ Mods version');

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
  await evaluate(`gameSwitch('beat-arcade').click()`);
  await check(`setupWizard.state.settings.games_menu===62&&setupWizard.settingsChanges().join()==='games_menu'&&plan().includes('Change which games show in the GAMES menu')`, 'switching off Beat Arcade is a settings change the review lists');

  await evaluate(`document.querySelector('nav button[data-page=tools]').click()`);
  await check(`txt('#page-title')==='Tools'&&[...document.querySelectorAll('#tools .tool-row strong')].map(s=>s.textContent).join('|')==='DJ Profile|Repair library|XZ Mods version|Network updates|App updates|About'&&[...document.querySelectorAll('#tools .tool-group-title')].map(h=>h.textContent).join('|')==='Your USB|Your XZ|This app'&&document.querySelectorAll('#tools .tool-row .tool-chevron').length===6&&!document.getElementById('back-to-tools').checkVisibility()`, 'Tools is six tools with chevrons in three groups: Your USB, Your XZ, This app');
  await shot('09-tools');
  for (const [page, title] of [['dj-profile', 'DJ Profile'], ['library-repair', 'Repair library'], ['device-updates', 'Network updates'], ['advanced', 'XZ Mods version'], ['updates', 'App updates'], ['licenses', 'About']]) {
    await evaluate(`document.querySelector('nav button[data-page=tools]').click();document.querySelector('.tool-row[data-open="${page}"]').click()`);
    await check(`!document.getElementById('${page}').classList.contains('hidden')&&txt('#page-title')==='${title}'&&document.querySelector('nav button.selected').dataset.page==='tools'&&document.getElementById('back-to-tools').checkVisibility()`, 'Tools › ' + title + ' opens with a back link and Tools selected');
  }
  await evaluate(`document.getElementById('back-to-tools').click()`);
  await check(`!document.getElementById('tools').classList.contains('hidden')&&txt('#page-title')==='Tools'`, 'the back link returns to Tools');
  await evaluate(`showPage('advanced')`);
  await check(`document.getElementById('mod-version')&&document.getElementById('setup-games-usb')&&document.getElementById('build')&&document.getElementById('loader-backups')&&!document.getElementById('prepare-usb')&&!document.querySelector('#advanced .intro')&&txt('#advanced .game-downloads summary')==='Game downloads'&&!document.querySelector('#advanced .game-downloads').open&&!document.getElementById('download-myhouse')&&!txt('#advanced').includes('My House')`, 'XZ Mods version page: build panel, loader restore, firmware file, games in one folded section; no Prepare a new USB');
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
  await evaluate(`showPage('tools')`);
  await check(`document.querySelector('.tool-row[data-open=updates] .mac-only').checkVisibility()&&txt('.tool-row[data-open=updates] .mac-only')==='Mac versions download from the release page'&&!document.querySelector('.tool-row[data-open=updates] .win-only').checkVisibility()`, 'Mac: the Tools row says the same in short');


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
