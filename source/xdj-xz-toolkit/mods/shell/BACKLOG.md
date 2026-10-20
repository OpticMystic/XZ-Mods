# FLAGSHIP shell backlog

Owner requests and audit findings not built yet. Newest first. Remove an entry when it ships.

## Owner requests

- **2026-10-19 · RULE: same presses as stock, at least as fast.** Every workflow takes exactly the
  button presses the stock XZ UI takes (XZ manual), no more and no fewer when that changes meaning, and
  responds at least as fast. Build a press-parity table (workflow, stock keys, shell keys) asserted in
  the tests, and measure list, waveform and load times on the deck against stock. Known deviations:
  encoder push loads instead of opening the track menu; LOAD pressed twice (instant double) is
  swallowed; lists wait on one background reader (today's hardware failure showed "LIST NOT READ YET"
  indefinitely while stock read the same stick).
- **2026-10-19 · Hardware fault to diagnose.** Both decks stopped playing or scrubbing, including a
  track stock loaded, while the shell's lists sat at "LIST NOT READ YET"; stock browsed the stick fine.
  The dev loader's shell (TCP 2323) was not up, so there is no log. Reproduce with XZME and --shell-debug.
- **2026-10-19 · A stick ejected under its browser returns to SOURCE.** While BROWSE (or any list) shows a
  USB stick and that stick is pulled, go straight to the SOURCE screen (the four tiles), cursor on the
  slot that went away. Today the list empties and shows a line pointing to the picker
  (`test_panels.c:169`); the DJ has to tap it.
- **2026-10-19 · LINK / CONTROL MODE screen in the new look.** The stock connection screen: LINK lists
  linked players' media and, with a computer on USB, CONTROL MODE; LOAD 1 / LOAD 2 puts that deck under
  the DJ software (XZ manual pp.113-114, 128-129). Show each deck's assignment and the 1/3, 2/4 layer;
  DECK SELECT in SHORTCUT (missing today); a CONTROL MODE state on the play screen instead of a stale
  track. Firmware code analysis of LINK / CONTROL MODE is off limits, so this is built from what the
  owner observes on the deck (photos of each stock state) on top of the stock hand-off: the LINK and
  rekordbox keys give the screen to stock (XZ-Mods e7d3af7).
- **2026-10-19 · Touch Preview.** Hold a track's waveform in a list to hear it in the headphones only,
  as the newer players do. Research verdict FEASIBLE-GATED
  (`docs/research/2026-10-19-xz-headphone-preview.md`): no headphone-only path exists (rbp sends two
  stereo deck streams to the mixer DSP, which does all mixing); the preview can only ride an idle deck's
  stream while that channel's ON AIR is off, cut within one 1.45 ms buffer. Owner decision pending;
  hardware must measure the leak window before it ships. Needs AIFF, mono, resampling and AAC gaps closed.

## Audit 2026-10-19 (runtime does not feed or enable what the shell draws)

In the owner's priority order:
1. Needle search on the overview (CAP_SEEK never set; no SEEK handler).
2. Encoder push opens the track menu (LOAD TO DECK 1/2, CUE/LOOP LIST) instead of loading onto the active
   deck; LOAD pressed twice = instant double.
3. Feed PLAY MODE, A. HOT CUE (red ON / white library setting), A. CUE per deck, ON AIR, the alert
   banners (emergency loop, E-8302/8304/8305), LOOP IN/OUT adjust time, MEMORY / DELETED confirmations.
4. Remove the non-XZ UTILITY/SHORTCUT rows BEAT JUMP BEAT VALUE and PLAY MODE.
5. Keep display settings (APPEARANCE, zoom, key notation, hide switches) across restarts.
6. Bigger: folder browsing on sticks without a library export, GRID ADJUST (RESET / SNAP / SHIFT), TAG
   LIST and rating edits, non-Latin track names, screen saver.
