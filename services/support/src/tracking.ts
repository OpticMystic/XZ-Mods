import { z } from 'zod';
import { CONTRACT, type ReportKind, type Lifecycle, type Outcome, type StatusTextKey, type ReportV2, type DeskFields, automaticFixEligible, inboxPriority } from './contract.generated';
import { DB, DAY, Env, bearer, bodyLength, bytes, equal, fail, json, owner, parse, sha } from './http';
import { triageSchema, validTriage } from './classification';

export type Tracking = {
  seq: number; report_id: string; receipt_key_hash: string; read_key_hash: string | null; reply_key_hash: string | null;
  expectation: string | null; impact: string | null; severity: string | null; taxonomy_json: string | null;
  payload_sha256: string; revision: number; classified_kind: ReportKind | null; kind_hint: ReportV2['kind_hint'] | null;
  outcome: Outcome | null; lifecycle: Lifecycle; status_text_key: StatusTextKey; question_key: keyof typeof QUESTIONS | null;
  created_at: number; updated_at: number; retention_until: number; diagnostics_expires_at: number;
};
const QUESTIONS = CONTRACT.questions;
export const trackingRow = (db: DB, id: string) => db.prepare('SELECT * FROM report_tracking WHERE report_id=?').bind(id).first<Tracking>();

