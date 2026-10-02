# XZ 1.26 signed beat-jump command

Inspected the local rbp with SHA256
`6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2`.
Addresses are ARM virtual addresses.

- `DjEngineIF::playBeatJump` at `0x444cc` passes channel and signed jump count
  through `PlayEngine::playBeatJump` at `0x4b050` to `Player::playBeatJump`
  at `0x66320`. The latter compares positive and negative magnitudes with
  the native jumpable-beat limits before dispatching through the existing
  loop/quantize implementation. It returns failure for rejected jumps.
- `Loop::calcBeatJumpPoint` at `0x84ec4` reads the signed count through its
  second reference argument. It advances or retreats grid entries by that
  count. There is no ordinal size-enum lookup on this path.
- `PlayerInnards::onKey_BeatJumpLoopMove` at `0x28cbd0` selects mode 4 on
  operation 0. It does not branch on Shift. The mod can record page 2 and
  let stock select Beat Jump without changing the native Shift field.
- The existing input decoder maps internal channels 1/2 to engine channels
  0/1. It now snapshots the input-blocking bytes at `this+0x44/+0x45`;
  blocked input cannot issue a custom jump.

The fixed-address call is behind the existing verified-application gate.
No instruction patches, grid edits or alternate seek path were added.
Static ABI evidence and isolated tests do not establish physical long-jump,
quantize or slip behavior. Native buffer limits may reject a requested size.

## Browse encoder in the pad editor

`UiKey_EncoderRotate` at `0xe15b0` receives a signed delta in r0, adds it
into the native encoder accumulator and returns an integer handled status.
Its first eight bytes are `08402de9023080e2`: push {r3,lr}; add r3,r0,#2.
Neither instruction uses a PC-relative operand, so the existing bounded
ARM trampoline can relocate them. The entry is added to the explicit hook
allowlist and checked against those bytes before installation.

The hook owns rotation only while MODS is visible on the beat-jump editor.
It applies the same clamped size step as the touch controls and queues the
same preference save. Other pages, closed MODS and stopped runtime forward
the original delta to the native function. Encoder failure leaves touch
editing available and the menu displays USE UP / DOWN.
