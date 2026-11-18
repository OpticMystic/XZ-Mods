import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

// Checks the Live Streamer view model: the frame rate shown is the one measured, never the target.
// Run: node tools/verify_live_stream.mjs
const { liveView, liveRate } = createRequire(import.meta.url)('../ui/live-stream.js');
const live = fields => liveView({ phase: 'live', capture_ms: 12, frames: 40, error: '', reason: '', ...fields });

// Loader v2 aims for 30 but says nothing about speed until a capture rate is measured.
let view = live({ protocol: 2, target_fps: 30, fps: null });
assert.equal(view.live, true);
assert.equal(view.fix, false);
assert.equal(view.badge, 'Measuring frame rate…');
assert.doesNotMatch(view.text + view.badge + view.note, /30/);

// Measured rates are what the badge and status report.
view = live({ protocol: 2, target_fps: 30, fps: 29.6 });
assert.equal(view.badge, 'Live · 30 fps');
assert.match(view.text, /^Live preview · 800 × 480 · 30 fps · 12 ms capture · 40 frames$/);
assert.doesNotMatch(view.note, /sending/);
view = live({ protocol: 2, target_fps: 30, fps: 14.2 });
assert.equal(view.badge, 'Live · 14 fps');
assert.match(view.note, /sending 14 fps/);
assert.equal(live({ protocol: 2, target_fps: 30, fps: 2.5 }).badge, 'Live · 2.5 fps');
assert.equal(live({ protocol: 2, target_fps: 30, fps: 0.4 }).badge, 'Live · under 1 fps');

// Loader v1 keeps its picture and offers one non-blocking update, without stealing focus.
view = live({ protocol: 1, target_fps: 1, fps: 1 });
assert.equal(view.live, true);
assert.equal(view.fix, true);
assert.equal(view.reason, 'legacy');
assert.equal(view.badge, 'Preview · 1 fps · not performance video');
assert.match(view.note, /Update the USB loader/);
assert.equal(live({ protocol: 1, target_fps: 1, fps: null }).badge, 'Preview · about 1 fps · not performance video');

// A backend without the new fields reads as the old one-picture-a-second stream, with no loader prompt.
view = live({});
assert.equal(view.fix, false);
assert.equal(view.badge, 'Preview · about 1 fps · not performance video');
assert.doesNotMatch(view.note, /Update/);
assert.equal(liveRate(null).text, 'about 1 fps');

// Blocking failures are unchanged: no screen service still means the loader fix and no rate badge.
view = liveView({ phase: 'connecting', reason: 'loader', error: 'no service' });
assert.deepEqual([view.live, view.fix, view.reason, view.badge, view.detail], [false, true, 'loader', '', 'no service']);
view = liveView({ phase: 'connecting', reason: 'stream', protocol: 2, target_fps: 30, fps: 30 });
assert.deepEqual([view.live, view.fix, view.badge, view.title], [false, false, '', 'Screen stream interrupted']);
view = liveView({ phase: 'stopped' });
assert.deepEqual([view.live, view.badge], [false, '']);
assert.match(view.note, /measures once connected/);
console.log('live stream view model: ok');

view=live({protocol:3,source_color_bits:8,requested_color_bits:8,fps:18,target_fps:30});
assert.equal(view.fix,false);assert.equal(view.badge,'Live · 18 fps');assert.match(view.mode,/8-bit.*256 colors.*800/);
view=live({protocol:2,source_color_bits:16,requested_color_bits:8,fps:18,target_fps:30});
assert.equal(view.fix,true);assert.equal(view.reason,'color-depth');assert.match(view.mode,/updated USB loader.*working 16-bit/);
