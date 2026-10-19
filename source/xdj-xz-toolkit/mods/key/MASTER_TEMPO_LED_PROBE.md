# Master Tempo LED qualification

The production mapping `xz_led_keysync_mapping` in `ui/native_led.c` is
zero-initialized and disabled. KEY SYNC never rewrites a status LED until a
record id and native slow-blink words are proven. Pad records are excluded.
The portable test uses hypothetical id 0x70 and invented words, not a native
mapping. This revision does not deploy or access hardware.

## Owner-run capture procedure

1. Arrange a stopped-deck maintenance session with the owner. Use an already
   approved RAM-only test runtime containing this probe. Do not write firmware
   or change a playing deck. Keep the normal rollback/power-cycle path ready.
2. Read the exported symbol `xz_mods_led_probe_v1` with the existing read-only
   process-memory tooling. It is version 1: a u32 version, two u32 overflow
   counters, then 2 x 64 entries. Each entry is 12 u32 words: sequence, seen,
   id, changes, and the eight original packet words at +8,+0xc,...,+0x24.
   Channel 1 is deck 1, channel 2 is deck 2. Records are 0x2c bytes. Match the
   record id at +0 and channel at +4. Pad ids 0x12..0x19 are never captured.
3. Take repeated snapshots at about 10 Hz. Accept an entry only if its sequence
   is equal and even before and after reading it. Check overflow counters are
   zero. Slots retain the latest words per id; changes/seen are counters and
   saturating seen does not prevent later updates. Save snapshots on the PC.
4. With KEY SYNC off, record a baseline. Have the owner press MASTER TEMPO on
   deck 1, wait two seconds, then press it off and wait. Repeat on deck 2.
   Save exact action times, visible LED state and the matching channel's word
   changes. Repeat the cycle to separate unrelated changing records. The MT
   candidate must follow only that deck's MT state in both directions.
5. Capture an owner-observed native LED slow blink for several cycles in the
   same session, recording the actual record id, all eight words and video
   timing. An on/off MT capture alone does not prove any blink encoding.
   Compare native off/on/blink records to identify each affected word and mask.
   Require approximately one complete blink cycle per second. Do not infer
   the meaning of a word from a plausible value or from the portable test.
6. Save the source commit, firmware identity, raw snapshots and visible timing
   in the evidence record. Fill the single mapping entry's id, eight masks
   and native blink words only from that proof, then enable it. Keep force
   status at +0x18 untouched. Test unmatched ids/channels and unsynced records
   remain byte-identical, then run `ui/verify_native_led.py --zig <zig>`.
7. In a separate approved hardware test, confirm only the synced deck blinks
   near 1 Hz. Second Shift+Sync, MT exit and a new load must return the LED to
   native ownership on its next native packet. Confirm both MT restore states
   and stock subsequent MT presses. Record physical acceptance separately.

The probe has bounded storage, at most 256 records per packet and 64 ids per
channel. It does no allocation, file I/O, logging or network work on the input
thread. It runs before any rewrite, so its words are always native words.
The rewrite leaves forced status and all unsynced records to the stock sender.
The input-thread hook has one writer; readers use the sequence protocol above.
