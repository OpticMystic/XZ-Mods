# Four-deck playback candidate

This private candidate reduces MP3 seek work and closes gaps during audio-generation changes. It retains the current UI and player controls from Preview.18.

The MP3 reader uses a bounded seek index and a lazy 512 KiB PCM cache per open decoder. Seek preroll restores synthesis history, failed seeks invalidate the physical cursor, and APE footer bounds use the correct size field. See `source/xdj-xz-toolkit/mods/audio/tests/MEDIA_CACHE_PATCH.md` for the decoder regression command and provenance.

The player prepares the first target block before a jump, then publishes that block and the new transport generation together. Pause and unload still flush immediately. Sink calls carry their captured USB route epoch, checked under the route lock, so a render that spans reconnect cannot publish into the replacement route.

The two extra-deck workers and their connector select CPU 2 from inside their own entrypoints. The USB worker inherits the connector's placement. This applies only on the verified ARM/Linux runtime with four online Cortex-A9 cores and ordinary scheduling. It preserves nice values and scheduling policy, verifies the resulting mask, and logs refusal or restoration failures. Native threads are never moved. Generic player clients retain their inherited placement unless they provide the optional worker-entry callback.

A same-binary, same-fixture silent controller comparison changed only the benchmark's CPU placement. CPU 0 showed long scheduler stalls; CPU 2 produced zero steady underruns and zero uncovered USB queue time in playback, controls, hot cues and other-deck loading. Peak measured use was 20.3 percent of one core. Initial PLAY and post-load delays remain reported separately against explicit latency budgets. The production placement helper also passed an isolated ARM check with unchanged scheduling policy and nice value. This proves the tested silent workload, not audible USB-loopback acceptance or every native playback load.

Verification for runtime `1bb3ae2a1cdae79428fbf30e61231ade2f99f50f3a4fc49b2441bd3fa0539c79` includes the ARM32/GLIBC 2.4 build gate, decoder ASan/UBSan suite, player and connector tests, ThreadSanitizer, reconnect negative controls, and 22 deterministic pipeline scenarios. Deliberate producer and USB stalls still fail the audio gate. Ten isolated library loads and seven native test executables passed on the controller without replacing its running trial.

These checks do not establish audible mixer playback, four-deck stem support, or a production release. Real-paced timing, silent controller headroom, and physical USB-loopback acceptance are recorded separately in the private lab receipts. The larger shared cache design and prepared-stem redundancy are separate work.
