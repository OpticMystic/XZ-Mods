import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, rm, readFile, writeFile, mkdir } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { serveUi } from './csp_server.mjs';

// Drives the DJ Profile page in headless Edge over CDP, served with the app's real CSP, against a mocked backend
// that follows the read_dj_profiles / read_logo / save_dj_profile / set_active_dj_profile / write_profile_track
// contract (xdj-xz-toolkit builder/dj_profile.py), including revision conflicts and unreadable profiles.
// Run: node tools/verify_dj_profile.mjs   (XZ_DJP_SHOTS=<dir> saves screenshots, XZ_TEST_BROWSER overrides the browser)
const app = resolve(dirname(fileURLToPath(import.meta.url)), '..');

function installMock() {
  const mode = location.hash.slice(1) || 'empty';
  const schema = window.__SCHEMA__, defaults = Object.fromEntries(schema.fields.map(field => [field.key, field.default]));
  const CONFLICT = 'The DJ profiles on this USB changed since they were loaded (maybe on the XZ). Reload the USB and try again.';
  const palette = ['#e0664f', '#4fbf6a', '#2997ff', '#f3e900', '#af52de', '#32ade6', '#ff2d55', '#ffa000', '#ffffff'];
  const art = (index, width, height) => {
    const side = 256, scale = side / Math.max(width, height), canvas = document.createElement('canvas');
    canvas.width = Math.round(width * scale); canvas.height = Math.round(height * scale);
    const g = canvas.getContext('2d');
    g.fillStyle = palette[index % palette.length]; g.fillRect(0, 0, canvas.width, canvas.height);
    g.fillStyle = '#000'; g.font = 'bold ' + Math.round(canvas.height * .5) + 'px sans-serif'; g.textAlign = 'center'; g.textBaseline = 'middle';
    g.fillText(String.fromCharCode(65 + index), canvas.width / 2, canvas.height / 2);
    return canvas.toDataURL('image/png');
  };
  const fx = window.fx = { mode, calls: [], requests: [], results: new Map(), picks: [], pickQueue: [], logos: new Map(), index: null, docs: new Map(), folders: [], problems: [], revisions: 0, tracks: new Map() };
  const logoFile = (path, index, width, height, bytes) => fx.logos.set(path, { path, width, height, bytes, format: 'png', preview: null, art: [index, width, height] });
  ['C:\\Logos\\kai-wordmark.png', 'C:\\Logos\\kai-square.png', 'C:\\Logos\\l3.png', 'C:\\Logos\\l4.png', 'C:\\Logos\\l5.png', 'C:\\Logos\\l6.png', 'C:\\Logos\\l7.png', 'C:\\Logos\\l8.png', 'C:\\Logos\\l9.png']
    .forEach((path, i) => logoFile(path, i, i === 0 ? 1600 : 1000, i === 0 ? 600 : 1000, i === 0 ? 412000 : 2500000));
  const preview = path => { const logo = fx.logos.get(path); if (!logo.preview) logo.preview = art(...logo.art); return logo.preview; };
  if (mode === 'folder') {
    logoFile('E:\\DJPROFILE\\DJ Mara\\logo.png', 4, 800, 800, 90000);
    fx.folders.push({ name: 'DJ Mara', logo: 'E:\\DJPROFILE\\DJ Mara\\logo.png' });
    fx.problems.push({ dj_id: 'folder:Broken', dj_name: 'Broken', error: 'DJPROFILE/Broken/logo.png: This logo file is empty or damaged.' });
  }
  const newRevision = () => '2026-10-12T10-00-0' + (++fx.revisions) + 'Z-ab' + fx.revisions;
  fx.bump = () => { fx.index.revision = newRevision(); };
  const view = (doc, active) => ({ dj_id: doc.dj_id, source: 'vjtools', editable: true, active, dj_name: doc.dj_name, tagline: doc.tagline, accent: doc.accent, aliases: doc.aliases, links: doc.links,
    page_url: doc.page_url, note: doc.note,
    logos: doc.logos.map(logo => ({ path: 'E:\\DJPROFILE\\VJTOOLS\\profiles\\' + doc.dj_id + '\\bundles\\b\\assets\\' + logo.path.split('\\').pop(), stick_path: 'DJPROFILE/VJTOOLS/profiles/' + doc.dj_id + '/bundles/b/assets/x.png',
      label: logo.label, note: logo.note, width: fx.logos.get(logo.path).width, height: fx.logos.get(logo.path).height, bytes: fx.logos.get(logo.path).bytes, sha256: 'a'.repeat(64), format: 'png', preview: preview(logo.path) })),
    tile: doc.logos.length ? { path: 'E:\\tile', stick_path: 'DJPROFILE/VJTOOLS/profiles/' + doc.dj_id + '/bundles/b/renditions/xz-tile-160.rgb565', preview: preview(doc.logos[0].path) } : null,
    track: fx.tracks.get(doc.dj_id) || null });
  const read = () => {
    const profiles = fx.index ? fx.index.profiles.map(id => view(fx.docs.get(id), id === fx.index.active)) : [];
    const taken = new Set(profiles.map(profile => profile.dj_name.toLowerCase()));
    for (const folder of fx.folders) if (!taken.has(folder.name.toLowerCase())) {
      const logo = fx.logos.get(folder.logo);
      profiles.push({ dj_id: 'folder:' + folder.name, source: 'folder', editable: false, active: false, dj_name: folder.name, tagline: '', accent: '#9828f0', aliases: [], links: [], page_url: null, note: '',
        logos: [{ path: folder.logo, stick_path: 'DJPROFILE/' + folder.name + '/logo.png', label: 'logo', note: '', width: logo.width, height: logo.height, bytes: logo.bytes, sha256: 'b'.repeat(64), format: 'png', preview: preview(folder.logo) }], tile: null, track: null });
    }
    const active = fx.index ? fx.index.active : profiles.find(profile => profile.source === 'folder')?.dj_id || null;
    for (const profile of profiles) profile.active = profile.dj_id === active;
    return { format: 'xz-dj-profiles/1', public_folder: fx.index || fx.folders.length ? 'DJPROFILE' : null, revision: fx.index ? fx.index.revision : null, active, stick_colour: '#9828f0', legacy: false, profiles, problems: fx.problems };
  };
  const summary = () => fx.index ? { count: fx.index.profiles.length, active_name: fx.docs.get(fx.index.active).dj_name } : { count: 0, active_name: null };
  const fail = error => ({ running: false, ok: false, error });
  const check = request => (fx.index ? fx.index.revision : null) === request.expected_revision;
  const save = request => {
    if (!check(request)) return fail(CONFLICT);
    const fields = request.profile, links = fields.links || [];
    if (!fields.dj_name?.trim()) return fail('DJ name must contain 1 to 160 printable characters');
    for (const link of links) if (!link.url.startsWith('https://')) return fail('Links must start with https:// (' + link.url + ' uses http://)');
    if ((request.logos || []).length > 8) return fail('A profile may list at most 8 logos');
    for (const logo of request.logos || []) if (!fx.logos.has(logo.path) && !fx.logos.has('C:\\Logos\\' + logo.path.split('\\').pop())) return fail('Logo not found: ' + logo.path);
    let id = fields.dj_id && !fields.dj_id.startsWith('folder:') ? fields.dj_id : null;
    if (!id) { id = 'djp_' + fields.dj_name.toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, ''); while (fx.docs.has(id)) id += '_2'; }
    const logos = request.logos.map(logo => ({ ...logo, path: fx.logos.has(logo.path) ? logo.path : 'C:\\Logos\\' + logo.path.split('\\').pop() }));
    fx.docs.set(id, { dj_id: id, dj_name: fields.dj_name, tagline: fields.tagline, accent: fields.accent, aliases: fields.aliases, links, page_url: fields.page_url, note: fields.note, logos });
    if (!fx.index) fx.index = { revision: newRevision(), active: id, profiles: [id] };
    else { if (!fx.index.profiles.includes(id)) fx.index.profiles.push(id); if (request.make_active) fx.index.active = id; fx.index.revision = newRevision(); }
    return { running: false, ok: true, result: { ...read(), saved: id } };
  };
  const status = request => {
    switch (request.method) {
      case 'list_mod_versions': return { running: false, ok: true, result: { format: 'xz-mod-versions/1', versions: [{ id: 'v1', label: 'Fixture build', runtime_sha256: 'newsha', hardware_qualified: false, source_commit: 'abc' }], selected: { id: 'v1', pinned: true, revision: 1 }, bundled_id: 'v1' } };
      case 'discover_usbs': return { running: false, ok: true, result: { format: 'xz-usbs/1', usbs: mode === 'nousb' ? [] : mode === 'multi' ? [usb('E:\\', 'KINGSTON'), usb('F:\\', 'SANDISK')] : [usb('E:\\', 'KINGSTON')] } };
      case 'inspect_usb': return { running: false, ok: true, result: { format: 'xz-mods-usb/1', volume: request.volume, identity: { serial: 1 }, loader: { exists: true, bytes: 4100000, sha256: 'abc' },
        recorded_mod_version: { id: 'v1', label: 'Fixture build', runtime_sha256: 'newsha', source_commit: 'abc' }, settings: { editable: true, values: { ...defaults, theme: 2 }, revision: { exists: true, bytes: 400 }, schema, path: request.volume + 'VJ.Tools\\XZ-Mods.cfg' },
        backups: [], games: [], direct_usb_root: true, requires_copy_to_usb_root: false, filesystem: 'FAT32', space: { free_bytes: 29100000000, total_bytes: 32000000000 }, stems: { tracks: 0, bytes: 0 }, rekordbox: true, dj_profiles: summary() } };
      case 'inspect_mod_settings': return { running: false, ok: true, result: { format: 'xz-mod-settings/1', mod_version_id: 'v1', settings_change: null } };
      case 'read_dj_profiles': return { running: false, ok: true, result: read() };
      case 'read_logo': {
        const logo = fx.logos.get(request.path);
        if (!logo) return fail('Choose a PNG or JPEG image. This file is neither.');
        if (logo.bytes > 8 * 1048576) return fail('This logo is larger than 8 MB. Choose a smaller file.');
        return { running: false, ok: true, result: { path: logo.path, width: logo.width, height: logo.height, bytes: logo.bytes, format: 'png', preview: preview(logo.path) } };
      }
      case 'save_dj_profile': return save(request);
      case 'set_active_dj_profile': {
        if (!check(request)) return fail(CONFLICT);
        if (!fx.index?.profiles.includes(request.dj_id)) return fail('Save this DJ profile to the USB before making it active');
        if (fx.index.active !== request.dj_id) { fx.index.active = request.dj_id; fx.index.revision = newRevision(); }
        return { running: false, ok: true, result: read() };
      }
      case 'write_profile_track': {
        const doc = fx.docs.get(request.dj_id);
        if (!doc) return fail('Save this DJ profile to the USB before making its track');
        if (!['folder', 'usb'].includes(request.destination)) return fail("Choose where the track goes: 'folder' or 'usb'");
        const name = 'DJ PROFILE - ' + doc.dj_name + '.mp3', bytes = 365000 + doc.logos.reduce((sum, logo) => sum + fx.logos.get(logo.path).bytes, 0);
        const path = request.destination === 'usb' ? 'E:\\DJPROFILE\\VJTOOLS\\profiles\\' + doc.dj_id + '\\track\\' + name : (request.folder || 'C:\\Users\\dj\\Music\\DJ Profile') + '\\' + name;
        if (request.destination === 'usb') fx.tracks.set(doc.dj_id, { path, stick_path: 'DJPROFILE/VJTOOLS/profiles/' + doc.dj_id + '/track/' + name, bytes });
        return { running: false, ok: true, result: { format: 'xz-dj-profile-track/1', destination: request.destination, path, bytes, sha256: 'c'.repeat(64), duration_seconds: 20.035,
          logos_embedded: doc.logos.length, logos_skipped: [], verified_silent: true, ffmpeg_verified: true } };
      }
    }
    throw Error('Unexpected method ' + request.method);
  };
  const usb = (volume, label) => ({ volume, label, removable: true, loader: true, settings: true, rekordbox: true, games: 0 });
  window.__TAURI__ = { core: { invoke: async (command, args) => {
    fx.calls.push(command);
    if (command === 'app_information') return { version: '0.3.0', platform: 'windows', installed: true, release_notes: { releases: [] } };
    if (command === 'check_app_update') return { phase: 'current', message: 'Up to date' };
    if (command === 'start_job') { fx.requests.push(args.request); return fx.requests.length; }
    if (command === 'job_status') { if (!fx.results.has(args.id)) fx.results.set(args.id, status(fx.requests[args.id - 1])); return fx.results.get(args.id); }
    if (command === 'cancel_job') return {};
    if (command === 'pick_path') { fx.picks.push(args.kind); return args.kind === 'image' ? fx.pickQueue.shift() ?? null : args.kind === 'folder' ? 'D:\\Promo' : null; }
    throw Error('Unexpected command ' + command);
  } } };
  fx.csp = [];
  document.addEventListener('securitypolicyviolation', event => fx.csp.push(event.violatedDirective + ' ' + (event.blockedURI || 'inline') + ' ' + (event.sourceFile || '').split('/').pop() + ':' + event.lineNumber));
  window.page = () => document.getElementById('dj-profile');
  window.button = (text, root = page()) => [...root.querySelectorAll('button')].find(item => item.textContent.trim() === text && item.checkVisibility());
  window.txt = (selector, root = document) => root.querySelector(selector)?.textContent.replace(/\s+/g, ' ').trim() || '';
  window.type = (selector, value) => { const field = page().querySelector(selector); field.value = value; field.dispatchEvent(new Event('input', { bubbles: true })); };
  window.err = key => txt('[data-err="' + key + '"]', page());
  window.cardOf = id => page().querySelector('.djp-card[data-id="' + id + '"]');
  window.last = method => [...fx.requests].reverse().find(request => request.method === method);
  window.count = method => fx.requests.filter(request => request.method === method).length;
}

