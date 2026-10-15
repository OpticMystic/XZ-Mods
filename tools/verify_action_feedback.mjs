import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, rm, readFile, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve, dirname, basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { serveUi } from './csp_server.mjs';
import { killBrowser } from './kill_browser.mjs';

// No packages needed. Exercise the actual renderer using Chromium's CDP API.
const app = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const endpoint = process.argv.find(arg => arg.startsWith('--cdp='))?.slice(6);
const output = process.argv.find(arg => arg.startsWith('--output='))?.slice(9);
assert(!output || output.endsWith('.json'), 'Evidence output must use a .json filename');
const mockSource = `window.testMode='error';window.testPickFailure=false;window.testCalls=[];
window.testLibrary={volume:'E:/Test USB',identity:{serial:'feedback-fixture'},total:18,matched:18,offset:0,page_size:50,free_bytes:1024,total_bytes:10737418240,
tracks:Array.from({length:18},(_,i)=>({id:String(i+1),title:'Test track '+(i+1),artist:'Feedback fixture',file_path:'Contents/test-'+i+'.wav',source:'E:/Test USB/Contents/test-'+i+'.wav',duration:120,stem_status:'present',stem_label:'Prepared',can_generate:true,can_verify:true,producer:'xz-mods',stored_bytes:440401920}))};
window.testMockInvoke=async(command,args)=>{
  window.testCalls.push(command);
  if(command==='app_information')return {version:'0.2.1',platform:'windows',installed:true,release_notes:{releases:[]}};
  if(command==='check_app_update')return {phase:'current',message:'Up to date'};
  if(command==='start_job'){window.testRequest=args.request;return 1;}
  if(command==='job_status'){
    if(window.testRequest.method==='discover_stem_usbs')return {running:false,ok:true,message:'Completed',result:{drives:[]}};
    if(window.testRequest.method==='browse_stem_library')return {running:false,ok:true,message:'Completed',result:window.testLibrary};
    if(window.testRequest.method==='list_mod_versions')return {running:false,ok:true,message:'Completed',result:{format:'xz-mod-versions/1',versions:[{id:'v1',label:'Feedback build'}],selected:{id:'v1',pinned:true,revision:1},bundled_id:'v1'}};
    if(window.testMode==='running')return {running:true,ok:false,message:'Preparing stems...'};
    if(window.testMode==='cancelled')return {running:false,ok:false,cancelled:true,message:'Cancelled safely'};
    if(window.testMode==='throw')throw Error('Not enough free space. Free 2 GB and try again.');
    return {running:false,ok:false,error:'Not enough free space. Free 2 GB and try again.',message:'Not enough free space. Free 2 GB and try again.'};
  }
  if(command==='pick_path'){await new Promise(r=>setTimeout(r,20));if(window.testPickFailure)throw Error('The folder could not be opened.');return 'E:/Test USB';}
  if(command==='cancel_job'){window.testMode='cancelled';return {};}
  throw Error('Unexpected test command: '+command);
};`;
const server = endpoint ? null : await serveUi(app);
const profile = endpoint ? null : await mkdtemp(join(tmpdir(), 'xz-feedback-'));
const child = endpoint ? null : spawn(process.env.XZ_TEST_BROWSER || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  ['--headless=new', '--disable-gpu', '--no-first-run', '--remote-debugging-port=0', '--user-data-dir=' + profile, 'about:blank'], { stdio: 'ignore', windowsHide: true });
let socket, base = endpoint;
const results = [];
try {
  if (!base) {
    for (let attempt = 0; attempt < 100; attempt++) {
      try { const port = (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; base = 'http://127.0.0.1:' + port; break; }
      catch { await new Promise(resolve => setTimeout(resolve, 100)); }
    }
    assert(base, 'Browser did not start');
  }
  const targets = await (await fetch(base + '/json/list', { signal: AbortSignal.timeout(5000) })).json();
  const target = endpoint ? targets.find(item => item.type === 'page' && /tauri|localhost/.test(item.url)) : targets.find(item => item.type === 'page');
  assert(target, 'No application page found');
  socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { const timer=setTimeout(()=>reject(Error('CDP connection timed out')),5000);socket.onopen=()=>{clearTimeout(timer);resolve();};socket.onerror=()=>{clearTimeout(timer);reject(Error('CDP connection failed'));}; });
  let sequence = 0;
  const pending = new Map();
  socket.onmessage = event => { const data = JSON.parse(event.data); if (!data.id) return; const task = pending.get(data.id); if (!task) return; pending.delete(data.id); data.error ? task.reject(Error(JSON.stringify(data.error))) : task.resolve(data.result); };
  const send = (method, params = {}) => new Promise((resolve, reject) => { const id = ++sequence;const timer=setTimeout(()=>{pending.delete(id);reject(Error('CDP timed out: '+method));},8000); pending.set(id, { resolve:value=>{clearTimeout(timer);resolve(value);}, reject:error=>{clearTimeout(timer);reject(error);} }); socket.send(JSON.stringify({ id, method, params })); });
  const evaluate = async expression => {
    const value = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    if (value.exceptionDetails) throw Error(value.exceptionDetails.text + ': ' + JSON.stringify(value.exceptionDetails.exception));
    return value.result.value;
  };
  await send('Page.enable');
  if (!endpoint) {
    await send('Page.addScriptToEvaluateOnNewDocument', { source: "window.__csp=[];document.addEventListener('securitypolicyviolation',event=>window.__csp.push(event.violatedDirective+' '+(event.blockedURI||'inline')+' '+(event.sourceFile||'').split('/').pop()+':'+event.lineNumber));" + mockSource + 'window.__TAURI__={core:{invoke:window.testMockInvoke}};' });
    await send('Page.navigate', { url: server.url + 'index.html' });
    for (let attempt = 0; attempt < 100; attempt++) {
      if (await evaluate('Boolean(document.getElementById("setup-games-usb")?.onclick)&&window.setupWizard?.state.usbs!==null&&activeJob===null')) break;
      await new Promise(resolve => setTimeout(resolve, 50));
    }
  }
  // Tauri's installed IPC bridge is immutable. Inject notices at the renderer
  // boundary and use real input validation; never start a native test job.
  if (endpoint) {
    assert(await evaluate('activeJob===null&&!appUpdating'), 'Do not interrupt a running operation');
    await evaluate(mockSource);
  }
  const waitFor = async expression => {
    for (let attempt = 0; attempt < 150; attempt++) {
      if (await evaluate(expression)) return;
      await new Promise(resolve => setTimeout(resolve, 20));
    }
    throw Error('Timed out: ' + expression);
  };
  const measure = async (actionId, name) => {
    const measured = await evaluate(`(()=>{
      const action=document.querySelector(${JSON.stringify(actionId)}),panel=document.getElementById('job-panel');
      const a=action.getBoundingClientRect(),context=(action.closest('#stem-tracks tr')||action.closest('.actions')||action).getBoundingClientRect(),p=panel.getBoundingClientRect(),m=document.getElementById('job-message').getBoundingClientRect();
      const clip=panel.closest('.table-scroll,dialog')?.getBoundingClientRect();
      return {width:innerWidth,height:innerHeight,actionTop:a.top,actionBottom:a.bottom,contextBottom:context.bottom,panelTop:p.top,messageBottom:m.bottom,message:document.getElementById('job-message').textContent,visible:panel.checkVisibility(),nearAction:p.top>=context.bottom-1&&p.top-context.bottom<80,inViewport:m.top>=Math.max(0,clip?.top||0)&&m.bottom<=Math.min(innerHeight,clip?.bottom||innerHeight)};
    })()`);
    measured.scenario = name;
    results.push(measured);
    console.log(JSON.stringify(measured));
    if (output) {
      const shot = await send('Page.captureScreenshot', { format: 'png' });
      await writeFile(output.replace(/\.json$/, '-' + name + '-' + measured.width + '.png'), Buffer.from(shot.data, 'base64'));
      await writeFile(output, JSON.stringify(results, null, 2) + '\n');
    }
    assert(measured.visible && measured.nearAction && measured.inViewport, name + ': notice must appear beside its action and inside the viewport');
    return measured;
  };
  if (endpoint) {
    await evaluate(`showPage('advanced');window.scrollTo(0,0);showError('Not enough free space. Free 2 GB and try again.',document.getElementById('setup-games-usb'));`);
    await measure('#setup-games-usb', 'installed-usb-free-space');
    await evaluate(`stemLibraryStarted=true;showPage('stems');renderStemLibrary(testLibrary);document.querySelector('#stem-tracks .st-open-button').click();document.querySelector('#stem-tracks [data-action="verify"]').scrollIntoView({block:'center'});showError('Not enough free space. Free 2 GB and try again.',document.querySelector('#stem-tracks [data-action="verify"]'));`);
    await measure('#stem-tracks [data-action="verify"]', 'installed-track-free-space');
    await evaluate(`showPage('advanced');document.getElementById('verify-inputs').closest('details').open=true;document.getElementById('verify-inputs').scrollIntoView({block:'center'});document.getElementById('firmware').value='';document.getElementById('verify-inputs').click();`);
    await measure('#verify-inputs', 'installed-required-input');
    await evaluate(`showPage('stems');stemEngine=stemsApi.engine({device:'cpu'});openEngineWizard('choose');showError('Not enough free space. Free 2 GB and try again.',document.getElementById('st-eng-primary'));`);
    await measure('#st-eng-primary', 'installed-setup-dialog');
    assert(await evaluate('testCalls.length===0&&activeJob===null'), 'Installed layout checks must not start native jobs');
    console.log('PASS: installed renderer notices are visible beside their actions; no native test jobs');
  } else {
  for (const size of [{width:900,height:680}, {width:1120,height:820}]) {
    if (!endpoint) await send('Emulation.setDeviceMetricsOverride', { ...size, deviceScaleFactor: 1, mobile: false });
    await evaluate(`showPage('advanced');document.querySelector('#advanced .game-downloads').open=true;document.getElementById('setup-games-usb').scrollIntoView({block:'center'});document.getElementById('setup-games-usb').click();`);
    await waitFor(`activeJob===null&&document.getElementById('job-panel').checkVisibility()&&document.getElementById('job-message').textContent.includes('Not enough free space')`);
    await measure('#setup-games-usb', 'usb-free-space');
  }
  await evaluate(`showPage('advanced');testPickFailure=true;document.getElementById('copy-doom-usb').click();`);
  await waitFor(`document.getElementById('job-message').textContent.includes('folder could not be opened')`);
  await measure('#copy-doom-usb', 'picker-failure');
  await evaluate(`testPickFailure=false;document.querySelector('nav button[data-page="stems"]').click();`);
  await waitFor('activeJob===null');
  await evaluate(`renderStemLibrary(testLibrary);document.querySelector('#stem-tracks .st-open-button').click();document.querySelector('#stem-tracks [data-action="verify"]').scrollIntoView({block:'center'});document.querySelector('#stem-tracks [data-action="verify"]').click();`);
  await waitFor(`activeJob===null&&document.getElementById('job-message').textContent.includes('Not enough free space')`);
  await measure('#stem-tracks [data-action="verify"]', 'track-free-space');
  assert(await evaluate(`Boolean(document.getElementById('job-panel').closest('td')?.closest('tr')?.previousElementSibling?.querySelector('[data-action="verify"]'))`), 'Per-track notice must sit in a valid table row beside the selected track');
  await evaluate(`browseStemLibrary(testLibrary.volume);`);
  await waitFor('activeJob===null&&!stemLibraryLoading');
  assert(await evaluate(`document.getElementById('job-panel').checkVisibility()&&document.getElementById('job-message').textContent.includes('Not enough free space')&&Boolean(document.getElementById('job-panel').closest('tr')?.previousElementSibling?.querySelector('[data-action="verify"]'))`), 'Background refresh must preserve the failure notice beside its track');
  await evaluate(`testMode='throw';document.querySelector('#stem-tracks [data-action="verify"]').click();`);
  await waitFor(`activeJob===null&&document.getElementById('job-message').textContent.includes('Not enough free space')`);
  await measure('#stem-tracks [data-action="verify"]', 'thrown-error');
  await evaluate(`testMode='error';showPage('advanced');document.getElementById('verify-inputs').closest('details').open=true;document.getElementById('verify-inputs').scrollIntoView({block:'center'});document.getElementById('firmware').value='';document.getElementById('verify-inputs').click();`);
  await waitFor(`document.getElementById('job-message').textContent.includes('required')`);
  await measure('#verify-inputs', 'required-input');
  await evaluate(`testMode='running';showPage('advanced');document.getElementById('download-doom').scrollIntoView({block:'center'});document.getElementById('download-doom').click();`);
  await waitFor('activeJob!==null');
  await measure('#download-doom', 'progress');
  await evaluate(`showPage('licenses');`);
  assert(await evaluate(`document.getElementById('job-panel').checkVisibility()&&document.getElementById('job-message').getBoundingClientRect().bottom<=innerHeight`), 'Active progress must remain visible after navigating');
  await evaluate(`testMode='error';`);
  await waitFor('activeJob===null');
  assert(await evaluate(`document.getElementById('job-panel').checkVisibility()&&document.getElementById('job-message').textContent.includes('Not enough free space')&&document.getElementById('job-message').getBoundingClientRect().bottom<=innerHeight`), 'Final failure must remain visible after navigating');
  await evaluate(`testMode='running';showPage('advanced');document.getElementById('download-doom').scrollIntoView({block:'center'});document.getElementById('download-doom').click();`);
  await waitFor('activeJob!==null');
  await evaluate(`document.getElementById('cancel').click();`);
  await waitFor('activeJob===null');
  await measure('#download-doom', 'cancelled');
  assert(await evaluate(`document.getElementById('job-message').textContent.includes('Cancelled')`), 'Cancellation must still work');
  await evaluate(`testMode='error';document.querySelector('nav button[data-page="stems"]').click();renderStemLibrary(testLibrary);openEngineWizard('check');`);
  await waitFor(`activeJob===null&&Boolean(document.getElementById('st-eng-report'))`);
  await evaluate(`document.getElementById('st-eng-report').click();`);
  await waitFor(`activeJob===null&&document.getElementById('job-message').textContent.includes('Not enough free space')`);
  await measure('#st-eng-report', 'setup-dialog');
  assert(await evaluate(`document.getElementById('st-engine').contains(document.getElementById('job-panel'))`), 'Dialog feedback must appear inside the active dialog');
  await evaluate(`closeEngineWizard();`);
  await waitFor(`!document.getElementById('st-engine').contains(document.getElementById('job-panel'))`);
  if (!endpoint) assert.deepEqual(await evaluate('window.__csp'), [], 'No CSP violations under the app policy');
  console.log('PASS: action feedback is visible beside the initiating action');
  }
} finally {
  if (endpoint && socket?.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ id: 99999, method: 'Page.reload', params: {} }));
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  if (!endpoint && base) {
    try {
      const version = await (await fetch(base + '/json/version', { signal: AbortSignal.timeout(2000) })).json();
      const browser = new WebSocket(version.webSocketDebuggerUrl);
      await new Promise(resolve => { browser.onopen = () => { browser.send(JSON.stringify({ id: 1, method: 'Browser.close' })); setTimeout(resolve, 300); }; browser.onerror = resolve; });
    } catch {}
  }
  socket?.close();
  await server?.close();
  killBrowser(child, profile);
  if (profile) {
    assert(dirname(resolve(profile))===resolve(tmpdir())&&basename(profile).startsWith('xz-feedback-'), 'Cleanup must stay inside the generated browser profile');
    await rm(profile, { recursive: true, force: true, maxRetries: 20, retryDelay: 100 });
  }
}
