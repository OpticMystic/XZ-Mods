import { formatReportReference, formatItemNumber, parseReportReference, parseItemNumber } from './contract.generated';
import { type DB, type Env, fail, json, owner } from './http';

export function randomReportReference(): string {
  const value = crypto.getRandomValues(new Uint8Array(5)).reduce((n, byte) => n * 256 + byte, 0);
  const reference = formatReportReference(value);
  if (reference === null) throw new Error('Invalid reference entropy');
  return reference;
}

// Only a unique-reference conflict is retryable. The entire acceptance batch
// rolls back on a collision; storage failures keep their existing semantics.
export async function acceptWithReference(db: DB, statements: (reference: string) => D1PreparedStatement[], generate = randomReportReference) {
  for (let attempt = 0; attempt < 8; attempt++) {
    try { return await db.batch(statements(generate())); }
    catch (error) {
      if (!(error instanceof Error) || !/UNIQUE constraint failed: (report_tracking|report_references)\.report_reference/.test(error.message)) throw error;
    }
  }
  return fail(503, 'reference_generation_failed');
}

// Future publication/outbox writers must copy this into their approved public
// projection. No report ID, receipt authority, prose, or private group data.
export async function publicationItem(db: DB, reportId: string) {
  const row = await db.prepare(`SELECT g.item_number FROM report_group_members m JOIN bug_groups g ON g.id=m.group_id WHERE m.report_id=?`)
    .bind(reportId).first<{item_number:number}>();
  if (!row) fail(409, 'item_required');
  return {item_number: formatItemNumber(row.item_number)};
}

export async function ownerLookup(req: Request, env: Env, db: DB, url: URL) {
  await owner(req, env);
  if ([...url.searchParams.keys()].some(k => k !== 'q') || url.searchParams.getAll('q').length !== 1) fail(400, 'invalid_query');
  const query = url.searchParams.get('q');
  if (!query || query.length > 64) fail(400, 'invalid_identifier');
  const reference = parseReportReference(query), item = parseItemNumber(query);
  if (reference !== null) {
    const row = await db.prepare(`SELECT t.report_id,t.report_reference,g.item_number FROM report_tracking t
      LEFT JOIN report_group_members m ON m.report_id=t.report_id LEFT JOIN bug_groups g ON g.id=m.group_id
      WHERE t.report_reference=? AND t.retention_until>?`).bind(formatReportReference(reference), Date.now())
      .first<{report_id:string;report_reference:string;item_number:number|null}>();
    if (!row) fail(404, 'not_found');
    return json({...row, item_number:row.item_number===null ? null : formatItemNumber(row.item_number)});
  }
  if (item === null) fail(400, 'invalid_identifier');
  const row = await db.prepare('SELECT id FROM bug_groups WHERE item_number=?').bind(item).first<{id:string}>();
  if (!row) fail(404, 'not_found');
  return json({group_id:row.id, item_number:formatItemNumber(item)});
}
