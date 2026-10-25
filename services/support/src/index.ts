import { encoder, reportSchema, publicSchema, issueSchema } from './schema';

type Env = Cloudflare.Env & { ADMIN_TOKEN_SHA256: string; RATE_KEY: string };
type DB = ReturnType<Env['DB']['withSession']>;
type Row = {
  id: string; digest: string; key_hash: string; object_key: string; bytes: number;
  state: 'reserved' | 'accepted' | 'deleting' | 'gone'; reserved_at: number;
  accepted_at: number | null; expires_at: number | null; gone_at: number | null;
  public_json: string | null; issue_state: 'pending' | 'user_provided' | 'verified'; issue_url: string | null;
};
const DAY = 86400000;
const MAX = 524288;
const ID = '[a-f0-9]{32}';
const SECRET = /^[a-f0-9]{64}$/;
class HttpError extends Error {
  constructor(readonly status: number, readonly code: string) { super(code); }
}
function fail(status: number, code: string): never { throw new HttpError(status,code); }
const headers = {
  'Cache-Control': 'no-store', 'Referrer-Policy': 'no-referrer',
  'X-Content-Type-Options': 'nosniff', 'Content-Security-Policy': "default-src 'none'; frame-ancestors 'none'"
};
function json(value: unknown, status = 200): Response {
  const body = JSON.stringify(value);
  if (encoder.encode(body).length >= 65536) fail(500,'response_limit');
  return new Response(body,{status,headers:{...headers,'Content-Type':'application/json'}});
}
const hex = (bytes: ArrayBuffer) => Array.from(new Uint8Array(bytes), b => b.toString(16).padStart(2,'0')).join('');
const sha = async (value: string | Uint8Array) => hex(await crypto.subtle.digest('SHA-256',typeof value === 'string' ? encoder.encode(value) : value));
function equal(a: string, b: string): boolean {
  return a.length === b.length && crypto.subtle.timingSafeEqual(encoder.encode(a),encoder.encode(b));
}
async function ipHash(env: Env, day: string, ip: string): Promise<string> {
  const key = await crypto.subtle.importKey('raw',encoder.encode(env.RATE_KEY),{name:'HMAC',hash:'SHA-256'},false,['sign']);
  return hex(await crypto.subtle.sign('HMAC',key,encoder.encode(`${day}\n${ip}`)));
}
function bearer(req: Request): string {
  const match = /^Bearer ([a-f0-9]{64})$/.exec(req.headers.get('Authorization') ?? '');
  return match?.[1] ?? fail(401,'unauthorized');
}
async function owner(req: Request, env: Env): Promise<void> {
  const match=/^Bearer ([a-zA-Z0-9_-]{32,256})$/.exec(req.headers.get('Authorization') ?? '');
  if (!match || !equal(await sha(match[1]),env.ADMIN_TOKEN_SHA256)) fail(401,'unauthorized');
}
const row = (db: DB, id: string) => db.prepare('SELECT * FROM reports WHERE id=?').bind(id).first<Row>();
function active(r: Row, now: number): void {
  if (r.state === 'gone' || r.state === 'deleting' || r.expires_at !== null && r.expires_at <= now ||
      r.state === 'reserved' && r.reserved_at <= now-900000) fail(410,'gone');
  if (r.state !== 'accepted') fail(503,'outcome_unknown');
}
async function reporter(req: Request, db: DB, id: string, now: number): Promise<Row> {
  const key = await sha(bearer(req));
  const r = await row(db,id);
  if (!r || !equal(r.key_hash,key)) fail(404,'not_found');
  active(r,now);
  return r;
}
function receipt(r: Row, env: Env) {
  if (r.accepted_at === null || r.expires_at === null) fail(503,'outcome_unknown');
  return {schema:'xz-support-receipt/1', report_id:r.id, payload_sha256:r.digest,
    accepted_at:new Date(r.accepted_at).toISOString(),expires_at:new Date(r.expires_at).toISOString(),
    private_logs:'stored',public_issue:r.issue_state,github_draft_url:`${env.PUBLIC_ORIGIN}/r/${r.id}/github`,issue_url:r.issue_url};
}
function bodyLength(req: Request, max: number): number {
  if (req.headers.get('Content-Type') !== 'application/json' || req.headers.has('Content-Encoding') ||
      req.headers.has('Transfer-Encoding')) fail(415,'unsupported_encoding');
  const raw = req.headers.get('Content-Length') ?? '';
  if (!/^[1-9][0-9]{0,6}$/.test(raw)) fail(411,'length_required');
  const length = Number(raw);
  if (length > max) fail(413,'too_large');
  return length;
}
async function bytes(req: Request, length: number): Promise<Uint8Array> {
  if (!req.body) fail(400,'length_mismatch');
  const reader = req.body.getReader();
  const out = new Uint8Array(length);
  let total = 0;
  const timeout = AbortSignal.timeout(15000);
  const timedOut = new Promise<never>((_,reject) => {
    timeout.addEventListener('abort',() => reject(new HttpError(408,'body_timeout')),{once:true});
  });
  try {
    while (true) {
      const next = await Promise.race([
        reader.read(),
        timedOut
      ]);
      if (next.done) break;
      total += next.value.byteLength;
      if (total > length) fail(400,'length_mismatch');
      out.set(next.value,total-next.value.byteLength);
    }
    if (total !== length) fail(400,'length_mismatch');
    return out;
  } finally { void reader.cancel().catch(() => {}); }
}
function parse(raw: Uint8Array): unknown {
  try { return JSON.parse(new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(raw)); }
  catch { return fail(400,'invalid_json'); }
}
async function submit(req: Request, env: Env, db: DB, now: number): Promise<Response> {
  const length = bodyLength(req,MAX);
  const id = req.headers.get('X-Report-ID') ?? '';
  const digest = req.headers.get('X-Report-SHA256') ?? '';
  if (!new RegExp(`^${ID}$`).test(id) || !SECRET.test(digest) ||
      req.headers.get('X-Report-Consent') !== 'public-summary-private-logs/v1') fail(400,'invalid_headers');
  const keyHash = await sha(bearer(req));
  let previous = await row(db,id);
  if (previous && (!equal(previous.key_hash,keyHash) || previous.digest !== digest || previous.bytes !== length)) fail(409,'conflict');
  if (previous && previous.state !== 'accepted') active(previous,now);
  if (!previous) {
    const day = new Date(now).toISOString().slice(0,10);
    const ip = req.headers.get('CF-Connecting-IP');
    if (!ip || ip.length > 64) fail(503,'rate_identity_unavailable');
    const ipKey = await ipHash(env,day,ip);
    try {
      await db.prepare(`INSERT INTO reports(id,digest,key_hash,object_key,ip_hash,day,bytes,state,reserved_at)
        SELECT ?,?,?,?,?,?,?,'reserved',? WHERE NOT EXISTS(SELECT 1 FROM reports WHERE id=?)`)
        .bind(id,digest,keyHash,`reports/${day}/${id}/${crypto.randomUUID()}`,ipKey,day,length,now,id).run();
    } catch (e) {
      if (e instanceof Error && e.message.includes('quota')) fail(429,'quota');
      throw e;
    }
    previous = await row(db,id);
    if (!previous) fail(503,'outcome_unknown');
    if (!equal(previous.key_hash,keyHash) || previous.digest !== digest || previous.bytes !== length) fail(409,'conflict');
  }
  const raw = await bytes(req,length);
  if (await sha(raw) !== digest) fail(400,'digest_mismatch');
  const parsed = reportSchema.safeParse(parse(raw));
  if (!parsed.success || parsed.data.report_id !== id) fail(400,'invalid_report');
  if (previous.state === 'accepted') { active(previous,Date.now()); return json(receipt(previous,env)); }
  // Conditional R2 creation allows one writer, even when requests race the D1 insert.
  const report = parsed.data;
  const publicFields = {title:report.public_issue.title, description:report.public_issue.description,
    origin:report.origin,category:report.category,
    version:/^[a-zA-Z0-9.+_-]{0,64}$/.test(report.build.app_version) ? report.build.app_version : ''};
  const stored = await env.REPORTS.put(previous.object_key,raw,{onlyIf:{etagDoesNotMatch:'*'},sha256:digest});
  if (!stored) fail(503,'outcome_unknown');
  const accepted = Date.now();
  const result = await db.prepare(`UPDATE reports SET state='accepted',accepted_at=?,expires_at=?,public_json=?
    WHERE id=? AND state='reserved' AND reserved_at>?`).bind(accepted,accepted+14*DAY,JSON.stringify(publicFields),id,accepted-900000).run();
  if (result.meta.changes !== 1) fail(503,'outcome_unknown');
  const done = await row(db,id);
  if (!done) fail(503,'outcome_unknown');
  return json(receipt(done,env));
}

