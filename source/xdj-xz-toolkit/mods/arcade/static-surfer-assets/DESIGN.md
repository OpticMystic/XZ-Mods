# STATIC SURFER, by VJ OpticMystic

Renderer: `source/mods/arcade/glyph_run_render.c`, entry `xz_glyph_render(g, pixels, count, stride)`.
It covers BRIEF, RUN and PAUSE. The Lab renderer still owns SETUP, CONTROLS and RESULTS. The
renderer only reads `g`.

## Final name and the birthday cows

- **Name:** STATIC SURFER, by VJ OpticMystic. It replaces Bitflip and Glyph Run in every
  visible string: HUD title, brief title, START button. The HUD title ends at x169, so it
  clears the score at x176.
- **Cat:** drawn at 0.72 scale (69 px solo) instead of 0.56. The translucent fill under the
  face is gone, so the character reads beyond the glow of the frame.
- **One blitter:** `blit_sprite` takes RGB and alpha pointers and draws both the cat and the
  cow (`xz_glyph_cow_rgb/alpha`) with the same integer inverse mapping.
- **Party fly-by:** runs for 4 pulse periods after `g.glyph[p].party_at`, which the engine sets
  at streak 4, 8 and so on. The `-10000` initial value is ignored.
  - The cow is 72 px solo and 48 px duo, upright with a gentle sway so the hat always reads.
  - It hangs in a gold tractor beam under a span-drawn saucer, with a "PARTY COW / STREAK n" tag.
  - It drifts in from the left and stops above the hero line during the first 62% of the 4
    pulses, then climbs out of the band top. It never moves into the incoming-obstacle side.
- **Confetti:** at most 24 pieces, falling out of the beam. Their sway and size follow the low
  waveform band at the playhead. They are cut off 175 units above the rail, so the corridor
  stays clear. There is no full-screen flash.
- **Brief:** the birthday cow is the featured character in the preview panel, in its beam
  under a saucer, labelled BIRTHDAY COW, next to the cat. The credits name cows 22, 26 and 31.
- **Brief footer** (y430..479): TEN STUDIES x0..159, BUTTONS x160..319 (shows the current
  action), MUSIC SETUP x320..599, SOLO/DUO x600..799 (shows `g.duo`). START y330..409 is
  unchanged.
- The engine's collision contract is unchanged; the renderer still only reads.

## The game in one line

A black glitch cat with huge cyan eyes rides a narrow diamond of light along a luminous rail
through the owner's own scenes. One tap jumps. A small jog turn (about 60 degrees either way)
tilts the diamond so it fits the slanted glyph gates. You never need a full spin.

## Screen map

| Area | Solo | Duo |
|---|---|---|
| Header y0..47 | HUD: title, score, streak and best, timing ms, 3-act progress bar, time left; DECK x704..799 | Same bar; P1 and P2 scores |
| Scene y48..423 | One band, floor at y340, scale 1 | Two bands of 188 px (y48..235, y236..423), floor at band top + 150, scale 0.6 |
| Footer y430..479 | RETRY 0..159, BUTTONS 160..319, JUMP/ACTION 320..519, RATE 520..639, MENU 640..799 | Same |

In duo the background still fills the whole 800x376 scene. The two bands are cut out of one
picture, not two shrunken copies, so the art keeps its full size and the screen reads as one
place.

## Using the owner's art

- **Backgrounds** (`xz_glyph_background[3]`, scenes 38, 49 and 122) are doubled exactly 2x into
  800x376 at y48. Each source pixel is read once and written as a 2x2 block (75k reads, 300k
  writes, about 1 ms on the ARM target).
- **Three acts.** The run moves through the three scenes at 0, 40 and 80 seconds (thirds of
  `g->duration`, 120 s default). The last 6% of each act uses a 4x4 Bayer dither from scene A
  to scene B. That gives a two-second crossfade at the cost of a plain copy, with no per-pixel
  blend. The HUD progress bar is split into three coloured segments (UV, magenta, cyan) that
  match the acts.
- **Dimming only where you play.** A per-row gain table, built once per frame, leaves the sky
  at full brightness. The corridor gets darker toward the rail (84% at the top down to 54% at
  the rail) and the ground strip sits at 46% so the waveform roots read. Columns are never
  dimmed, so the left and right of each scene stay intact.
