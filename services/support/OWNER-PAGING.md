# Owner private report pages

Approved root handoff, 2026-10-25. This extends only the private owner GET response. Reporter receipts and GitHub drafts keep protocol v1. Root owns `src-tauri/src/support.rs`; this directory contains no Loader edits.

`GET /v1/admin/reports/<32lowerhex>?offset=<integer byte offset>` requires the owner bearer on **every request**. Missing offset starts at 0. Only a single canonical decimal offset parameter is accepted. Empty, signed, fractional, exponent, duplicate or unknown parameters return 400. An offset at or beyond the total byte length returns 416. The reporter receipt key cannot authorize pages. Expired/deleted reports return 410.

```json
{
  "schema": "xz-support-owner-page/1",
  "report_id": "0123456789abcdef0123456789abcdef",
  "payload_sha256": "64lowerhex_entire_frozen_report_digest",
  "total_bytes": 360000,
  "offset": 0,
  "encoding": "base64",
  "data_base64": "<base64 of up to 24576 exact bytes>",
  "next_offset": 24576,
  "receipt": { "schema": "xz-support-receipt/1", "...": "unchanged protocol v1 receipt" }
}
```

The last page sets `next_offset` to JSON `null`. Bytes can split inside UTF-8 code points. Base64-decode and concatenate bytes; do not decode each page as text. Responses are JSON, `Cache-Control: no-store`, and strictly smaller than 65536 bytes. The service reads only the bounded R2 range, then rechecks active state against D1 before responding.

Loader must enforce:

- Correct page schema/encoding and the requested report ID on every page.
- One immutable `payload_sha256`, `total_bytes` in 1..524288, and matching nested receipt report ID/hash throughout.
- Response offset equals requested offset. Decoded chunk length is 1..24576, never beyond remaining total bytes. A non-final `next_offset` equals offset plus decoded length and strictly advances; final null occurs only at total length.
- At most 22 requests for service-produced pages. Reject short non-final pages if relying on this bound. Stop on redirects, auth failure, Gone, cancellation, drift, malformed base64, or response size limit.
- Reassembled byte length equals total and SHA256 equals immutable hash **before** parsing, previewing, or exporting. Nothing about opening the public draft establishes GitHub submission.

The service tests reconstruct a 360 KiB multibyte report and verify exact bytes plus final SHA256. This is service/client-algorithm evidence, not proof that the Rust Loader consumer has shipped. Root must run its own consumer tests.


## V2 handoff, 2026-10-27

The same authenticated byte-page format is available at `/v2/admin/reports/{id}`. Its nested receipt is
`xz-support-receipt/2` for v2 payloads. The frozen body, report ID, whole-payload digest, 24576-byte chunks,
strict offset parsing, expiry/deletion recheck, and response bound are unchanged. Both outer and nested
report ID/digest must match on every page. Current Rust still accepts only v1. Phase 3 owns that migration.

GET `/v2/admin/reports?limit=50` returns owner taxonomy rows and next_cursor. Follow it as
`?limit=50&cursor=ENCODED_CURSOR`; do not treat a short page as the end if a cursor is present. Priority
ordering is frozen in a 15-minute snapshot with an accepted-record high water. Later inserts are excluded,
deleted rows disappear, and changed priorities cannot skip or repeat a remaining report. An expired
cursor returns cursor_expired; start a new snapshot and deduplicate by report_id/revision.

Use `?events_after=0&limit=100` separately for incremental changes. Persist next_event_cursor after processing
the page and continue while has_more. The event sequence advances on accepted reports and status/triage
changes, so an old report does not disappear merely because it is outside the newest page. Private notes
do not publish reporter events. None of these lists contains private original text or source facts.

Owner rows contain kind, severity, impact, expectation, desk affected_features/in_use_since/reach,
proposed_setting, group request count, lifecycle, revision, and eligibility. The eligibility boolean is
the contract rule only. AUTOMATION_ENABLED is false and later execution gates are not implemented.

Reporter GET `/v2/reports/{id}/status` is a different schema and authority. It contains fixed safe text,
the reporter's own ID/revision, and retention dates. It never exposes the owner row, original report,
canonical report/group identity, votes, private notes, or a private report's issue/PR association.
