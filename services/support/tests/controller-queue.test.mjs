import {before,after,beforeEach,test} from 'node:test';
import assert from 'node:assert/strict';
import {createHash,randomBytes} from 'node:crypto';
import {mkdir,readFile} from 'node:fs/promises';
import {build} from 'esbuild';
import {Miniflare,convertV4MiniflareOptions} from 'miniflare';
import {applyMigration} from './migrations.mjs';
import {contract,formatInstallationId,parseInstallationId,formatLoaderId,parseLoaderId} from '../../../spec/generated/support-contract.mjs';

const origin='https://support.test',installationKey='c'.repeat(64),loaderKey='d'.repeat(64),admin='a'.repeat(64);
const installation='I-4F7K-2M9Q',loader='L-4F7K-2M9Q',sha=v=>createHash('sha256').update(v).digest('hex');
const fixture=JSON.parse(await readFile('../../spec/fixtures/controller-pending-report.json','utf8'));
let mf,db,bucket,backfill;
const post=(path,body,key=installationKey,extra={})=>{
  const raw=typeof body==='string'?body:JSON.stringify(body);
  return mf.dispatchFetch(origin+path,{method:'POST',body:raw,headers:{'Content-Type':'application/json','Content-Length':String(Buffer.byteLength(raw)),Authorization:`Bearer ${key}`,'CF-Connecting-IP':'203.0.113.1',...extra}});
};
const get=(path,key=installationKey)=>mf.dispatchFetch(origin+path,{headers:{Authorization:`Bearer ${key}`}});
async function ok(response){const r=await response;assert.equal(r.status,200,await r.clone().text());return r.json();}
const draft=()=>({...structuredClone(fixture),report_id:randomBytes(16).toString('hex')});
const upload=(report=draft(),key=loaderKey,path='/v2/xz/pending')=>{
  const raw=JSON.stringify(report);
  return post(path,raw,key,{'X-Report-ID':report.report_id,'X-Report-SHA256':sha(raw),'X-Report-Consent':contract.consent});
};
const registerLoader=(id=loader,key=loaderKey,previous=null,install=installation)=>post(`/v2/installations/${install}/loaders`,{loader_id:id,loader_key_hash:sha(key),previous_key_hash:previous});
const claim=ref=>post(`/v2/installations/${installation}/pending/${ref}/claim`,{});
before(async()=>{
  const out=process.env.XZ_SUPPORT_TEST_OUTPUT;if(!out)throw Error('XZ_SUPPORT_TEST_OUTPUT required');await mkdir(out,{recursive:true});
  const built=await build({entryPoints:['tests/entry.ts'],bundle:true,write:false,format:'esm',platform:'browser',target:'es2022'});
  mf=new Miniflare(convertV4MiniflareOptions({modules:true,script:built.outputFiles[0].text,compatibilityDate:'2026-10-24',d1Databases:['DB'],r2Buckets:['REPORTS'],unsafeEphemeralDurableObjects:true,
    bindings:{ENABLED:'true',PUBLIC_ORIGIN:origin,ADMIN_TOKEN_SHA256:sha(admin),RATE_KEY:'synthetic-rate-key-'.repeat(4),AUTOMATION_ENABLED:'false',PUBLIC_POSTING_ENABLED:'false'}}));
  db=await mf.getD1Database('DB');bucket=await mf.getR2Bucket('REPORTS');
  for(const name of ['0001_reports.sql','0002_bugdesk_tracking.sql','0003_support_identifiers.sql']) await applyMigration(db,name);
  const now=Date.now(),id='1'.repeat(32);
  await db.prepare(`INSERT INTO reports(id,digest,key_hash,object_key,ip_hash,day,bytes,state,reserved_at,accepted_at,expires_at,public_json) VALUES(?,?,?,?,?,?,?,'accepted',?,?,?,?)`).bind(id,'f'.repeat(64),sha(installationKey),'legacy','ip','old',100,now,now,now+86400000,'{"origin":"controller"}').run();
  await db.prepare(`INSERT INTO report_tracking(report_id,report_reference,receipt_key_hash,payload_sha256,created_at,updated_at,retention_until,diagnostics_expires_at,last_operation,last_operation_hash) VALUES(?,?,?,?,?,?,?,?,'accepted',?)`).bind(id,'R-0000-0001',sha(installationKey),'f'.repeat(64),now,now,now+86400000,now+86400000,'f'.repeat(64)).run();
  await applyMigration(db,'0004_controller_queue.sql');backfill=await db.prepare('SELECT * FROM report_tracking').first();
});
after(async()=>{await mf?.dispose();});
beforeEach(async()=>{
  for(const table of ['report_tracking','reports','loaders','installations'])await db.prepare('DELETE FROM '+table).run();
  for(const item of (await bucket.list()).objects)await bucket.delete(item.key);
  await ok(post('/v2/installations',{installation_id:installation}));await ok(registerLoader());
});
test('migration backfills known origin without inventing installation, loader or pending comments',()=>{
  assert.equal(backfill.origin,'controller');assert.equal(backfill.installation_id,null);assert.equal(backfill.loader_id,null);assert.equal(backfill.comments_state,'completed');
});
test('identity parsers share tolerant Crockford vectors with canonical formatting',async()=>{
  const worker=await ok(get('/__test/controller-identifiers'));
  for(const [name,parse,format] of [['installation_id',parseInstallationId,formatInstallationId],['loader_id',parseLoaderId,formatLoaderId]]) {
    for(const v of contract.identifiers[name].vectors){assert.equal(parse(v.input),v.value);if(v.value!==null)assert.equal(format(v.value),v.canonical);}
    assert.deepEqual(worker[name],contract.identifiers[name].vectors.map(v=>v.value));
  }
});
test('loader can submit and recover only a reference, never list, claim, read, complete, register or cross installations',async()=>{
  const r=draft(),accepted=await ok(upload(r));assert.match(accepted.report_reference,/^R-/);
  assert.deepEqual(Object.keys(accepted).sort(),['accepted_at','comments_pending','payload_sha256','report_id','report_reference','schema']);
  assert.deepEqual(await ok(upload(r)),accepted);
  for(const path of [`/v2/installations/${installation}/pending`,`/v2/reports/${r.report_id}/receipt`,`/v2/reports/${r.report_id}/status`,`/v2/admin/reports`])assert.ok([401,404].includes((await get(path,loaderKey)).status));
  assert.equal((await post(`/v2/installations/${installation}/pending/${accepted.report_reference}/claim`,{},loaderKey)).status,404);
  assert.equal((await post(`/v2/reports/${accepted.report_reference}/complete`,{action:'complete_without_comments'},loaderKey)).status,404);
  assert.equal((await post(`/v2/installations/${installation}/loaders`,{loader_id:'L-0000-0001',loader_key_hash:sha(loaderKey),previous_key_hash:null},loaderKey)).status,404);
  assert.equal((await upload({...draft(),installation_id:'I-0000-0001'})).status,404);
  assert.equal((await upload({...r,description:'changed'})).status,409);
  const inbox=await ok(get('/v2/admin/reports',admin));assert.equal(inbox.reports[0].comments_status,"awaiting the DJ's comments");assert.equal(inbox.reports[0].installation_id,installation);assert.equal(inbox.reports[0].loader_id,loader);
});
test('claim is stable across retries and completion preserves reference, payload bytes and private owner description',async()=>{
  const r=draft(),accepted=await ok(upload(r));
  const queue=await ok(get(`/v2/installations/${installation}/pending`));assert.equal(queue.pending[0].description,'');assert.equal(queue.pending[0].remind,true);
  const claimed=await ok(claim(accepted.report_reference)),again=await ok(claim(accepted.report_reference));assert.equal(claimed.receipt_key,again.receipt_key);assert.notEqual(claimed.receipt_key,loaderKey);
  assert.equal((await get(`/v2/reports/${r.report_id}/receipt`,claimed.receipt_key)).status,200);
  assert.equal((await get(`/v2/reports/${accepted.report_reference}/details`,loaderKey)).status,404);
  assert.equal((await get(`/v2/reports/${accepted.report_reference}/details`,installationKey)).status,404);
  const details=await ok(get(`/v2/reports/${accepted.report_reference}/details`,claimed.receipt_key));assert.equal(Buffer.from(details.data_base64,'base64').toString(),JSON.stringify(r));
  const action={action:'complete',comments:'PRIVATE-DJ-COMMENT'};
  const done=await ok(post(`/v2/reports/${accepted.report_reference}/complete`,action,claimed.receipt_key));assert.equal(done.report_reference,accepted.report_reference);assert.equal(done.comments_state,'completed');
  assert.equal((await post(`/v2/reports/${accepted.report_reference}/complete`,action,claimed.receipt_key)).status,200);
  assert.equal((await post(`/v2/reports/${accepted.report_reference}/complete`,{action:'complete_without_comments'},claimed.receipt_key)).status,409);
  assert.equal((await ok(get(`/v2/installations/${installation}/pending`))).pending.length,0);
  const page=await ok(get(`/v2/admin/reports/${r.report_id}`,admin));assert.equal(page.completion.description,action.comments);assert.equal(page.payload_sha256,sha(JSON.stringify(r)));
  const status=await ok(get(`/v2/reports/${r.report_id}/status`,claimed.receipt_key));assert.ok(!JSON.stringify(status).includes('PRIVATE-DJ-COMMENT'));
});
test('without comments, dismissal and seven day expiry are distinct and never hide reports from owner',async()=>{
  const a=await ok(upload()),b=await ok(upload()),c=await ok(upload());
  const ac=await ok(claim(a.report_reference)),bc=await ok(claim(b.report_reference));
  await ok(post(`/v2/reports/${a.report_reference}/complete`,{action:'complete_without_comments'},ac.receipt_key));
  await ok(post(`/v2/reports/${b.report_reference}/dismiss-reminders`,{},bc.receipt_key));
  let queue=await ok(get(`/v2/installations/${installation}/pending`));assert.equal(queue.pending.find(x=>x.report_reference===b.report_reference).remind,false);
  await db.prepare('UPDATE report_tracking SET reminders_until=0').run();
  queue=await ok(get(`/v2/installations/${installation}/pending`));assert.equal(queue.pending.length,2);assert.ok(queue.pending.every(x=>!x.remind&&x.comments_state==='expired_reminders'));
  const inbox=await ok(get('/v2/admin/reports',admin));assert.equal(inbox.reports.length,3);assert.equal(inbox.reports.filter(x=>x.comments_pending).length,2);
  await ok(post(`/v2/reports/${b.report_reference}/complete`,{action:'complete',comments:'late comments'},bc.receipt_key));
  assert.equal((await ok(get(`/v2/installations/${installation}/pending`))).pending[0].report_reference,c.report_reference);
});
test('revocation refuses loader uploads; rotation is compare-and-swap and old keys never reactivate',async()=>{
  await ok(post(`/v2/installations/${installation}/loaders/${loader}/revoke`,{expected_key_hash:sha(loaderKey)}));
  assert.equal((await upload()).status,404);assert.equal((await registerLoader()).status,409);
  assert.equal((await upload(draft(),installationKey,`/v2/installations/${installation}/pending`)).status,404);
  const next='e'.repeat(64);assert.equal((await registerLoader(loader,next,sha('f'.repeat(64)))).status,409);
  await ok(registerLoader(loader,next,sha(loaderKey)));await ok(registerLoader(loader,next,sha(loaderKey)));
  assert.equal((await upload()).status,404);await ok(upload(draft(),next));
  assert.equal((await post(`/v2/installations/${installation}/loaders/${loader}/revoke`,{expected_key_hash:sha(loaderKey)})).status,409);
});
test('atomic per-loader and per-installation quotas include reservations, idempotent retries spend once',async()=>{
  const r=draft();await ok(upload(r));await ok(upload(r));
  const responses=await Promise.all(Array.from({length:8},()=>upload()));assert.equal(responses.filter(x=>x.status===200).length,4);assert.ok(responses.every(x=>[200,429].includes(x.status)));
  for(let n=1;n<=3;n++){const lid=formatLoaderId(n);await ok(registerLoader(lid));for(let j=0;j<5;j++)await ok(upload({...draft(),loader_id:lid}));}
  await ok(registerLoader('L-0000-0004'));assert.equal((await upload({...draft(),loader_id:'L-0000-0004'})).status,429);
});
test('USB relay supports pre-identity exports but refuses another installations named loader',async()=>{
  const legacy={...draft(),loader_id:null,installation_id:null};const path=`/v2/installations/${installation}/pending`;
  const receipt=await ok(upload(legacy,installationKey,path));assert.equal((await ok(claim(receipt.report_reference))).pending.installation_id,installation);
  assert.equal((await upload({...draft(),installation_id:'I-0000-0001'},installationKey,path)).status,404);
  assert.equal((await upload({...draft(),loader_id:'L-0000-0002'},installationKey,path)).status,404);
});
test('strict fixture rejects screenshots, screen text, unknown enums, identity mismatch and oversized typed text',async()=>{
  const mutations=[r=>r.screenshot='PRIVATE',r=>r.screen_state.track='PRIVATE',r=>r.screen_state.stock_page='Song Title',r=>r.screen_state.mods_page='ABOUT',r=>r.loader_id=null,r=>r.description='x'.repeat(281),r=>r.comments_pending=false];
  for(const mutate of mutations){const r=draft();mutate(r);assert.equal((await upload(r)).status,400);}
  assert.equal((await bucket.list()).objects.length,0);
  await ok(upload({...draft(),description:'😀'.repeat(280)}));
  assert.equal((await upload({...draft(),description:'😀'.repeat(281)})).status,400);
});
test('concurrent duplicate upload and claim converge; different completions cannot both win',async()=>{
  const r=draft();const results=await Promise.all(Array.from({length:6},()=>upload(r)));assert.ok(results.every(x=>[200,503].includes(x.status)));
  const accepted=await ok(upload(r));const claims=await Promise.all(Array.from({length:5},()=>ok(claim(accepted.report_reference))));assert.equal(new Set(claims.map(x=>x.receipt_key)).size,1);
  const done=await Promise.all(['one','two'].map(comments=>post(`/v2/reports/${accepted.report_reference}/complete`,{action:'complete',comments},claims[0].receipt_key)));
  assert.deepEqual(done.map(x=>x.status).sort(),[200,409]);assert.equal((await db.prepare('SELECT count(*) AS n FROM controller_queue').first()).n,1);
});
test('private expiry and deletion remove queued prose while retaining references and completion authority',async()=>{
  const r={...draft(),description:'PRIVATE-ORIGINAL'},accepted=await ok(upload(r)),claimed=await ok(claim(accepted.report_reference));
  await db.prepare('UPDATE controller_queue SET private_expires_at=0').run();
  await ok(post(`/v2/reports/${accepted.report_reference}/complete`,{action:'complete',comments:'NEW-COMMENT'},claimed.receipt_key));
  const row=await db.prepare('SELECT * FROM controller_queue').first();assert.equal(row.description,null);assert.equal(row.comments,'NEW-COMMENT');
  await db.prepare("UPDATE reports SET state='deleting' WHERE id=?").bind(r.report_id).run();
  assert.equal((await ok(get(`/v2/admin/reports/${r.report_id}/completion`,admin))).completion.description,'NEW-COMMENT','diagnostic expiry does not expire a later comment');
  const deletion=await mf.dispatchFetch(origin+`/v1/admin/reports/${r.report_id}`,{method:'DELETE',headers:{Authorization:`Bearer ${admin}`}});assert.equal(deletion.status,200);
  assert.equal((await db.prepare('SELECT comments FROM controller_queue').first()).comments,null);
  assert.equal((await get(`/v2/reports/${accepted.report_reference}/details`,claimed.receipt_key)).status,410);
  assert.equal((await get(`/v2/reports/${r.report_id}/receipt`,claimed.receipt_key)).status,200);
  assert.equal((await get(`/v2/admin/reports/${r.report_id}/completion`,claimed.receipt_key)).status,401);
  assert.equal((await ok(get(`/v2/admin/reports/${r.report_id}/completion`,admin))).completion.description,null);
});
test('queue pagination never leaks another installation and hash possession is insufficient',async()=>{
  assert.equal((await upload(draft(),sha(loaderKey))).status,404);
  await ok(post('/v2/installations',{installation_id:'I-0000-0001'},'e'.repeat(64)));
  assert.equal((await get('/v2/installations/I-0000-0001/pending')).status,404);
  for(let n=0;n<25;n++) {await ok(upload());await db.prepare("UPDATE reports SET day='previous-day'").run();}
  const first=await ok(get(`/v2/installations/${installation}/pending`));assert.equal(first.pending.length,20);assert.ok(first.next_after>0);
  const second=await ok(get(`/v2/installations/${installation}/pending?after=${first.next_after}`));assert.equal(second.pending.length,5);assert.equal(second.next_after,null);
  assert.equal(new Set([...first.pending,...second.pending].map(x=>x.report_id)).size,25);
  assert.equal((await ok(get('/v2/installations/I-0000-0001/pending','e'.repeat(64)))).pending.length,0);
});
test('installation and loader registration quotas bound persistent identity records',async()=>{
  for(let n=1;n<10;n++)await ok(post('/v2/installations',{installation_id:formatInstallationId(n)}));
  assert.equal((await post('/v2/installations',{installation_id:formatInstallationId(10)})).status,429);
  for(let n=1;n<64;n++)await ok(registerLoader(formatLoaderId(n)));
  assert.equal((await registerLoader(formatLoaderId(64))).status,429);
});
