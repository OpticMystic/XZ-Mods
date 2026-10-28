# Controller reports and loader identity

The backend implements `XZ-REPORTS.md` delivery item 1. Queue UI and Windows notifications are a separate phase.
The original diagnostic bytes and digest never change. Completion is private text attached to that report,
under the same reference, and returned as its effective description by the completion reader.

## Wire files

`spec/support-contract.json` owns the schemas, limits, enums, and ID parsing vectors. Run
`python -B tools/generate_support_contract.py --check` from the repository root to check every generated file.

- Identity: `VJ.Tools/loader-identity.json`, UTF-8, maximum 2048 bytes. Its schema is
  `spec/generated/loader-identity.schema.json`. `spec/fixtures/loader-identity.json` has a synthetic key.
  Fields are `version: 1`, `loader_id`, `loader_key`, `installation_id`, `created_at` and `app_version`.
  Keys are 32 random bytes encoded as 64 lowercase hexadecimal characters. The registration hash is
  SHA-256 of those 64 ASCII characters. An installation key never goes on a stick.
- Pending: `VJ.Tools/XZ-Mods-reports/pending/<report_id>.json`. The filename is exactly 32 lowercase hex
  characters followed by `.json`. The file contains the report object itself, without a wrapper, key,
  receipt, screenshot or rendered screen text. Maximum 524288 UTF-8 bytes. The full shape is
  `spec/generated/controller-pending.schema.json`; the firmware fixture is
  `spec/fixtures/controller-pending-report.json`.
- The writer must finish a temporary file, flush it and rename it to the final name. A reader ignores
  temporary names. `report_id` is random and immutable, and must match the filename. Retries preserve exact
  bytes. Worker request headers include the SHA-256 of those exact file bytes.
- A pre-identity loader uses `loader_id: null` and `installation_id: null` together. Its report can be
  relayed by the app with installation authority. Named reports can only be relayed by their installation.
- `comments_pending` is `true` even if the controller supplied optional text. Description is at most
  280 Unicode characters and 1120 UTF-8 bytes. Empty description means the DJ skipped typing.
- `screen_state` contains `surface`, `stock_page`, `mods_page`, `interface`, `deck_view`, `alert_code` and
  `counter_code`. All are generated enums. An inactive page is `UNKNOWN`. Unrecognized codes are `UNKNOWN`,
  absent errors are `NONE`. No arbitrary text suffix is permitted. Current known counter codes include
  E-8302, E-8304, E-8305 and E-8307. MODS page names follow the current `xz_ui_page` names without `XZ_UI_`.
- The app moves the original file to `VJ.Tools/XZ-Mods-reports/sent/` only after a matching Worker acceptance.
  Rejections, offline errors, changed files, and mismatched receipts leave the pending file in place.
  Windows moves use the verified file handle while denying replacement and writes. Existing sent bytes
  cannot be overwritten. No music folders are traversed.

## API

All routes use the existing pinned HTTPS origin, bounded JSON framing, no redirects, and no-store replies.
The examples below name credential roles, not real credentials. `Authorization: Bearer <key>` is required.

| Method and route | Credential | Body / result |
|---|---|---|
| `POST /v2/installations` | Installation key | `{installation_id}`; idempotent registration |
| `POST /v2/installations/{id}/loaders` | Installation key | `{loader_id,loader_key_hash,previous_key_hash}`; previous hash is null on first registration, otherwise a compare-and-swap rotation |
| `POST /v2/installations/{id}/loaders/{loader}/revoke` | Installation key | `{expected_key_hash}`; idempotent revocation, fenced against a newer key |
| `POST /v2/xz/pending` | Loader key | Exact pending report bytes; only a small acceptance with reference and digest is returned |
| `POST /v2/installations/{id}/pending` | Installation key | USB relay, including legacy null identities; same exact bytes and acceptance |
| `GET /v2/installations/{id}/pending?after=0` | Installation key | Up to 20 items, `next_after` cursor or null; includes dismissed and expired reminders with `remind: false` |
| `POST /v2/installations/{id}/pending/{ref}/claim` | Installation key | `{}`; stable receipt key, receipt, byte count and pending projection |
| `GET /v2/reports/{ref}/details?offset=0` | Claimed receipt key | Existing 24 KiB authenticated page format, digest and nested receipt checked on every page |
| `POST /v2/reports/{ref}/complete` | Claimed receipt key | `{action:"complete",comments:"..."}` or `{action:"complete_without_comments"}` |
| `POST /v2/reports/{ref}/dismiss-reminders` | Claimed receipt key | `{}`; stops reminders without completing or deleting |
| `GET /v2/admin/reports/{report_id}/completion` | Owner key | Effective private description and completion state, including comments added after diagnostic expiry |

