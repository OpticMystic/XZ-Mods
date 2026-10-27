# Support identifiers

`spec/support-contract.json` defines the formats, bounds, and parsing vectors.
`python -B tools/generate_support_contract.py --check` checks the generated TS,
Python, JS, and Rust implementations. Parse returns the numeric value or null
(`None` in Python/Rust); format returns the canonical display string or null.
Only ASCII spaces and dashes are ignored. Report symbols accept I/L as 1 and O
as 0. Item numbers accept decimal digits, including leading zeros.

The Worker assigns `R-XXXX-XXXX` from five CSPRNG bytes at acceptance. A D1
unique index arbitrates concurrent requests. A reference collision rolls back
the acceptance batch and retries with fresh entropy, at most eight times. Other
storage errors retain the existing uncertain-send behavior. Accepted retries
return the stored reference. Clients never supply or change it.

Migration `0003_support_identifiers.sql` backfills all retained tracking rows,
including v2. It reads the first ten hexadecimal characters of `report_id` as a
big-endian 40-bit integer, then encodes that integer in Crockford base32. Rows
are processed in ascending `report_id` order. If a reference is occupied, the
migration adds one modulo 2^40 until it finds an unused value. Prefix collisions
therefore produce deterministic, distinct references. Backfill uses no random
SQL function or external script. New acceptance always uses CSPRNG entropy.

Each new group receives a D1 AUTOINCREMENT item number starting at 1001.
Existing groups receive numbers in `(created_at, id)` order. The allocation
ledgers have no foreign key to expiring reports or tracking. They retain only
internal IDs and display identifiers, preventing later reuse. Payload cleanup
does not remove group membership or its history. Tracking expiry removes that
report's membership/history, while item allocations and groups remain.

Owner triage assigns an item even without a strong fingerprint, including
features and questions. An optional `item_number` in the existing triage body
joins a same-kind item. `DUPLICATE` can target it explicitly. Existing citation,
review, revision, and authentication checks still apply. Changes append
`report_item_history`; `GET /v2/admin/reports/{id}/items?before=<revision>` reads
that history under owner authority. Repeating an operation does not append it.

`GET /v2/admin/lookup?q=<identifier>` resolves a reference to its report and item,
or an item to its group. `GET /v2/admin/reports?q=<identifier>` searches the inbox
and paginates a stable snapshot. Pass only the returned cursor on later pages.
Both endpoints require owner authentication and use the shared tolerant parser.

Receipts, status projections, and owner inbox rows expose `report_reference`
and nullable canonical `item_number`. These identifiers grant no reading,
tracking, publication, or mutation authority. Receipt keys and their scoped
derived capabilities retain the existing permission boundaries. Public-safe
tracking exposes only this reporter's reference and the assigned item number,
never another report's ID, private text, or canonical report pointer.

`publicationItem` supplies only `{item_number}` for a later approved outbox
projection. Phase 5 must prefix issue titles with `[XZ-1042]` and use
`Support reference: XZ-1042` in PRs. It must bind this field into public-text
approval; grouping or title changes invalidate that approval. No publisher or
outbox execution is added here. Legacy v1 manual GitHub drafts remain unchanged.

Verification uses local Miniflare D1/R2. The transaction behavior follows the
[D1 batch API](https://developers.cloudflare.com/d1/worker-api/d1-database/).
No migration has been applied to the hosted service.
