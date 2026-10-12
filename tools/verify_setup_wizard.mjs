import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, rm, readFile, writeFile, mkdir } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { serveUi } from './csp_server.mjs';

// Drives ui/index.html in headless Edge over CDP with a mocked Tauri invoke, served with the app's real CSP. No packages needed.
// Run: node tools/verify_setup_wizard.mjs   (XZ_TEST_BROWSER overrides the browser path)
const app = resolve(dirname(fileURLToPath(import.meta.url)), '..');

function installMock() {
  const mode = location.hash.slice(1) || 'fresh';
  const hasLoader = ['loader', 'unknown', 'downgrade', 'nonotes'].includes(mode);
  const schema = mode === 'gamesmenu' ? { ...window.__SCHEMA__, fields: [...window.__SCHEMA__.fields, { key: 'games_menu', label: 'Games menu', group: 'extras', default: 63, min: 0, max: 63, kind: 'int', readonly: false }] } : window.__SCHEMA__;
  const defaults = Object.fromEntries(schema.fields.map(field => [field.key, field.default]));
  const OLD = '1111111aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', NEW = '2222222bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
  const kinds = ['fixed', 'new', 'new', 'fixed', 'improved', 'new', 'fixed', 'improved', 'new'];
  Object.defineProperty(window, 'MOD_CHANGES', { writable: false, value: { format: 'xz-mod-changes-ui/1',
    changes: Object.fromEntries(kinds.map((kind, index) => ['c' + (index + 1), { kind, area: ['Stems', 'Games', 'Waveform'][index % 3], text: 'Fixture change ' + (index + 1) }])),
    builds: { [OLD]: ['c1'], [NEW]: kinds.map((kind, index) => 'c' + (kinds.length - index)) } } });
  const recorded = { loader: { id: 'old', label: 'Old build', runtime_sha256: 'oldsha', source_commit: OLD.slice(0, 10) }, nonotes: { id: 'old', label: 'Old build', runtime_sha256: 'oldsha', source_commit: OLD },
    downgrade: { id: 'new', label: 'New build', runtime_sha256: 'nsha', source_commit: NEW } }[mode] || null;
  const savedCommit = { downgrade: OLD, nonotes: 'abcdef1234567' }[mode] || NEW;
  const fx = window.fx = { mode, calls: [], requests: [], saved: null, written: [], recorded, games: { doom: false, chex: hasLoader, freedoom: false, myhouse: false } };  // myhouse: an older backend may still list it; the UI must never offer it
  const usb = { volume: 'E:\\', label: 'KINGSTON', removable: true, loader: hasLoader, settings: hasLoader, rekordbox: true, games: hasLoader ? 1 : 0 };
  const gamesOnUsb = () => Object.entries(fx.games).map(([id, installed]) => ({ id, name: { doom: 'Doom shareware', chex: 'Chex Quest', freedoom: 'Freedoom Phase 2', myhouse: 'My House' }[id], installed, folder: 'E:\\VJTOOLS\\Games\\' + id, requires_gzdoom: id === 'myhouse', bytes: 2450688 }));
  const inspection = () => ({ format: 'xz-mods-usb/1', volume: 'E:\\', identity: { serial: 1 }, loader: { exists: usb.loader, bytes: 4100000, sha256: 'abc' },
    recorded_mod_version: usb.loader ? fx.recorded : null, installed_version: null, runtime_sha256: 'newsha',
    settings: { editable: true, values: { ...defaults, theme: 2 }, revision: { exists: usb.settings, bytes: 400 }, schema, path: 'E:\\VJ.Tools\\XZ-Mods.cfg' }, backups: [], games: gamesOnUsb(), direct_usb_root: true, requires_copy_to_usb_root: false, filesystem: 'FAT32',
    space: { free_bytes: 29100000000, total_bytes: 32000000000 }, stems: { tracks: hasLoader ? 12 : 0, bytes: 734003200 }, rekordbox: true });
  const status = request => {
    switch (request.method) {
      case 'list_mod_versions': return { running: false, ok: true, result: { format: 'xz-mod-versions/1', versions: [{ id: 'v1', label: 'Fixture build', runtime_sha256: 'newsha', hardware_qualified: false, source_commit: savedCommit }], selected: { id: mode === 'noversion' ? 'missing' : 'v1', pinned: true, revision: 1 }, bundled_id: 'v1' } };
      case 'discover_usbs': return { running: false, ok: true, result: { format: 'xz-usbs/1', usbs: mode === 'nousb' ? [] : [usb] } };
      case 'inspect_usb': return { running: false, ok: true, result: inspection() };
      case 'inspect_mod_settings': return { running: false, ok: true, result: { format: 'xz-mod-settings/1', mod_version_id: 'v1', settings_change: null } };
      case 'prepare_usb': case 'update_usb': {
        fx.written.push(request); usb.loader = true; usb.settings = true; fx.recorded = { id: 'v1', label: 'Fixture build', runtime_sha256: 'newsha', source_commit: savedCommit };
        const games = (request.games || []).map(id => { fx.games[id] = true; return { game: id, name: id, ok: true }; });
        return { running: false, ok: true, result: { format: request.method === 'update_usb' ? 'xz-mods-loader-updated/1' : 'xz-mods-usb-prepared/1', image: 'E:\\autoexec.bin', backup: 'E:\\VJ.Tools\\backup', game_setup: { format: 'xz-games-usb/1', games, failed_count: 0 }, inspection: inspection() } };
      }
      case 'prepare_games_usb': { fx.written.push(request); const games = request.games.map(id => { fx.games[id] = true; return { game: id, name: id, ok: true }; }); return { running: false, ok: true, result: { format: 'xz-games-usb/1', games, failed_count: 0 } }; }
      case 'read_usb_settings': return { running: false, ok: true, result: { identity: { serial: 1 }, settings: inspection().settings } };
      case 'read_dj_profiles': return { running: false, ok: true, result: { format: 'xz-dj-profiles/1', public_folder: null, revision: null, active: null, stick_colour: '#f870f8', legacy: false, profiles: [], problems: [] } };
      case 'save_usb_settings': fx.saved = request; fx.written.push(request); return { running: false, ok: true, result: { format: 'xz-mods-settings-saved/1', settings: { ...inspection().settings, values: request.values } } };
    }
    throw Error('Unexpected method ' + request.method);
  };
  window.__TAURI__ = { core: { invoke: async (command, args) => {
    fx.calls.push(command);
    if (command === 'app_information') return { version: '0.3.0', platform: 'windows', installed: true, release_notes: { releases: [] } };
    if (command === 'check_app_update') return { phase: 'current', message: 'Up to date' };
    if (command === 'start_job') { fx.requests.push(args.request); return fx.requests.length; }
    if (command === 'job_status') return status(fx.requests[args.id - 1]);
    if (command === 'cancel_job') return {};
    if (command === 'pick_path') return 'F:\\';
    throw Error('Unexpected command ' + command);
  } } };
  fx.csp = [];
  document.addEventListener('securitypolicyviolation', event => fx.csp.push(event.violatedDirective + ' ' + (event.blockedURI || 'inline') + ' ' + (event.sourceFile || '').split('/').pop() + ':' + event.lineNumber + ':' + event.columnNumber));
  window.button = (text, root = document) => [...root.querySelectorAll('button')].find(item => item.textContent.trim() === text && item.checkVisibility() && !item.closest('nav'));
  window.row = id => document.querySelector('.tree-row[data-id="' + id + '"]');
  window.rowTxt = id => row(id)?.textContent.replace(/\s+/g, ' ').trim() || '';
  window.badge = id => row(id)?.querySelector('.tree-badge').textContent || '';
  window.box = id => row(id)?.querySelector('input[type=checkbox]') || null;
  window.game = id => document.querySelector('.page:not(.hidden) .games-row[data-id="' + id + '"]');
  window.gameBox = id => game(id)?.querySelector('.tree-check input') || null;
  window.gameSwitch = id => game(id)?.querySelector('.games-switch input') || null;
  window.muItems = () => [...document.querySelectorAll('.tree-aside .mu-item')].length;
  window.txt = selector => document.querySelector(selector)?.textContent.replace(/\s+/g, ' ').trim() || '';
}

