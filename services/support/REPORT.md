# Report service candidate and paging handoff

The existing standalone Worker implements protocol v1 uploads, private D1/R2 storage, hashed reporter receipts, fixed public GitHub drafts, optional public API linkage, owner authorization/deletion, quotas and expiry cleanup. The intervening provider enabled it and provisioned D1 in this directory. Those configuration/resource changes are preserved. No deployment, secret read, Git operation, device call, Loader edit, or root-source edit was performed by this implementation pass.

Root reports deployed version `98254135-5370-4668-b5fe-ee87e09fc787`. This is provider handoff evidence, not an independently verified deployment receipt. `.wrangler/tmp` contains no retained bundle to compare with that version. Retrieving deployed bytes would require provider access excluded from this task, so deployed-byte equality is **unverified**. The source paging change described below has not been deployed.

## Concrete fix

The inspected owner GET returned the entire parsed report, exceeding the 64 KiB response cap for otherwise valid large uploads. The approved fix returns authenticated byte pages. [OWNER-PAGING.md](OWNER-PAGING.md) gives the exact JSON shape and Loader acceptance rules. Every page carries report ID, immutable whole-report SHA256, total bytes, current offset, base64 encoding, up to 24576 exact bytes, next offset or null, and the unchanged receipt. Integer offsets are validated; expiry/deletion is rechecked after R2 reads. Public receipts and redirects are unchanged.

Root owns `src-tauri/src/support.rs`. Its consumer must concatenate bytes, enforce contiguous offsets and immutable metadata, bound requests/total size, and validate the complete SHA256 before parsing/display/export. Local service reassembly tests do not establish Rust consumer acceptance.

## Focused evidence

`npm run typecheck` passed. Nine tests passed together in 12.46 seconds; the subsequently added exact-limit test passed separately in 1.91 seconds. All ten unique tests passed, covering:

- HTTP privacy canaries, fixed GitHub URL, reporter/owner separation, exact stored bytes.
- Large multibyte owner pages, per-page authorization, invalid offsets, expiry, exact reassembly/hash.
- Concurrent identical uploads, immutable conflicts and receipt recovery; atomic racing IP/global quotas and 1 GiB reserved-ledger boundary.
- R2 failure and D1 acceptance failure, truthful `outcome_unknown`, orphan cleanup and tombstones.
- Strict schema/types/UTF-8/digest/framing/encoding limits, exact 512 KiB acceptance.
- Owner deletion, expiry, late orphan writes, abandoned reservations and seven-day tombstone purge.
- GitHub API unavailability, mismatched markers, verified markers and no verified-state downgrade.

Tests use real local workerd, D1 and R2 through Miniflare. Only external GitHub responses and explicitly injected storage failures are substituted. A test-only wrapper injects false framing because undici can reject mismatched Content-Length before the Worker runs. No test endpoint is included by Wrangler. Current pinned Miniflare 5 uses its published V4-options converter; the older stable runtime could not run the requested compatibility date.

Rerun from this directory:

```powershell
npm ci --no-audit --no-fund
npm run typecheck
npm test
python tools/byte_patch.py check baseline/index.ts paging.byte-patch.json
```

## Integration and remaining gaps

`baseline/index.ts` is the exact intervening source snapshot, SHA256 `d3e12a8530c12631d348fa1186d7a8a4097dd81ad6bf05ff88241a53033924f2`. Candidate `src/index.ts` is `0552f07b4764772bcc1ead944af9d295edb2be0e6f8dfcfa79f6d4907fc8bd02`. `paging.byte-patch.json` contains three byte-preserving edits. The patch tool refuses any different baseline. It does not use Git or normalize line endings. Root can check/apply it to a matching integration file; do not apply it blindly to independently changed source.

`wrangler.jsonc` remains enabled with the existing database ID, bucket, account, origin and cron. The migration is unchanged. For root placement in `apps/xz-support-worker`, use source/config/migration/package lock/generated types/tests and these handoff documents. Exclude `node_modules`, caches, `.test`, `.wrangler` and `.local-config`. No broad artifact seal is needed.

Root should preserve existing resources/secrets and verify the private bucket has public access disabled plus a 14-day object lifecycle backstop. Cron reaps expired/15-minute abandoned reservations in batches and keeps seven-day tombstones, but local tests cannot prove hosted lifecycle, cron delivery, physical deletion timing or billing. The 1 GiB test proves the D1 reservation ledger; late-object bounds additionally depend on cleanup/lifecycle. Invalid attempted uploads consume daily quota conservatively. The owner list is bounded to 100 records without a cursor; older IDs remain individually addressable. No claim of universal inbox pagination is made.

Primary references checked: [Workers best practices](https://developers.cloudflare.com/workers/best-practices/workers-best-practices/), [D1 binding API](https://developers.cloudflare.com/d1/worker-api/d1-database/), [R2 range/conditional API](https://developers.cloudflare.com/r2/api/workers/workers-api-reference/), [R2 lifecycles](https://developers.cloudflare.com/r2/buckets/object-lifecycles/), [GitHub issues API](https://docs.github.com/en/rest/issues/issues#get-an-issue). Current installed Wrangler schema and Workers types were used; generated binding declarations are retained.
