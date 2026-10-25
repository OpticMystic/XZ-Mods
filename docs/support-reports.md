# Four-deck recovery and manual reports

The experimental four-deck runtime targets XDJ-XZ firmware 1.26. Connect the
rear USB-B port to top USB1 with a USB-B to USB-A cable, and keep music in USB2.
The native decks must be stopped for initial bridge admission. A late music
mount now remains pending and is retried automatically. Firmware, kernel,
device ownership and active-media identity checks remain enforced.

This is not universal support for other models or firmware. Do not remove the
music USB during playback. Full RAM carry and active music swapping are not
supported by this beta.

## Submit a report

In the Loader, open **Report an issue**. Choose the boot USB if available,
describe the problem, prepare a draft and inspect its public summary and
private diagnostics. Approve the reviewed draft and explicitly send it.
Uploading stores private logs; it does not create a public GitHub issue.
Open the separate GitHub draft and submit it there.

The on-device Support page provides the same manual workflow, plus a QR link
to the public issue draft. If the XZ's clock is wrong, enter the current UTC
date and time for certificate validation. This changes only the reporting
helper's clock, never the playback or system clock. Alternatively export the
sanitized report to USB and select that USB in the Loader.

Only known diagnostic codes, build identities and explicitly approved text
are included. No firmware, boot keys, music, track databases or automatic
telemetry are uploaded. Unknown log text and device identifiers are omitted.
Logs are private, retained for 14 days, and accessible only to the maintainer
through a separate credential. Public issue drafts contain the approved
summary and a random report identifier, not private logs or contact details.

If a send is interrupted, check its receipt before retrying. Retries keep the
same report ID and reviewed bytes. The maintainer inbox authenticates each
page and verifies the complete report hash before displaying it.

## Verification boundary

Host and ARM tests cover delayed mounting, existing loopback startup,
identity replacement, report privacy, certificate checks, interrupted sends
and exact report reconstruction. Those results do not establish hardware
acceptance on every XZ or USB device. Keep a known working loader for rollback.
