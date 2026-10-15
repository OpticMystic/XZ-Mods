import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, rm, readFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve, dirname, basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { serveUi } from './csp_server.mjs';
import { killBrowser } from './kill_browser.mjs';

// No packages needed. Drives the real renderer in headless Edge over CDP with a mocked Tauri invoke.
// Run: node tools/verify_stems_page.mjs   (XZ_TEST_BROWSER overrides the browser path)
const app = resolve(dirname(fileURLToPath(import.meta.url)), '..');

function installMock() {
  const GiB = 1073741824, MiB = 1048576, mode = location.hash.slice(1) || 'cuda';
  const names = ['Glass Hours (Extended Mix)', 'Low Orbit', 'Undertow (Club Edit)', 'Carbon Bloom', 'Night Market', 'Kinetic', 'Saltwater', 'Satellite Hearts', 'Echo Chamber', 'Ultraviolet'];
  const fx = window.fx = {
    mode, calls: [], requests: [], installed: ['fresh', 'cpu'].includes(mode) ? null : 'balanced', free: 42.9 * GiB, total: 59.6 * GiB, indexBytes: 20000, stemsBytes: 1.5 * GiB,
    drives: mode === 'nousb' ? [] : [{ volume: 'E:\\', label: 'KINGSTON', removable: true }], batch: { done: 0, stage: 'decode', pct: 10 }, cancelNext: false,
    setupFrame: 'installing', importPlan: 'review', pickFiles: ['C:\\Stems\\vox_lead.wav', 'C:\\Stems\\kick.wav', 'C:\\Stems\\hats_loop.wav', 'C:\\Stems\\track_07_a.wav', 'C:\\Stems\\stem3.wav']
  };
  fx.tracks = Array.from({ length: 212 }, (_, index) => {
    const id = index + 1, duration = 200 + (index * 53) % 230, bytes = Math.round(duration * 1.4 * MiB);
    let ext = index % 4 === 0 ? 'flac' : 'wav', status = 'missing', extra = {};
    if (id <= 3) { status = 'present'; extra = { stored_bytes: (420 + id) * MiB, producer: id === 2 ? 'OverCue' : 'xz-mods', prepared_at: 1791590400, preset: 'balanced', can_verify: true }; }
    if (id === 3) { ext = 'mp3'; Object.assign(extra, { timeline: 'gapless', needs_regeneration: true }); }
    if (id === 4) ext = 'mp3';
    if (id === 5) { ext = 'aiff'; status = 'unsupported'; }
    if (id === 6) status = 'unavailable';
    const title = id === 4 ? names[0] : id === 5 ? 'Paper Lanterns' : id === 6 ? 'Fever Dream (VIP)' : id <= 3 ? ['Neon Tide', 'Halcyon Drive', 'Atlas'][id - 1] : (names[id % names.length] + ' ' + id);
    return { id, title, artist: 'Artist ' + (id % 12), duration, file_path: '/Contents/Artist ' + (id % 12) + '/' + title + '.' + ext, source: 'E:\\Contents\\' + title + '.' + ext,
      stem_status: status, stem_label: status, can_generate: status === 'missing' || (status === 'present' && id !== 2), can_verify: false,
      estimated_bytes: bytes, estimate: { bytes, low_bytes: Math.round(bytes * 0.8), high_bytes: Math.round(bytes * 1.2), fits_format: true }, ...extra };
  });
  fx.byId = id => fx.tracks.find(track => track.id === Number(id));
  const counts = () => { const c = { present: 0, missing: 0, unsupported: 0, unavailable: 0, error: 0 }; for (const track of fx.tracks) c[track.stem_status]++; return c; };
  const browse = request => {
    const status = request.status, query = (request.query || '').toLowerCase();
    const matched = fx.tracks.filter(track => (!status || (status === 'cant' ? ['unsupported', 'unavailable', 'error'].includes(track.stem_status) : track.stem_status === status)) && (track.title + ' ' + track.artist).toLowerCase().includes(query));
    const offset = Math.min(request.offset || 0, Math.max(0, Math.floor((matched.length - 1) / 40) * 40));
    return { volume: request.volume, identity: { serial: 'fixture-' + request.volume }, total: fx.tracks.length, matched: matched.length, offset, page_size: 40, free_bytes: fx.free, total_bytes: fx.total,
      index_bytes: fx.indexBytes, summary: { counts: counts(), stems_bytes: fx.stemsBytes, tracks: fx.tracks.length }, size_model: { learned: true, observations: 14 }, tracks: matched.slice(offset, offset + 40), index_error: null };
  };
  const enginePlan = () => mode === 'intelmac' ? { supported: false, device: 'cpu' } : mode === 'cpu'
    ? { device: 'cpu', cpu_name: 'Intel Core i5-8500', cpu_cores: 6, ram_bytes: 16 * GiB, presets: [{ id: 'balanced', seconds_per_track: 480 }, { id: 'fast', seconds_per_track: 240 }, { id: 'light' }], installed_preset: fx.installed, download_bytes_estimate: 0.6 * GiB }
    : { device: 'cuda', device_name: 'NVIDIA GeForce RTX 4070 Ti SUPER', vram_bytes: 16 * GiB, recommended: 'balanced', download_bytes_estimate: 2.4 * GiB, installed_preset: fx.installed,
        presets: [{ id: 'balanced', seconds_per_track: 60 }, { id: 'fast', seconds_per_track: 30 }, { id: 'light' }], parts: [{ id: 'runtime', bytes: 1.9 * GiB }, { id: 'models', bytes: 520 * MiB }], data_drive: 'C:', data_free_bytes: 212 * GiB };
  const batchItems = (ids, done, current) => ids.map((id, index) => ({ id: String(id), title: fx.byId(id).title, artist: fx.byId(id).artist,
    state: index < done ? 'prepared' : index === done ? current : 'queued', stage: index < done ? 'complete' : index === done ? fx.batch.stage : 'waiting' }));
  const plans = {
    review: { files: [['vox_lead.wav', 'vocals', 0.95], ['kick.wav', 'drums', 0.95], ['hats_loop.wav', 'drums', 0.7], ['track_07_a.wav', null, 0], ['stem3.wav', null, 0]], drum: false, verdict: 'needs_review', ms: 23 },
    two: { files: [['Glass Hours (Vocals).mp3', 'vocals', 0.95], ['Glass Hours (Instrumental).mp3', 'instrumental', 0.95]], drum: true, verdict: 'ready', ms: 41 }
  };
  const importPlan = request => {
    const plan = plans[fx.importPlan], files = plan.files.map(([name, group, confidence]) => ({ name, path: 'C:\\Stems\\' + name, group, label: group && name.replace(/\.\w+$/, ''), confidence, origin: 'name', frames: 17000000, seconds: 385.5 }));
    return { format: 'stem-import-plan/1', file_path: request.file_path, track_frames: Math.round(fx.byId(fx.tracks.find(track => track.file_path === request.file_path).id).duration * 44100), files,
      mapping: { verdict: plan.verdict === 'ready' ? 'auto' : 'needs_review', source: 'generic', layout: plan.drum ? 'vocals_instrumental' : 'groups', warnings: [] },
      alignment: { offset_samples: Math.round(plan.ms * 44.1), offset_ms: plan.ms, gain: 1, correlation: 0.998, residual_db: -38, verdict: 'aligned' }, fit_length: false,
      needs_drum_split: plan.drum, verdict: plan.verdict, warnings: [], import_request: { files: files.filter(file => file.group).map(file => ({ path: file.path, group: file.group })), alignment: { offset_samples: Math.round(plan.ms * 44.1), gain: 1 } } };
  };
  const status = request => {
    switch (request.method) {
      case 'discover_stem_usbs': return { running: false, ok: true, result: { drives: fx.drives } };
      case 'browse_stem_library': return { running: false, ok: true, result: browse(request) };
      case 'plan_engine_setup': return { running: false, ok: true, result: enginePlan() };
      case 'list_mod_versions': return { running: false, ok: true, result: { format: 'xz-mod-versions/1', versions: [{ id: 'v1', label: 'Fixture build' }], selected: { id: 'v1', pinned: true, revision: 1 }, bundled_id: 'v1' } };
      case 'inspect_overcue': return { running: false, ok: true, result: { format: 'overcue-stems/4', verified_mixes: 7, all_pages_verified: true, source_identity_verified: true } };
      case 'plan_stem_import': return { running: false, ok: true, result: importPlan(request) };
      case 'import_grouped_stems': fx.lastImport = request; Object.assign(fx.tracks.find(track => track.file_path === request.file_path), { stem_status: 'present', stored_bytes: 534 * MiB, producer: 'xz-mods', can_verify: true }); return { running: false, ok: true, result: { format: 'overcue-index/1', stored_bytes: 534 * MiB, verified_mixes: 7, all_pages_verified: true, source_identity_verified: true } };
      case 'setup_engine':
        if (fx.setupFrame === 'installing') return { running: true, stage: 'models', message: 'Checking models', pct: 60, done_bytes: 310 * MiB, total_bytes: 520 * MiB, eta_seconds: 70 };
        if (fx.setupFrame === 'test') return { running: true, stage: 'separate-vocals', message: 'Testing', pct: 72 };
        if (fx.setupFrame === 'driver') return { running: false, ok: false, error: 'Graphics driver too old', error_code: 'driver', error_detail: { min_driver: '525', driver: '516.94' } };
        fx.installed = request.preset;
        return { running: false, ok: true, result: { preset: request.preset, installed: true, test: { seconds: 5.8 } } };
      case 'generate_library_stem_batch': {
        const ids = request.track_ids;
        if (fx.cancelNext) { fx.cancelNext = false; return { running: false, ok: false, cancelled: true, error: 'Cancelled', result: { format: 'xz-stem-batch/1', cancelled: true, aborted: false, items: batchItems(ids, fx.batch.done, 'cancelled') } }; }
        if (fx.batch.aborted) return { running: false, ok: false, error: 'The USB was removed', result: { format: 'xz-stem-batch/1', cancelled: false, aborted: true, error: 'The USB was removed', items: batchItems(ids, fx.batch.done, 'failed') } };
        if (fx.batch.final) {
          const items = ids.map((id, index) => fx.batch.failed.includes(index) ? { id: String(id), state: 'failed', stage: 'failed', error: fx.batch.error || 'Graphics card ran out of memory' } : { id: String(id), state: 'prepared', stage: 'complete' });
          fx.stemsBytes += items.filter(item => item.state === 'prepared').reduce((sum, item) => sum + fx.byId(item.id).estimate.bytes, 0);
          return { running: false, ok: true, result: { format: 'xz-stem-batch/1', cancelled: false, aborted: false, items } };
        }
        return { running: true, stage: fx.batch.stage, message: 'Working', pct: fx.batch.pct, eta_seconds: fx.batch.eta, batch: { format: 'xz-stem-batch/1', items: batchItems(ids, fx.batch.done, 'running') } };
      }
    }
    throw Error('Unexpected method ' + request.method);
  };
  window.__TAURI__ = { core: { invoke: async (command, args) => {
    fx.calls.push(command);
    if (command === 'app_information') return { version: '0.3.0', platform: 'windows', installed: true, release_notes: { releases: [] } };
    if (command === 'check_app_update') return { phase: 'current', message: 'Up to date' };
    if (command === 'start_job') { fx.requests.push(args.request); return fx.requests.length; }
    if (command === 'job_status') return status(fx.requests[args.id - 1]);
    if (command === 'cancel_job') { fx.cancelNext = true; return {}; }
    if (command === 'pick_stem_files') return fx.pickFiles;
    if (command === 'pick_path') return 'F:\\';
    throw Error('Unexpected command ' + command);
  } } };
  window.button = (text, root = document) => [...root.querySelectorAll('button')].find(item => item.textContent.trim() === text && item.checkVisibility() && !item.closest('nav'));
  window.txt = selector => document.querySelector(selector)?.textContent.replace(/\s+/g, ' ').trim() || '';
}