- **Music.** The whole scene's base gain is 200/256 + bass (`music[0]`) x 36 + `pulse` x 20,
  capped at 1. The pulse can lift the scene by at most about 8%, which you feel without seeing a
  strobe. Mids (`music[1]`) grow the foreground crystal teeth. Highs (`music[2]`) brighten the
  motes, add gold glints to the teeth and switch on the cat's glitch slices.
- **The cat** (`xz_glyph_cat_rgb/alpha`, piece 8) is drawn at 0.56x (54 px solo, 32 px duo),
  rotated by the jog angle. It uses one `sinf/cosf` pair per frame and 16.16 integer inverse
  mapping (two adds per pixel), with nearest sampling and the real alpha. Partial alpha blends,
  alpha 248 and up is copied, and alpha under 8 is skipped. The glitch shifts about one source
  row in 16 by three pixels and tints it cyan or magenta. It is keyed to a 7-per-pulse tick, so
  it shimmers instead of flashing.

## Readable obstacles that match the physics

Every world length is `logical * s`. An event sits at `x = herox + (e.end - pos)/period * 120 * s`.

- **Spike (crystal spine).** Its tip is exactly `22 + energy*28` above the rail, with two smaller
  side shards. It has a dark crystal body and a cyan or magenta edge (alternating by id). The
  next spike to clear gets a gold tip.
- **Gap (void).** A 54-unit black cut through the rail and roots, with UV depth lines and
  magenta crystal posts on both lips. The hero stays above 25 for about 108 units of a
  well-timed jump, so the gap is visibly shorter than the jump.
- **Glyph gate.** A tall diamond portal tilted by `e.angle`, centred 111 units up
  (`28 + (36+130)/2`) with half-height 84. Its slot is the physics window: hero bottom 36..130
  means the hero's frame (tips at plus or minus 31) fits inside top to bottom. Etched pillars
  run from the band top to the top vertex and from the bottom vertex to the rail, so the slot
  is plainly the only way through. A dotted axis runs along the slot.
- **The same mark.** The gate's top vertex and the hero frame's top tip both carry the same
  short gold tick along their long axis. When the two ticks point the same way, you pass. The
  jog dial in each band's top-right shows the needle (your tilt) against a gold tick (the next
  gate's tilt) on the same scale.

## Tap timing

- A **gold chevron launch mark** rides the rail at `e.at` for every pending obstacle. It moves
  left and touches the **strike zone**, a small gold diamond under the hero line, at the moment
  to tap. The strike zone lights up as a mark arrives. Marks dim with distance (full gold inside
  one pulse). Gate marks also show a small tilted tick with the gate's angle, so you can read
  the tilt before the gate is close.
- A faint UV **ideal arc** from the next launch mark traces the engine's parabola (apex 90 at
  0.52 pulse). Its apex lands on the obstacle, which teaches why tapping at the mark works.

## Feedback

- **Jump.** The frame you see is the engine's `glyph.height`, so it rises on the frame the tap
  lands. During the first 0.3 pulse, rail sparks kick out sideways and short speed streaks
  appear under the frame. Landing puffs UV dust.
- **Approaching a gate (2.4 pulses).** If you are within 0.29 rad of the gate's tilt, the gate
  rim, the frame rim and the dial needle turn gold and **LOCK** appears. If not, the rim turns
  magenta and an arrow above the hero plus **TILT CW** or **TILT CCW** says which way to turn
  (positive is clockwise). This is feedback for the angle alone, separate from timing.
- **Results.** For 0.7 pulse after an encounter, a ten-shard burst plays at the obstacle (gold
  for PERFECT, the player colour for GOOD, grey for a miss) and the obstacle keeps that colour.
  A label above the hero uses the engine's `feedback` string when one is set. Otherwise it
  shows PERFECT, GOOD, CLIPPED, FELL IN, TILT CW/CCW TO MATCH, TOO LOW or TOO HIGH.
- **Collision.** `glyph.flash` fades the frame rim and the cat toward red. The engine owns the
  decay, so it never strobes. While `pos < safe_until`, a thin UV outline 1.5x the size of the
  frame shows the grace period without blinking.
- **Streak trails.** The afterimages are rebuilt from `jump_at` with the engine's own
  gravity and jump speed, so they trace the path you actually flew:
  - streak under 4: 5 fading diamonds
  - 4 to 11: 9 diamonds joined by a ribbon
  - 12 and up: 13 diamonds, a cyan/magenta helix and gold cross sparks

