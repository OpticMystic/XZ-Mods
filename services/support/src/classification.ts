import { z } from 'zod';
import { CONTRACT, type ReportV2, type ReportKind, type DeskFields } from './contract.generated';
import { sha } from './http';

export const deskSchema = z.fromJSONSchema(JSON.parse(JSON.stringify(CONTRACT.desk_schema)));
const privateText = z.string().min(1).max(2048).refine(v => v.isWellFormed() && new TextEncoder().encode(v).length <= 2048);
export const triageSchema = z.strictObject({
  expected_revision: z.number().int().positive(), operation_id: z.string().regex(/^[a-f0-9]{32}$/),
  classified_kind: z.enum(CONTRACT.kinds), outcome: z.enum(CONTRACT.outcomes),
  source_commit: z.string().regex(/^[a-f0-9]{40}$/), evidence_revision: z.literal(1),
  reason: z.enum(CONTRACT.desk_history.reason_codes),
  expected: privateText, actual: privateText,
  desk: deskSchema,
  item_number: z.string().max(64).optional(),
  expectation_citation: z.strictObject({path:z.string().regex(/^(?:manual\/chapters\/|docs\/)[a-zA-Z0-9_./-]{1,180}$/).refine(p=>!p.includes('..')),heading:privateText,commit:z.string().regex(/^[a-f0-9]{40}$/)}).nullable(),
  citations: z.array(z.strictObject({
    path: z.string().regex(/^(?:manual\/chapters\/|docs\/|source\/xdj-xz-toolkit\/mods\/)[a-zA-Z0-9_./-]{1,180}$/).refine(p => !p.includes('..')),
    heading: privateText, commit: z.string().regex(/^[a-f0-9]{40}$/)
  })).min(1).max(4),
  reviewer: z.strictObject({decision: z.literal('approved'), review_id: z.string().regex(/^[a-f0-9]{32}$/), source_commit: z.string().regex(/^[a-f0-9]{40}$/)}).nullable(),
  reproduction: privateText.nullable(),
  question_key: z.enum(['decks_stopped','reproduce_again','app_version','more_detail']).nullable()
});
export function validTriage(input: z.infer<typeof triageSchema>, previous: ReportKind | null): boolean {
  const desk=input.desk as DeskFields;
  if(desk.expectation!=='NONE' && (!input.expectation_citation || input.expectation_citation.commit!==input.source_commit)) return false;
  if(desk.impact==='RESTORES_EXPECTED' && desk.expectation==='NONE') return false;
  if(desk.impact==='OPT_IN_CHANGE' && !desk.proposed_setting) return false;
  if(CONTRACT.automatic_fix.kinds.some(k=>k===input.classified_kind) && !desk.severity) return false;
  if(desk.affected_features.length===0) return false;
  if (input.citations.some(c => c.commit !== input.source_commit)) return false;
  if ((input.classified_kind === 'NOT_A_BUG' || CONTRACT.automatic_fix.kinds.some(k=>k===previous) && !CONTRACT.automatic_fix.kinds.some(k=>k===input.classified_kind)) &&
      (!input.reviewer || input.reviewer.source_commit !== input.source_commit)) return false;
  if (CONTRACT.automatic_fix.kinds.some(k=>k===input.classified_kind) && input.expected === input.actual) return false;
  if ((input.outcome === 'NEEDS_INFO' || input.outcome === 'CANNOT_REPRODUCE') && !input.question_key) return false;
  if (input.outcome === 'CANNOT_REPRODUCE' && !input.reproduction) return false;
  return true;
}

// No client fingerprint, prose, contact, path, device identity, or payload hash
// enters either signature. Weak facts deliberately produce no exact match.
export async function fingerprint(report: ReportV2) {
  const components = {bridge:'bridge', audio:'runtime', display:'receiver', app_usb:'desktop', other:'unknown'} as const;
  const component = components[report.category];
  const reasons = report.private.items.flatMap(i => i.facts.map(f => f.code)).filter(c =>
    c !== undefined && !CONTRACT.fingerprint.weak_reasons.some(weak => weak === c));
  const reason = [...new Set(reasons)].sort()[0];
  if (!reason || component === 'unknown') return null;
  const canonical = {app_version:report.build.desktop_version, component, error:null,
    fingerprint_version:1, reason, runtime_release:'unknown'};
  const exact = await sha(JSON.stringify(canonical));
  const family = await sha(JSON.stringify({...canonical,app_version:'unknown'}));
  return {exact,family,component};
}
