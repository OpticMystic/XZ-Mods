import assert from 'node:assert/strict';
const endpoint=process.argv[2]||'http://127.0.0.1:9224';
const targets=await(await fetch(endpoint+'/json/list')).json();
const page=targets.find(page=>page.type==='page'&&/tauri|localhost/.test(page.url));
assert(page,'Installed XZ Mods WebView not found');
const socket=new WebSocket(page.webSocketDebuggerUrl);
await new Promise((resolve,reject)=>{socket.onopen=resolve;socket.onerror=reject;});
let sequence=0;
const pending=new Map();
socket.onmessage=event=>{const data=JSON.parse(event.data);if(!data.id)return;const task=pending.get(data.id);if(!task)return;pending.delete(data.id);data.error?task.reject(Error(JSON.stringify(data.error))):task.resolve(data.result);};
const send=(method,params={})=>new Promise((resolve,reject)=>{const id=++sequence;const timer=setTimeout(()=>{pending.delete(id);reject(Error('CDP timeout'));},10000);pending.set(id,{resolve:r=>{clearTimeout(timer);resolve(r);},reject:e=>{clearTimeout(timer);reject(e);}});socket.send(JSON.stringify({id,method,params}));});
async function evaluate(expression){const result=await send('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});assert(!result.exceptionDetails,JSON.stringify(result.exceptionDetails));return result.result.value;}
async function wait(expression){for(let n=0;n<100;n++){if(await evaluate(expression))return;await new Promise(r=>setTimeout(r,50));}throw Error('Timed out: '+expression);}
let original;
try{
 await wait("!!document.getElementById('live-privacy')&&!document.getElementById('live-privacy').disabled");
 assert(await evaluate('activeJob===null&&!appUpdating'),'Do not interrupt an operation');
 original=await evaluate("window.__TAURI__.core.invoke('live_stream_privacy',{})");
 await evaluate("showPage('live-stream')");
 for(const enabled of [true,false,true]){
  await evaluate(`document.getElementById('live-privacy').checked=${!enabled};document.getElementById('live-privacy').click()`);
  await wait(`!document.getElementById('live-privacy').disabled&&document.getElementById('live-privacy').checked===${enabled}`);
  assert.equal(await evaluate("window.__TAURI__.core.invoke('live_stream_privacy',{})"),enabled);
 }
 await send('Page.reload');
 await wait("!!document.getElementById('live-privacy')&&!document.getElementById('live-privacy').disabled");
 assert.equal(await evaluate("document.getElementById('live-privacy').checked"),true,'Preference survives renderer reload');
 await evaluate("showPage('live-stream');document.getElementById('live-privacy').scrollIntoView({block:'center'})");
 console.log(JSON.stringify({installedUrl:page.url,toggleNativeAndSaved:true,reload:true,activeJob:await evaluate('activeJob')},null,2));
 const capture=await send('Page.captureScreenshot',{format:'png'});
 if(process.argv[3]){const{writeFile}=await import('node:fs/promises');await writeFile(process.argv[3],Buffer.from(capture.data,'base64'));}
}finally{
 if(typeof original==='boolean'){
  await evaluate(`window.__TAURI__.core.invoke('live_stream_privacy',{enabled:${original}})`);
  await send('Page.reload');
  await wait("!!document.getElementById('live-privacy')&&!document.getElementById('live-privacy').disabled");
  await evaluate("showPage('live-stream');document.getElementById('live-privacy').scrollIntoView({block:'center'})");
 }
 socket.close();
}