export function initialTracking(db: DB, id: string, keyHash: string, digest: string, accepted: number, hint: ReportV2['kind_hint'] = null) {
  return db.prepare(`INSERT INTO report_tracking(report_id,receipt_key_hash,payload_sha256,kind_hint,created_at,updated_at,retention_until,
    diagnostics_expires_at,last_operation,last_operation_hash)
    SELECT id,key_hash,digest,?,?,?,?,?,'accepted',digest FROM reports
    WHERE id=? AND key_hash=? AND digest=? AND state='accepted' AND accepted_at=? ON CONFLICT(report_id) DO NOTHING`)
    .bind(hint ?? null,accepted,accepted,accepted+CONTRACT.retention.inactive_tracking_days*DAY,
      accepted+CONTRACT.retention.diagnostic_days*DAY,id,keyHash,digest,accepted);
}
export async function capabilityHashes(key: string) {
  const read = await sha(CONTRACT.capability_domains.read + key);
  const reply = await sha(CONTRACT.capability_domains.reply + key);
  return {read:await sha(read),reply:await sha(reply)};
}
async function initializeCapabilities(db: DB, id: string, key: string) {
  const hashes = await capabilityHashes(key);
  // Backfilled v1 rows know only the receipt hash. Derive lazily after full auth.
  await db.prepare(`UPDATE report_tracking SET read_key_hash=?,reply_key_hash=? WHERE report_id=? AND read_key_hash IS NULL`)
    .bind(hashes.read,hashes.reply,id).run();
}
async function authenticated(req: Request, db: DB, id: string, readOnly = false) {
  const token = /^Bearer ([a-f0-9]{64})$/.exec(req.headers.get('Authorization') ?? '')?.[1];
  const r = await trackingRow(db,id);
  if (!r || r.retention_until <= Date.now() || !token) fail(404,'not_found');
  const hash = await sha(token);
  if (readOnly) {
    if (!r.read_key_hash || !equal(r.read_key_hash,hash)) {
      const session=await db.prepare(`SELECT s.report_id FROM tracking_sessions s JOIN report_tracking t ON t.report_id=s.report_id
        WHERE s.token_hash=? AND s.report_id=? AND s.expires_at>? AND s.capability_revision=t.capability_revision`)
        .bind(hash,id,Date.now()).first();
      if(!session) fail(404,'not_found');
    }
  } else {
    if (!equal(r.receipt_key_hash,hash)) fail(404,'not_found');
    await initializeCapabilities(db,id,token);
  }
  return r;
}
export async function trackingSession(req: Request, db: DB) {
  const input=z.strictObject({report_id:z.string().regex(/^[a-f0-9]{32}$/)}).safeParse(parse(await bytes(req,bodyLength(req,128))));
  if(!input.success) fail(404,'not_found');
  const id=input.data.report_id;
  const r=await authenticated(req,db,id,true);
  // Only a read capability creates sessions; a session cannot extend itself.
  if(!r.read_key_hash || !equal(r.read_key_hash,await sha(bearer(req)))) fail(404,'not_found');
  const token=Array.from(crypto.getRandomValues(new Uint8Array(32)),b=>b.toString(16).padStart(2,'0')).join('');
  const expires=Math.min(Date.now()+900000,r.retention_until);
  await db.batch([
    db.prepare('DELETE FROM tracking_sessions WHERE expires_at<=?').bind(Date.now()),
    db.prepare(`INSERT INTO tracking_sessions(token_hash,report_id,capability_revision,expires_at)
      SELECT ?,report_id,capability_revision,? FROM report_tracking WHERE report_id=? AND
      (SELECT count(*) FROM tracking_sessions WHERE report_id=?)<20`).bind(await sha(token),expires,id,id)
  ]);
  if(!await db.prepare('SELECT token_hash FROM tracking_sessions WHERE token_hash=?').bind(await sha(token)).first()) fail(429,'session_limit');
  return json({session:token,expires_at:expires});
}
export function projection(r: Tracking) {
  return {schema:'xz-support-status/2',report_id:r.report_id,revision:r.revision,classified_kind:r.classified_kind,
    kind_hint:r.kind_hint,outcome:r.outcome,lifecycle:r.lifecycle,status_text_key:r.status_text_key,
    status_text:CONTRACT.status_text[r.status_text_key],question:r.question_key ? QUESTIONS[r.question_key] : null,
    created_at:new Date(r.created_at).toISOString(),updated_at:new Date(r.updated_at).toISOString(),
    retention_until:new Date(r.retention_until).toISOString(),diagnostics_expires_at:new Date(r.diagnostics_expires_at).toISOString()};
}
export async function receiptV2(db: DB, id: string) {
  const r = await trackingRow(db,id);
  if (!r || r.retention_until <= Date.now()) fail(404,'not_found');
  const stored = await db.prepare('SELECT state,expires_at FROM reports WHERE id=?').bind(id).first<{state:string;expires_at:number}>();
  return {schema:'xz-support-receipt/2',report_id:id,payload_sha256:r.payload_sha256,
    accepted_at:new Date(r.created_at).toISOString(),expires_at:new Date(r.diagnostics_expires_at).toISOString(),
    private_logs:stored?.state === 'accepted' && stored.expires_at > Date.now() ? 'stored' : 'gone',
    publication_mode:'none',tracking:projection(r)};
}
export async function readTracking(req: Request, db: DB, id: string, mode: 'receipt' | 'status' | 'read') {
  const r = await authenticated(req,db,id,mode === 'read');
  if (mode === 'receipt') return json(await receiptV2(db,id));
  const etag = `"${r.revision}"`;
  if (req.headers.get('If-None-Match') === etag) return new Response(null,{status:304,headers:{'ETag':etag,'Cache-Control':'no-store','Referrer-Policy':'no-referrer'}});
  const response = json(projection(r)); response.headers.set('ETag',etag); return response;
}
export async function revokeCapabilities(req: Request, db: DB, id: string) {
  await authenticated(req,db,id);
  const input = z.strictObject({action:z.literal('revoke')}).safeParse(parse(await bytes(req,bodyLength(req,128))));
  if (!input.success) fail(400,'invalid_capability_action');
  // A revoked hash is not NULL: a subsequent receipt read cannot re-enable it.
  await db.prepare(`UPDATE report_tracking SET read_key_hash='',reply_key_hash='',capability_revision=capability_revision+1 WHERE report_id=?`).bind(id).run();
  return json({revoked:true});
}

const command = z.strictObject({expected_revision:z.number().int().positive(),operation_id:z.string().regex(/^[a-f0-9]{32}$/),
  lifecycle:z.enum(CONTRACT.lifecycles),question_key:z.enum(['decks_stopped','reproduce_again','app_version','more_detail']).nullable()});
const note = z.strictObject({expected_revision:z.number().int().positive(),operation_id:z.string().regex(/^[a-f0-9]{32}$/),
  content:z.string().min(1).max(4096).refine(v => v.isWellFormed() && new TextEncoder().encode(v).length<=4096)});
