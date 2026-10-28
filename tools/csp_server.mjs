import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join, normalize, resolve, sep } from 'node:path';

// Serves ui/ with the Content-Security-Policy the desktop app ships, so the headless verifiers see what the
// installed WebView sees. file:// pages have no CSP and once let blocked inline styles pass. An overlay folder, laid
// out like ui/, is served first: a verifier stages test media there instead of writing into ui/.
const types = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.svg': 'image/svg+xml', '.json': 'application/json', '.woff2': 'font/woff2', '.mp4': 'video/mp4', '.webp': 'image/webp' };

export async function serveUi(app, { overlay } = {}) {
  const csp = JSON.parse(await readFile(join(app, 'src-tauri/tauri.conf.json'), 'utf8')).app.security.csp;
  if (!csp) throw Error('src-tauri/tauri.conf.json has no app.security.csp');
  const root = resolve(app, 'ui'), over = overlay && resolve(overlay);
  const server = createServer(async (request, response) => {
    const name = decodeURIComponent(new URL(request.url, 'http://x').pathname);
    const path = normalize(join(root, name)), staged = over && normalize(join(over, name));
    if (!path.startsWith(root + sep) || (staged && !staged.startsWith(over + sep))) { response.writeHead(403).end(); return; }
    try {
      const body = staged ? await readFile(staged).catch(() => readFile(path)) : await readFile(path);
      response.writeHead(200, { 'Content-Type': types[extname(path)] || 'application/octet-stream', 'Content-Security-Policy': csp, 'Cache-Control': 'no-store' }).end(body);
    } catch { response.writeHead(404, { 'Content-Security-Policy': csp }).end(); }
  });
  await new Promise(done => server.listen(0, '127.0.0.1', done));
  return { csp, url: 'http://127.0.0.1:' + server.address().port + '/', close: () => new Promise(done => server.close(done)) };
}