Both upload routes require `Content-Type: application/json`, exact `Content-Length`, `X-Report-ID`,
`X-Report-SHA256` and `X-Report-Consent: private-report/v2`. Loader authentication proves possession by
hashing the supplied key and comparing it with the registered hash using constant-time equality. The hash
itself is not a submission credential. Loader authority cannot list, claim, read details/status, complete,
register loaders or access another installation. Revocation is rechecked at the D1 acceptance boundary.

Claim keys are HMAC-SHA256 with the installation key's ASCII bytes as key and
`xz-controller-claim/v1\n` plus the report ID as message. This makes a lost claim response recoverable.
The Worker retains only the hash. Receipt and installation keys never reach the renderer or Python.
Completion is first-write-wins. Identical retries succeed; different comments after an uncertain completion
return conflict. The app seals the completion before sending and retries those exact comments.

Quotas are atomic D1 constraints: five reports per loader per UTC day, twenty per installation, and
64 loader records per installation. Installation creation permits ten per IP-derived daily bucket and
100 globally per day. Existing report count/storage reservations still apply. Reserved uploads count;
accepted retries do not spend another slot. Revoked loader records remain so stale keys cannot recreate them.

Migration `0004_controller_queue.sql` adds identity/queue storage and backfills known legacy controller
origin. Old reports remain completed, with no inferred loader or installation. V2 private payloads are not
decoded by SQL, so older rows without stored origin metadata keep the legacy default. New accepted reports
have the origin from their validated payload. References and old receipt keys are unchanged.

The comments lifecycle is separate from triage: `pending_comments`, `completed`,
`completed_without_comments`, `expired_reminders`. Seven days ends reminders, not maintainer access or
completion authority. Owner inbox rows include origin, installation, loader, comments state and the fixed
text "awaiting the DJ's comments". Original queue prose expires with diagnostics after 14 days; completion
text lasts 14 days from completion. Deletion clears private queue prose immediately. Tracking and reference
retention follow the existing contract.

## Native commands for the UI phase

| Tauri command | Arguments / response |
|---|---|
| `support_pending_reports` | Local `{pending:[...]}`; no network |
| `support_poll_pending` | Explicit bounded sync; `{pending,connected,identity,error,usb_scan_error}`, or `{checking:true}` if already running |
| `support_pending_details` | `{id: report_id}`; verified original report, or expired/unavailable error |
| `support_complete_pending` | `{id,comments: string}` or `{id,comments:null}` for Send without comments |
| `support_dismiss_pending` | `{id}`; seals local dismissal immediately and retries the Worker request |
| `support_loader_identities` | Installation ID and loader IDs, registration/revocation state; no keys |
| `support_revoke_loader` | `{loaderId: "L-..."}`; native snake_case parameter is mapped by Tauri to camelCase |
| `support_inbox` | New `action:"completion", id: report_id` reads owner-only completion text |

Listen for `xz-controller-queue-changed`. Its payload is the sync projection, never credentials. Each pending
row includes `report_id`, `report_reference`, `created_at` in Unix milliseconds, topic, controller description,
screen enums, comments state, `remind`, `details_expired` and `completion_pending`. Use `report_id` for local
commands and the reference for display. Treat description/comments as text, never HTML. `remind` already
applies server expiry and local dismissal. A disconnected result retains saved rows and queued actions.

The backend checks on startup, update check and every 180 seconds, with one sync at a time and a 120-second
overall deadline. Python reads have a native 20-second watchdog. An unavailable USB backend does not prevent
cloud polling. USB work waits while a foreground job runs. Registration and revocation retry on connected
checks. The identity is sealed before a loader write; its new key is activated only after verified identity
write/readback. Rewriting a recognized stick retains its loader ID and rotates the key. Interrupted writes
can reconcile their staged key when the stick is seen again.

The next UI phase builds the From your XZ section, editable description, Send and Send without comments,
notification that opens the report, My reports badge, startup callout, reminder dismissal and loader revocation
controls. Any unavailable state needs the established one-button setup wizard. No visible UI or notification
is added here, so manual figures and chapters do not change in this backend phase.