export async function ownerHistory(req: Request, env: Env, db: DB, id: string, action: string) {
  await owner(req,env);
  if(!await trackingRow(db,id)) fail(404,'not_found');
  if(action==='notes') {
    const rows=await db.prepare('SELECT operation_id,revision,content,created_at,expires_at FROM private_notes WHERE report_id=? AND expires_at>? ORDER BY created_at,operation_id LIMIT 12').bind(id,Date.now()).all();
    return json({notes:rows.results});
  }
  if(action==='history') {
    const rows=await db.prepare('SELECT revision,previous_json,new_json,reason_code,created_at FROM taxonomy_events WHERE report_id=? ORDER BY revision DESC LIMIT 16').bind(id).all();
    return json({history:rows.results});
  }
  if(action==='triage') {
    const row=await db.prepare('SELECT revision,evidence_json,created_at,expires_at FROM triage_events WHERE report_id=? AND expires_at>? ORDER BY revision DESC LIMIT 1').bind(id,Date.now()).first();
    return json({triage:row});
  }
  const rows=await db.prepare('SELECT revision,classified_kind,outcome,lifecycle,status_text_key,created_at FROM status_events WHERE report_id=? ORDER BY revision DESC LIMIT 50').bind(id).all();
  return json({events:rows.results});
}
function statusKey(kind: ReportKind | null, lifecycle: Lifecycle): StatusTextKey {
  if(lifecycle==='triaged' && kind==='REGRESSION') return 'triaged_regression';
  if(lifecycle==='triaged' && kind==='DEGRADATION') return 'triaged_degradation';
  if(lifecycle==='triaged' && kind==='EXPECTATION') return 'triaged_expectation';
  if (lifecycle === 'triaged') return kind === 'BUG' ? 'triaged_bug' : kind === 'FEATURE' ? 'triaged_feature' : 'triaged_question';
  if (lifecycle === 'closed' && kind === 'NOT_A_BUG') return 'closed_not_a_bug';
  return lifecycle;
}
function transition(previous: Tracking, kind: ReportKind | null, lifecycle: Lifecycle) {
  const allowed: readonly string[] = CONTRACT.lifecycle_by_kind[kind ?? 'unclassified'];
  const next: readonly string[] = CONTRACT.transitions[previous.lifecycle];
  if (!allowed.includes(lifecycle) || !next.includes(lifecycle)) fail(409,'invalid_transition');
  if (lifecycle in CONTRACT.phase_gates) fail(409,'evidence_gate_unavailable');
}
export async function ownerMutation(req: Request, env: Env, db: DB, id: string, action: string) {
  await owner(req,env);
  const r = await trackingRow(db,id);
  if (!r || r.retention_until <= Date.now()) fail(404,'not_found');
  const raw = await bytes(req,bodyLength(req,16384)); const body = parse(raw); const hash = await sha(raw); const now=Date.now();
  if (action === 'notes') {
    const input=note.safeParse(body); if(!input.success) fail(400,'invalid_note');
    const n=input.data;
    const previous=await db.prepare('SELECT operation_hash FROM private_notes WHERE report_id=? AND operation_id=?').bind(id,n.operation_id).first<{operation_hash:string}>();
    if(previous) { if(previous.operation_hash!==hash) fail(409,'conflict'); return json({saved:true}); }
    const count=await db.prepare('SELECT count(*) AS n FROM private_notes WHERE report_id=?').bind(id).first<{n:number}>();
    if((count?.n ?? 0)>=12) fail(429,'note_limit');
    const result=await db.prepare(`INSERT OR IGNORE INTO private_notes(report_id,operation_id,operation_hash,revision,content,created_at,expires_at)
      SELECT report_id,?,?,revision,?,?,? FROM report_tracking WHERE report_id=? AND revision=? AND retention_until>?
      AND (SELECT count(*) FROM private_notes WHERE report_id=?)<12`)
      .bind(n.operation_id,hash,n.content,now,now+14*DAY,id,n.expected_revision,now,id).run();
    if(result.meta.changes!==1) {
      const retry=await db.prepare('SELECT operation_hash FROM private_notes WHERE report_id=? AND operation_id=?').bind(id,n.operation_id).first<{operation_hash:string}>();
      if(retry?.operation_hash!==hash) fail(409,'revision_conflict');
    }
    return json({saved:true});
  }
  let kind=r.classified_kind, outcome=r.outcome, lifecycle:Lifecycle, question:keyof typeof QUESTIONS|null, expected:number, operation:string;
  const triage=action==='triage' ? triageSchema.safeParse(body) : null;
  if (triage) {
    if(!triage.success || !validTriage(triage.data,r.classified_kind)) fail(400,'invalid_triage');
    const t=triage.data; kind=t.classified_kind; outcome=t.outcome; question=t.question_key; expected=t.expected_revision; operation=t.operation_id;
    lifecycle=outcome==='NEEDS_INFO'?'needs_info':outcome==='CANNOT_REPRODUCE'?'cannot_reproduce':outcome==='DUPLICATE'?'duplicate':kind==='NOT_A_BUG'?'closed':'triaged';
  } else {
    const input=command.safeParse(body); if(!input.success) fail(400,'invalid_status');
    const c=input.data; lifecycle=c.lifecycle; question=c.question_key; expected=c.expected_revision; operation=c.operation_id;
    // A status update cannot assert classification, reproduction, or membership.
    if(['triaged','cannot_reproduce','duplicate'].includes(lifecycle)) fail(409,'triage_required');
    if(lifecycle==='needs_info') outcome='NEEDS_INFO';
  }
  const previous=await db.prepare('SELECT operation_hash FROM status_events WHERE report_id=? AND operation_id=?').bind(id,operation).first<{operation_hash:string}>();
  if(previous) {if(previous.operation_hash!==hash) fail(409,'conflict');return json(projection(r));}
  if(r.revision!==expected) fail(409,'revision_conflict');
  transition(r,kind,lifecycle);
  if(lifecycle==='needs_info' && !question) fail(400,'question_required');
  const desk=triage?.success ? triage.data.desk as DeskFields : r.taxonomy_json ? JSON.parse(r.taxonomy_json) as DeskFields : null;
  if(lifecycle.startsWith('fix_') && !automaticFixEligible(kind,desk?.impact ?? null)) fail(409,'owner_approval_required');
  const candidate=CONTRACT.automatic_fix.kinds.some(k=>k===kind) ? await db.prepare('SELECT * FROM report_signatures WHERE report_id=?').bind(id).first<{exact_hash:string;family_hash:string;component:string}>() : null;
  const signature=candidate ? {...candidate,exact_hash:await sha(JSON.stringify({kind,candidate:candidate.exact_hash})),family_hash:await sha(JSON.stringify({kind,candidate:candidate.family_hash}))} : null;
  if(lifecycle==='duplicate' && (!signature || !await db.prepare('SELECT group_id FROM bug_fingerprints WHERE kind=? AND exact_hash=?').bind(kind,signature.exact_hash).first())) fail(409,'verified_group_required');
  const revision=r.revision+1, group=crypto.randomUUID().replaceAll('-','');
  const statements=[db.prepare(`UPDATE report_tracking SET expectation=?,impact=?,severity=?,taxonomy_json=?,priority=?,severity_rank=?,revision=?,classified_kind=?,outcome=?,lifecycle=?,status_text_key=?,question_key=?,updated_at=?,retention_until=?,last_operation=?,last_operation_hash=?
    WHERE report_id=? AND revision=? AND retention_until>?`).bind(desk?.expectation ?? null,desk?.impact ?? null,desk?.severity ?? null,desk ? JSON.stringify(desk) : null,
      inboxPriority(kind,desk?.impact ?? null,desk?.expectation ?? null,desk?.severity ?? null),desk?.severity ? CONTRACT.severities.indexOf(desk.severity) : 4,revision,kind,outcome,lifecycle,statusKey(kind,lifecycle),question,now,
      now+(lifecycle==='closed'||lifecycle==='released'?180:365)*DAY,operation,hash,id,expected,now)];
  if(triage?.success) {
    statements.push(db.prepare(`INSERT INTO taxonomy_events(report_id,revision,previous_json,new_json,reason_code,created_at)
      SELECT report_id,revision,?,?,?,? FROM report_tracking WHERE report_id=? AND revision=? AND last_operation=? ON CONFLICT DO NOTHING`)
      .bind(r.taxonomy_json ? JSON.stringify({kind:r.classified_kind,...JSON.parse(r.taxonomy_json)}) : null,JSON.stringify({kind,...desk}),triage.data.reason,now,id,revision,operation));
    statements.push(db.prepare(`INSERT INTO triage_events(report_id,revision,previous_kind,new_kind,outcome,evidence_json,created_at,expires_at)
      SELECT report_id,revision,?,?,?,?,?,? FROM report_tracking WHERE report_id=? AND revision=? AND last_operation=? AND last_operation_hash=? ON CONFLICT DO NOTHING`)
      .bind(r.classified_kind,kind,outcome,JSON.stringify(triage.data),now,now+14*DAY,id,revision,operation,hash));
    // Reclassification removes obsolete membership, never changes another report.
    statements.push(db.prepare(`DELETE FROM report_group_members WHERE report_id=? AND EXISTS(SELECT 1 FROM report_tracking WHERE report_id=? AND revision=? AND last_operation=?)`)
      .bind(id,id,revision,operation));
    if(signature) {
      const guard=`EXISTS(SELECT 1 FROM report_tracking WHERE report_id=? AND revision=? AND last_operation=?)`;
      statements.push(db.prepare(`INSERT INTO bug_groups(id,kind,component,created_at) SELECT ?,?,?,? WHERE ${guard}
        AND NOT EXISTS(SELECT 1 FROM bug_fingerprints WHERE kind=? AND exact_hash=?)`).bind(group,kind,signature.component,now,id,revision,operation,kind,signature.exact_hash));
      statements.push(db.prepare(`INSERT INTO bug_fingerprints(fingerprint_version,kind,exact_hash,family_hash,group_id)
        SELECT 1,?,?,?,? WHERE ${guard} AND EXISTS(SELECT 1 FROM bug_groups WHERE id=?) ON CONFLICT DO NOTHING`)
        .bind(kind,signature.exact_hash,signature.family_hash,group,id,revision,operation,group));
      statements.push(db.prepare(`INSERT INTO report_group_members(report_id,group_id,triage_revision,joined_at)
        SELECT ?,group_id,?,? FROM bug_fingerprints WHERE kind=? AND exact_hash=? AND ${guard} ON CONFLICT DO NOTHING`)
        .bind(id,revision,now,kind,signature.exact_hash,id,revision,operation));
    }
  }
  const results=await db.batch(statements);
  // D1 changes includes the status-event trigger. Zero means the CAS lost.
  if(results[0].meta.changes===0) {
    const retry=await db.prepare('SELECT operation_hash FROM status_events WHERE report_id=? AND operation_id=?').bind(id,operation).first<{operation_hash:string}>();
    if(retry?.operation_hash!==hash) fail(409,'revision_conflict');
  }
  const updated=await trackingRow(db,id); if(!updated) fail(404,'not_found'); return json(projection(updated));
}

