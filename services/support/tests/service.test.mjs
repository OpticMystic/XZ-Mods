import { before, after, beforeEach, test } from 'node:test';
import assert from 'node:assert/strict';
import {applyMigration} from './migrations.mjs';
import { createHash, randomBytes } from 'node:crypto';
import { readFile, mkdir } from 'node:fs/promises';
import { build } from 'esbuild';
import { Miniflare, convertV4MiniflareOptions } from 'miniflare';

const sha = value => createHash('sha256').update(value).digest('hex');
const origin='https://xz-mods-support.will-026.workers.dev';
const admin='a'.repeat(64), key='b'.repeat(64);
let mf, db, bucket, github = {status:503,body:{}}, outbound=[];
before(async () => {
  const out=process.env.XZ_SUPPORT_TEST_OUTPUT; if(!out) throw new Error('XZ_SUPPORT_TEST_OUTPUT must name scratch output');
  await mkdir(out,{recursive:true});
  await build({entryPoints:['tests/entry.ts'],outfile:process.env.XZ_SUPPORT_TEST_OUTPUT+'/worker-v1.mjs',bundle:true,format:'esm',platform:'browser',target:'es2022'});
  mf=new Miniflare(convertV4MiniflareOptions({modules:true,script:await readFile(process.env.XZ_SUPPORT_TEST_OUTPUT+'/worker-v1.mjs','utf8'),compatibilityDate:'2026-10-24',
    d1Databases:['DB'],r2Buckets:['REPORTS'],unsafeEphemeralDurableObjects:true,
    bindings:{ENABLED:'true',PUBLIC_ORIGIN:origin,ADMIN_TOKEN_SHA256:sha(admin),RATE_KEY:'local-test-rate-key-only-'.repeat(3)},
    outboundService: async req => {
      outbound.push(req.url);
      return new Response(JSON.stringify(github.body),{status:github.status,headers:{'Content-Type':'application/json'}});
    }}));
  db=await mf.getD1Database('DB'); bucket=await mf.getR2Bucket('REPORTS');
  for(const name of ['0001_reports.sql','0002_bugdesk_tracking.sql','0003_support_identifiers.sql']) await applyMigration(db,name);
});
after(async () => { await mf?.dispose(); });
beforeEach(async () => {
  await db.prepare('DELETE FROM report_tracking').run();
  await db.prepare('DELETE FROM reports').run();
  const list=await bucket.list();
  for(const object of list.objects) await bucket.delete(object.key);
  outbound=[]; github={status:503,body:{}};
});
function draft(id=randomBytes(16).toString('hex')) {
  return {schema:'xz-support-report/1',report_id:id,created_at:'unknown',origin:'loader',category:'bridge',
    build:{app_version:'0.5.0-preview.19',runtime_sha256:null,bridge_sha256:null,module_sha256:null},
    public_issue:{title:'Four decks do not appear',description:'Approved reproduction steps'},
    private:{contact:'PRIVATE-CONTACT-CANARY',items:[{label:'bridge.log',content:'PRIVATE-LOG-CANARY https://private.invalid/secret TOKEN-CANARY',truncated:false,missing:false}]},
    redactions:{count:0,omitted_lines:0}};
}
function upload(report, options={}) {
  const raw=options.raw ?? JSON.stringify(report);
  return mf.dispatchFetch(`${origin}/v1/reports`,{method:'POST',body:raw,headers:{
    'Content-Type':'application/json','Content-Length':String(Buffer.byteLength(raw)),
    'X-Report-ID':report.report_id,'X-Report-SHA256':sha(raw),'X-Report-Consent':'public-summary-private-logs/v1',
    Authorization:`Bearer ${key}`,'CF-Connecting-IP':options.ip ?? '203.0.113.1',...options.headers}});
}
const get=(path,token=key) => mf.dispatchFetch(origin+path,{redirect:'manual',headers:token?{Authorization:`Bearer ${token}`}:{}});
const cleanup=now => mf.dispatchFetch(origin+'/__test/cleanup',{headers:{'test-now':String(now)}});
async function accepted(report=draft()) {
  const res=await upload(report); assert.equal(res.status,200,await res.clone().text());
  return {report,receipt:await res.json()};
}
test('public HTTP privacy, exact R2 bytes and distinct reporter/owner authority',async () => {
  const report=draft(), raw=JSON.stringify(report,null,2)+'\n';
  const res=await upload(report,{raw}); assert.equal(res.status,200,await res.clone().text());
  const receipt=await res.json(); assert.equal(receipt.payload_sha256,sha(raw));
  assert.equal(receipt.public_issue,'pending'); assert.equal(receipt.issue_url,null);
  const stored=await db.prepare('SELECT * FROM reports').first();
  assert.equal(await (await bucket.get(stored.object_key)).text(),raw);
  assert.equal(stored.key_hash,sha(key)); assert.notEqual(stored.ip_hash,'203.0.113.1');
  const redirect=await get(`/r/${report.report_id}/github`,null);
  assert.equal(redirect.status,303); assert.equal(await redirect.text(),'');
  const location=redirect.headers.get('location'), target=new URL(location);
  assert.equal(target.origin,'https://github.com'); assert.equal(target.pathname,'/OpticMystic/XZ-Mods/issues/new');
  assert.match(target.searchParams.get('body'),new RegExp(`XZ support report: ${report.report_id}`));
  for(const publicResponse of [JSON.stringify(receipt),decodeURIComponent(location),await (await get(`/v1/reports/${report.report_id}/receipt`)).text()]) {
    for(const canary of ['PRIVATE-CONTACT','PRIVATE-LOG','private.invalid','TOKEN-CANARY',key]) assert.ok(!publicResponse.includes(canary),canary);
  }
  for(const path of [`/v1/reports/${report.report_id}`,`/v1/reports/${report.report_id}/receipt`,`/v1/admin/reports/${report.report_id}`]) {
    assert.ok([401,404].includes((await get(path,null)).status));
  }
  assert.equal((await get('/v1/admin/reports',key)).status,401);
  assert.equal((await get(`/v1/reports/${report.report_id}/receipt`,admin)).status,404);
  const list=await get('/v1/admin/reports',admin); assert.equal(list.status,200); assert.equal(list.headers.get('cache-control'),'no-store');
  assert.equal((await list.json()).reports.length,1);
});
test('owner pages stay below 64 KiB, require authorization each time and reassemble the immutable exact bytes',async () => {
  const report=draft();
  report.private.items=[
    {label:'bridge.log',content:'🙂'.repeat(30000),truncated:false,missing:false},
    {label:'runtime.log',content:'私'.repeat(40000),truncated:false,missing:false},
    {label:'app.log',content:'x'.repeat(120000),truncated:true,missing:false}
  ];
  const raw=JSON.stringify(report,null,2)+'\r\n';
  const accepted=await upload(report,{raw}); assert.equal(accepted.status,200);
  const expected=await accepted.json(); const path=`/v1/admin/reports/${report.report_id}`;
  let offset=0; const chunks=[];
  do {
    const pageResponse=await get(`${path}?offset=${offset}`,admin);
    assert.equal(pageResponse.status,200,await pageResponse.clone().text());
    assert.equal(pageResponse.headers.get('cache-control'),'no-store');
    const pageText=await pageResponse.text(); assert.ok(Buffer.byteLength(pageText)<65536);
    const page=JSON.parse(pageText); const bytes=Buffer.from(page.data_base64,'base64');
    assert.equal(page.schema,'xz-support-owner-page/1'); assert.equal(page.encoding,'base64');
    assert.equal(page.report_id,report.report_id); assert.equal(page.payload_sha256,expected.payload_sha256);
    assert.deepEqual(page.receipt,expected); assert.equal(page.total_bytes,Buffer.byteLength(raw));
    assert.equal(page.offset,offset); assert.ok(bytes.length>0 && bytes.length<=24576);
    assert.equal((await get(`${path}?offset=${offset}`,null)).status,401);
    assert.equal((await get(`${path}?offset=${offset}`,key)).status,401);
    chunks.push(bytes);
    assert.equal(page.next_offset,offset+bytes.length<page.total_bytes?offset+bytes.length:null);
    offset=page.next_offset;
  } while(offset!==null);
  const rebuilt=Buffer.concat(chunks);
  assert.deepEqual(rebuilt,Buffer.from(raw)); assert.equal(sha(rebuilt),expected.payload_sha256);
  assert.deepEqual(JSON.parse(rebuilt.toString('utf8')),report);
  for(const suffix of ['?offset=','?offset=-1','?offset=1.5','?offset=1e2','?offset=NaN','?offset=00','?offset=+1','?offset=1&offset=2','?offset=1&extra=2','?extra=0']) {
    assert.equal((await get(path+suffix,admin)).status,400,suffix);
  }
  assert.equal((await get(path+`?offset=${Buffer.byteLength(raw)}`,admin)).status,416);
  assert.equal((await get(path+'?offset=-1',null)).status,401);
  assert.equal((await get(path,admin)).status,200);
  assert.equal((await get(`/v1/reports/${report.report_id}/receipt?offset=0`)).status,400);
  await db.prepare('UPDATE reports SET expires_at=? WHERE id=?').bind(Date.now()-1,report.report_id).run();
  assert.equal((await get(path+'?offset=24576',admin)).status,410);
});
test('concurrent same-ID retries are immutable and produce one object; exact retry recovers receipt',async () => {
  const report=draft();
  const responses=await Promise.all(Array.from({length:8},()=>upload(report)));
  assert.ok(responses.some(r=>r.status===200)); assert.ok(responses.every(r=>[200,503].includes(r.status)));
  const receipts=await Promise.all(responses.filter(r=>r.status===200).map(r=>r.json()));
  for(const r of receipts) assert.deepEqual(r,receipts[0]);
  const retry=await upload(report); assert.equal(retry.status,200); assert.deepEqual(await retry.json(),receipts[0]);
  assert.equal((await bucket.list()).objects.length,1);
  assert.equal((await db.prepare('SELECT count(*) n FROM reports').first()).n,1);
  assert.equal((await upload({...report,category:'audio'})).status,409);
  assert.equal((await upload(report,{headers:{Authorization:`Bearer ${'c'.repeat(64)}`}})).status,409);
});
test('atomic IP quota survives racing different reports and retries do not spend another slot',async () => {
  const reports=Array.from({length:12},()=>draft());
  const responses=await Promise.all(reports.map(r=>upload(r)));
  assert.equal(responses.filter(r=>r.status===200).length,5);
  assert.equal(responses.filter(r=>r.status===429).length,7);
  const first=reports[responses.findIndex(r=>r.status===200)];
  assert.equal((await upload(first)).status,200);
  assert.equal((await bucket.list()).objects.length,5);
});
test('storage failures never fabricate acceptance and abandoned R2 objects are accounted then removed',async () => {
  for(const fault of ['r2-put','d1-accept']) {
    const report=draft();
    const response=await upload(report,{headers:{'test-storage-fault':fault}});
    assert.equal(response.status,503); assert.deepEqual(await response.json(),{error:'outcome_unknown'});
    const reserved=await db.prepare('SELECT * FROM reports WHERE id=?').bind(report.report_id).first();
    assert.equal(reserved.state,'reserved'); assert.equal(reserved.accepted_at,null);
    assert.equal((await get(`/v1/reports/${report.report_id}/receipt`)).status,503);
    assert.equal((await upload(report)).status,503);
    const object=await bucket.get(reserved.object_key);
    assert.equal(Boolean(object),fault==='d1-accept');
    await db.prepare('UPDATE reports SET reserved_at=? WHERE id=?').bind(Date.now()-1000000,report.report_id).run();
    await cleanup(Date.now());
    assert.equal(await bucket.get(reserved.object_key),null);
    assert.equal((await upload(report)).status,410);
  }
});
async function seed({count=1,size=524288,day=new Date().toISOString().slice(0,10),state='reserved',reserved=Date.now(),expires=null}={}) {
  // Seed real storage at a quota boundary rather than spending 100 uploads per test.
  const batch=[];
  for(let i=0;i<count;i++) {
    const id=randomBytes(16).toString('hex');
    batch.push(db.prepare(`INSERT INTO reports(id,digest,key_hash,object_key,ip_hash,day,bytes,state,reserved_at,expires_at)
      VALUES(?,?,?,?,?,?,?,?,?,?)`).bind(id,'0'.repeat(64),sha(key),`reports/${day}/${id}/seed`,`seed-${i}`,day,size,state,reserved,expires));
  }
  await db.batch(batch);
}
test('global count and retained-byte reservations enforce limits atomically',async () => {
  await seed({count:99,size:1});
  const race=await Promise.all([upload(draft(),{ip:'203.0.113.8'}),upload(draft(),{ip:'203.0.113.9'})]);
  assert.deepEqual(race.map(r=>r.status).sort(),[200,429]);
  await db.prepare('DELETE FROM report_tracking').run();
  await db.prepare('DELETE FROM reports').run();
  // Distinct days avoid daily quotas while filling retained reservations to exactly 1 GiB.
  for(let d=1;d<=21;d++) await seed({count:d===21?48:100,day:`2026-09-${String(d).padStart(2,'0')}`});
  assert.equal((await upload(draft())).status,429);
  assert.equal((await bucket.list()).objects.length,1); // only earlier accepted object; no new write
});
test('strict validation rejects unknown fields/types, bad UTF-8/length/digest/encoding before R2',async () => {
  const cases=[
    r=>({...r,extra:true}),r=>({...r,private:{...r.private,token:'bad'}}),
    r=>({...r,private:{...r.private,items:[{...r.private.items[0],missing:0}]}}),
    r=>({...r,private:{...r.private,items:[r.private.items[0],r.private.items[0]]}}),
    r=>({...r,build:{...r.build,runtime_sha256:'A'.repeat(64)}}),
    r=>({...r,public_issue:{...r.public_issue,title:'😀'.repeat(41)}}),
    r=>({...r,private:{...r.private,items:[{...r.private.items[0],content:'x'.repeat(131073)}]}}),
    r=>({...r,redactions:{count:0.5,omitted_lines:0}}),r=>({...r,created_at:'2026-02-30T00:00:00Z'}),
    r=>({...r,public_issue:{title:'\ud800',description:''}})
  ];
  for(let i=0;i<cases.length;i++) assert.equal((await upload(cases[i](draft()),{ip:`203.0.113.${i+2}`})).status,400);
  assert.equal((await upload(draft(),{headers:{'X-Report-SHA256':'0'.repeat(64)},ip:'198.51.100.1'})).status,400);
  assert.equal((await upload(draft(),{raw:new Uint8Array([0xff]),ip:'198.51.100.2'})).status,400);
  assert.equal((await upload(draft(),{headers:{'Content-Encoding':'gzip'}})).status,415);
  assert.equal((await upload(draft(),{headers:{'Content-Type':'text/plain'}})).status,415);
  assert.equal((await upload(draft(),{headers:{'test-declared-length':'524289'}})).status,413);
  assert.equal((await upload(draft(),{headers:{'test-declared-length':'10'},ip:'198.51.100.3'})).status,400);
  assert.equal((await upload(draft(),{headers:{'test-declared-length':'1000'},ip:'198.51.100.4'})).status,400);
  assert.equal((await upload(draft(),{headers:{'X-Report-Consent':'no'}})).status,400);
  assert.equal((await get('/v1/reports/../../arbitrary')).status,404);
  assert.equal((await mf.dispatchFetch(origin+'/v1/reports',{method:'PUT'})).status,404);
  assert.equal((await bucket.list()).objects.length,0);
});
test('the exact 512 KiB boundary is accepted as frozen bytes and oversized framing is rejected',async () => {
  const report=draft(); const encoded=JSON.stringify(report);
  const raw=encoded+' '.repeat(524288-Buffer.byteLength(encoded));
  const response=await upload(report,{raw}); assert.equal(response.status,200);
  const receipt=await response.json(); assert.equal(receipt.payload_sha256,sha(raw));
  const r=await db.prepare('SELECT * FROM reports WHERE id=?').bind(report.report_id).first();
  assert.equal(r.bytes,524288); assert.equal(await (await bucket.get(r.object_key)).text(),raw);
  assert.equal((await upload(draft(),{headers:{'test-declared-length':'524289'}})).status,413);
});
test('delete, expiry, abandoned reservations and late orphan writes remain inaccessible and are swept',async () => {
  const {report}=await accepted();
  const r=await db.prepare('SELECT * FROM reports WHERE id=?').bind(report.report_id).first();
  const deleted=await mf.dispatchFetch(`${origin}/v1/admin/reports/${r.id}`,{method:'DELETE',headers:{Authorization:`Bearer ${admin}`}});
  assert.equal(deleted.status,200); assert.equal(await bucket.get(r.object_key),null);
  assert.equal((await upload(report)).status,410); assert.equal((await get(`/v1/reports/${r.id}/receipt`)).status,410);
  // Simulate an R2 put completing after a deleting request's first delete.
  await bucket.put(r.object_key,'late private orphan');
  await cleanup(Date.now()+1000); assert.equal(await bucket.get(r.object_key),null);
  const second=await accepted();
  await db.prepare('UPDATE reports SET expires_at=? WHERE id=?').bind(Date.now()-1,second.report.report_id).run();
  assert.equal((await get(`/r/${second.report.report_id}/github`,null)).status,410);
  assert.equal((await get(`/v1/admin/reports/${second.report.report_id}`,admin)).status,410);
  await seed({size:500,reserved:Date.now()-1000000,day:'2026-01-01'});
  await cleanup(Date.now());
  assert.equal((await bucket.list()).objects.length,0);
  assert.equal((await db.prepare("SELECT count(*) n FROM reports WHERE state!='gone'").first()).n,0);
  await cleanup(Date.now()+8*86400000);
  assert.equal((await db.prepare('SELECT count(*) n FROM reports').first()).n,0);
});
test('GitHub links use only the fixed public API and distinguish unavailable/mismatched/verified issues',async () => {
  const {report}=await accepted(); const id=report.report_id;
  async function link(issue_url) {
    const raw=JSON.stringify({issue_url});
    return mf.dispatchFetch(`${origin}/v1/reports/${id}/issue`,{method:'POST',body:raw,headers:{'Content-Type':'application/json','Content-Length':String(Buffer.byteLength(raw)),Authorization:`Bearer ${key}`}});
  }
  for(const url of ['https://evil.invalid/1','https://github.com/Other/XZ-Mods/issues/1','https://github.com/OpticMystic/XZ-Mods/issues/0','https://github.com/OpticMystic/XZ-Mods/issues/12?token=bad']) assert.equal((await link(url)).status,400);
  assert.equal(outbound.length,0);
  const url='https://github.com/OpticMystic/XZ-Mods/issues/12';
  const first=await link(url); assert.equal(first.status,200,await first.clone().text());
  assert.equal((await first.json()).public_issue,'user_provided');
  github={status:200,body:{body:'wrong marker',html_url:url,number:12}};
  assert.equal((await (await link(url)).json()).public_issue,'user_provided');
  github.body.body=`Approved text\n\nXZ support report: ${id}`;
  assert.equal((await (await link(url)).json()).public_issue,'verified',JSON.stringify(outbound));
  github={status:503,body:{}};
  assert.equal((await (await link(url)).json()).public_issue,'verified');
  assert.ok(outbound.every(url=>url==='https://api.github.com/repos/OpticMystic/XZ-Mods/issues/12'));
});