const schema = JSON.parse(await readFile(resolve(app, process.env.XZ_SETTINGS_SCHEMA || '../VJToolsLibrary/packages/xdj-xz-toolkit/mods/settings-schema.json'), 'utf8'));
const server = await serveUi(app);
const profile = await mkdtemp(join(tmpdir(), 'xz-wizard-'));
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
  const errors = [];
  const pending = new Map();
  socket.onmessage = event => { const data = JSON.parse(event.data), task = pending.get(data.id); if (!task) return; pending.delete(data.id); data.error ? task.reject(Error(JSON.stringify(data.error))) : task.resolve(data.result); };
  const send = (method, params = {}) => new Promise((resolve, reject) => { const id = ++sequence; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); setTimeout(() => reject(Error('CDP timed out: ' + method)), 10000); });
  const evaluate = async expression => {
    const value = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    if (value.exceptionDetails) throw Error(expression.slice(0, 120) + ': ' + (value.exceptionDetails.exception?.description || value.exceptionDetails.text));
    return value.result.value;
  };
  const context = () => evaluate(`'step: '+setupWizard.state.step+' job: '+activeJob+' requests: '+fx.requests.map(r=>r.method).join(',')+'\\n  pane: '+txt('.wz-pane').slice(0,300)`);
  const waitFor = async (expression, label) => {
    for (let attempt = 0; attempt < 200; attempt++) { if (await evaluate(expression)) return; await new Promise(done => setTimeout(done, 25)); }
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
  await send('Emulation.setDeviceMetricsOverride', { width: 1100, height: 760, deviceScaleFactor: 1, mobile: false });
  const cspClean = async () => {
    const violations = await evaluate(`window.fx?.csp||[]`);
    assert.deepEqual(violations.slice(0, 5), [], 'CSP violations (' + violations.length + ')');
  };
  const open = async mode => {
    await cspClean();
    await send('Page.navigate', { url: 'about:blank' });
    await new Promise(done => setTimeout(done, 50));
    await send('Page.navigate', { url: server.url + 'index.html#' + mode });
    await waitFor(`document.readyState==='complete'&&window.setupWizard&&modInventory!==null`, 'page loaded (' + mode + ')');
    errors.length = 0;
  };
  const click = async text => { assert(await evaluate(`Boolean(button(${JSON.stringify(text)}))`), 'button "' + text + '" is visible\n  ' + await context()); await evaluate(`button(${JSON.stringify(text)}).click()`); };
  // XZ_WIZARD_SHOTS=<dir> saves a screenshot at each named point for a visual review.
  const shots = process.env.XZ_WIZARD_SHOTS; if (shots) await mkdir(shots, { recursive: true });
  const shot = async name => { if (!shots) return; await new Promise(done => setTimeout(done, 120)); const image = await send('Page.captureScreenshot', { format: 'png' }); await writeFile(join(shots, name + '.png'), Buffer.from(image.data, 'base64')); };
  const wide = async name => {
    await send('Emulation.setDeviceMetricsOverride', { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
    await shot(name);
    await send('Emulation.setDeviceMetricsOverride', { width: 1100, height: 760, deviceScaleFactor: 1, mobile: false });
  };
  const step = async (name) => waitFor(`setupWizard.state.step===${JSON.stringify(name)}&&activeJob===null`, 'step ' + name);

  await open('fresh');
  await check(`document.querySelector('nav button.selected').dataset.page==='setup'&&txt('#page-title')==='Set up USB'`, 'wizard is the first page');
  await check(`[...document.querySelectorAll('nav button')].map(b=>b.dataset.page).join()==='setup,dj-profile,stems,library-repair,device-updates,advanced,updates,licenses'&&!document.getElementById('games')`, 'nav: no Games page; DJ Profile right after Set up USB');
  await check(`[...document.querySelectorAll('.wz-step')].map(b=>b.textContent.replace(/^\\d/,'')).join()==='USB,Loadout,DJ Profile,Write,Done'&&document.querySelector('.wz-step[data-state=current]').textContent.includes('USB')`, 'five steps: no NonEssentials or Settings step, USB current');
  await click('Find my USB');
  await waitFor(`document.querySelector('.wz-usb')`, 'USB card appears');
  await check(`txt('.wz-usb').includes('KINGSTON')&&txt('.wz-usb').includes('No loader yet')`, 'USB card says no loader yet');
  await shot('00-usb');
  await evaluate(`document.querySelector('.wz-usb').click()`);
  await step('loadout');
  await check(`txt('.wz-pane h2')==='What is on this USB'&&document.querySelector('.tree[data-mode=edit]')`, 'choosing the USB opens the loadout tree');
  await check(`row('loader').dataset.state==='missing'&&box('loader').checked&&badge('loader')==='Will add'&&row('loader').dataset.plan==='1'`, 'fresh: loader not on USB, ticked, Will add');
  await check(`rowTxt('drive').includes('KINGSTON')&&rowTxt('drive').includes('FAT32')&&rowTxt('drive').includes('29.1 GB free of 32 GB')`, 'drive row shows label, filesystem and free space');
  await check(`rowTxt('rekordbox').includes('Never touched')&&!box('rekordbox')&&!row('rekordbox').querySelector('button')`, 'Rekordbox row reads never touched, with no control');
  await check(`rowTxt('stems').includes('None yet')&&badge('stems')==='Not on USB'&&button('Prepare stems',row('stems'))`, 'stems row: none yet, one link to Prepare stems');
  await check(`rowTxt('loader').includes('Fixture build')&&txt('.tree-aside').includes('Not hardware-verified')&&!txt('.tree-aside').includes('Fixture build')`, 'loader box names the saved version; the line under it does not repeat it');
  await check(`['doom','chex','freedoom'].every(id=>row('game-'+id)&&!box('game-'+id)&&badge('game-'+id)==='Will add')&&!row('game-myhouse')&&!txt('#wiz').includes('My House')`, 'fresh USB: every working free game is planned by default; My House is gone even when the backend lists it');
  await check(`button('Choose games',row('games'))&&!button('Edit settings')`, 'Games folder links to Choose games; there is no settings editor');
  await check(`document.querySelector('.tree').scrollWidth<=document.querySelector('.tree').clientWidth&&document.documentElement.scrollWidth<=innerWidth`, 'tree fits without horizontal scroll');
  await check(`['drive','loader','settings','game-doom'].every(id=>getComputedStyle(row(id)).gridTemplateColumns.split(' ').length===5)&&getComputedStyle(row('game-doom')).gridTemplateColumns.startsWith('44px')`, 'tree rows lay out as five grid tracks with the depth indent (CSP keeps the --depth style)');
  await check(`txt('.tree-legend').includes('Up to date')&&txt('.tree-legend').includes('Will be written')`, 'legend explains the dots');
  await shot('01-loadout');
  await wide('01b-loadout-wide');
  await click('Choose games');
  await step('write');
  await check(`document.querySelector('.wz-custom').open&&txt('.wz-custom-title')==='Custom installation'&&txt('.wz-custom-summary')==='3 free games will be added'`, 'Choose games opens the Write step with Custom installation expanded and a one-line summary');
  await check(`txt('.wz-custom .games-builtin-line')==='Beat Arcade, Wave Rider, Rhythm Lab and Static Surfer are built into the loader and always come with it.'&&!document.querySelector('.wz-custom .games-group[data-kind=builtin]')`, 'built-in games without games_menu: one line, no rows that cannot be ticked');
  await check(`(()=>{const rows=[...document.querySelectorAll('.wz-custom .games-group[data-kind=download] .games-row')];return rows.map(r=>r.dataset.id).join()==='doom,chex,freedoom'&&rows.every(r=>r.querySelector('.tree-check input').checked)&&txt('.wz-custom .games-row[data-id=doom]').includes('Add · 2.3 MB');})()`, 'Custom installation: the three working downloads, all ticked, with sizes');
  await evaluate(`gameBox('chex').click()`);
  await check(`setupWizard.newGames().join()==='doom,freedoom'&&!row('game-chex').dataset.plan&&row('game-chex').dataset.dim==='1'&&txt('.wz-custom-summary')==='2 free games will be added'&&document.querySelector('.wz-custom').open`, 'unticking Chex Quest takes it off the plan; the summary follows and the panel stays open');
  await check(`document.querySelector('.wz-custom .games').scrollWidth<=document.querySelector('.wz-custom .games').clientWidth&&document.documentElement.scrollWidth<=innerWidth`, 'custom installation fits without horizontal scroll');
  await shot('02-write-custom');
  await wide('02b-write-custom-wide');
  await click('Back');
  await step('profile');
  await click('Back');
  await step('loadout');
  await check(`!row('game-chex').dataset.plan&&row('game-doom').dataset.plan==='1'&&badge('game-doom')==='Will add'&&!box('game-doom')`, 'loadout tree follows the custom choice');
  await evaluate(`box('loader').click()`);
  await check(`setupWizard.loaderChoice()==='keep'&&!row('loader').dataset.plan&&!box('loader').checked&&badge('loader')==='Not on USB'`, 'unticking the loader keeps it off the plan');
  await evaluate(`box('loader').click()`);
  await check(`setupWizard.loaderChoice()==='add'&&badge('loader')==='Will add'`, 'ticking it again plans the add');
  await click('Next');
  await step('profile');
  await waitFor(`document.querySelector('#wz-djp-slot #djp-name')`, 'DJ Profile step editor');
  await check(`txt('.wz-pane h2')==='Your DJ profile'&&Boolean(button('Skip for now'))&&fx.requests.filter(r=>r.method==='read_dj_profiles'&&r.volume==='E:\\\\').length===1&&document.querySelector('.djp-swatch[data-hex="#f870f8"]').getAttribute('aria-pressed')==='true'`, 'DJ Profile step: reads the USB once, opens the editor in its rekordbox colour, Skip for now');
  await shot('01c-profile-step');
  await wide('01d-profile-step-wide');
  await click('Skip for now');
  await step('write');
  await check(`(()=>{const planned=[...document.querySelectorAll('.tree-row[data-plan="1"]')].map(r=>r.dataset.id).sort().join();const rest=[...document.querySelectorAll('.tree-row:not([data-plan])')];return planned==='game-doom,game-freedoom,loader,settings'&&rest.length>0&&rest.every(r=>r.dataset.dim==='1');})()`, 'write tree highlights loader, settings defaults and the two games; everything else is dimmed');
  await check(`!document.querySelector('.tree input')&&badge('loader')==='Will add'&&badge('settings')==='Will add'&&txt('.wz-lead').includes('never touched')`, 'write tree has no controls and says what is never touched');
  await click('Write to USB');
  await step('done');
  await check(`fx.written[0].method==='prepare_usb'&&JSON.stringify(fx.written[0].games)==='["doom","freedoom"]'&&!fx.saved`, 'prepare_usb carries the chosen games; no separate settings write');
  await check(`txt('.wz-pane h2')==='Your USB is ready'&&txt('.wz-done').includes('Loader written')&&txt('.wz-done').includes('Games on USB: doom, freedoom.')`, 'done step reports loader and games');
  await check(`document.querySelector('.tree[data-mode=result]')&&row('game-doom').dataset.state==='present'&&row('game-chex').dataset.state==='missing'&&row('loader').dataset.state==='current'&&!document.querySelector('.tree [data-plan]')`, 'done tree shows Doom on the USB, Chex left out, the loader up to date');
  await check(`txt('.wz-next').includes('REKORDBOX')`, 'done step tells the DJ how to open MODS on the XZ');

  await open('loader');
  await click('Find my USB');
  await waitFor(`document.querySelector('.wz-usb')`, 'USB card appears');
  await check(`txt('.wz-usb').includes('XZ Mods loader')&&txt('.wz-usb').includes('1 game on USB')`, 'card shows loader and game count');
  await evaluate(`document.querySelector('.wz-usb').click()`);
  await step('loadout');
  await check(`row('loader').dataset.state==='outdated'&&box('loader').checked&&badge('loader')==='Will update · 8 changes'&&rowTxt('loader').includes('Old build')`, 'older loader: Update available, ticked, Will update');
  await check(`!box('game-chex')&&badge('game-chex')==='On USB'&&badge('game-doom')==='Not on USB'&&!row('game-doom').dataset.plan`, 'a USB that has the loader adds no games by default');
  await check(`rowTxt('stems').includes('12 tracks')&&rowTxt('stems').includes('700 MB')`, 'stems row counts prepared tracks');
  await check(`txt('.tree-aside .mu h4')==='What changes on your XZ'&&txt('.tree-aside .mu-head small')==='Old build → Fixture build'`, 'update preview under the loader row names both versions');
  await check(`[...document.querySelectorAll('.tree-aside .mu-group h5')].map(h=>h.textContent).join()==='New,Improved,Fixed'&&muItems()===6&&button('Show all 8 changes')`, 'preview groups New, Improved, Fixed, shows six, offers Show all 8');
  await check(`document.querySelector('.tree').scrollWidth<=document.querySelector('.tree').clientWidth&&document.documentElement.scrollWidth<=innerWidth`, 'tree with the preview fits without horizontal scroll');
  await shot('06-loadout-update-changelog');
  await click('Show all 8 changes');
  await check(`muItems()===8&&!button('Show all 8 changes')`, 'Show all expands the preview in place');
  await shot('07-changelog-expanded');
  await click('Next'); await step('profile'); await click('Skip for now'); await step('write');
  await check(`badge('loader')==='Will update · 8 changes'&&!document.querySelector('.tree .mu')&&!document.querySelector('.wz-custom').open&&txt('.wz-custom-summary')==='No games will be added · 1 game on the USB kept'`, 'write step: loader badge counts the changes; Custom installation starts closed with its summary');
  await shot('10-write');
  await evaluate(`setupWizard.goto('loadout')`);
  await step('loadout');
  await evaluate(`box('loader').click()`);
  await check(`setupWizard.loaderChoice()==='keep'&&badge('loader')==='Update available'&&muItems()===8`, 'unticking keeps the loader; the expanded preview survives the re-render');
  await click('Next'); await step('profile'); await click('Skip for now'); await step('write');
  await evaluate(`document.querySelector('.wz-custom summary').click()`);
  await check(`document.querySelector('.wz-custom').open&&!gameBox('chex')&&txt('.wz-custom .games-row[data-id=chex] .tree-badge')==='On USB'`, 'opening Custom installation: the installed game reads On USB with no box');
  await click('Add all free games');
  await check(`setupWizard.newGames().join()==='doom,freedoom'&&[...document.querySelectorAll('.tree-row[data-plan="1"]')].map(r=>r.dataset.id).join()==='game-doom,game-freedoom'&&row('loader').dataset.dim==='1'&&row('settings').dataset.dim==='1'`, 'Add all plans the two missing games; loader and settings stay dimmed');
  await click('Write to USB');
  await step('done');
  await check(`fx.written.length===1&&fx.written[0].method==='prepare_games_usb'&&JSON.stringify(fx.written[0].games)==='["doom","freedoom"]'`, 'keeping the loader writes only the games');

  await open('loader');
  await click('Find my USB');
  await waitFor(`document.querySelector('.wz-usb')`, 'USB card appears');
  await evaluate(`document.querySelector('.wz-usb').click()`);
  await step('loadout');
  await evaluate(`box('loader').click()`);
  await click('Next'); await step('profile'); await click('Skip for now'); await step('write');
  await check(`button('Write to USB').disabled&&txt('.wz-pane').includes('Nothing to write')`, 'Write is disabled when nothing changes');

  // No USB found and no saved mod version both end in one clear next action.
  await open('nousb');
  await click('Find my USB');
  await waitFor(`txt('.wz-pane').includes('No USB drive was found')`, 'no-USB notice');
  await check(`button('Look again')&&button('Choose a folder instead')`, 'no USB: Look again and folder fallback');
  await open('noversion');
  await click('Find my USB');
  await waitFor(`document.querySelector('.wz-usb')`, 'USB card appears');
  await evaluate(`document.querySelector('.wz-usb').click()`);
  await step('loadout');
  await check(`box('loader').disabled&&!row('loader').dataset.plan&&button('Choose mod version')&&button('Next').disabled`, 'missing mod version: loader box disabled, nothing planned, one button into the chooser, Next held');
  await shot('12-loadout-noversion');

  const chooseFirstUsb = async () => {
    await click('Find my USB');
    await waitFor(`document.querySelector('.wz-usb')`, 'USB card appears');
    await evaluate(`document.querySelector('.wz-usb').click()`);
    await step('loadout');
  };
  await open('unknown');
  await chooseFirstUsb();
  await check(`row('loader').dataset.state==='unknown'&&txt('.tree-aside .mu-head small')==='Unknown version → Fixture build'&&txt('.mu-note').includes('is not known')&&txt('.mu-note').includes('Here is what Fixture build brings')`, 'unknown loader version: the preview says so');
  await check(`muItems()===6&&button('Show all 9 changes')`, 'unknown loader version: lists every change in the target build');
  await shot('08-unknown-version');

  await open('downgrade');
  await chooseFirstUsb();
  await check(`txt('.tree-aside .mu-head small')==='New build → Fixture build'&&txt('.mu-group[data-group=removed] h5')==='Not in Fixture build'&&document.querySelectorAll('.mu-group[data-group=removed] .mu-item').length===8&&document.querySelectorAll('.mu-group').length===1`, 'downgrade: Not in Fixture build lists the eight changes the DJ loses');
  await shot('09-downgrade');

  await open('nonotes');
  await chooseFirstUsb();
  await check(`txt('.tree-aside .mu-quiet')==='Notes for Fixture build are not available yet.'&&!document.querySelector('.mu')`, 'target build without notes: one quiet line');

  await open('gamesmenu');
  await chooseFirstUsb();
  await click('Choose games');
  await step('write');
  await check(`[...document.querySelectorAll('.wz-custom .games-group[data-kind=builtin] .games-switch input')].map(input=>input.closest('.games-row').dataset.id).join()==='beat-arcade,wave-rider,rhythm-lab,static-surfer'&&txt('.wz-custom .games-group[data-kind=builtin] .games-note')==='Built into the loader.'&&gameSwitch('beat-arcade').checked&&!document.querySelector('.wz-custom .games-builtin-line')`, 'games_menu in the schema: built-in games become rows with a Show in GAMES menu switch');
  await check(`gameSwitch('doom')?.checked&&gameSwitch('doom').dataset.bit==='4'`, 'a download headed for the USB gets its menu switch too');
  await evaluate(`gameSwitch('beat-arcade').click()`);
  await check(`setupWizard.state.settings.games_menu===62&&setupWizard.settingsChanges().join()==='games_menu'&&!gameSwitch('beat-arcade').checked`, 'switching off Beat Arcade clears bit 0 and counts as a settings change');
  await check(`badge('settings')==='Will change · 1'&&rowTxt('settings').includes('Games menu')`, 'write step: the settings row counts the games menu change');
  await shot('03-custom-switches');

  // Advanced tools keeps the old controls reachable, without My House.
  await evaluate(`showPage('advanced')`);
  await check(`txt('#page-title')==='Advanced tools'&&document.getElementById('mod-version')&&document.getElementById('setup-games-usb')&&document.getElementById('build')&&!document.getElementById('download-myhouse')&&!txt('#advanced').includes('My House')`, 'Advanced tools holds mod version, game downloads and the firmware-file build, and no My House');
  assert.deepEqual(errors, [], 'no page errors: ' + errors.join(' / '));
  await cspClean();
  await check(`(()=>{const probe=document.createElement('div');probe.setAttribute('style','color:rgb(255, 0, 0)');document.body.append(probe);const blocked=getComputedStyle(probe).color!=='rgb(255, 0, 0)';probe.remove();return location.protocol==='http:'&&blocked;})()`, 'the page runs under the app CSP: a style attribute is blocked, as in the installed app');
  console.log('\n' + passed + ' checks passed');
} finally {
  socket?.close();
  await server.close();
  child.kill();
  await new Promise(done => setTimeout(done, 300));
  await rm(profile, { recursive: true, force: true }).catch(() => {});
}
