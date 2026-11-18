import { z } from 'zod';
import { CONTRACT, SOURCES, type ReportV2 } from './contract.generated';
import { encoder } from './schema';

// The shape itself is generated from the JSON contract. Zod's JSON Schema
// importer handles strict objects; UTF-8 and cross-field rules follow below.
export function utf8(value: unknown, definition: object): boolean {
  const schema: Record<string, unknown> = Object.fromEntries(Object.entries(definition));
  if (Array.isArray(schema.anyOf)) {
    return schema.anyOf.some(s => typeof s === 'object' && s !== null &&
      (s.type === 'null' ? value === null : value !== null && utf8(value, s)));
  }
  if (typeof value === 'string') {
    return value.isWellFormed() && (typeof schema['x-maxUtf8'] !== 'number' || encoder.encode(value).length <= schema['x-maxUtf8']);
  }
  if (Array.isArray(value) && typeof schema.items === 'object' && schema.items !== null) {
    const itemSchema = schema.items;
    return value.every(v => utf8(v, itemSchema));
  }
  if (value !== null && typeof value === 'object' && typeof schema.properties === 'object' && schema.properties !== null) {
    return Object.entries(schema.properties).every(([k, s]) => !(k in value) || utf8(Reflect.get(value, k), s));
  }
  return true;
}
export function parseReportV2(value: unknown): ReportV2 | null {
  const wire = z.fromJSONSchema(JSON.parse(JSON.stringify(CONTRACT.report_schema)));
  const result = wire.safeParse(value);
  if (!result.success || !utf8(value, CONTRACT.report_schema)) return null;
  // Validated by the generated shape above, then constrained semantically here.
  const report = result.data as ReportV2;
  if ((!report.description.trim() && !(report.origin === 'controller' && report.comments_pending === true)) || report.incident.before_seconds + report.incident.after_seconds > 3600) return null;
  if (report.private.contact !== null && !report.private.contact_consent) return null;
  if ((report.kind_hint === 'FEATURE' || report.reporter_hints?.topic === 'NEW_IDEA') && report.private.items.length !== 0) return null;
  if (new Set(report.private.items.map(i => i.item_id)).size !== report.private.items.length) return null;
  if (encoder.encode(JSON.stringify(report.private.items)).length > CONTRACT.limits.diagnostic_bytes) return null;
  for (const item of report.private.items) {
    if (encoder.encode(JSON.stringify(item)).length > CONTRACT.limits.item_bytes) return null;
    if (item.missing_reason !== null && item.facts.length !== 0) return null;
    const source = SOURCES.sources.find(s => s.source_id === item.source_id);
    if (!source) return null;
    for (const fact of item.facts) {
      const fields: readonly string[] = source.safe_fields;
      if(Object.keys(fact).some(k=>!['relative_seconds','clock_basis','window_match'].includes(k) && !fields.includes(k))) return null;
      if(fact.sync_phase_error_ms && fact.sync_phase_error_ms.length!==4) return null;
      if (fact.relative_seconds === null && fact.window_match !== 'unknown') return null;
      if (fact.clock_basis === 'unknown' && (fact.relative_seconds !== null || fact.window_match !== 'unknown')) return null;
      if ((fact.error_namespace === undefined) !== (fact.error_code === undefined)) return null;
      if ((fact.setting === undefined) !== (fact.value === undefined)) return null;
      if (fact.setting !== undefined && fact.value !== undefined) {
        const bounds = SOURCES.setting_bounds[fact.setting];
        if (fact.value < bounds[0] || fact.value > bounds[1]) return null;
      }
      if (fact.version !== undefined && (fact.component === undefined || fact.provenance === undefined)) return null;
    }
  }
  return report;
}
