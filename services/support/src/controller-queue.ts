import { z } from 'zod';
import { CONTRACT, formatInstallationId, parseInstallationId, formatLoaderId, parseLoaderId, formatReportReference, parseReportReference, type PendingReport } from './contract.generated';
import { parseReportV2, utf8 } from './schema-v2';
import { DB, Env, DAY, MAX, SECRET, bearer, bodyLength, bytes, equal, fail, ipHash, json, parse, sha } from './http';
import { receiptV2 } from './tracking';

const limits = CONTRACT.controller_queue;
const secret = z.string().regex(SECRET);
const installationId = z.string().refine(v => { const n=parseInstallationId(v); return n!==null && formatInstallationId(n)===v; });
const loaderId = z.string().refine(v => { const n=parseLoaderId(v); return n!==null && formatLoaderId(n)===v; });
type Loader = {loader_id:string;installation_id:string;key_hash:string;revoked:number};
export type PendingAdmission = {installationId:string;loaderId:string|null;loaderKeyHash:string|null;keyHash:string;report:PendingReport};
export type Intake = (req:Request, env:Env, db:DB, now:number, v2:boolean, referenceGenerator:undefined, pending:PendingAdmission) => Promise<Response>;

export function parsePendingReport(value:unknown): PendingReport | null {
  // JSON Schema counts Unicode characters. Zod's string maximum counts UTF-16
  // units, so enforce the character limit after parsing instead.
  const schema={...CONTRACT.pending_report_schema,properties:{...CONTRACT.pending_report_schema.properties,
    description:{...CONTRACT.pending_report_schema.properties.description,maxLength:CONTRACT.pending_report_schema.properties.description['x-maxUtf8']}}};
  const result=z.fromJSONSchema(JSON.parse(JSON.stringify(schema))).safeParse(value);
  if(!result.success || !utf8(value,CONTRACT.pending_report_schema) || !parseReportV2(value)) return null;
  const report=result.data as PendingReport;
  if([...report.description].length>limits.typed_characters) return null;
  if((report.loader_id===null)!==(report.installation_id===null)) return null;
  const screen=report.screen_state;
  if(screen.surface!=='MODS' && screen.mods_page!=='UNKNOWN' || screen.surface!=='STOCK' && screen.stock_page!=='UNKNOWN') return null;
  return report;
}
async function input(req:Request,cap=2048) { return parse(await bytes(req,bodyLength(req,cap))); }
async function installation(req:Request,db:DB,id:string) {
  const key=bearer(req);
  const row=await db.prepare('SELECT key_hash FROM installations WHERE installation_id=?').bind(id).first<{key_hash:string}>();
  if(!row || !equal(row.key_hash,await sha(key))) fail(404,'not_found');
  return key;
}
async function quota(action:()=>Promise<unknown>) {
  try { await action(); } catch(error) {
    if(error instanceof Error && error.message.includes('quota')) fail(429,'quota');
    throw error;
  }
}
export async function expireReminders(db:DB,now:number) {
  await db.prepare(`UPDATE report_tracking SET comments_state='expired_reminders' WHERE comments_state='pending_comments' AND reminders_until<=?`).bind(now).run();
  await db.prepare('UPDATE controller_queue SET description=NULL,topic=NULL,screen_state=NULL,comments=NULL WHERE private_expires_at<=?').bind(now).run();
}
type QueueRow = {seq:number;report_id:string;report_reference:string;installation_id:string;loader_id:string|null;comments_state:string;created_at:number;reminders_until:number;reminders_dismissed:number;description:string|null;topic:string|null;screen_state:string|null;claimed:number;completed_at:number|null};
const queueSelect=`SELECT t.*,q.description,q.topic,q.screen_state,q.claimed,q.completed_at FROM report_tracking t JOIN controller_queue q ON q.report_id=t.report_id`;
function projection(row:QueueRow,now:number) {
  return {report_id:row.report_id,report_reference:row.report_reference,installation_id:row.installation_id,loader_id:row.loader_id,origin:'controller',
    comments_state:row.comments_state,comments_pending:!['completed','completed_without_comments'].includes(row.comments_state),
    remind:row.comments_state==='pending_comments' && !row.reminders_dismissed && row.reminders_until>now,
    created_at:row.created_at,reminders_until:row.reminders_until,description:row.description,topic:row.topic,
    screen_state:row.screen_state ? JSON.parse(row.screen_state) : null,details_expired:row.description===null,claimed:row.claimed===1};
}
function reference(path:string) {
  const number=parseReportReference(path);
  return number===null ? fail(404,'not_found') : formatReportReference(number)!;
}
async function receiptKey(key:string,id:string) {
  const hmac=await crypto.subtle.importKey('raw',new TextEncoder().encode(key),{name:'HMAC',hash:'SHA-256'},false,['sign']);
  const signed=await crypto.subtle.sign('HMAC',hmac,new TextEncoder().encode(limits.receipt_domain+id));
  return Array.from(new Uint8Array(signed),v=>v.toString(16).padStart(2,'0')).join('');
}
export async function completionDetails(db:DB,id:string) {
  const row=await db.prepare(`SELECT t.comments_state,q.description,q.comments,q.completed_at,q.private_expires_at FROM report_tracking t JOIN controller_queue q ON q.report_id=t.report_id WHERE t.report_id=?`).bind(id).first<{comments_state:string;description:string|null;comments:string|null;completed_at:number|null;private_expires_at:number}>();
  if(!row) return null;
  const pending=!['completed','completed_without_comments'].includes(row.comments_state);
  return {comments_state:row.comments_state,comments_pending:pending,status_text:pending ? limits.awaiting_text : null,
    description:row.private_expires_at>Date.now() ? row.comments ?? row.description : null,completed_at:row.completed_at};
}
export async function controllerRoutes(req:Request,env:Env,db:DB,now:number,intake:Intake):Promise<Response|null> {
  const url=new URL(req.url),path=url.pathname;
  if(path==='/v2/installations' && req.method==='POST' && !url.search) {
    const value=z.strictObject({installation_id:installationId}).safeParse(await input(req));
    if(!value.success) fail(400,'invalid_installation');
    const id=value.data.installation_id,keyHash=await sha(bearer(req));
    const old=await db.prepare('SELECT key_hash FROM installations WHERE installation_id=?').bind(id).first<{key_hash:string}>();
    if(old && !equal(old.key_hash,keyHash)) fail(409,'identity_conflict');
    if(!old) {
      const ip=req.headers.get('CF-Connecting-IP');if(!ip||ip.length>64) fail(503,'rate_identity_unavailable');
      const day=new Date(now).toISOString().slice(0,10),ipKey=await ipHash(env,day,ip);
      await quota(()=>db.prepare('INSERT INTO installations(installation_id,key_hash,created_at,day,ip_hash) SELECT ?,?,?,?,? WHERE NOT EXISTS(SELECT 1 FROM installations WHERE installation_id=?)').bind(id,keyHash,now,day,ipKey,id).run());
      await installation(req,db,id);
    }
    return json({installation_id:id,registered:true});
  }
  const route=/^\/v2\/installations\/(I-[0-9A-HJKMNP-TV-Z]{4}-[0-9A-HJKMNP-TV-Z]{4})\/(loaders|pending)(?:\/([^/]+)(?:\/(claim|revoke))?)?$/.exec(path);
  if(route) {
    const [,id,kind,target,action]=route;
    const key=await installation(req,db,id);
    if(kind==='loaders' && !target && req.method==='POST' && !url.search) {
      const value=z.strictObject({loader_id:loaderId,loader_key_hash:secret,previous_key_hash:secret.nullable()}).safeParse(await input(req));
      if(!value.success) fail(400,'invalid_loader');
      const v=value.data;
      const old=await db.prepare('SELECT * FROM loaders WHERE loader_id=?').bind(v.loader_id).first<Loader>();
      if(old && old.installation_id!==id) fail(409,'identity_conflict');
      if(old && equal(old.key_hash,v.loader_key_hash)) {
        if(old.revoked) fail(409,'loader_revoked');
      } else if(old) {
        const result=await db.prepare('UPDATE loaders SET key_hash=?,revoked=0,updated_at=? WHERE loader_id=? AND installation_id=? AND key_hash=?').bind(v.loader_key_hash,now,v.loader_id,id,v.previous_key_hash).run();
        if(result.meta.changes!==1) fail(409,'rotation_conflict');
      } else {
        if(v.previous_key_hash!==null) fail(409,'rotation_conflict');
        await quota(()=>db.prepare('INSERT INTO loaders(loader_id,installation_id,key_hash,created_at,updated_at) SELECT ?,?,?,?,? WHERE NOT EXISTS(SELECT 1 FROM loaders WHERE loader_id=?)').bind(v.loader_id,id,v.loader_key_hash,now,now,v.loader_id).run());
        const current=await db.prepare('SELECT * FROM loaders WHERE loader_id=?').bind(v.loader_id).first<Loader>();
        if(!current || current.installation_id!==id || !equal(current.key_hash,v.loader_key_hash) || current.revoked) fail(409,'identity_conflict');
      }
      return json({loader_id:v.loader_id,registered:true});
    }
    if(kind==='loaders' && target && action==='revoke' && req.method==='POST' && !url.search) {
      const value=z.strictObject({expected_key_hash:secret}).safeParse(await input(req));
      if(!value.success) fail(400,'invalid_revocation');
      const result=await db.prepare('UPDATE loaders SET revoked=1,updated_at=? WHERE loader_id=? AND installation_id=? AND key_hash=?').bind(now,target,id,value.data.expected_key_hash).run();
      if(result.meta.changes!==1) fail(409,'rotation_conflict');
      return json({loader_id:target,revoked:true});
    }
    if(kind==='pending' && !target && req.method==='GET') {
      if(url.search && !/^\?after=(0|[1-9][0-9]{0,14})$/.test(url.search)) fail(400,'invalid_cursor');
      const after=Number(url.searchParams.get('after')??0);
      await expireReminders(db,now);
      const rows=await db.prepare(queueSelect+` WHERE t.installation_id=? AND t.seq>? AND t.retention_until>? AND t.comments_state IN ('pending_comments','expired_reminders') ORDER BY t.seq LIMIT ?`).bind(id,after,now,limits.queue_page_size+1).all<QueueRow>();
      const page=rows.results.slice(0,limits.queue_page_size);
      return json({pending:page.map(r=>projection(r,now)),next_after:rows.results.length>page.length ? page.at(-1)?.seq : null});
    }
    if(kind==='pending' && target && action==='claim' && req.method==='POST' && !url.search) {
      if(!z.strictObject({}).safeParse(await input(req)).success) fail(400,'invalid_claim');
      const ref=reference(target);
      const row=await db.prepare(queueSelect+' WHERE t.installation_id=? AND t.report_reference=? AND t.retention_until>?').bind(id,ref,now).first<QueueRow>();
      if(!row) fail(404,'not_found');
      const receipt=await receiptKey(key,row.report_id),hash=await sha(receipt);
      await db.batch([
        db.prepare('UPDATE reports SET key_hash=? WHERE id=? AND installation_id=?').bind(hash,row.report_id,id),
        db.prepare('UPDATE report_tracking SET receipt_key_hash=? WHERE report_id=? AND installation_id=?').bind(hash,row.report_id,id),
        db.prepare('UPDATE controller_queue SET claimed=1 WHERE report_id=?').bind(row.report_id)
      ]);
      const stored=await db.prepare('SELECT bytes FROM reports WHERE id=?').bind(row.report_id).first<{bytes:number}>();
      return json({receipt_key:receipt,receipt:await receiptV2(db,row.report_id),bytes:stored?.bytes??0,pending:projection({...row,claimed:1},now)});
    }
    if(kind==='pending' && !target && req.method==='POST' && !url.search) return pendingUpload(req,env,db,now,intake,id);
    fail(404,'not_found');
  }
  if(path==='/v2/xz/pending' && req.method==='POST' && !url.search) return pendingUpload(req,env,db,now,intake);
  const complete=/^\/v2\/reports\/([^/]+)\/(complete|dismiss-reminders)$/.exec(path);
  if(complete && req.method==='POST' && !url.search) {
    const ref=reference(complete[1]),keyHash=await sha(bearer(req));
    const row=await db.prepare('SELECT t.*,q.claimed,q.completion_hash FROM report_tracking t JOIN controller_queue q ON q.report_id=t.report_id WHERE t.report_reference=? AND t.retention_until>?').bind(ref,now).first<{report_id:string;receipt_key_hash:string;claimed:number;comments_state:string;completion_hash:string|null}>();
    if(!row || !row.claimed || !equal(row.receipt_key_hash,keyHash)) fail(404,'not_found');
    if(complete[2]==='dismiss-reminders') {
      if(!z.strictObject({}).safeParse(await input(req)).success) fail(400,'invalid_dismissal');
      await db.prepare('UPDATE report_tracking SET reminders_dismissed=1 WHERE report_id=?').bind(row.report_id).run();
      return json({report_reference:ref,dismissed:true});
    }
    const value=z.discriminatedUnion('action',[
      z.strictObject({action:z.literal('complete'),comments:z.string().min(1).max(4096).refine(s=>s.trim().length>0&&s.isWellFormed()&&new TextEncoder().encode(s).length<=4096)}),
      z.strictObject({action:z.literal('complete_without_comments')})]).safeParse(await input(req,25000));
    if(!value.success) fail(400,'invalid_completion');
    const body=value.data,hash=await sha(JSON.stringify(body));
    const state=body.action==='complete'?'completed':'completed_without_comments';
    await db.batch([
      db.prepare(`UPDATE controller_queue SET description=CASE WHEN private_expires_at<=? THEN NULL ELSE description END,
        topic=CASE WHEN private_expires_at<=? THEN NULL ELSE topic END,screen_state=CASE WHEN private_expires_at<=? THEN NULL ELSE screen_state END,
        completion_hash=?,comments=?,completed_at=?,private_expires_at=? WHERE report_id=? AND completion_hash IS NULL`)
        .bind(now,now,now,hash,body.action==='complete'?body.comments:null,now,now+14*DAY,row.report_id),
      db.prepare(`UPDATE report_tracking SET comments_state=?,reminders_dismissed=1,revision=revision+1,updated_at=?,last_operation='controller_complete',last_operation_hash=? WHERE report_id=? AND comments_state IN ('pending_comments','expired_reminders') AND EXISTS(SELECT 1 FROM controller_queue WHERE report_id=? AND completion_hash=?)`).bind(state,now,hash,row.report_id,row.report_id,hash)
    ]);
    const done=await db.prepare('SELECT completion_hash FROM controller_queue WHERE report_id=?').bind(row.report_id).first<{completion_hash:string}>();
    if(done?.completion_hash!==hash) fail(409,'completion_conflict');
    return json({report_reference:ref,comments_state:state,comments_pending:false,receipt:await receiptV2(db,row.report_id)});
  }
  return null;
}
async function pendingUpload(req:Request,env:Env,db:DB,now:number,intake:Intake,installationId?:string) {
  const raw=await bytes(req,bodyLength(req,MAX)),report=parsePendingReport(parse(raw));
  if(!report) fail(400,'invalid_pending_report');
  const digest=await sha(raw);
  if(req.headers.get('X-Report-ID')!==report.report_id || req.headers.get('X-Report-SHA256')!==digest || req.headers.get('X-Report-Consent')!==CONTRACT.consent) fail(400,'invalid_headers');
  let loaderKeyHash:string|null=null;
  if(installationId) {
    if(report.installation_id!==null && report.installation_id!==installationId) fail(404,'not_found');
    if(report.loader_id!==null && !await db.prepare('SELECT loader_id FROM loaders WHERE loader_id=? AND installation_id=? AND revoked=0').bind(report.loader_id,installationId).first()) fail(404,'not_found');
  } else {
    loaderKeyHash=await sha(bearer(req));
    const loader=await db.prepare('SELECT * FROM loaders WHERE loader_id=?').bind(report.loader_id).first<Loader>();
    if(!loader || loader.revoked || loader.installation_id!==report.installation_id || !equal(loader.key_hash,loaderKeyHash)) fail(404,'not_found');
    installationId=loader.installation_id;
  }
  const old=await db.prepare('SELECT installation_id,loader_id,key_hash,is_pending FROM reports WHERE id=?').bind(report.report_id).first<{installation_id:string;loader_id:string|null;key_hash:string;is_pending:number}>();
  if(old && (old.installation_id!==installationId || old.loader_id!==report.loader_id || !old.is_pending)) fail(409,'conflict');
  const random=Array.from(crypto.getRandomValues(new Uint8Array(32)),v=>v.toString(16).padStart(2,'0')).join('');
  const keyHash=old?.key_hash ?? await sha(random);
  const response=await intake(new Request(req,{body:raw}),env,db,now,true,undefined,{installationId,loaderId:report.loader_id,loaderKeyHash,keyHash,report});
  if(!response.ok) return response;
  // A loader gets its acceptance reference, never a tracking or claim credential.
  const receipt=await receiptV2(db,report.report_id);
  return json({schema:'xz-controller-accepted/1',report_id:report.report_id,report_reference:receipt.report_reference,payload_sha256:digest,accepted_at:receipt.accepted_at,comments_pending:true});
}