async function githubDraft(db: DB, id: string, now: number): Promise<Response> {
  const r = await row(db,id);
  if (!r) fail(404,'not_found');
  active(r,now);
  const p = publicSchema.parse(JSON.parse(r.public_json ?? 'null'));
  const url = new URL('https://github.com/OpticMystic/XZ-Mods/issues/new');
  url.searchParams.set('title',p.title);
  url.searchParams.set('body',`${p.description}\n\nOrigin: ${p.origin}\nCategory: ${p.category}\nApp version: ${p.version}\n\nXZ support report: ${id}`);
  return new Response(null,{status:303,headers:{...headers,Location:url.href}});
}

async function linkIssue(req: Request, env: Env, db: DB, id: string, now: number): Promise<Response> {
  const r = await reporter(req,db,id,now);
  const parsed = issueSchema.safeParse(parse(await bytes(req,bodyLength(req,1024))));
  if (!parsed.success) fail(400,'invalid_issue');
  const match = /^https:\/\/github\.com\/OpticMystic\/XZ-Mods\/issues\/([1-9][0-9]{0,9})$/.exec(parsed.data.issue_url);
  if (!match) fail(400,'invalid_issue');
  let verified = false;
  try {
    const response = await fetch(`https://api.github.com/repos/OpticMystic/XZ-Mods/issues/${match[1]}`,{
      redirect:'manual',signal:AbortSignal.timeout(5000),headers:{Accept:'application/vnd.github+json','User-Agent':'xz-mods-support','X-GitHub-Api-Version':'2022-11-28'}
    });
    if (response.ok) {
      // Public GitHub data is untrusted and bounded independently from upload bodies.
      const content = await boundedResponse(response,262144);
      const data: unknown = JSON.parse(content);
      verified = typeof data === 'object' && data !== null && 'body' in data && typeof data.body === 'string' &&
        data.body.split(/\r?\n/).includes(`XZ support report: ${id}`) && !('pull_request' in data) &&
        'html_url' in data && data.html_url === parsed.data.issue_url && 'number' in data && data.number === Number(match[1]);
    }
  } catch { /* An unavailable API leaves a user-provided, unverified link. */ }
  // Recheck expiry/deletion at commit; never downgrade an already verified link.
  await db.prepare(`UPDATE reports SET issue_state=?,issue_url=? WHERE id=? AND state='accepted' AND expires_at>?
    AND issue_state!='verified'`).bind(verified?'verified':'user_provided',parsed.data.issue_url,id,Date.now()).run();
  const updated = await row(db,r.id);
  if (!updated) fail(404,'not_found');
  active(updated,Date.now());
  return json(receipt(updated,env));
}
async function boundedResponse(response: Response, limit: number): Promise<string> {
  if (!response.body) throw new Error('empty');
  const reader = response.body.getReader();
  const chunks: Uint8Array[] = [];
  let total=0;
  try {
    while(true) {
      const {done,value} = await reader.read();
      if(done) break;
      total+=value.length;
      if(total>limit) throw new Error('large');
      chunks.push(value);
    }
  } finally { void reader.cancel().catch(() => {}); }
  const out=new Uint8Array(total); let offset=0;
  for(const chunk of chunks) { out.set(chunk,offset); offset+=chunk.length; }
  return new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(out);
}