## Music you can see

- **Waveform roots.** `xz_lab_band(g, at, band)` is sampled every 4 px (5 px in duo) and hangs
  under the rail as roots: low is a wide UV root, mid a magenta one, high a thin cyan one, each
  with a bright tip. The x to time mapping is the same as the obstacles, so every spike, gap and
  gate stands on the stretch of track that placed it. The label in the ground strip names the
  real source (**STEMS: DRUMS / HARMONICS / VOCALS** when `wave.kind` is 1, otherwise **BANDS:
  LOW / MID / HIGH**) and the clock type (estimated or tap pulse). It never claims bars. With no
  waveform it says **NO WAVEFORM FROM THE DECK**.
- **Motes** (30 solo, 14 per band in duo) drift in the sky above the corridor only. **Spores**
  rise out of the roots in the ground strip. Neither enters the corridor.
- **Foreground crystal teeth** stand along each band's bottom edge with 1.3x parallax. They are
  sparse (60%), capped below the rail, dark-bodied with neon edges, and grow with the mids.

## Brief (one screen, same world)

- Same scene 0 background, with the left half darkened for the text.
- **STATIC SURFER** at 4x in gold, **BY VJ OPTICMYSTIC**, and a two-line pitch.
- The real mapping from `xz_lab_action_label(g, deck)`:
  - solo: the control deck, with the jog named left or right
  - duo: P1 top band and P2 lower band, each with its own label
- Ready chip: NO TRACK, PULSE NEEDS SETUP, or READY with the source.
- A source line (study pattern or track accents / clock / pulse only) and the root-band label.
- A **live preview panel** drawn with the game's own functions: the real waveform roots and the
  rail, a gold-locked gate tilted +0.62, a spike, the launch chevron (labelled TAP), the ideal
  arc, and the cat hopping every two pulses on a frame tilted to match ("MATCH THE TILT").
- START at y330..409 reads "START STATIC SURFER" or, without a usable pulse, "SET UP MUSIC FIRST /
  THIS OPENS MUSIC SETUP".
- Credits line: art by VJ OpticMystic, scenes 38, 49 and 122, glitch cat from piece 8.
- Footer: see "Brief footer" above. DECK is in the header.

## Pause

Pause reuses the Lab's box and hit target. **TRACK ENDED** comes with **BACK TO DECK** at
x220..579, y204..245. Otherwise it says **MUSIC PAUSED / PRESS PLAY ON THE DECK**. Nothing seeks
or restarts music.

## Budget and constraints

- No heap, no IO, no libpng. Writes go through the canvas or clipped spans bounded by the band.
- Trig: one `sinf/cosf` per hero and per visible gate or gate mark. Everything else uses
  `xz_sin1024` table lookups per object, never per pixel.
- No `xz_disc`, `xz_ring` or AA lines. The code has its own Bresenham square-pen lines, span
  fills, isosceles crystal fill and convex-quad translucent fill. The UI calls are `xz_rect`,
  `xz_round_rect` (buttons), `xz_pixel_text` and `xz_blend`.
- Estimated cost per frame on the ARM target: background about 1 ms, the cat about 4k pixels, and
  the roots about 600 band lookups. Everything else draws a few hundred small spans, well under
  20 ms.
- Rejects `stride < 800` or a short buffer (returns 0), otherwise returns 1.

## Not verified here

I had no compiler or device in this session. The file was written against the provided headers
for C11 `-Wall -Wextra -Werror` (ARM softfp VFP) but has not been compiled or run. Check
these first:

- The order of `xz_glyph_background[0..2]` is assumed to be 38, 49, 122.
- The cat art is assumed to be centred in its 96x96 box.
- The miss-reason fallback for gates reads the tilt and height at draw time, just after the
  encounter. The engine's `feedback` string takes priority when it is set.

## Native performance pass

The renderer caches doubled background rows with16 brightness steps and uses RGB565 packed blending plus simple rounded spans. The cache adds about301KB. The final guarded ARM test measured8.05ms mean and10.44ms worst process CPU time across48 solo/duo frames with cow fly-bys. Wall time was34.6ms mean under nice15 alongside the existing DJ UI, so it is not a presented-frame-rate measurement. Native game feel and audio continuity need the owner test.
