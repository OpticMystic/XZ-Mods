import {before,after,beforeEach,test} from 'node:test';
import assert from 'node:assert/strict';
import {createHash,randomBytes} from 'node:crypto';
import {readFile,mkdir} from 'node:fs/promises';
import {build} from 'esbuild';
import {Miniflare,convertV4MiniflareOptions} from 'miniflare';
import {execFileSync} from 'node:child_process';
import {Script} from 'node:vm';
import {contract,automaticFixEligible} from '../../../spec/generated/support-contract.mjs';

const origin='https://support.test',key='b'.repeat(64),admin='a'.repeat(64),sha=v=>createHash('sha256').update(v).digest('hex');
const id=()=>randomBytes(16).toString('hex');
const readKey=sha(contract.capability_domains.read+key),replyKey=sha(contract.capability_domains.reply+key);
let mf,db,bucket,outbound=[],migration,ipSequence=0;
const send=(path,body,token=admin,extra={})=>{
  const raw=typeof body==='string'?body:JSON.stringify(body);
  return mf.dispatchFetch(origin+path,{method:'POST',body:raw,headers:{'Content-Type':'application/json','Content-Length':String(Buffer.byteLength(raw)),Authorization:`Bearer ${token}`,...extra}});
};
const get=(path,token=key,headers={})=>mf.dispatchFetch(origin+path,{redirect:'manual',headers:{Authorization:`Bearer ${token}`,...headers}});
const fact={code:'WAIT_LOOPBACK',relative_seconds:null,clock_basis:'unknown',window_match:'unknown'};
function draft() {return {schema:'xz-support-report/2',report_id:id(),origin:'loader',kind_hint:null,description:'The extra decks never become available. PRIVATE-CANARY',category:'bridge',
  build:{desktop_version:'0.5.0-preview.22.6.bugdesk.1',desktop_provenance:'desktop',runtime_version:'unknown',runtime_provenance:'unknown'},
  incident:{before_seconds:900,after_seconds:0,clock_confidence:'unknown'},
  private:{contact:'CONTACT-CANARY',contact_consent:true,items:[{item_id:id(),source_id:'usb_session',source_slot:'usb_a',parser_version:1,capture:'at_collection',clock_confidence:'unknown',selected:true,
    facts:[{...fact}],missing_reason:null,truncated:false,omission_count:2}]},coverage:{omitted_items:0,unfinished_sources:0,scan_generation:id()},redactions:{count:2,omitted_lines:2}};}
async function upload(r=draft(),raw=JSON.stringify(r),headers={}) {
  return send('/v2/reports',raw,key,{'X-Report-ID':r.report_id,'X-Report-SHA256':sha(raw),'X-Report-Consent':contract.consent,'CF-Connecting-IP':'203.0.113.'+(++ipSequence),...headers});
}
async function accepted(r=draft()) {const response=await upload(r);assert.equal(response.status,200,await response.clone().text());return r;}
function triage(kind='BUG',revision=1) {return {expected_revision:revision,operation_id:id(),classified_kind:kind,outcome:'CLASSIFIED',source_commit:'c'.repeat(40),evidence_revision:1,
  reason:kind==='BUG'?'contract_mismatch':'documented_behavior',expected:'Expected per manual PRIVATE-EXPECTED',actual:'Observed failure PRIVATE-ACTUAL',
  desk:{expectation:'NONE',impact:'ADDITIVE',affected_features:['four_decks'],in_use_since:'unknown',severity:'WORKAROUND_EXISTS',reach:{versions:[],interfaces:[],decks:[]},proposed_setting:null},expectation_citation:null,
  citations:[{path:'manual/chapters/four-decks.md',heading:'PRIVATE-HEADING',commit:'c'.repeat(40)}],reviewer:null,reproduction:null,question_key:null};}
