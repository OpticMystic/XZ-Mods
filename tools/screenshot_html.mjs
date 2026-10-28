import { spawn } from 'node:child_process';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { killBrowser } from './kill_browser.mjs';

// Screenshots one local HTML file in headless Edge over CDP, at device scale 1 on a transparent background.
// tools/xz_buttons.py uses it for the glyph sheet and the firmware previews. No packages needed.
// Run: node tools/screenshot_html.mjs <page.html> <out.png> <width> <height, 0 for the whole page>   (XZ_TEST_BROWSER overrides the browser)
const [page, out, width, height] = process.argv.slice(2);
if (!height) throw Error('usage: screenshot_html.mjs <page.html> <out.png> <width> <height>');
const profile = await mkdtemp(join(tmpdir(), 'xz-shot-'));
const child = spawn(process.env.XZ_TEST_BROWSER || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  ['--headless=new', '--disable-gpu', '--no-first-run', '--hide-scrollbars', '--remote-debugging-port=0', '--user-data-dir=' + profile, 'about:blank'], { stdio: 'ignore', windowsHide: true });
let socket;
try {
  let base;
  for (let attempt = 0; attempt < 100 && !base; attempt++) {
    try { base = 'http://127.0.0.1:' + (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; }
    catch { await new Promise(done => setTimeout(done, 100)); }
  }
  if (!base) throw Error('Browser did not start');
  const target = (await (await fetch(base + '/json/list')).json()).find(item => item.type === 'page');
  socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((done, fail) => { socket.onopen = done; socket.onerror = () => fail(Error('CDP connection failed')); });
  let sequence = 0;
  const pending = new Map(), events = [];
  socket.onmessage = event => {
    const data = JSON.parse(event.data);
    if (data.method) events.push(data.method);
    const task = pending.get(data.id);
    if (!task) return;
    pending.delete(data.id);
    data.error ? task.reject(Error(JSON.stringify(data.error))) : task.resolve(data.result);
  };
  const send = (method, params = {}) => new Promise((resolve, reject) => { const id = ++sequence; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); setTimeout(() => reject(Error('CDP timed out: ' + method)), 60000); });
  await send('Page.enable');
  await send('Emulation.setDeviceMetricsOverride', { width: +width, height: +height, deviceScaleFactor: 1, mobile: false });
  await send('Emulation.setDefaultBackgroundColorOverride', { color: { r: 0, g: 0, b: 0, a: 0 } });
  await send('Page.navigate', { url: pathToFileURL(page).href });
  for (let i = 0; i < 400 && !events.includes('Page.loadEventFired'); i++) await new Promise(done => setTimeout(done, 25));
  await new Promise(done => setTimeout(done, 200));
  let tall = +height;  // 0: the whole page
  if (!tall) {
    tall = (await send('Runtime.evaluate', { expression: 'document.documentElement.scrollHeight', returnByValue: true })).result.value;
    await send('Emulation.setDeviceMetricsOverride', { width: +width, height: tall, deviceScaleFactor: 1, mobile: false });
    await new Promise(done => setTimeout(done, 150));
  }
  const shot = await send('Page.captureScreenshot', { format: 'png', clip: { x: 0, y: 0, width: +width, height: tall, scale: 1 }, captureBeyondViewport: false });
  await writeFile(out, Buffer.from(shot.data, 'base64'));
} finally {
  socket?.close();
  killBrowser(child, profile);
  await rm(profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 }).catch(() => {});
}
