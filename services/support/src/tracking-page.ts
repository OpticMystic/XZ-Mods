const head = {
  'Cache-Control':'no-store','Referrer-Policy':'no-referrer','X-Robots-Tag':'noindex, nofollow',
  'X-Content-Type-Options':'nosniff','Content-Security-Policy':"default-src 'none'; script-src 'self'; connect-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'"
};
const html = `<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Track an XZ Mods report</title>
<main><h1>Your report</h1><p id="status" role="status">Opening your private status link...</p><p id="question"></p><p id="retention"></p>
<button id="refresh" type="button" hidden>Check for updates</button><button id="setup" type="button">Open a saved status link</button>
<section id="connection" hidden aria-label="Open report status"><h2>Connect to your report</h2><p>Choose the status link saved with your receipt. We will check that it still works.</p>
<label for="link">Saved status link</label><input id="link" type="url" autocomplete="off"><button id="connect" type="button">Check this link</button><p id="connection-status" role="status"></p></section>
<p>This page shows safe progress only. Original text and diagnostics stay private.</p></main><script src="/track.js" defer></script></html>`;
const js = `'use strict';
(() => {
  const fragment = new URLSearchParams(location.hash.slice(1));
  const id = fragment.get('id'), read = fragment.get('read');
  history.replaceState(null, '', '/track');
  fragment.delete('read'); fragment.delete('reply');
  const status = document.getElementById('status'), button = document.getElementById('refresh');
  let session = null, expires = 0;
  document.getElementById('setup').addEventListener('click', () => {
    document.getElementById('connection').hidden=false;
    document.getElementById('link').focus();
  });
  document.getElementById('connect').addEventListener('click', () => {
    try {
      const link=new URL(document.getElementById('link').value);
      const keys=new URLSearchParams(link.hash.slice(1));
      if(link.origin!==location.origin || link.pathname!=='/track' || link.search ||
        !/^[a-f0-9]{32}$/.test(keys.get('id') || '') || !/^[a-f0-9]{64}$/.test(keys.get('read') || '')) throw new Error();
      location.replace('/track#id='+keys.get('id')+'&read='+keys.get('read'));
    } catch { document.getElementById('connection-status').textContent='Choose a status link from this support service.'; }
  });
  async function refresh() {
    button.disabled = true;
    try {
      if (!session || Date.now() >= expires) throw new Error();
      const response = await fetch('/v2/tracking/' + id, {headers:{Authorization:'Bearer ' + session},cache:'no-store',credentials:'omit',redirect:'error'});
      if (!response.ok) throw new Error();
      const data = await response.json();
      status.textContent = data.status_text;
      document.getElementById('question').textContent = data.question || '';
      document.getElementById('retention').textContent = 'Tracking available until ' + data.retention_until;
    } catch { status.textContent = 'This status link is unavailable. Open My reports in XZ Mods to get a new link.'; }
    finally { button.disabled = false; }
  }
  button.addEventListener('click', refresh);
  if (!/^[a-f0-9]{32}$/.test(id || '') || !/^[a-f0-9]{64}$/.test(read || '')) {
    status.textContent = 'Open the status link saved with your report receipt.'; return;
  }
  fetch('/v2/tracking/session', {method:'POST',headers:{'Content-Type':'application/json',Authorization:'Bearer ' + read},
    body:JSON.stringify({report_id:id}),cache:'no-store',credentials:'omit',redirect:'error'})
    .then(async response => { if (!response.ok) throw new Error(); const data=await response.json(); session=data.session; expires=data.expires_at; button.hidden=false; await refresh(); })
    .catch(() => { status.textContent='This status link has expired or was revoked.'; });
})();`;
export function trackingPage(path: string) {
  return new Response(path === '/track' ? html : js,{headers:{...head,'Content-Type':path === '/track' ? 'text/html; charset=utf-8' : 'text/javascript; charset=utf-8'}});
}
