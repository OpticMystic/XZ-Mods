import { encoder } from './schema';
export type Env = Cloudflare.Env & { ADMIN_TOKEN_SHA256: string; RATE_KEY: string };
export type DB = ReturnType<Env['DB']['withSession']>;
export type Row = {
  id: string; digest: string; key_hash: string; object_key: string; bytes: number;
  state: 'reserved' | 'accepted' | 'deleting' | 'gone'; reserved_at: number;
  accepted_at: number | null; expires_at: number | null; gone_at: number | null;
  installation_id:string|null; loader_id:string|null; is_pending:number;
  protocol_version: 1 | 2; legacy_public_consent: number;
  public_json: string | null; issue_state: 'pending' | 'user_provided' | 'verified'; issue_url: string | null;
};
export const DAY = 86400000;
export const MAX = 524288;
export const ID = '[a-f0-9]{32}';
export const SECRET = /^[a-f0-9]{64}$/;
export class HttpError extends Error {
  constructor(readonly status: number, readonly code: string) { super(code); }
}
export function fail(status: number, code: string): never { throw new HttpError(status,code); }
export const headers = {
  'X-Robots-Tag': 'noindex, nofollow',
  'Cache-Control': 'no-store', 'Referrer-Policy': 'no-referrer',
  'X-Content-Type-Options': 'nosniff', 'Content-Security-Policy': "default-src 'none'; frame-ancestors 'none'"
};
export function json(value: unknown, status = 200): Response {
  const body = JSON.stringify(value);
  if (encoder.encode(body).length >= 65536) fail(500,'response_limit');
  return new Response(body,{status,headers:{...headers,'Content-Type':'application/json'}});
}
const hex = (bytes: ArrayBuffer) => Array.from(new Uint8Array(bytes), b => b.toString(16).padStart(2,'0')).join('');
export const sha = async (value: string | Uint8Array) => hex(await crypto.subtle.digest('SHA-256',typeof value === 'string' ? encoder.encode(value) : value));
export function equal(a: string, b: string): boolean {
  return a.length === b.length && crypto.subtle.timingSafeEqual(encoder.encode(a),encoder.encode(b));
}
export async function ipHash(env: Env, day: string, ip: string): Promise<string> {
  const key = await crypto.subtle.importKey('raw',encoder.encode(env.RATE_KEY),{name:'HMAC',hash:'SHA-256'},false,['sign']);
  return hex(await crypto.subtle.sign('HMAC',key,encoder.encode(`${day}\n${ip}`)));
}
export function bearer(req: Request): string {
  const match = /^Bearer ([a-f0-9]{64})$/.exec(req.headers.get('Authorization') ?? '');
  return match?.[1] ?? fail(401,'unauthorized');
}
export async function owner(req: Request, env: Env): Promise<void> {
  const match=/^Bearer ([a-zA-Z0-9_-]{32,256})$/.exec(req.headers.get('Authorization') ?? '');
  if (!match || !equal(await sha(match[1]),env.ADMIN_TOKEN_SHA256)) fail(401,'unauthorized');
}
export function bodyLength(req: Request, max: number): number {
  if (req.headers.get('Content-Type') !== 'application/json' || req.headers.has('Content-Encoding') ||
      req.headers.has('Transfer-Encoding')) fail(415,'unsupported_encoding');
  const raw = req.headers.get('Content-Length') ?? '';
  if (!/^[1-9][0-9]{0,6}$/.test(raw)) fail(411,'length_required');
  const length = Number(raw);
  if (length > max) fail(413,'too_large');
  return length;
}
export async function bytes(req: Request, length: number): Promise<Uint8Array> {
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
export function parse(raw: Uint8Array): unknown {
  try { return JSON.parse(new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(raw)); }
  catch { return fail(400,'invalid_json'); }
}