export async function cleanup(env: Env, now = Date.now()): Promise<void> {
  const db=env.DB.withSession('first-primary');
  await db.prepare(`UPDATE reports SET state='deleting',gone_at=? WHERE
    (state='accepted' AND expires_at<=?) OR (state='reserved' AND reserved_at<=?)`).bind(now,now,now-900000).run();
  const pending = await db.prepare(`SELECT * FROM reports WHERE state IN ('deleting','gone') ORDER BY last_swept LIMIT 200`).all<Row>();
  if(!pending.results.length) return;
  await env.REPORTS.delete(pending.results.map(r=>r.object_key));
  const ids=JSON.stringify(pending.results.map(r=>r.id));
  await db.batch([
    db.prepare(`UPDATE reports SET state='gone',public_json=NULL,issue_url=NULL,last_swept=?
      WHERE id IN (SELECT value FROM json_each(?)) AND state IN ('deleting','gone')`).bind(now,ids),
    db.prepare(`DELETE FROM reports WHERE state='gone' AND gone_at<=?
      AND id IN (SELECT value FROM json_each(?))`).bind(now-7*DAY,ids)
  ]);
}

async function ownerPage(req: Request, env: Env, db: DB, id: string, search: string): Promise<Response> {
  // Authenticate every page before checking offsets or report existence.
  await owner(req,env);
  const match=/^\?offset=(0|[1-9][0-9]{0,5})$/.exec(search);
  if(search && !match) fail(400,'invalid_offset');
  const offset=match ? Number(match[1]) : 0;
  const r=await row(db,id);
  if(!r) fail(404,'not_found');
  active(r,Date.now());
  if(!Number.isSafeInteger(offset) || offset<0 || offset>=r.bytes) fail(416,'invalid_offset');
  const length=Math.min(24576,r.bytes-offset);
  const object=await env.REPORTS.get(r.object_key,{range:{offset,length}});
  if(!object || object.size!==r.bytes) fail(503,'outcome_unknown');
  const data=new Uint8Array(await object.arrayBuffer());
  if(data.length!==length) fail(503,'outcome_unknown');
  // A deletion/expiry racing the R2 read must not return a newly authorized page.
  const latest=await row(db,id);
  if(!latest) fail(404,'not_found');
  active(latest,Date.now());
  if(latest.digest!==r.digest || latest.object_key!==r.object_key) fail(409,'conflict');
  const next=offset+length;
  return json({schema:'xz-support-owner-page/1',report_id:r.id,payload_sha256:r.digest,
    total_bytes:r.bytes,offset,encoding:'base64',data_base64:btoa(String.fromCharCode(...data)),
    next_offset:next<r.bytes?next:null,receipt:receipt(latest,env)});
}