const schema = JSON.parse(await readFile(resolve(app, process.env.XZ_SETTINGS_SCHEMA || '../VJToolsLibrary/packages/xdj-xz-toolkit/mods/settings-schema.json'), 'utf8'));
const server = await serveUi(app);
const profile = await mkdtemp(join(tmpdir(), 'xz-djp-'));
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
  const context = () => evaluate(`'phase: '+djProfilePage.state.phase+' job: '+activeJob+' requests: '+fx.requests.map(r=>r.method).join(',')+'\\n  page: '+txt('#djp-host').slice(0,400)`);
  const waitFor = async (expression, label) => {
    for (let attempt = 0; attempt < 200; attempt++) { if (await evaluate(expression)) return; await new Promise(done => setTimeout(done, 25)); }
    throw Error('Timed out: ' + label + '\n  ' + await context() + '\n  page errors: ' + errors.join(' / '));
  };
  const check = async (expression, label) => {
    if (!await evaluate(expression)) throw Error('Failed: ' + label + '\n  ' + await context() + '\n  page errors: ' + errors.join(' / '));
    passed++; console.log('ok', label);
  };
  await send('Runtime.enable');
  socket.addEventListener('message', event => { const data = JSON.parse(event.data); if (data.method === 'Runtime.exceptionThrown') errors.push((data.params.exceptionDetails.exception?.description || data.params.exceptionDetails.text)); });
  await send('Page.enable');
  await send('Page.addScriptToEvaluateOnNewDocument', { source: 'window.__SCHEMA__=' + JSON.stringify(schema) + ';(' + installMock + ')();' });
  const size = (width, height) => send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
  await size(1100, 760);
  const cspClean = async () => { const violations = await evaluate(`window.fx?.csp||[]`); assert.deepEqual(violations.slice(0, 5), [], 'CSP violations (' + violations.length + ')'); };
  const open = async mode => {
    await cspClean();
    await send('Page.navigate', { url: 'about:blank' });
    await new Promise(done => setTimeout(done, 50));
    await send('Page.navigate', { url: server.url + 'index.html#' + mode });
    await waitFor(`document.readyState==='complete'&&window.djProfilePage&&modInventory!==null`, 'page loaded (' + mode + ')');
    errors.length = 0;
  };
  const idle = () => waitFor(`activeJob===null&&!['loading','saving'].includes(djProfilePage.state.phase)&&!djProfilePage.state.track?.running`, 'idle');
  const click = async (text, root = 'page()') => { assert(await evaluate(`Boolean(button(${JSON.stringify(text)},${root}))`), 'button "' + text + '" is visible\n  ' + await context()); await evaluate(`button(${JSON.stringify(text)},${root}).click()`); await idle(); };
  const shots = process.env.XZ_DJP_SHOTS; if (shots) await mkdir(shots, { recursive: true });
  const shot = async (name, width = 1100, height = 760) => {
    if (!shots) return;
    await size(width, height);
    await evaluate('window.scrollTo(0,0)');
    await new Promise(done => setTimeout(done, 150));
    const { cssContentSize } = await send('Page.getLayoutMetrics');
    const image = await send('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true, clip: { x: 0, y: 0, width, height: Math.max(height, Math.ceil(cssContentSize.height)), scale: 1 } });
    await writeFile(join(shots, name + '.png'), Buffer.from(image.data, 'base64'));
    await size(1100, 760);
  };
  const showDj = () => evaluate(`showPage('dj-profile');window.scrollTo(0,0)`);

  await open('empty');
  await check(`[...document.querySelectorAll('nav button')].map(b=>b.dataset.page).join()==='setup,games,dj-profile,stems,library-repair,device-updates,advanced,updates,licenses'&&txt('nav button[data-page="dj-profile"]')==='DJ Profile'`, 'nav: DJ Profile right after Games');
  await showDj();
  await check(`txt('#page-title')==='DJ Profile'&&page().querySelectorAll('button').length===1&&Boolean(button('Find my USB'))&&!fx.requests.some(r=>r.method==='read_dj_profiles')`, 'no USB: one Find my USB button and no read yet');
  await click('Find my USB');
  await waitFor(`djProfilePage.state.phase==='list'`, 'list after Find my USB');
  await check(`document.querySelector('nav button.selected').dataset.page==='dj-profile'&&setupWizard.state.usb==='E:\\\\'&&last('read_dj_profiles').volume==='E:\\\\'`, 'one USB: picked automatically, shared with the wizard, profiles read');
  await check(`txt('#dj-profile h2')==='DJ profiles on KINGSTON'&&txt('#dj-profile .wz-lead')==='No DJ profile on this USB yet. A profile puts your name and logo on the XZ and lets VJ software on the same network pick them up.'&&button('New DJ profile').classList.contains('primary')`, 'empty stick: the one sentence and the primary New DJ profile button');
  await check(`!page().querySelector('.djp-track-panel')&&!page().querySelector('.djp-cards')`, 'empty stick: no cards and no track section yet');
  await shot('01-empty');
  await shot('01b-empty-wide', 1440, 900);

  await click('New DJ profile');
  await check(`djProfilePage.state.phase==='editing'&&txt('#dj-profile h2')==='New DJ profile'&&document.getElementById('djp-save').disabled&&txt('.djp-save-why')==='Add your DJ name to save.'`, 'editor: Save disabled until there is a name, and says why');
  await check(`page().querySelectorAll('.djp-link').length===1&&page().querySelector('.djp-link select').value==='media-kit'&&page().querySelector('.djp-link select option:checked').textContent==='EPK'&&txt('.djp-group-head small',page().querySelectorAll('.djp-group')[1]).startsWith('Link to your EPK or files')`, 'links start with one EPK row and the EPK hint');
  await check(`[...page().querySelector('.djp-link select').options].map(o=>o.textContent).join()==='EPK,Files,Website,Instagram,SoundCloud,Mixcloud,Bandcamp,Other'`, 'link kinds: EPK first, then the short list');
  await check(`!page().querySelector('.djp-more').open&&txt('.djp-more summary').startsWith('More')&&txt('.djp-edit').includes('first profile on the USB')&&!document.getElementById('djp-make-active')`, 'aliases and note wait under More; first profile becomes active without a tick box');
  await check(`txt('.djp-dev-head .dv-profile strong')==='TAP TO SET UP'&&page().querySelector('.djp-dev-head .dv-avatar').dataset.empty==='1'`, 'preview header chip shows the device empty state before a name');
  await shot('02-editor');
  await evaluate(`type('#djp-name','   ')`);
  await check(`document.getElementById('djp-save').disabled`, 'a blank name still cannot be saved');
  await evaluate(`type('#djp-name','DJ Kai')`);
  await check(`!document.getElementById('djp-save').disabled&&txt('.djp-save-why')===''`, 'a name enables Save');
  await check(`txt('.djp-dev-head .dv-profile strong')==='DJ Kai'&&txt('.djp-dev-head .dv-avatar')==='DK'&&txt('.djp-dev-text strong')==='DJ Kai'`, 'preview chip and tile follow the typed name (monogram before a logo)');
  await evaluate(`type('.djp-link input[type=url]','http://drive.google.com/kai')`);
  await check(`err('link-0')==='Use an https:// address. This one starts with http://.'&&document.getElementById('djp-save').disabled`, 'http:// link rejected inline and blocks Save');
  await evaluate(`type('.djp-link input[type=url]','https://drive.google.com/drive/folders/kai-epk')`);
  await check(`err('link-0')===''&&!document.getElementById('djp-save').disabled`, 'https:// link accepted');
  await evaluate(`type('#djp-tagline','x'.repeat(81))`);
  await check(`err('tagline')==='Use at most 80 characters.'&&document.getElementById('djp-save').disabled`, 'tagline over 80 characters rejected inline');
  await evaluate(`type('#djp-tagline','Techno all night long')`);
  await evaluate(`type('#djp-accent','#12')`);
  await check(`err('accent')==='Use a colour like #2997ff.'&&document.getElementById('djp-save').disabled`, 'bad accent hex rejected inline');
  await evaluate(`type('#djp-accent','#E0664F')`);
  await check(`err('accent')===''&&document.getElementById('djp-accent-pick').value==='#e0664f'`, 'hex field drives the colour picker');

  await evaluate(`fx.pickQueue.push('C:\\\\Logos\\\\kai-wordmark.png','C:\\\\Logos\\\\kai-square.png')`);
  await click('Add logo');
  await click('Add logo');
  await check(`fx.picks.filter(k=>k==='image').length===2&&fx.requests.filter(r=>r.method==='read_logo').map(r=>r.path).join('|')==='C:\\\\Logos\\\\kai-wordmark.png|C:\\\\Logos\\\\kai-square.png'`, 'Add logo uses pick_path kind image, then read_logo');
  await check(`page().querySelectorAll('.djp-logo').length===2&&txt('.djp-logo[data-main="1"] .djp-main')==='Main logo'&&txt('.djp-logo small').startsWith('1600 × 600 px')&&Boolean(button('Make main'))`, 'logo rows show size, the main logo, and Make main on the other');
  await evaluate(`fx.pickQueue.push('C:\\\\Logos\\\\kai-square.png')`);
  await click('Add logo');
  await check(`err('logos')==='That logo is already in the list.'&&page().querySelectorAll('.djp-logo').length===2`, 'the same file twice is refused inline');
  await evaluate(`fx.pickQueue.push('C:\\\\Logos\\\\notes.txt')`);
  await click('Add logo');
  await check(`err('logos')==='Choose a PNG or JPEG image. This file is neither.'`, 'a non-image shows the backend reason inline');
  await click('Make main');
  await check(`djProfilePage.state.draft.logos[0].path==='C:\\\\Logos\\\\kai-square.png'&&page().querySelectorAll('.djp-logo')[0].dataset.main==='1'`, 'Make main moves that logo first');
  await check(`page().querySelector('.djp-dev-head .dv-avatar img')?.getAttribute('src')===djProfilePage.state.draft.logos[0].preview&&page().querySelector('.djp-dev-body .djp-tile img')?.getAttribute('src')===djProfilePage.state.draft.logos[0].preview`, 'preview chip and 160 tile show the main logo');
  await check(`getComputedStyle(page().querySelector('.djp-dev-body .djp-tile')).width==='160px'&&getComputedStyle(page().querySelector('.djp-dev-body .djp-tile img')).objectFit==='contain'&&getComputedStyle(page().querySelector('.djp-dev-body .djp-tile')).backgroundColor==='rgb(0, 0, 0)'`, 'tile is 160 px, logo fit inside over black');
  await check(`getComputedStyle(page().querySelector('.djp-device')).getPropertyValue('--dv-accent').trim()==='#54c1e6'`, 'preview uses the USB settings theme (CYBERPUNK)');
  await check(`txt('.djp-vj').includes('DJ Kai')&&txt('.djp-vj').includes('Techno all night long')&&txt('.djp-vj').includes('EPK drive.google.com')`, 'What VJ software sees: name, tagline and links');
  await click('Add link');
  await check(`page().querySelectorAll('.djp-link select')[1].value==='website'`, 'second link suggests Website once EPK is there');
  await evaluate(`type('.djp-link:nth-child(2) input[type=url]','https://djkai.example')`);

  await evaluate(`fx.pickQueue.push('C:\\\\Logos\\\\l3.png','C:\\\\Logos\\\\l4.png','C:\\\\Logos\\\\l5.png','C:\\\\Logos\\\\l6.png','C:\\\\Logos\\\\l7.png','C:\\\\Logos\\\\l8.png','C:\\\\Logos\\\\l9.png')`);
  for (let i = 0; i < 6; i++) await click('Add logo');
  await check(`page().querySelectorAll('.djp-logo').length===8&&document.getElementById('djp-add-logo').disabled&&txt('.djp-group').includes('8 logos is the most a profile holds.')`, '8 logos: Add logo is disabled and says why');
  await evaluate(`document.getElementById('djp-add-logo').click()`);
  await idle();
  await check(`page().querySelectorAll('.djp-logo').length===8&&fx.picks.filter(k=>k==='image').length===10`, 'a 9th logo is refused without opening the picker');
  await check(`txt('#djp-budget')==='6.9 MB of 24 MB left for logos.'`, 'remaining logo budget shows once it gets close');
  for (let i = 0; i < 6; i++) await evaluate(`page().querySelectorAll('.djp-logo .djp-remove')[2].click()`);
  await check(`page().querySelectorAll('.djp-logo').length===2&&!document.getElementById('djp-budget')&&!document.getElementById('djp-add-logo').disabled`, 'removing logos brings Add logo back and hides the budget');
  await check(`document.documentElement.scrollWidth<=innerWidth&&page().querySelector('.djp-editor').scrollWidth<=page().querySelector('.djp-editor').clientWidth`, 'editor fits without horizontal scroll');
  await evaluate(`page().querySelector('.djp-more').open=true`);
  await waitFor(`djProfilePage.state.draft.moreOpen`, 'More opens');
  await evaluate(`type('#djp-aliases','Kai\\nKai B2B Lena')`);
  await shot('03-editor-filled');
  await shot('03b-editor-filled-wide', 1440, 900);
  await click('Save to USB');
  await waitFor(`djProfilePage.state.phase==='list'`, 'list after save');
  await check(`(()=>{const r=last('save_dj_profile');return r.volume==='E:\\\\'&&r.expected_revision===null&&r.make_active===true&&r.profile.dj_id===null&&r.profile.dj_name==='DJ Kai'&&r.profile.accent==='#e0664f'&&r.profile.aliases.join('|')==='Kai|Kai B2B Lena';})()`, 'save request: first profile, active, no revision yet, aliases split per line');
  await check(`(()=>{const r=last('save_dj_profile');return r.logos.map(l=>l.path).join('|')==='C:\\\\Logos\\\\kai-square.png|C:\\\\Logos\\\\kai-wordmark.png'&&JSON.stringify(r.profile.links)===JSON.stringify([{kind:'media-kit',url:'https://drive.google.com/drive/folders/kai-epk'},{kind:'website',url:'https://djkai.example'}]);})()`, 'save request: main logo first, EPK link as media-kit');
  await check(`cardOf('djp_dj_kai').dataset.active==='1'&&cardOf('djp_dj_kai').dataset.highlight==='1'&&txt('.djp-active',cardOf('djp_dj_kai'))==='Active on the XZ'`, 'list: the saved profile is highlighted with the active badge');
  await check(`cardOf('djp_dj_kai').querySelector('.djp-tile img')&&txt('.djp-saved').startsWith('Saved DJ Kai to the USB.')&&button('Create profile track',page().querySelector('.djp-saved')).classList.contains('primary')&&button('New DJ profile').classList.contains('secondary')`, 'list: the tile shows, and the profile track is offered right after the save');
  await check(`txt('.djp-track-panel h3')==='Profile track for newer players'&&txt('.djp-track-panel .wz-lead').startsWith('Some newer players block reading extra files from the USB.')`, 'track section with its one sentence');
  await check(`setupWizard.state.inspection.dj_profiles.count===1&&setupWizard.state.inspection.dj_profiles.active_name==='DJ Kai'`, 'the wizard learns the new profile count without another inspection');
  await shot('04-list');

  await click('Create profile track', `page().querySelector('.djp-saved')`);
  await check(`djProfilePage.state.phase==='track'&&txt('#dj-profile h2')==='Profile track for DJ Kai'&&txt('.djp-steps')==='1Where2Create3Add it to rekordbox'&&txt('.djp-steps [aria-current=step]').endsWith('Where')`, 'track wizard opens in place on step 1 Where');
  await check(`txt('#djp-track-folder')==='Music › DJ Profile in your user folder'&&!document.getElementById('djp-usb-copy').checked&&button('Create profile track').classList.contains('primary')`, 'default destination: this computer, Music › DJ Profile; USB copy off');
  await shot('05-track-where');
  await evaluate(`document.getElementById('djp-usb-copy').click()`);
  await click('Create profile track');
  await waitFor(`djProfilePage.state.track.result`, 'track written');
  await check(`(()=>{const writes=fx.requests.filter(r=>r.method==='write_profile_track');return writes.length===2&&writes[0].dj_id==='djp_dj_kai'&&writes[0].destination==='folder'&&!('folder' in writes[0])&&writes[1].destination==='usb'&&writes[1].dj_id==='djp_dj_kai';})()`, 'write_profile_track: the saved dj_id, backend default folder, then the USB copy');
  await check(`txt('.djp-facts').includes('2 logos embedded at full resolution')&&txt('.djp-facts').includes('Every sample is silent. Checked.')&&txt('.djp-facts').includes('20 seconds of silence')&&txt('.djp-facts').includes('EPK, Website')&&txt('.djp-facts').includes('A copy is in DJPROFILE on KINGSTON.')`, 'result: logos embedded, verified silent, length, links, USB copy');
  await check(`page().querySelector('.djp-path input').value==='C:\\\\Users\\\\dj\\\\Music\\\\DJ Profile\\\\DJ PROFILE - DJ Kai.mp3'&&Boolean(button('Copy path'))`, 'no open-folder command: the path with a Copy button');
  await shot('06-track-done');
  await click('Next: add it to rekordbox');
  await check(`(()=>{const steps=[...page().querySelectorAll('.djp-howto li')].map(li=>li.textContent);return steps.length===5&&steps[0]==='Open rekordbox on this computer.'&&steps[1].includes('DJ PROFILE - DJ Kai.mp3')&&steps[1].includes('Collection')&&steps[2]==='Make a playlist named DJ PROFILE and put the track in it.'&&steps[3].includes('Export the DJ PROFILE playlist to this USB');})()`, 'rekordbox steps: drag into Collection, DJ PROFILE playlist, export to this USB');
  await shot('07-track-rekordbox');
  await click('Done');
  await waitFor(`djProfilePage.state.phase==='list'`, 'list after Done');
  await check(`txt('.djp-card-text',cardOf('djp_dj_kai')).includes('Profile track on this USB')&&button('Make a new profile track')`, 'a track on the USB shows on the card and renames the button');

  await click('New DJ profile');
  await evaluate(`type('#djp-name','Lena B')`);
  await check(`document.getElementById('djp-make-active')&&!document.getElementById('djp-make-active').checked`, 'second profile: an unticked Make this the active profile box');
  await click('Save to USB');
  await waitFor(`djProfilePage.state.phase==='list'`, 'list after second save');
  await check(`last('save_dj_profile').make_active===false&&typeof last('save_dj_profile').expected_revision==='string'`, 'second save leaves the active profile alone');
  await check(`cardOf('djp_lena_b').dataset.active!=='1'&&Boolean(button('Make active',cardOf('djp_lena_b')))&&!button('Make active',cardOf('djp_dj_kai'))`, 'list: Make active only on the inactive profile');
  const revision = await evaluate(`djProfilePage.state.data.revision`);
  await click('Make active', `cardOf('djp_lena_b')`);
  await check(`last('set_active_dj_profile').dj_id==='djp_lena_b'&&last('set_active_dj_profile').expected_revision===${JSON.stringify(revision)}&&cardOf('djp_lena_b').dataset.active==='1'&&cardOf('djp_dj_kai').dataset.active!=='1'`, 'Make active calls set_active_dj_profile with the expected revision and moves the badge');
  await check(`setupWizard.state.inspection.dj_profiles.active_name==='Lena B'`, 'the wizard follows the new active profile');

  await evaluate(`fx.bump()`);
  await click('Make active', `cardOf('djp_dj_kai')`);
  await check(`txt('.djp-banner p')==="This USB's profiles changed since you opened them. Reload and try again."&&page().querySelectorAll('.djp-banner button').length===1&&Boolean(button('Reload'))`, 'revision conflict: the reload message and one Reload button');
  await click('Reload');
  await check(`!page().querySelector('.djp-banner')&&djProfilePage.state.data.revision===fx.index.revision`, 'Reload reads the USB again');
  await click('Make active', `cardOf('djp_dj_kai')`);
  await check(`cardOf('djp_dj_kai').dataset.active==='1'`, 'after Reload, Make active works');

  await evaluate(`page().querySelector('.djp-card[data-id="djp_lena_b"] .djp-edit').click()`);
  await check(`txt('#dj-profile h2')==='Edit Lena B'&&document.getElementById('djp-name').value==='Lena B'`, 'Edit opens the saved profile');
  await evaluate(`type('#djp-tagline','B2B nights')`);
  await evaluate(`fx.bump()`);
  await click('Save to USB');
  await check(`djProfilePage.state.phase==='editing'&&txt('.djp-banner p')==="This USB's profiles changed since you opened them. Reload and try again."`, 'conflict while saving keeps the editor and shows the reload message');
  await click('Reload');
  await check(`djProfilePage.state.phase==='editing'&&document.getElementById('djp-tagline').value==='B2B nights'&&!page().querySelector('.djp-banner')`, 'Reload keeps the draft');
  await click('Save to USB');
  await waitFor(`djProfilePage.state.phase==='list'`, 'saved after reload');
  await check(`last('save_dj_profile').profile.dj_id==='djp_lena_b'&&txt('.djp-card-text',cardOf('djp_lena_b')).includes('B2B nights')`, 'the retried save updates the same profile');

  await evaluate(`showPage('setup')`);
  await check(`setupWizard.state.step==='loadout'&&txt('.tree-row[data-id=djprofile] .tree-detail')==='2 profiles · active DJ Kai'&&document.querySelector('.tree-row[data-id=djprofile]').dataset.state==='present'&&txt('.tree-row[data-id=djprofile] small')==='DJPROFILE'`, 'loadout tree: DJ profiles row with count and active name');
  await check(`txt('.tree-row[data-id=djprofile] .tree-badge')==='On USB'&&!document.querySelector('.tree-row[data-id=djprofile] input')`, 'the DJ profiles row has no tick box: the wizard never writes profiles');
  await shot('08-loadout-row');
  await evaluate(`document.querySelector('.tree-row[data-id=djprofile] button').click()`);
  await check(`document.querySelector('nav button.selected').dataset.page==='dj-profile'&&djProfilePage.state.phase==='list'`, 'its Edit DJ profiles link opens the DJ Profile page');
  await evaluate(`showPage('setup');setupWizard.goto('settings')`);
  await check(`txt('.dv-header .dv-profile strong')==='DJ Kai'&&txt('.dv-header .dv-usb')==='USB · E:\\\\'`, 'settings menu header carries the active DJ chip, like the XZ');
  await evaluate(`document.querySelector('.dv-header .dv-profile').click()`);
  await check(`document.querySelector('nav button.selected').dataset.page==='dj-profile'`, 'the header chip opens the DJ Profile page');

  await open('empty');
  await evaluate(`showPage('setup')`);
  await evaluate(`button('Find my USB',document.getElementById('setup')).click()`);
  await waitFor(`document.querySelector('.wz-usb')`, 'wizard USB card');
  await evaluate(`document.querySelector('.wz-usb').click()`);
  await waitFor(`setupWizard.state.step==='loadout'&&activeJob===null`, 'wizard loadout');
  await check(`txt('.tree-row[data-id=djprofile] .tree-detail')==='No DJ profile yet'&&txt('.tree-row[data-id=djprofile] button')==='Create a DJ profile'&&count('read_dj_profiles')===0`, 'empty stick tree row: Create a DJ profile, and no profile read while the page is hidden');
  await evaluate(`document.querySelector('.tree-row[data-id=djprofile] button').click()`);
  await waitFor(`djProfilePage.state.phase==='list'`, 'list after the tree link');
  await check(`last('read_dj_profiles').volume==='E:\\\\'&&Boolean(button('New DJ profile'))`, 'a USB chosen in the wizard is enough: the page reads it on open');

  await open('folder');
  await showDj();
  await click('Find my USB');
  await waitFor(`djProfilePage.state.phase==='list'`, 'folder list');
  await check(`txt('.djp-hand',cardOf('folder:DJ Mara'))==='Made by hand · Save to edit'&&cardOf('folder:DJ Mara').dataset.active==='1'&&!button('Make active',cardOf('folder:DJ Mara'))`, 'folder profile: Made by hand, active by the folder rule, no Make active');
  await check(`txt('.djp-problems')==='Broken could not be read: DJPROFILE/Broken/logo.png: This logo file is empty or damaged.'&&Boolean(button('New DJ profile'))`, 'a broken profile is one quiet line and does not block the page');
  await check(`!page().querySelector('.djp-track-panel')`, 'no track section until a profile is saved');
  await evaluate(`cardOf('folder:DJ Mara').querySelector('.djp-edit').click()`);
  await check(`txt('#dj-profile h2')==='Save DJ Mara as a profile'&&document.getElementById('djp-name').value==='DJ Mara'&&djProfilePage.state.draft.logos.length===1&&djProfilePage.state.draft.logos[0].path==='E:\\\\DJPROFILE\\\\DJ Mara\\\\logo.png'&&!document.getElementById('djp-save').disabled`, 'Edit on a folder profile opens a new profile prefilled with its name and logo');
  await click('Save to USB');
  await waitFor(`djProfilePage.state.phase==='list'`, 'folder saved');
  await check(`last('save_dj_profile').profile.dj_id===null&&last('save_dj_profile').logos[0].path==='E:\\\\DJPROFILE\\\\DJ Mara\\\\logo.png'&&cardOf('djp_dj_mara').dataset.source==='vjtools'&&!cardOf('folder:DJ Mara')`, 'saving it writes a VJTOOLS profile that replaces the folder card');

  await open('nousb');
  await showDj();
  await click('Find my USB');
  await check(`txt('#dj-profile .notice').startsWith('No USB drive was found.')&&Boolean(button('Look again'))`, 'no USB found: a plain notice and Look again');
  await open('multi');
  await showDj();
  await click('Find my USB');
  await check(`page().querySelectorAll('.wz-usb').length===2&&djProfilePage.state.phase==='no-usb'`, 'several USBs: the USB cards, nothing chosen yet');
  await evaluate(`page().querySelectorAll('.wz-usb')[1].click()`);
  await waitFor(`djProfilePage.state.phase==='list'`, 'list after card');
  await check(`last('read_dj_profiles').volume==='F:\\\\'&&txt('#dj-profile h2')==='DJ profiles on SANDISK'`, 'choosing a card reads that USB');

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