const cursorSchema=z.strictObject({v:z.literal(2),snapshot:z.string().regex(/^[a-f0-9]{32}$/),after:z.number().int().nonnegative()});
export async function ownerInbox(req: Request, env: Env, db: DB, url: URL) {
  await owner(req,env);
  if([...url.searchParams.keys()].some(k=>!['cursor','limit','events_after'].includes(k)) || [...url.searchParams.keys()].some(k=>url.searchParams.getAll(k).length!==1)) fail(400,'invalid_query');
  const limitText=url.searchParams.get('limit') ?? '50'; if(!/^(?:[1-9]|[1-9][0-9]|100)$/.test(limitText)) fail(400,'invalid_limit');
  const limit=Number(limitText);
  const after=url.searchParams.get('events_after');
  if(after!==null) {
    if(url.searchParams.has('cursor') || !/^(0|[1-9][0-9]{0,14})$/.test(after)) fail(400,'invalid_cursor');
    const rows=await db.prepare('SELECT seq,report_id,revision FROM status_events WHERE seq>? ORDER BY seq LIMIT ?').bind(Number(after),limit+1).all<{seq:number;report_id:string;revision:number}>();
    const page=rows.results.slice(0,limit);
    return json({events:page,next_event_cursor:page.at(-1)?.seq ?? Number(after),has_more:rows.results.length>limit});
  }
  let cursor:z.infer<typeof cursorSchema>;
  const raw=url.searchParams.get('cursor');
  if(raw) {
    try { const decoded=cursorSchema.safeParse(JSON.parse(atob(raw))); if(!decoded.success) fail(400,'invalid_cursor'); cursor=decoded.data; }
    catch { return fail(400,'invalid_cursor'); }
  } else {
    const now=Date.now();
    await db.prepare('DELETE FROM inbox_snapshots WHERE expires_at<=?').bind(now).run();
    const count=await db.prepare('SELECT count(*) AS n FROM inbox_snapshots').first<{n:number}>();
    if((count?.n ?? 0)>=32) fail(429,'snapshot_limit');
    const high=await db.prepare('SELECT COALESCE(MAX(seq),0) AS high FROM report_tracking').first<{high:number}>();
    cursor={v:2,snapshot:crypto.randomUUID().replaceAll('-',''),after:0};
    // Materialize only IDs and order, so changing triage/priority between pages
    // cannot skip or repeat a report. No private content enters the snapshot.
    await db.batch([
      db.prepare('INSERT INTO inbox_snapshots(id,high_water,expires_at) VALUES(?,?,?)').bind(cursor.snapshot,high?.high ?? 0,now+900000),
      db.prepare(`INSERT INTO inbox_snapshot_rows(snapshot_id,ordinal,report_id)
        SELECT ?,row_number() OVER(ORDER BY t.priority,t.severity_rank,
          (SELECT count(*) FROM report_group_members peer WHERE peer.group_id=m.group_id) DESC,t.created_at,t.report_id),t.report_id
        FROM report_tracking t LEFT JOIN report_group_members m ON m.report_id=t.report_id
        WHERE t.seq<=? AND t.retention_until>?`).bind(cursor.snapshot,high?.high ?? 0,now)
    ]);
  }
  const snapshot=await db.prepare('SELECT high_water FROM inbox_snapshots WHERE id=? AND expires_at>?').bind(cursor.snapshot,Date.now()).first<{high_water:number}>();
  if(!snapshot) fail(400,'cursor_expired');
  const rows=await db.prepare(`SELECT t.*,s.ordinal,
    (SELECT count(*) FROM report_group_members peer WHERE peer.group_id=m.group_id) AS reach_count
    FROM inbox_snapshot_rows s JOIN report_tracking t ON t.report_id=s.report_id
    LEFT JOIN report_group_members m ON m.report_id=t.report_id
    WHERE s.snapshot_id=? AND s.ordinal>? AND t.retention_until>? ORDER BY s.ordinal LIMIT ?`)
    .bind(cursor.snapshot,cursor.after,Date.now(),limit+1).all<Tracking & {ordinal:number;reach_count:number}>();
  const reports=[];let last:typeof rows.results[number]|undefined;
  // Dense taxonomy rows can hit the response byte cap before the requested count.
  for(const r of rows.results.slice(0,limit)) {
    const view={report_id:r.report_id,revision:r.revision,kind:r.classified_kind,severity:r.severity,impact:r.impact,
    expectation:r.expectation,desk:r.taxonomy_json ? JSON.parse(r.taxonomy_json) : null,reach_count:Math.max(1,r.reach_count),
    lifecycle:r.lifecycle,outcome:r.outcome,automatic_fix_eligible:automaticFixEligible(r.classified_kind,r.impact),
    automation_enabled:env.AUTOMATION_ENABLED==='true',created_at:r.created_at};
    if(new TextEncoder().encode(JSON.stringify([...reports,view])).length>55000) break;
    reports.push(view);last=r;
  }
  return json({reports,next_cursor:rows.results.length>reports.length && last ? btoa(JSON.stringify({...cursor,after:last.ordinal})) : null,high_water:snapshot.high_water});
}
export async function cleanupTracking(db: DB, now: number) {
  await db.batch([
    db.prepare('DELETE FROM private_notes WHERE expires_at<=?').bind(now),
    db.prepare('DELETE FROM triage_events WHERE expires_at<=?').bind(now),
    db.prepare('DELETE FROM tracking_sessions WHERE expires_at<=?').bind(now),
    db.prepare('DELETE FROM inbox_snapshots WHERE expires_at<=?').bind(now),
    db.prepare('DELETE FROM report_tracking WHERE retention_until<=?').bind(now)
  ]);
}
