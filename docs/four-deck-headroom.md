# Four-deck playback candidate

This private candidate reduces MP3 seek work and closes gaps during audio-generation changes. It retains the current UI and player controls from Preview.18.

The MP3 reader uses a bounded seek index and a lazy 512 KiB PCM cache per open decoder. Seek preroll restores synthesis history, failed seeks invalidate the physical cursor, and APE footer bounds use the correct size field. See `source/xdj-xz-toolkit/mods/audio/tests/MEDIA_CACHE_PATCH.md` for the decoder regression command and provenance.

The player prepares the first target block before a jump, then publishes that block and the new transport generation together. Pause and unload still flush immediately. Sink calls carry their captured USB route epoch, checked under the route lock, so a render that spans reconnect cannot publish into the replacement route.

Verification for runtime `1bb3ae2a1cdae79428fbf30e61231ade2f99f50f3a4fc49b2441bd3fa0539c79` includes the ARM32/GLIBC 2.4 build gate, decoder ASan/UBSan suite, player and connector tests, ThreadSanitizer, reconnect negative controls, and 22 deterministic pipeline scenarios. Deliberate producer and USB stalls still fail the audio gate. Ten isolated library loads and seven native test executables passed on the controller without replacing its running trial.

These checks do not establish audible mixer playback, four-deck stem support, or a production release. Real-paced timing, silent controller headroom, and physical USB-loopback acceptance are recorded separately in the private lab receipts. The larger shared cache design and prepared-stem redundancy are separate work.