async function dispatch(req: Request, env: Env): Promise<Response> {
  if(env.ENABLED !== 'true' || !SECRET.test(env.ADMIN_TOKEN_SHA256 ?? '') || encoder.encode(env.RATE_KEY ?? '').length<32 ||
    equal(env.ADMIN_TOKEN_SHA256,await sha(env.RATE_KEY))) fail(503,'not_configured');
  const url=new URL(req.url);
  if(url.protocol!=='https:') fail(400,'https_required');
  let headerSize=0;
  for(const [name,value] of req.headers) { headerSize+=name.length+value.length; if(value.length>1024) fail(431,'headers_too_large'); }
  if(headerSize>8192 || url.href.length>1024) fail(431,'headers_too_large');
  const db=env.DB.withSession('first-primary'); const now=Date.now(); const path=url.pathname;
  const ownerPath=new RegExp(`^/v1/admin/reports/(${ID})$`).exec(path);
  if(ownerPath && req.method==='GET') return ownerPage(req,env,db,ownerPath[1],url.search);
  if(url.search) fail(400,'invalid_query');
  if(req.method==='POST' && path==='/v1/reports') return submit(req,env,db,now);
  let m=new RegExp(`^/v1/reports/(${ID})/receipt$`).exec(path);
  if(m && req.method==='GET') return json(receipt(await reporter(req,db,m[1],now),env));
  m=new RegExp(`^/r/(${ID})/github$`).exec(path);
  if(m && req.method==='GET') return githubDraft(db,m[1],now);
  m=new RegExp(`^/v1/reports/(${ID})/issue$`).exec(path);
  if(m && req.method==='POST') return linkIssue(req,env,db,m[1],now);
  if(path==='/v1/admin/reports' || new RegExp(`^/v1/admin/reports/${ID}$`).test(path)) {
    await owner(req,env);
    if(path==='/v1/admin/reports' && req.method==='GET') {
      const rows=await db.prepare(`SELECT id,state,bytes,accepted_at,expires_at,issue_state FROM reports ORDER BY reserved_at DESC LIMIT 100`).all();
      return json({reports:rows.results});
    }
    const id=path.slice('/v1/admin/reports/'.length);
    if(new RegExp(`^${ID}$`).test(id)) {
      const r=await row(db,id); if(!r) fail(404,'not_found');
      if(req.method==='DELETE') {
        await db.prepare(`UPDATE reports SET state='deleting',gone_at=? WHERE id=? AND state NOT IN ('deleting','gone')`).bind(now,id).run();
        await env.REPORTS.delete(r.object_key);
        await db.prepare(`UPDATE reports SET state='gone',public_json=NULL,issue_url=NULL WHERE id=? AND state='deleting'`).bind(id).run();
        return json({report_id:id,state:'gone'});
      }
    }
  }
  return fail(404,'not_found');
}
export default {
  async fetch(req,env) {
    try { return await dispatch(req,env); }
    catch(e) { return json({error:e instanceof HttpError?e.code:'outcome_unknown'},e instanceof HttpError?e.status:503); }
  },
  async scheduled(_event,env) { await cleanup(env); }
} satisfies ExportedHandler<Env>;
