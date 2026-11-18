import { z } from 'zod';

export const encoder = new TextEncoder();
const text = (max: number, min = 0) => z.string().refine(s =>
  s.isWellFormed() && encoder.encode(s).length >= min && encoder.encode(s).length <= max);
const digest = z.string().regex(/^[a-f0-9]{64}$/).nullable();
export const reportSchema = z.strictObject({
  schema: z.literal('xz-support-report/1'),
  report_id: z.string().regex(/^[a-f0-9]{32}$/),
  created_at: text(32).refine(s => s === 'unknown' ||
    /^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d{1,3})?Z$/.test(s) &&
    Number.isFinite(Date.parse(s)) && new Date(s).toISOString().slice(0,19) === s.slice(0,19)),
  origin: z.enum(['loader', 'controller']),
  category: z.enum(['bridge', 'audio', 'display', 'other']),
  build: z.strictObject({app_version: text(64), runtime_sha256: digest, bridge_sha256: digest, module_sha256: digest}),
  public_issue: z.strictObject({title: text(160,1), description: text(4096)}),
  private: z.strictObject({
    contact: text(256).nullable(),
    items: z.array(z.strictObject({
      label: z.enum(['build.json','loader.json','bridge.json','bridge.log','bootstrap.log','runtime.log','network.json','app.log']),
      content: text(131072), truncated: z.boolean(), missing: z.boolean()
    })).max(8).refine(items => new Set(items.map(i => i.label)).size === items.length)
  }),
  redactions: z.strictObject({count: z.number().int().min(0).max(1000000), omitted_lines: z.number().int().min(0).max(1000000)})
});
export const publicSchema = z.strictObject({
  title: text(160,1), description: text(4096),
  origin: z.enum(['loader','controller']), category: z.enum(['bridge','audio','display','other']),
  version: z.string().regex(/^[a-zA-Z0-9.+_-]{0,64}$/)
});
export const issueSchema = z.strictObject({issue_url: z.string().max(200)});
