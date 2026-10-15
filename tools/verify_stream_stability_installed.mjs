import assert from 'node:assert/strict';
import {writeFile} from 'node:fs/promises';
const endpoint=process.argv[2]||'http://127.0.0.1:9224',host=process.argv[3],output=process.argv[4];
assert(host,'Pass the already connected XZ address');
let page;
for(let n=0;n<100;n++){
 try{const pages=await(await fetch(endpoint+'/json/list')).json();page=pages.find(p=>p.type==='page'&&/tauri|localhost/.test(p.url));if(page)break;}catch{}
 await new Promise(r=>setTimeout(r,100));
}
assert(page,'Installed WebView was not found');
const socket=new WebSocket(page.webSocketDebuggerUrl);
await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
let sequence=0;const pending=new Map();
socket.onmessage=event=>{const data=JSON.parse(event.data);if(!data.id)return;const task=pending.get(data.id);if(!task)return;pending.delete(data.id);data.error?task.reject(Error(JSON.stringify(data.error))):task.resolve(data.result);};
function send(method,params={}){return new Promise((resolve,reject)=>{const id=++sequence,timer=setTimeout(()=>{pending.delete(id);reject(Error('CDP timeout'));},8000);pending.set(id,{resolve:r=>{clearTimeout(timer);resolve(r);},reject:e=>{clearTimeout(timer);reject(e);}});socket.send(JSON.stringify({id,method,params}));});}
async function evaluate(expression){const r=await send('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});assert(!r.exceptionDetails,JSON.stringify(r.exceptionDetails));return r.result.value;}
try{
 for(let n=0;n<100;n++){
  if(await evaluate("Boolean(document.getElementById('live-connect')?.onclick)"))break;
  await new Promise(r=>setTimeout(r,100));
 }
 assert(await evaluate('activeJob===null&&!appUpdating'),'Do not interrupt an operation');
 let status=await evaluate("window.__TAURI__.core.invoke('live_stream_status')");
 if(status.phase==='stopped'){
  await evaluate(`showPage('live-stream');document.getElementById('live-host').value=${JSON.stringify(host)};document.getElementById('live-connect').click()`);
 }
 let connected=false;const rows=[];
 for(let n=0;n<160;n++){
  status=await evaluate("window.__TAURI__.core.invoke('live_stream_status')");
  delete status.diagnostics;
  if(status.phase==='live')connected=true;
  if(connected){rows.push({...status,placeholderHidden:await evaluate("document.getElementById('live-placeholder').classList.contains('hidden')")});}
  if(rows.length>=120)break;
  await new Promise(r=>setTimeout(r,250));
 }
 assert(rows.length>=100,'Not enough live observation time');
 const result={source:'installed native IPC and actual renderer',samples:rows.length,nonLive:rows.filter(r=>r.phase!=='live').length,placeholderFlashes:rows.slice(8).filter(r=>!r.placeholderHidden).length,samplesOverOldCutoff:rows.filter(r=>r.age_ms>=1000).length,maxAgeMs:Math.max(...rows.map(r=>r.age_ms||0)),framesAdvanced:rows.at(-1).frames-rows[0].frames,errors:[...new Set(rows.map(r=>r.error).filter(Boolean))],url:rows.at(-1).url,rows};
 const viewerHTML=await(await fetch(result.url)).text();
 assert(viewerHTML.includes('const MAX_FRAME_AGE_MS=3000;'),'Installed OBS page must contain the corrected freshness policy');
 if(output)await writeFile(output,JSON.stringify(result,null,2)+'\n');
 console.log(JSON.stringify({...result,rows:undefined},null,2));
 assert(result.framesAdvanced>5,'The picture must keep advancing');
 assert.equal(result.nonLive,0,'The connected stream must not flash disconnected');
 assert.equal(result.placeholderFlashes,0,'The renderer must keep its reconnect placeholder hidden');
 assert.equal(result.errors.length,0);
}finally{socket.close();}
