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
