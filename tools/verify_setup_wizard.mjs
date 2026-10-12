import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, rm, readFile, writeFile, mkdir } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

// Drives ui/index.html in headless Edge over CDP with a mocked Tauri invoke. No packages needed.
// Run: node tools/verify_setup_wizard.mjs   (XZ_TEST_BROWSER overrides the browser path)
const app = resolve(dirname(fileURLToPath(import.meta.url)), '..');

function installMock() {
  const mode = location.hash.slice(1) || 'fresh';
  const schema = window.__SCHEMA__;
  const defaults = Object.fromEntries(schema.fields.map(field => [field.key, field.default]));
  const fx = window.fx = { mode, calls: [], requests: [], saved: null, written: [], recorded: mode === 'loader' ? { id: 'old', label: 'Old build', runtime_sha256: 'oldsha' } : null, games: { doom: false, chex: mode === 'loader', freedoom: false, myhouse: false } };
  const usb = { volume: 'E:\\', label: 'KINGSTON', removable: true, loader: mode === 'loader', settings: mode === 'loader', rekordbox: true, games: mode === 'loader' ? 1 : 0 };
  const gamesOnUsb = () => Object.entries(fx.games).map(([id, installed]) => ({ id, name: { doom: 'Doom shareware', chex: 'Chex Quest', freedoom: 'Freedoom Phase 2', myhouse: 'My House' }[id], installed, folder: 'E:\\VJTOOLS\\Games\\' + id, requires_gzdoom: id === 'myhouse', bytes: 2450688 }));
  const inspection = () => ({ format: 'xz-mods-usb/1', volume: 'E:\\', identity: { serial: 1 }, loader: { exists: usb.loader, bytes: 4100000, sha256: 'abc' },
    recorded_mod_version: usb.loader ? fx.recorded : null, installed_version: null, runtime_sha256: 'newsha',
    settings: { editable: true, values: { ...defaults, theme: 2 }, revision: { exists: usb.settings, bytes: 400 }, schema, path: 'E:\\VJ.Tools\\XZ-Mods.cfg' }, backups: [], games: gamesOnUsb(), direct_usb_root: true, requires_copy_to_usb_root: false, filesystem: 'FAT32',
    space: { free_bytes: 29100000000, total_bytes: 32000000000 }, stems: { tracks: mode === 'loader' ? 12 : 0, bytes: 734003200 }, rekordbox: true });
  const status = request => {
    switch (request.method) {
      case 'list_mod_versions': return { running: false, ok: true, result: { format: 'xz-mod-versions/1', versions: [{ id: 'v1', label: 'Fixture build', runtime_sha256: 'newsha', hardware_qualified: false }], selected: { id: mode === 'noversion' ? 'missing' : 'v1', pinned: true, revision: 1 }, bundled_id: 'v1' } };
      case 'discover_usbs': return { running: false, ok: true, result: { format: 'xz-usbs/1', usbs: mode === 'nousb' ? [] : [usb] } };
      case 'inspect_usb': return { running: false, ok: true, result: inspection() };
      case 'inspect_mod_settings': return { running: false, ok: true, result: { format: 'xz-mod-settings/1', mod_version_id: 'v1', settings_change: null } };
      case 'prepare_usb': case 'update_usb': {
        fx.written.push(request); usb.loader = true; usb.settings = true; fx.recorded = { id: 'v1', label: 'Fixture build', runtime_sha256: 'newsha' };
        const games = (request.games || []).map(id => { fx.games[id] = true; return { game: id, name: id, ok: true }; });
        return { running: false, ok: true, result: { format: request.method === 'update_usb' ? 'xz-mods-loader-updated/1' : 'xz-mods-usb-prepared/1', image: 'E:\\autoexec.bin', backup: 'E:\\VJ.Tools\\backup', game_setup: { format: 'xz-games-usb/1', games, failed_count: 0 }, inspection: inspection() } };
      }
      case 'prepare_games_usb': { fx.written.push(request); const games = request.games.map(id => { fx.games[id] = true; return { game: id, name: id, ok: true }; }); return { running: false, ok: true, result: { format: 'xz-games-usb/1', games, failed_count: 0 } }; }
      case 'read_usb_settings': return { running: false, ok: true, result: { identity: { serial: 1 }, settings: inspection().settings } };
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
  window.button = (text, root = document) => [...root.querySelectorAll('button')].find(item => item.textContent.trim() === text && item.checkVisibility() && !item.closest('nav'));
  window.row = id => document.querySelector('.tree-row[data-id="' + id + '"]');
  window.rowTxt = id => row(id)?.textContent.replace(/\s+/g, ' ').trim() || '';
  window.badge = id => row(id)?.querySelector('.tree-badge').textContent || '';
  window.box = id => row(id)?.querySelector('input[type=checkbox]') || null;
  window.txt = selector => document.querySelector(selector)?.textContent.replace(/\s+/g, ' ').trim() || '';
}

const schema = JSON.parse(await readFile(resolve(app, process.env.XZ_SETTINGS_SCHEMA || '../VJToolsLibrary/packages/xdj-xz-toolkit/mods/settings-schema.json'), 'utf8'));
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
  const open = async mode => {
    await send('Page.navigate', { url: 'about:blank' });
    await new Promise(done => setTimeout(done, 50));
    await send('Page.navigate', { url: 'file:///' + resolve(app, 'ui/index.html').replace(/\\/g, '/') + '#' + mode });
    await waitFor(`document.readyState==='complete'&&window.setupWizard&&modInventory!==null`, 'page loaded (' + mode + ')');
    errors.length = 0;
  };
  const click = async text => { assert(await evaluate(`Boolean(button(${JSON.stringify(text)}))`), 'button "' + text + '" is visible\n  ' + await context()); await evaluate(`button(${JSON.stringify(text)}).click()`); };
  // XZ_WIZARD_SHOTS=<dir> saves a screenshot at each named point for a visual review.
  const shots = process.env.XZ_WIZARD_SHOTS; if (shots) await mkdir(shots, { recursive: true });
  const shot = async name => { if (!shots) return; await new Promise(done => setTimeout(done, 120)); const image = await send('Page.captureScreenshot', { format: 'png' }); await writeFile(join(shots, name + '.png'), Buffer.from(image.data, 'base64')); };
  const step = async (name) => waitFor(`setupWizard.state.step===${JSON.stringify(name)}&&activeJob===null`, 'step ' + name);

  await open('fresh');
  await check(`document.querySelector('nav button.selected').dataset.page==='setup'&&txt('#page-title')==='Set up USB'`, 'wizard is the first page');
  await check(`[...document.querySelectorAll('nav button')].map(b=>b.dataset.page).join()==='setup,stems,library-repair,device-updates,advanced,updates,licenses'`, 'nav: USB loader, USB settings and Games pages are gone');
  await check(`[...document.querySelectorAll('.wz-step')].map(b=>b.textContent.replace(/^\\d/,'')).join()==='USB,Loadout,Settings,Write,Done'&&document.querySelector('.wz-step[data-state=current]').textContent.includes('USB')`, 'five steps, Loadout second, USB current');
  await click('Find my USB');
  await waitFor(`document.querySelector('.wz-usb')`, 'USB card appears');
  await check(`txt('.wz-usb').includes('KINGSTON')&&txt('.wz-usb').includes('No loader yet')`, 'USB card says no loader yet');
  await shot('01-usb');
  await evaluate(`document.querySelector('.wz-usb').click()`);
  await step('loadout');
  await check(`txt('.wz-pane h2')==='What is on this USB'&&document.querySelector('.tree[data-mode=edit]')`, 'choosing the USB opens the loadout tree');
  await check(`row('loader').dataset.state==='missing'&&box('loader').checked&&badge('loader')==='Will add'&&row('loader').dataset.plan==='1'`, 'fresh: loader not on USB, ticked, Will add');
  await check(`rowTxt('drive').includes('KINGSTON')&&rowTxt('drive').includes('FAT32')&&rowTxt('drive').includes('29.1 GB free of 32 GB')`, 'drive row shows label, filesystem and free space');
  await check(`rowTxt('rekordbox').includes('Never touched')&&!box('rekordbox')&&!row('rekordbox').querySelector('button')`, 'Rekordbox row reads never touched, with no control');
  await check(`rowTxt('stems').includes('None yet')&&badge('stems')==='Not on USB'&&button('Prepare stems',row('stems'))`, 'stems row: none yet, one link to Prepare stems');
  await check(`txt('.tree-aside').includes('Fixture build')`, 'loader row names the saved mod version');
  await check(`['doom','chex','freedoom','myhouse'].every(id=>box('game-'+id)&&!box('game-'+id).checked&&badge('game-'+id)==='Not on USB')`, 'four games listed, none ticked');
  await check(`rowTxt('game-myhouse').includes('Not playable on the XZ yet')&&rowTxt('game-doom').includes('Add · 2.3 MB')`, 'game rows show size and the GZDoom caveat');
  await check(`document.querySelector('.tree').scrollWidth<=document.querySelector('.tree').clientWidth&&document.documentElement.scrollWidth<=innerWidth`, 'tree fits without horizontal scroll');
  await check(`txt('.tree-legend').includes('Up to date')&&txt('.tree-legend').includes('Will be written')`, 'legend explains the dots');
  await shot('02-loadout');
  await evaluate(`box('loader').click()`);
  await check(`setupWizard.loaderChoice()==='keep'&&!row('loader').dataset.plan&&!box('loader').checked&&badge('loader')==='Not on USB'`, 'unticking the loader keeps it off the plan');
  await evaluate(`box('loader').click()`);
  await check(`setupWizard.loaderChoice()==='add'&&badge('loader')==='Will add'`, 'ticking it again plans the add');
  await evaluate(`box('game-doom').click()`);
  await check(`row('game-doom').dataset.plan==='1'&&badge('game-doom')==='Will add'&&setupWizard.newGames().join()==='doom'`, 'ticking Doom plans only Doom');
  await shot('03-loadout-ticked');
  await click('Next');
  await step('settings');
  await check(`document.querySelector('.dv')&&document.querySelectorAll('.dv-rail-tab').length===5&&[...document.querySelectorAll('.dv-rail-tab')].map(b=>b.textContent.trim()).join()==='GENERAL,CONTROLS,APPEARANCE,ADVANCED,EXTRAS'`, 'device menu: five rail tabs in device order');
  await check(`txt('.dv-title strong')==='GENERAL'&&document.querySelector('.dv-rail-tab[aria-current=page]').textContent.trim()==='GENERAL'`, 'GENERAL opens first, like the XZ');
  await check(`txt('.dv-usb')==='USB · E:\\\\'`, 'header shows the USB');
  await check(`getComputedStyle(document.querySelector('.dv')).getPropertyValue('--dv-accent').trim()==='#54c1e6'`, 'menu is drawn in the saved theme (CYBERPUNK)');
  await check(`document.querySelector('.dv-toggle[data-key=stems]').getAttribute('aria-pressed')==='true'`, 'STEM AUDIO toggle reads the saved value');
  await evaluate(`document.querySelector('.dv-toggle[data-key=stems]').click()`);
  await check(`document.querySelector('.dv-toggle[data-key=stems]').getAttribute('aria-pressed')==='false'&&setupWizard.state.settings.stems===0`, 'toggle flips the value');
  await check(`txt('.dv-dock').includes('STEM AUDIO')&&txt('.dv-dock').includes('OFF')&&txt('.dv-dock').includes('1 change')`, 'dock names the control, its state and the pending change');
  await shot('04-settings');
  await evaluate(`document.querySelector('.dv-link[data-page=controls]').click()`);
  await check(`txt('.dv-title strong')==='CONTROLS'&&document.querySelectorAll('.dv-pad').length===8&&document.querySelector('.dv-cycle[data-key=stem_order]')`, 'STEM SETTINGS opens CONTROLS with the pad diagram and STEM ORDER');
  await evaluate(`document.querySelector('.dv-cycle[data-key=stem_order] .dv-arrow:last-child').click()`);
  await check(`setupWizard.state.settings.stem_order===1`, 'STEM ORDER arrow cycles the saved field');
  await evaluate(`document.querySelectorAll('.dv-pad')[5].click()`);
  await check(`setupWizard.state.settings.stem_bank===1&&document.querySelectorAll('.dv-pad')[4].dataset.assigned==='1'`, 'tapping pad F moves the stem shortcuts to E-H');
  await evaluate(`document.querySelector('.dv-back').click()`);
  await check(`txt('.dv-title strong')==='GENERAL'`, 'BACK returns to GENERAL');
  await evaluate(`[...document.querySelectorAll('.dv-rail-tab')][2].click()`);
  await check(`document.querySelectorAll('.dv-theme').length===24&&document.querySelector('.dv-theme[aria-checked=true]').textContent.includes('CYBERPUNK')`, 'APPEARANCE shows the 24 device themes with the saved one marked');
  await evaluate(`document.querySelectorAll('.dv-theme')[21].click()`);
  await check(`setupWizard.state.settings.theme===21&&getComputedStyle(document.querySelector('.dv')).getPropertyValue('--dv-accent').trim()==='#ffc000'`, 'choosing LCARS reskins the menu live');
  await evaluate(`[...document.querySelectorAll('.dv-rail-tab')][3].click()`);
  await check(`document.querySelector('.dv-choice[data-key=wave_mode]')&&document.querySelector('.dv-link[data-page=browse]')`, 'ADVANCED has the waveform choice and a BROWSE COLUMNS page');
  await evaluate(`document.querySelector('.dv-choice[data-key=wave_mode] [data-value="2"]').click()`);
  await check(`setupWizard.state.settings.wave_mode===2`, 'choice row sets the waveform mode');
  await evaluate(`document.querySelector('.dv-link[data-page=browse]').click()`);
  await check(`document.querySelectorAll('.dv-cycle').length===3&&document.querySelectorAll('.dv-toggle').length===2`, 'BROWSE COLUMNS edits the five browse fields the XZ reads but does not show');
  const editable = schema.fields.filter(f => !f.readonly).map(f => f.key);
  await check(`(()=>{const pages=SETTINGS_MENU_PAGES,keys=new Set(['theme','stem_bank','stem_drums_color','stem_harmonics_color','stem_vocals_color','jump_1','jump_2','jump_3','jump_4','jump_5','jump_6','jump_7','jump_8']);for(const page of Object.values(pages))for(const row of page.rows)if(['toggle','cycle','choice'].includes(row[0]))keys.add(row[1]);const missing=${JSON.stringify(editable)}.filter(k=>!keys.has(k));if(missing.length)console.error('missing',missing);return missing.length===0;})()`, 'every editable schema field has a place in the menu');
  await evaluate(`[...document.querySelectorAll('.dv-rail-tab')][4].click()`);
  await check(`document.querySelectorAll('.dv-card').length===2`, 'EXTRAS shows VJ.TOOLS and GAMES cards');
  await evaluate(`document.querySelector('.dv-card[data-target=games]').click()`);
  await step('loadout');
  await check(`txt('.wz-pane h2')==='What is on this USB'`, 'GAMES card lands on the Loadout step');
  await check(`badge('settings')==='Will change · 5'&&row('settings').dataset.plan==='1'&&box('game-doom').checked`, 'Settings row shows five pending changes; Doom stays ticked');
  await click('Next');
  await step('settings');
  await check(`setupWizard.state.settings.theme===21&&setupWizard.settingsChanges().length===5`, 'settings survive leaving and returning');
  await click('Review and write');
  await step('write');
  await check(`(()=>{const planned=[...document.querySelectorAll('.tree-row[data-plan="1"]')].map(r=>r.dataset.id).sort().join();const rest=[...document.querySelectorAll('.tree-row:not([data-plan])')];return planned==='game-doom,loader,settings'&&rest.length>0&&rest.every(r=>r.dataset.dim==='1');})()`, 'write tree highlights loader, Doom and settings; everything else is dimmed');
  await check(`!document.querySelector('.tree input')&&badge('loader')==='Will add'&&txt('.wz-lead').includes('never touched')`, 'write tree has no controls and says what is never touched');
  await shot('05-write');
  await click('Write to USB');
  await step('done');
  await check(`fx.written[0].method==='prepare_usb'&&JSON.stringify(fx.written[0].games)==='["doom"]'`, 'prepare_usb carries only the chosen game');
  await check(`fx.saved&&fx.saved.values.theme===21&&fx.saved.values.stems===0&&fx.saved.values.wave_mode===2&&fx.saved.values.stem_order===1&&fx.saved.values.stem_bank===1`, 'settings saved after the loader with every change');
  await check(`txt('.wz-pane h2')==='Your USB is ready'&&txt('.wz-done').includes('Loader written')&&txt('.wz-done').includes('Settings saved')`, 'done step reports loader, games and settings');
  await check(`document.querySelector('.tree[data-mode=result]')&&row('game-doom').dataset.state==='present'&&row('loader').dataset.state==='current'&&!document.querySelector('.tree [data-plan]')`, 'done tree shows Doom on the USB and the loader up to date');
  await check(`txt('.wz-next').includes('REKORDBOX')`, 'done step tells the DJ how to open MODS on the XZ');
  await shot('06-done');

  await open('loader');
  await click('Find my USB');
  await waitFor(`document.querySelector('.wz-usb')`, 'USB card appears');
  await check(`txt('.wz-usb').includes('XZ Mods loader')&&txt('.wz-usb').includes('1 game on USB')`, 'card shows loader and game count');
  await evaluate(`document.querySelector('.wz-usb').click()`);
  await step('loadout');
  await check(`row('loader').dataset.state==='outdated'&&box('loader').checked&&badge('loader')==='Will update'&&rowTxt('loader').includes('Old build')`, 'older loader: Update available, ticked, Will update');
  await check(`!box('game-chex')&&badge('game-chex')==='On USB'`, 'installed game reads On USB with no checkbox');
  await check(`rowTxt('stems').includes('12 tracks')&&rowTxt('stems').includes('700 MB')`, 'stems row counts prepared tracks');
  await shot('07-loadout-update');
  await evaluate(`box('loader').click()`);
  await check(`setupWizard.loaderChoice()==='keep'&&badge('loader')==='Update available'`, 'unticking keeps the loader and shows its state again');
  await click('Add all free games');
  await check(`setupWizard.newGames().join()==='doom,freedoom,myhouse'`, 'Add all selects the three missing games');
  await click('Next');
  await step('settings');
  await click('Review and write');
  await step('write');
  await check(`[...document.querySelectorAll('.tree-row[data-plan="1"]')].map(r=>r.dataset.id).join()==='game-doom,game-freedoom,game-myhouse'&&row('loader').dataset.dim==='1'&&row('settings').dataset.dim==='1'`, 'write tree: only the three games are planned; loader and settings dimmed');
  await click('Write to USB');
  await step('done');
  await check(`fx.written.length===1&&fx.written[0].method==='prepare_games_usb'&&JSON.stringify(fx.written[0].games)==='["doom","freedoom","myhouse"]'`, 'keeping the loader writes only the games');

  await open('loader');
  await click('Find my USB');
  await waitFor(`document.querySelector('.wz-usb')`, 'USB card appears');
  await evaluate(`document.querySelector('.wz-usb').click()`);
  await step('loadout');
  await evaluate(`box('loader').click()`);
  await click('Next'); await step('settings'); await click('Review and write'); await step('write');
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
  await check(`box('loader').disabled&&button('Choose mod version')&&button('Next').disabled`, 'missing mod version: loader box disabled, one button into the chooser, Next held');
  await shot('08-loadout-noversion');

  // Advanced tools keeps the old controls reachable.
  await evaluate(`showPage('advanced')`);
  await check(`txt('#page-title')==='Advanced tools'&&document.getElementById('mod-version')&&document.getElementById('setup-games-usb')&&document.getElementById('build')`, 'Advanced tools holds mod version, game downloads and the firmware-file build');
  assert.deepEqual(errors, [], 'no page errors: ' + errors.join(' / '));
  console.log('\n' + passed + ' checks passed');
} finally {
  socket?.close();
  child.kill();
  await new Promise(done => setTimeout(done, 300));
  await rm(profile, { recursive: true, force: true }).catch(() => {});
}