before(async()=>{
  const out=process.env.XZ_SUPPORT_TEST_OUTPUT;if(!out)throw new Error('XZ_SUPPORT_TEST_OUTPUT must name scratch output');await mkdir(out,{recursive:true});
  const built=await build({entryPoints:['tests/entry.ts'],bundle:true,write:false,format:'esm',platform:'browser',target:'es2022'});
  mf=new Miniflare(convertV4MiniflareOptions({modules:true,script:built.outputFiles[0].text,compatibilityDate:'2026-10-24',d1Databases:['DB'],r2Buckets:['REPORTS'],unsafeEphemeralDurableObjects:true,
    bindings:{ENABLED:'true',PUBLIC_ORIGIN:origin,ADMIN_TOKEN_SHA256:sha(admin),RATE_KEY:'synthetic-rate-key-'.repeat(4),GITHUB_BOT_TOKEN:'NEVER-USED-CANARY',AUTOMATION_ENABLED:'false',PUBLIC_POSTING_ENABLED:'false'},
    outboundService:async request=>{outbound.push(request.url);return new Response('{}',{status:503});}}));
  db=await mf.getD1Database('DB');bucket=await mf.getR2Bucket('REPORTS');
  const apply=async name=>{for(const statement of (await readFile('migrations/'+name,'utf8')).split(/;\s*(?=(?:--[^\n]*\n\s*)*(?:CREATE|ALTER|INSERT))/)) await db.prepare(statement).run();};
  await apply('0001_reports.sql');
  const legacy='1'.repeat(32),now=Date.now();
  await db.prepare(`INSERT INTO reports(id,digest,key_hash,object_key,ip_hash,day,bytes,state,reserved_at,accepted_at,expires_at,public_json)
    VALUES(?,?,?,?,?,?,?,'accepted',?,?,?,?)`).bind(legacy,'c'.repeat(64),sha(key),'legacy','ip','old',100,now,now,now+14*86400000,JSON.stringify({title:'PRIVATE-MIGRATION',description:'SECRET'})).run();
  await apply('0002_bugdesk_tracking.sql');
  migration={row:await db.prepare('SELECT * FROM report_tracking').first(),redirect:(await get('/r/'+legacy+'/github')).status,receipt:await (await get('/v2/reports/'+legacy+'/receipt')).json()};
});
after(async()=>{await mf?.dispose();});
beforeEach(async()=>{
  await db.prepare('DELETE FROM report_tracking').run();await db.prepare('DELETE FROM reports').run();await db.prepare('DELETE FROM bug_fingerprints').run();await db.prepare('DELETE FROM bug_groups').run();
  for(const o of (await bucket.list()).objects) await bucket.delete(o.key);outbound=[];ipSequence=0;
});
test('migration retains receipts without inferring public consent or classification',()=>{
  assert.equal(migration.row.receipt_key_hash,sha(key));assert.equal(migration.row.read_key_hash,null);assert.equal(migration.row.classified_kind,null);assert.equal(migration.redirect,404);
  assert.equal(migration.receipt.schema,'xz-support-receipt/2');assert.ok(!JSON.stringify(migration).includes('PRIVATE-MIGRATION'));
});
test('one sentence, unknown hint, immutable retry, exact escaped limit, private zero GitHub calls',async()=>{
  const r=draft();delete r.kind_hint;r.description='It stopped.';r.private.items=[];r.private.contact=null;r.private.contact_consent=false;
  const raw=JSON.stringify(r)+' '.repeat(524288-Buffer.byteLength(JSON.stringify(r)));
  let response=await upload(r,raw);assert.equal(response.status,200,await response.clone().text());const receipt=await response.json();assert.equal(receipt.payload_sha256,sha(raw));
  response=await upload(r,raw);assert.equal(response.status,200);assert.equal((await response.json()).report_id,r.report_id);
  assert.equal((await upload(r,JSON.stringify({...r,description:'changed'}))).status,409);
  assert.equal((await get('/r/'+r.report_id+'/github')).status,404);
  assert.equal((await send('/v1/reports/'+r.report_id+'/issue',{issue_url:'https://github.com/OpticMystic/XZ-Mods/issues/1'},key)).status,404);
  assert.equal((await get('/v2/reports/'+r.report_id+'/receipt')).status,200);assert.equal(outbound.length,0);
  const stored=await db.prepare('SELECT * FROM reports').first();assert.equal(stored.public_json,null);assert.equal((await bucket.get(stored.object_key)).size,524288);
});
test('strict v2 schema rejects unknown fields, identities, unsafe facts, UTF-8 overflow and ideas with logs',async()=>{
  const mutations=[r=>r.public_issue={title:'leak'},r=>r.fingerprint='forged',r=>r.private.items[0].facts[0]={...fact,serial:'SERIAL-CANARY'},
    r=>r.private.items[0].facts[0].code='SECRET-CANARY',r=>r.description='😀'.repeat(1025),r=>r.kind_hint='NOT_A_BUG',r=>r.kind_hint='FEATURE',
    r=>r.private.contact_consent=false,r=>r.private.items[0].selected=false,r=>r.private.items.push(r.private.items[0]),r=>r.incident.before_seconds=3601];
  for(const mutate of mutations) {const r=draft();mutate(r);const response=await upload(r);assert.equal(response.status,400,await response.text());}
  assert.equal((await bucket.list()).objects.length,0);
});
test('receipt, owner, read, reply and browser sessions cannot cross authority; revoke and expiry work',async()=>{
  const r=await accepted();const path='/v2/reports/'+r.report_id;
  assert.equal((await get(path+'/status',readKey)).status,404);assert.equal((await get(path+'/status',admin)).status,404);
  await get(path+'/receipt');
  const status=await get('/v2/tracking/'+r.report_id,readKey);assert.equal(status.status,200);assert.equal(status.headers.get('etag'),'"1"');
  assert.equal((await get('/v2/tracking/'+r.report_id,replyKey)).status,404);
  assert.equal((await get('/v2/tracking/'+id(),readKey)).status,404);
  assert.equal((await get(path+'/status',key,{'If-None-Match':'"1"'})).status,304);
  assert.equal((await send('/v2/admin/reports/'+r.report_id+'/status',{expected_revision:1,operation_id:id(),lifecycle:'needs_info',question_key:'more_detail'},readKey)).status,401);
  const response=await send('/v2/tracking/session',{report_id:r.report_id},readKey);assert.equal(response.status,200);const session=await response.json();
  assert.equal((await get('/v2/tracking/'+r.report_id,session.session)).status,200);
  assert.equal((await send('/v2/tracking/session',{report_id:r.report_id},session.session)).status,404);
  await db.prepare('UPDATE tracking_sessions SET expires_at=0').run();assert.equal((await get('/v2/tracking/'+r.report_id,session.session)).status,404);
  assert.equal((await send(path+'/capabilities',{action:'revoke'},key)).status,200);
  await get(path+'/receipt');assert.equal((await get('/v2/tracking/'+r.report_id,readKey)).status,404);
});
test('diagnostic and private note expiry preserves tracking and prevents private paging forever',async()=>{
  const r=await accepted(),now=Date.now(),path='/v2/admin/reports/'+r.report_id;
  const n={expected_revision:1,operation_id:id(),content:'PRIVATE-NOTE-CANARY C:/Users/person/song.wav'};
  assert.equal((await send(path+'/notes',n)).status,200);assert.equal((await send(path+'/notes',n)).status,200);
  const cleanup=await get('/__test/cleanup',admin,{'test-now':String(now+22*86400000)});assert.equal(cleanup.status,200);
  assert.equal((await get(path,admin)).status,410);assert.equal((await db.prepare('SELECT count(*) AS n FROM private_notes').first()).n,0);
  await get('/__test/cleanup',admin,{'test-now':String(now+30*86400000)});
  assert.equal((await get(path,admin)).status,404);
  assert.equal((await upload(r)).status,409);
  const receipt=await (await get('/v2/reports/'+r.report_id+'/receipt')).json();assert.equal(receipt.private_logs,'gone');assert.equal(receipt.tracking.lifecycle,'received');
  assert.ok(!JSON.stringify(receipt).includes('CANARY'));assert.equal((await bucket.list()).objects.length,0);
});
test('owner transitions require citations, independent dismissal review and later-phase evidence gates',async()=>{
  const r=await accepted(),path='/v2/admin/reports/'+r.report_id;
  const bad=triage();bad.citations=[];assert.equal((await send(path+'/triage',bad)).status,400);
  const good=triage();let response=await send(path+'/triage',good);assert.equal(response.status,200,await response.clone().text());
  const status=await response.json();assert.equal(status.classified_kind,'BUG');assert.equal(status.revision,2);assert.ok(!JSON.stringify(status).includes('PRIVATE'));
  assert.equal((await send(path+'/triage',good)).status,200);
  const dismissal=triage('NOT_A_BUG',2);assert.equal((await send(path+'/triage',dismissal)).status,400);
  dismissal.reviewer={decision:'approved',review_id:id(),source_commit:'c'.repeat(40)};
  response=await send(path+'/triage',dismissal);assert.equal(response.status,200,await response.clone().text());assert.equal((await response.json()).lifecycle,'closed');
  const reopen=triage('FEATURE',3);response=await send(path+'/triage',reopen);assert.equal(response.status,200);
  for(const lifecycle of ['fix_in_progress','in_test_build','released']) assert.equal((await send(path+'/status',{expected_revision:4,operation_id:id(),lifecycle,question_key:null})).status,409);
  const events=await db.prepare('SELECT * FROM status_events WHERE report_id=?').bind(r.report_id).all();assert.equal(events.results.length,4);assert.ok(!JSON.stringify(events).includes('PRIVATE'));
});
test('concurrent compare-and-swap records exactly one event; stale private notes fail',async()=>{
  const r=await accepted(),path='/v2/admin/reports/'+r.report_id;
  const replies=await Promise.all(Array.from({length:8},()=>send(path+'/status',{expected_revision:1,operation_id:id(),lifecycle:'needs_info',question_key:'more_detail'})));
  assert.equal(replies.filter(r=>r.status===200).length,1);assert.ok(replies.every(r=>[200,409].includes(r.status)));
  assert.equal((await db.prepare('SELECT count(*) AS n FROM status_events').first()).n,2);
  assert.equal((await send(path+'/notes',{expected_revision:1,operation_id:id(),content:'stale'})).status,409);
});
test('fingerprints use selected safe facts, keep kinds separate and never reveal canonical reports',async()=>{
  const a=await accepted(),b=draft();b.description='Different private prose';b.private.contact='OTHER-CONTACT';await accepted(b);
  const idea=draft();idea.kind_hint='FEATURE';idea.private.items=[];await accepted(idea);
  for(const r of [a,b]) {const response=await send('/v2/admin/reports/'+r.report_id+'/triage',triage());assert.equal(response.status,200,await response.text());}
  assert.equal((await db.prepare('SELECT count(*) AS n FROM bug_groups').first()).n,1);
  assert.equal((await send('/v2/admin/reports/'+idea.report_id+'/triage',triage('FEATURE'))).status,200);
  assert.equal((await db.prepare('SELECT count(*) AS n FROM report_group_members').first()).n,2);
  const duplicate=triage('BUG',2);duplicate.outcome='DUPLICATE';let response=await send('/v2/admin/reports/'+b.report_id+'/triage',duplicate);assert.equal(response.status,200,await response.clone().text());
  const visible=await (await get('/v2/reports/'+b.report_id+'/status')).text();assert.ok(!visible.includes(a.report_id));assert.ok(!visible.includes('CONTACT'));assert.equal(outbound.length,0);
});
test('owner keyset inbox crosses 100 tied records, excludes later inserts, survives deletion, and exposes event cursor',async()=>{
  const now=Date.now(),ids=Array.from({length:125},()=>id()).sort();
  for(const report of ids) await db.prepare(`INSERT INTO report_tracking(report_id,receipt_key_hash,payload_sha256,created_at,updated_at,retention_until,diagnostics_expires_at,last_operation,last_operation_hash)
    VALUES(?,?,?,?,?,?,?,'accepted',?)`).bind(report,sha(key),'c'.repeat(64),now,now,now+365*86400000,now+14*86400000,'c'.repeat(64)).run();
  let page=await (await get('/v2/admin/reports?limit=50',admin)).json();assert.equal(page.reports.length,50);const seen=page.reports.map(r=>r.report_id);
  const later=await accepted();await db.prepare('DELETE FROM report_tracking WHERE report_id=?').bind(ids[60]).run();
  await db.prepare('UPDATE report_tracking SET priority=0 WHERE report_id=?').bind(ids[90]).run();
  while(page.next_cursor) {const response=await get('/v2/admin/reports?limit=50&cursor='+encodeURIComponent(page.next_cursor),admin);assert.equal(response.status,200,await response.clone().text());page=await response.json();seen.push(...page.reports.map(r=>r.report_id));}
  assert.equal(seen.length,124);assert.equal(new Set(seen).size,124);assert.ok(!seen.includes(later.report_id));
  assert.deepEqual(seen,ids.filter(value=>value!==ids[60]));
  const events=await (await get('/v2/admin/reports?events_after=0&limit=100',admin)).json();assert.equal(events.events.length,100);assert.equal(events.has_more,true);
  assert.equal((await get('/v2/admin/reports?cursor=invalid',admin)).status,400);assert.equal((await get('/v2/admin/reports?limit=0',admin)).status,400);
});
test('tracking shell clears URL fragments, uses textContent, never embeds report bytes or remote scripts',async()=>{
  const page=await get('/track',null),html=await page.text(),script=await (await get('/track.js',null)).text();
  assert.equal(page.headers.get('referrer-policy'),'no-referrer');assert.equal(page.headers.get('cache-control'),'no-store');assert.match(page.headers.get('content-security-policy'),/frame-ancestors 'none'/);
  assert.match(script,/history.replaceState/);assert.match(script,/textContent/);assert.ok(!/innerHTML|localStorage|https?:\/\//.test(script));assert.ok(!html.includes('CANARY'));
  assert.doesNotThrow(()=>new Script(script));
});
test('taxonomy eligibility evaluates every pair; hints never grant automatic authority',async()=>{
  for(const kind of [null,...contract.kinds]) for(const impact of [null,...contract.impacts]) {
    assert.equal(automaticFixEligible(kind,impact),['BUG','REGRESSION','DEGRADATION'].includes(kind)&&['ADDITIVE','RESTORES_EXPECTED'].includes(impact));
  }
  const r=draft();r.reporter_hints={topic:'GOT_WORSE',degradation_timing:'DURING_SET_OVER_TIME',expectation_baseline:'STOCK_XZ'};await accepted(r);
  const row=await db.prepare('SELECT classified_kind,impact FROM report_tracking WHERE report_id=?').bind(r.report_id).first();assert.equal(row.classified_kind,null);assert.equal(row.impact,null);
  const bad=draft();bad.desk={impact:'ADDITIVE'};assert.equal((await upload(bad)).status,400);
});
test('inbox sorts expected behavior before features and retains a private-safe taxonomy history',async()=>{
  const cases=[['FEATURE','ADDITIVE','NONE',null],['BUG','ADDITIVE','NONE','ANNOYANCE'],['EXPECTATION','RESTORES_EXPECTED','DJ_CONVENTION',null],
    ['EXPECTATION','RESTORES_EXPECTED','XZ_CONSISTENCY',null],['REGRESSION','RESTORES_EXPECTED','STOCK_XZ','WORKAROUND_EXISTS'],['DEGRADATION','ADDITIVE','NONE','LIVE_SET_STOPPER']];
  const ids=[];
  for(const [kind,impact,expectation,severity] of cases) {
    const r=await accepted();ids.push(r.report_id);const t=triage(kind);t.desk={...t.desk,impact,expectation,severity};
    if(expectation!=='NONE')t.expectation_citation={path:'docs/accepted-behavior.md',heading:'PRIVATE-CITATION',commit:'c'.repeat(40)};
    const response=await send('/v2/admin/reports/'+r.report_id+'/triage',t);assert.equal(response.status,200,await response.clone().text());
  }
  const response=await get('/v2/admin/reports?limit=100',admin);assert.equal(response.status,200);
  const rows=(await response.json()).reports;assert.deepEqual(rows.map(r=>r.report_id),[...ids].reverse());assert.ok(!JSON.stringify(rows).includes('PRIVATE'));
  const history=await (await get('/v2/admin/reports/'+ids[0]+'/history',admin)).json();assert.equal(history.history.length,1);assert.match(history.history[0].new_json,/FEATURE/);assert.ok(!JSON.stringify(history).includes('PRIVATE'));
});
test('Python collector bytes pass the same Worker contract, including actual native export when provided',async()=>{
  const out=process.env.XZ_SUPPORT_TEST_OUTPUT+'/python-roundtrip-'+id();
  const args=['-B','../../tools/support_roundtrip_fixture.py','--out',out];
  if(process.env.XZ_SUPPORT_NATIVE_HELPER)args.push('--native-helper',process.env.XZ_SUPPORT_NATIVE_HELPER);
  execFileSync('python',args,{stdio:'pipe',timeout:60000});
  const raw=await readFile(out+'/report.json','utf8'),report=JSON.parse(raw);
  const response=await upload(report,raw);assert.equal(response.status,200,await response.clone().text());assert.equal((await response.json()).payload_sha256,sha(raw));
  assert.ok(!raw.includes('CANARY'));assert.ok(raw.includes('WAIT_LOOPBACK'));
});