const server = await serveUi(app);
const profile = await mkdtemp(join(tmpdir(), 'xz-stems-'));
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
  const context = () => evaluate(`'phase: '+stemPhase+' job: '+activeJob+' requests: '+fx.requests.map(r=>r.method).join(',')+'\\n  connect: '+txt('#stem-connect')+'\\n  bar: '+txt('#st-bar')+'\\n  empty: '+txt('#st-empty')+'\\n  tabs: '+txt('#st-tabs')+'\\n  engine: '+txt('#st-engine')+'\\n  import: '+txt('#st-import-wiz')`);
  const waitFor = async (expression, label) => {
    for (let attempt = 0; attempt < 200; attempt++) { if (await evaluate(expression)) return; await new Promise(done => setTimeout(done, 25)); }
    throw Error('Timed out: ' + label + '\n  ' + await context() + '\n  page errors: ' + errors.join(' / '));
  };
  const check = async (expression, label) => {
    if (!await evaluate(expression)) throw Error('Failed: ' + label + '\n  ' + await context() + '\n  page errors: ' + errors.join(' / '));
    passed++; console.log('ok', label);
  };
  await send('Runtime.enable');
  socket.addEventListener('message', event => { const data = JSON.parse(event.data); if (data.method === 'Runtime.exceptionThrown') errors.push(data.params.exceptionDetails.exception?.description || data.params.exceptionDetails.text); });
  await send('Page.enable');
  await send('Page.addScriptToEvaluateOnNewDocument', { source: "window.__csp=[];document.addEventListener('securitypolicyviolation',event=>window.__csp.push(event.violatedDirective+' '+(event.blockedURI||'inline')+' '+(event.sourceFile||'').split('/').pop()+':'+event.lineNumber));" + '(' + installMock + ')();' });
  await send('Emulation.setDeviceMetricsOverride', { width: 1100, height: 720, deviceScaleFactor: 1, mobile: false });
  const open = async mode => {
    await send('Page.navigate', { url: 'about:blank' });
    await send('Page.navigate', { url: server.url + 'index.html#' + mode });
    await waitFor(`typeof renderStems==='function'&&Boolean(document.querySelector('nav button[data-page="stems"]')?.onclick)&&activeJob===null`, 'app loaded');
    await evaluate(`document.querySelector('nav button[data-page="stems"]').click()`);
  };
  const ready = `activeJob===null&&stemPhase==='ready'&&!document.getElementById('st-page').classList.contains('hidden')`;
  const selectMissing = count => evaluate(`(()=>{let n=0;for(;n<${count};n++){const box=[...document.querySelectorAll('#stem-tracks input[data-pick]')].find(item=>!item.checked&&!item.disabled);if(!box)break;box.click();}return n;})()`);
  const selectedBytes = () => evaluate(`[...stemSelection.values()].reduce((sum,track)=>sum+track.estimate.bytes,0)`);
  const gb = bytes => (bytes / 1073741824).toFixed(1) + ' GB';

  await open('nousb');
  await waitFor(`!document.getElementById('st-empty').classList.contains('hidden')`, 'no-USB state');
  await check(`txt('#st-empty')==='Connect a Rekordbox USBPlug in the USB you export to from Rekordbox. XZ Mods finds it and lists its tracks.Connect a USB'&&document.querySelectorAll('#st-empty button').length===1`, 'no USB shows one sentence and one Connect a USB button');
  await evaluate(`fx.drives=[{volume:'E:\\\\',label:'KINGSTON',removable:true}];button('Connect a USB').click()`);
  await check(`document.getElementById('stem-connect').open`, 'Connect a USB opens the connect wizard');
  await evaluate(`document.getElementById('stem-connect-find').click()`);
  await waitFor(ready + `&&!document.getElementById('stem-connect').open`, 'connect wizard finds the USB and closes');
  passed++; console.log('ok', 'connect wizard: find lists the library and closes');

  await open('cuda');
  await waitFor(ready, 'library loaded');
  await check(`txt('#st-bar .st-count')==='212 tracks · 3 ready'&&txt('#st-bar .st-free')==='42.9 GB free of 59.6 GB'`, 'bar shows track counts and free space');
  await check(`txt('#st-bar .st-estimate')==='No tracks selected·207 tracks need stems'&&button('Prepare stems').disabled`, 'nothing selected: estimate line and disabled Prepare stems');
  await check(`txt('#st-bar .st-engine')==='Engine: Balanced on RTX 4070 Ti SUPER · Change'`, 'engine line names the preset and the graphics card');
  await check(`txt('#st-tabs')==="All212Missing207Ready3Can't prepare2"&&document.querySelector('#st-tabs [aria-pressed=true]').textContent==='All212'`, 'filter tabs carry library-wide counts');
  await check(`(()=>{const row=id=>document.querySelector('#stem-tracks tr[data-id="'+id+'"]');return row(4).querySelector('.st-fmt').textContent==='MP3'&&row(4).querySelector('.st-pill').textContent==='Missing'&&row(4).querySelector('input[data-pick]')
    &&row(5).querySelector('.st-pill').textContent==='Not yet'&&row(5).querySelector('.st-sub').textContent==='AIFF support is coming'&&!row(5).querySelector('input')
    &&row(6).querySelector('.st-pill').textContent==='No audio file'&&row(1).querySelector('.st-pill').textContent==='Ready'&&row(1).querySelector('.st-nocheck')
    &&row(7).querySelector('.st-size').textContent.startsWith('\\u2248 ')&&row(1).querySelector('.st-size').textContent==='421 MB';})()`, 'rows: MP3 behaves like any Missing track, AIFF Not yet, missing audio, Ready with size');
  await check(`(()=>{const row=id=>document.querySelector('#stem-tracks tr[data-id="'+id+'"]');return row(3).querySelector('.st-pill').textContent==='Ready'&&row(3).querySelector('.st-sub').textContent==='Prepare again · MP3 timing'&&row(3).querySelector('.st-sub').dataset.tone==='warn'&&!row(1).querySelector('.st-sub');})()`, 'an MP3 bundle from before the timing fix says Prepare again; other Ready rows stay quiet');
  await evaluate(`document.querySelector('#stem-tracks tr[data-id="3"] .st-open-button').click()`);
  await check(`txt('#stem-tracks tr.st-drawer[data-for="3"] .st-regen').startsWith('Made from an MP3 before XZ Mods 0.3.0-preview.2, so these stems start 25 ms early')&&Boolean(button('Prepare again',document.querySelector('#stem-tracks tr.st-drawer[data-for="3"]')))`, 'its details say why and offer Prepare again');
  await evaluate(`document.querySelector('#stem-tracks tr[data-id="3"] .st-open-button').click()`);
  await check(`!document.getElementById('stems').innerText.includes('OverCue')&&!/beta/i.test(document.getElementById('stems').innerText)`, 'no OverCue or Beta wording with every drawer closed');
  await evaluate(`document.querySelector('#stem-tracks tr[data-id="2"] .st-open-button').click()`);
  await check(`(document.body.innerText.match(/OverCue/g)||[]).length===1&&txt('#stem-tracks tr.st-drawer').includes('Prepared outside XZ Mods · 422 MB')`, 'OverCue is named exactly once, in the Ready drawer');
  await evaluate(`document.querySelector('#stem-tracks tr[data-id="2"] .st-open-button').click()`);

  assert.equal(await selectMissing(12), 12);
  const bytes12 = await selectedBytes();
  await check(`txt('#st-bar .st-estimate').startsWith('12 tracks selected·about ${gb(bytes12)}·${gb(42.9 * 1073741824 - bytes12)} free after')&&document.querySelector('#st-bar .st-estimate').dataset.fit==='ok'`, 'twelve selected: estimate line with free space after');
  await check(`button('Prepare 12 tracks')&&!button('Prepare 12 tracks').disabled&&txt('#st-tabs').endsWith('Selected12')`, 'primary reads Prepare 12 tracks and the Selected tab appears');
  await check(`document.querySelector('#st-bar .st-range').title==='Estimate learned from 14 tracks prepared on this computer'`, 'range tooltip says where the estimate comes from');

  await evaluate(`fx.free=${bytes12}+1.4*1073741824;browseStemLibrary(stemLibrary.volume,0)`);
  await waitFor(ready + `&&document.querySelector('#st-bar .st-estimate')?.dataset.fit==='tight'`, 'tight state');
  await check(`txt('#st-bar .st-estimate').includes('only 1.4 GB free after')&&button('Prepare 12 tracks')`, 'tight fit: only 1.4 GB free after, still Prepare 12 tracks');
  const free = bytes12 - 2.3 * 1073741824;
  const fit = await evaluate(`(()=>{let used=0,n=0;for(const t of stemSelection.values()){if(used+t.estimate.bytes>${free})break;used+=t.estimate.bytes;n++;}return n;})()`);
  await evaluate(`fx.free=${free};browseStemLibrary(stemLibrary.volume,0)`);
  await waitFor(ready + `&&document.querySelector('#st-bar .st-estimate')?.dataset.fit==='over'`, 'over state');
  await check(`txt('#st-bar .st-estimate').includes('2.3 GB more than this USB has free')&&Boolean(button('Prepare ${fit} that fit'))`, `does not fit: 2.3 GB short and Prepare ${fit} that fit`);
  await evaluate(`fx.free=42.9*1073741824;fx.indexBytes=4*1048576-5*1200;browseStemLibrary(stemLibrary.volume,0)`);
  await waitFor(ready + `&&document.querySelector('#st-bar .st-estimate')?.dataset.fit==='index'`, 'index full state');
  await check(`txt('#st-bar .st-estimate')==='12 tracks selected·the stem index on this USB has room for 5 more'&&Boolean(button('Prepare 5 that fit'))`, 'stem index full: room for 5 more and Prepare 5 that fit');
  await evaluate(`fx.indexBytes=20000;browseStemLibrary(stemLibrary.volume,0)`);
  await waitFor(ready + `&&Boolean(button('Prepare 12 tracks'))`, 'back to fits');

  await evaluate(`stemEngine.testSeconds=null;fx.batch={done:0,stage:'decode',pct:40};button('Prepare 12 tracks').click()`);
  await waitFor(`stemBatch?.running&&document.querySelector('#stem-tracks .st-pill[data-s=running]')`, 'batch running');
  await check(`/^Preparing 1 of 12·.+·Reading the track·\\d+%·Timing this computer, 0:0\\d so far$/.test(txt('#st-bar .st-progress-line'))`, 'first run: Timing this computer, elapsed so far');
  await check(`button('Cancel')&&!button('Cancel').disabled&&button('Change').disabled&&[...document.querySelectorAll('#stem-tracks input[data-pick]')].every(box=>box.disabled)`, 'during a batch: Cancel is the only action, rows and Change locked');
  await evaluate(`fx.batch={done:3,stage:'separate-drums',pct:62,eta:540}`);
  await waitFor(`txt('#st-bar .st-progress-line').startsWith('Preparing 4 of 12')`, 'fourth track running');
  const ids = await evaluate(`stemBatch.items.map(item=>item.id)`);
  await check(`(()=>{const row=id=>document.querySelector('#stem-tracks tr[data-id="'+id+'"]');const pill=id=>row(id).querySelector('.st-pill').textContent;
    return ['${ids[0]}','${ids[1]}','${ids[2]}'].every(id=>pill(id)==='ReadyNew')&&pill('${ids[3]}')==='Preparing 65%'&&row('${ids[3]}').querySelector('.st-sub').textContent==='Separating drums'
      &&pill('${ids[4]}')==='Queued'&&pill('${ids[11]}')==='Queued';})()`, 'rows change in place: Ready New, Preparing 65% (blended), Queued');
  await check(`/^Preparing 4 of 12·.+·Separating drums·65%·about 9 min left$/.test(txt('#st-bar .st-progress-line'))&&document.querySelector('#st-bar progress').value>0.3`, 'progress line: stage, blended percent and about 9 min left');
  await evaluate(`fx.batch={done:3,stage:'separate-drums',pct:20,eta:530}`);
  await new Promise(done => setTimeout(done, 500));
  await check(`txt('#stem-tracks tr[data-id="${ids[3]}"] .st-pill')==='Preparing 65%'`, 'displayed percent never goes down');
  await evaluate(`fx.batch={final:true,failed:[3]}`);
  await waitFor(`stemBatch&&!stemBatch.running&&stemBatch.ending&&activeJob===null`, 'batch finished');
  await check(`/^Done in \\d+ s·11 prepared·1 failed·\\d+\\.\\d GB written$/.test(txt('#st-bar .st-summary'))&&Boolean(button('Retry 1 failed'))`, 'finished with a failure: summary and Retry 1 failed');
  await check(`txt('#stem-tracks tr[data-id="${ids[3]}"] .st-pill')==='Failed'&&txt('#stem-tracks tr[data-id="${ids[3]}"] .st-sub')==='Graphics card ran out of memory'&&stemSelection.size===1`, 'failed row shows the reason and stays selected; prepared rows leave the selection');

  await evaluate(`fx.batch={done:0,stage:'separate-vocals',pct:30};button('Retry 1 failed').click()`);
  await waitFor(`stemBatch?.running&&Boolean(button('Cancel',document.getElementById('st-bar')))`, 'retry started');
  await evaluate(`button('Cancel',document.getElementById('st-bar')).click()`);
  await waitFor(`stemBatch&&!stemBatch.running&&stemBatch.ending?.kind==='cancelled'`, 'cancelled');
  await check(`txt('#st-bar .st-summary')==='Cancelled·0 prepared and kept·1 not started'&&Boolean(button('Prepare 1 remaining'))&&txt('#stem-tracks tr[data-id="${ids[3]}"] .st-pill')==='Cancelled'`, 'cancel: summary, Prepare 1 remaining, row Cancelled');

  await evaluate(`stemFilter='all';document.querySelector('#stem-tracks tr[data-id="7"] .st-open-button').click()`);
  await evaluate(`button('Import stems made elsewhere').click()`);
  await check(`document.getElementById('st-import-wiz').open&&txt('#st-import-wiz .st-target').startsWith('For '+fx.byId(7).title)`, 'import wizard opens from the row with its track');
  await evaluate(`button('Choose files',document.getElementById('st-import-wiz')).click()`);
  await waitFor(`imp?.files.length===5`, 'files added');
  await evaluate(`button('Next',document.getElementById('st-import-wiz')).click()`);
  await waitFor(`imp?.step==='check'`, 'plan ready');
  await check(`txt('#st-import-title')==='2 files need a place'&&button('Place 2 files to continue').disabled&&fx.requests.at(-1).method==='plan_stem_import'`, 'needs review: 2 files need a place, primary disabled');
  await check(`txt('#st-import-wiz .st-slots')==='Vocalsvox_leadDrumskick, hats_loopHarmonicsNothing yet'&&document.querySelectorAll('#st-import-wiz tr[data-need]').length===2`, 'slots list their files; empty Harmonics reads Nothing yet; two rows need a place');
  await evaluate(`(()=>{const selects=[...document.querySelectorAll('#st-import-wiz tr[data-need] select')];selects[0].value='harmonics';selects[0].dispatchEvent(new Event('change'));})()`);
  await check(`txt('#st-import-title')==='1 file needs a place'&&Boolean(button('Place 1 file to continue'))`, 'placing one file updates the count');
  await evaluate(`(()=>{const select=document.querySelector('#st-import-wiz tr[data-need] select');select.value='mix';select.dispatchEvent(new Event('change'));})()`);
  await check(`txt('#st-import-title')==='Everything has a place'&&txt('#st-import-wiz .st-note').startsWith('In time with the track on your USB. Shifted 23 ms and matched the level.')&&txt('#st-import-wiz .st-foot-left')==='Leftover difference \\u221238 dB'`, 'all placed: Everything has a place with the timing note');
  await evaluate(`button('Next',document.getElementById('st-import-wiz')).click()`);
  await check(`txt('#st-import-title')==='Ready to write'&&txt('#st-import-wiz .st-kv').includes('KINGSTON (E:)')&&Boolean(button('Write stems to USB'))`, 'write step names the USB and the size');
  await evaluate(`button('Write stems to USB').click()`);
  await waitFor(`imp?.step==='done'`, 'import written');
  await check(`txt('#st-import-title')==='Stems written and checked'&&txt('#st-import-wiz').includes('is ready for the XZ. 534 MB written.')`, 'import done: written and checked');
  await check(`(()=>{const files=fx.lastImport.files;return fx.lastImport.method==='import_grouped_stems'&&files.length===4&&files.some(f=>f.path.endsWith('track_07_a.wav')&&f.group==='harmonics')&&!files.some(f=>f.path.endsWith('stem3.wav'))&&fx.lastImport.alignment.gain===1;})()`, 'write uses the plan import_request with the user overrides');
  await evaluate(`button('Done',document.getElementById('st-import-wiz')).click()`);
  await waitFor(ready + `&&!document.getElementById('st-import-wiz').open`, 'import closed');
  await check(`txt('#stem-tracks tr[data-id="7"] .st-pill')==='ReadyNew'&&txt('#stem-tracks tr[data-id="7"] .st-size')==='534 MB'`, 'the imported row turns Ready New with its real size');

  await open('fresh');
  await waitFor(ready, 'fresh library');
  await check(`Boolean(button('Set up stem engine'))&&txt('#st-bar .st-engine')==='Stem engine not set up yet · a few minutes, once'`, 'engine not set up: primary becomes Set up stem engine');
  await selectMissing(12);
  await check(`Boolean(button('Set up stem engine'))`, 'selecting keeps the setup button');
  await evaluate(`button('Set up stem engine').click()`);
  await waitFor(`document.getElementById('st-engine').open&&txt('#st-engine-title')==='Graphics card found'`, 'engine check');
  await check(`txt('#st-engine .st-found')==='\\u25a6NVIDIA GeForce RTX 4070 Ti SUPER16 GB video memory'`, 'engine check: graphics card card');
  await evaluate(`button('Next',document.getElementById('st-engine')).click()`);
  await check(`txt('#st-engine-title')==='Choose quality'&&document.querySelector('#st-engine input[value=balanced]').checked&&txt('#st-engine .st-preset').includes('Recommended')&&txt('#st-engine .st-preset').includes('About 1 minute per track on this computer.')`, 'choose: Balanced recommended with a per-track time');
  await evaluate(`button('Next',document.getElementById('st-engine')).click()`);
  await check(`txt('#st-engine-title')==='Download and install'&&txt('#st-engine').includes('XZ Mods downloads 2.4 GB to this computer, once.')&&txt('#st-engine .st-kv')==='Graphics runtime1.9 GBBalanced models520 MBSpace on C:212 GB free'`, 'install: size, parts and free space');
  await evaluate(`button('Download and install').click()`);
  await waitFor(`txt('#st-engine-title')==='Installing'&&txt('#st-engine .st-stages').includes('310 of 520 MB · about 1 min left')`, 'installing progress');
  passed++; console.log('ok', 'installing: stage list with 310 of 520 MB · about 1 min left');
  await evaluate(`fx.setupFrame='test'`);
  await waitFor(`txt('#st-engine-title')==='Testing on 30 seconds of your music'`, 'test step');
  await check(`txt('#st-engine').includes('Separating 0:45 to 1:15 of '+[...stemSelection.values()][0].title+'. Nothing is written to the USB.')`, 'test runs on the first selected track');
  await evaluate(`fx.setupFrame='done'`);
  await waitFor(`txt('#st-engine-title')==='Stem engine ready'`, 'engine ready');
  await check(`txt('#st-engine').includes('30 seconds took 5.8 s. A 5-minute track will take about a minute.')&&Boolean(button('Prepare 12 tracks',document.getElementById('st-engine')))`, 'engine ready ends on Prepare 12 tracks');
  await evaluate(`fx.batch={done:0,stage:'decode',pct:5};button('Prepare 12 tracks',document.getElementById('st-engine')).click()`);
  await waitFor(`stemBatch?.running`, 'batch from wizard');
  await check(`fx.requests.at(-1).preset==='balanced'&&txt('#st-bar .st-engine')==='Engine: Balanced on RTX 4070 Ti SUPER · Change'`, 'the wizard hands off straight into the batch');
  await evaluate(`fx.batch={final:true,failed:[]}`);
  await waitFor(`stemBatch&&!stemBatch.running&&stemBatch.ending?.kind==='all'`, 'all prepared');
  await check(`/^Done in \\d+ s·12 prepared·\\d+\\.\\d GB written$/.test(txt('#st-bar .st-summary'))&&button('Prepare stems').disabled&&stemSelection.size===0`, 'all prepared: done line, selection cleared, Prepare stems disabled');

  await open('fresh');
  await waitFor(ready, 'fresh library for 2-stem import');
  await evaluate(`fx.importPlan='two';fx.pickFiles=['C:\\\\Stems\\\\Glass Hours stems.zip'];button('Import stems').click()`);
  await evaluate(`button('Choose files',document.getElementById('st-import-wiz')).click()`);
  await waitFor(`txt('#st-import-wiz').includes('Which track are these for?')`, 'track picker');
  await check(`button('Next',document.getElementById('st-import-wiz')).disabled&&txt('#st-import-wiz .st-note')==='Glass Hours stems.zip'`, 'toolbar import asks which track and keeps Next disabled');
  await evaluate(`button(fx.byId(4).title+' · '+fx.byId(4).artist).click()`);
  await evaluate(`button('Next',document.getElementById('st-import-wiz')).click()`);
  await waitFor(`imp?.step==='check'`, '2-stem plan');
  await check(`txt('#st-import-title')==='Drums need the stem engine'&&Boolean(button('Set up stem engine',document.getElementById('st-import-wiz')))&&!document.getElementById('st-import-wiz').innerText.includes('Write without')`, '2-stem without engine: one Set up stem engine button, no write-without-drums');
  await evaluate(`button('Set up stem engine',document.getElementById('st-import-wiz')).click()`);
  await waitFor(`document.getElementById('st-engine').open&&!document.getElementById('st-import-wiz').open&&txt('#st-engine-title')==='Graphics card found'`, 'engine wizard from import');
  await evaluate(`fx.setupFrame='done';button('Next',document.getElementById('st-engine')).click()`);
  await evaluate(`button('Next',document.getElementById('st-engine')).click()`);
  await evaluate(`button('Download and install').click()`);
  await waitFor(`txt('#st-engine-title')==='Stem engine ready'`, 'engine ready for import');
  await evaluate(`button('Done',document.getElementById('st-engine')).click()`);
  await waitFor(`document.getElementById('st-import-wiz').open&&txt('#st-import-title')==='Everything has a place'`, 'import resumes');
  await check(`txt('#st-import-wiz .st-slots').includes('Separated from the instrumental')&&txt('#st-import-wiz').includes('Drums will be separated from the instrumental on this computer while writing, about ')&&txt('#st-import-wiz').includes('Shifted 41 ms to undo the MP3 start delay.')`, 'after setup the import continues with the plan intact');

  await open('cpu');
  await waitFor(ready, 'cpu library');
  await evaluate(`button('Set up stem engine').click()`);
  await waitFor(`txt('#st-engine-title')==='No graphics card to use'`, 'cpu check');
  await check(`txt('#st-engine .st-found').includes('Intel Core i5-8500')&&txt('#st-engine .st-found').includes('6 cores, 16 GB memory')`, 'cpu: processor card');
  await evaluate(`button('Next',document.getElementById('st-engine')).click()`);
  await check(`document.querySelector('#st-engine input[value=fast]').checked&&document.querySelector('#st-engine input[value=fast]').closest('label').textContent.includes('Recommended')&&txt('#st-engine').includes('About 4 minutes per track on this computer.')`, 'cpu: Fast is recommended by default');
  await evaluate(`fx.setupFrame='driver';button('Next',document.getElementById('st-engine')).click()`);
  await evaluate(`button('Download and install').click()`);
  await waitFor(`txt('#st-engine-title')==='Graphics driver too old'`, 'driver failure');
  await check(`txt('#st-engine .st-fail')==='The graphics engine needs NVIDIA driver 525 or newer. This computer has 516.94.Use the processor now, or update the driver and check again for full speed.'&&Boolean(button('Use the processor'))&&Boolean(button('Open NVIDIA drivers'))`, 'driver too old: one reason, Use the processor');
  await evaluate(`fx.setupFrame='done';button('Use the processor').click()`);
  await waitFor(`txt('#st-engine-title')==='Stem engine ready'`, 'cpu fallback ready');
  await check(`fx.requests.at(-1).device==='cpu'&&txt('#st-engine .fine')==='Fast on this processor'`, 'Use the processor reruns setup on the CPU');

  await open('intelmac');
  await waitFor(ready, 'intel mac library');
  await check(`Boolean(button('Import stems',document.getElementById('st-bar')))&&!document.querySelector('#stem-tracks input[data-pick]')&&txt('#st-bar .st-engine')==='Making stems needs a Windows computer or an Apple silicon Mac. You can still import stems here.'&&document.getElementById('st-select-all-label').classList.contains('hidden')`, 'Intel Mac: Import stems is the primary, no checkboxes');

  await open('cuda');
  await waitFor(ready, 'library for the batch limit');
  await evaluate(`fx.free=100*1073741824;fx.total=128*1073741824;browseStemLibrary(stemLibrary.volume,0)`);
  await waitFor(ready + `&&stemLibrary.free===100*1073741824`, 'larger USB');
  for (let page = 0; page < 3; page++) {
    await evaluate(`document.getElementById('st-select-all').click()`);
    await evaluate(`document.getElementById('stem-next').click()`);
    await waitFor(ready + `&&stemLibrary.offset===${(page + 1) * 40}`, 'next page');
  }
  await evaluate(`document.getElementById('st-select-all').click()`);
  await check(`stemSelection.size===100&&txt('#st-bar .st-estimate').startsWith('100 tracks selected (most per batch)·about ')&&Boolean(button('Prepare 100 tracks'))&&document.querySelector('#stem-tracks input[data-pick]:not(:checked)').disabled`, 'header checkbox stops at 100: (most per batch), unchecked rows locked');
  const fitFree = await evaluate(`[...stemSelection.values()].slice(0,7).reduce((sum,track)=>sum+track.estimate.bytes,0)+1048576`);
  await evaluate(`fx.free=${fitFree};browseStemLibrary(stemLibrary.volume,stemLibrary.offset)`);
  await waitFor(ready + `&&Boolean(button('Prepare 7 that fit'))`, 'seven fit');
  await evaluate(`fx.batch={final:true,failed:[5,6],error:'Not enough USB space for this track. Free space and try again.'};button('Prepare 7 that fit').click()`);
  await waitFor(`stemBatch&&!stemBatch.running&&stemBatch.ending`, 'fit batch finished');
  await check(`fx.requests.filter(r=>r.method==='generate_library_stem_batch').at(-1).track_ids.length===7&&txt('#st-bar .st-summary')==='The USB is full. 5 tracks are prepared; 2 did not fit.'&&Boolean(button('Choose fewer tracks'))`, 'Prepare 7 that fit sends seven; the USB filled up: 2 did not fit');
  await evaluate(`button('Choose fewer tracks').click()`);
  await check(`stemSelection.size===93&&txt('#st-bar .st-estimate').startsWith('93 tracks selected')`, 'Choose fewer tracks drops the two that did not fit and the estimate returns');
  await evaluate(`fx.free=100*1073741824;fx.batch={done:2,stage:'decode',pct:50};browseStemLibrary(stemLibrary.volume,stemLibrary.offset)`);
  await waitFor(ready + `&&Boolean(button('Prepare 93 tracks'))`, 'ninety-three fit');
  await evaluate(`button('Prepare 93 tracks').click()`);
  await waitFor(`stemBatch?.running`, 'batch running before unplug');
  await evaluate(`fx.batch={aborted:true,done:2}`);
  await waitFor(`stemBatch&&!stemBatch.running&&stemBatch.ending`, 'unplugged');
  await check(`txt('#st-bar .st-summary')==='The USB was disconnected. 2 tracks are prepared and nothing was left half-written. Plug it back in to continue.'&&Boolean(button('Continue'))`, 'USB unplugged mid-batch: nothing half-written, Continue');

  const files = ['ui/app.js', 'ui/index.html', 'ui/style.css'];
  const blocks = await evaluate(`(document.getElementById('stems').innerText+document.getElementById('st-engine').innerText+document.getElementById('st-import-wiz').innerText)`);
  assert(!blocks.includes('\u2014'), 'No em dash in the stems page or wizards');
  for (const file of files) {
    const text = await readFile(join(app, file), 'utf8'), start = text.indexOf('// Prepare stems.'), end = text.indexOf('// Library repair.');
    if (file === 'ui/app.js') assert(!text.slice(start, end).includes('\u2014'), 'No em dash in the stems module');
  }
  assert.deepEqual(errors, [], 'No page exceptions');
  assert.deepEqual(await evaluate('window.__csp'), [], 'No CSP violations under the app policy');
  console.log(`PASS: ${passed} stems page checks`);
} finally {
  try {
    const version = await (await fetch(base + '/json/version', { signal: AbortSignal.timeout(2000) })).json();
    const browser = new WebSocket(version.webSocketDebuggerUrl);
    await new Promise(done => { browser.onopen = () => { browser.send(JSON.stringify({ id: 1, method: 'Browser.close' })); setTimeout(done, 300); }; browser.onerror = done; });
  } catch {}
  socket?.close();
  await server.close();
  killBrowser(child, profile);
  assert(dirname(resolve(profile)) === resolve(tmpdir()) && basename(profile).startsWith('xz-stems-'), 'Cleanup must stay inside the generated browser profile');
  await rm(profile, { recursive: true, force: true, maxRetries: 20, retryDelay: 100 });
}
