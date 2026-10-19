# Feature toggle audit

Audit date: 2026-10-19. Source checkout: XZ-Mods, main. Local commit only.

## Decision table

Classification follows the trigger, not the resulting audio. A new MODS action,
an otherwise unused standalone SHIFT combination, or an added screen state is
ADDITIVE and has no master enable. An automatic effect or a replacement of a
stock-meaningful input, LED or screen remains ALTERS STOCK BEHAVIOUR. Native
shortcut routing excludes computer-controlled sources, where shifted mode
buttons may have software mappings. Plain mode-button selection is preserved;
SHIFT adds the shortcut rather than replacing a separate standalone shifted
function. SHIFT + BEAT JUMP keeps priority over stem mode-button slot 3.

`S` below means one version-1 settings value, shared by MODS, the desktop USB
settings backend/schema, USB profile loading and internal remembered settings.
Those are writers/readers of the same value, not an AND of separate switches.
An explicit applied profile or menu action is the last writer. Missing trailing
fields inherit current values through presence-aware overlay. Defaults apply
when there is no previous record. DJ identity profiles have no feature flags.
The desktop `usbLoadout` tree inventories files; it does not enable features.

| feature | old switches | class | new switch | reason |
|---|---|---|---|---|
| Page-1 custom Beat Jump | S `jump_enabled`; also gated page 2 | ALTERS STOCK BEHAVIOUR | MODS `jump_enabled`, page 1 only | Replaces normal pad jump lengths. |
| SHIFT + BEAT JUMP page 2 | S `jump_shift_page` AND `jump_enabled` | ADDITIVE | Always available | Unused stock combo opens a new page; retain all eight length options. |
| USB stem audio | S `stems`; boot `XZ_MODS_STEMS`; post-restore `XZ_MODS_STEMS_FORCE`; per-track gains/mute/bypass | ALTERS STOCK BEHAVIOUR | MODS `stems` | Boot overrides removed; mute/gain/bypass remain intentional performance actions, not independent engine preferences. |
| Gate cue | S `gate`; boot `XZ_MODS_GATE_CUE` | ALTERS STOCK BEHAVIOUR | MODS `gate` | Changes cue press/release behavior. Boot flag previously seeded the same value. |
| Smart cue | S `smart`; boot `XZ_MODS_SMART_CUE` | ALTERS STOCK BEHAVIOUR | MODS `smart` | Changes cue and transport behavior. Boot flag removed. |
| Theme and FLAGSHIP screen ownership | S `theme`; post-restore `XZ_MODS_THEME`; shell ownership follows theme | ALTERS STOCK BEHAVIOUR | MODS `theme`, ORIGINAL is off | Replaces stock visuals. Theme override removed. No shell files edited. |
| Scrolling waveform | S `wave_mode`; `XZ_MODS_WAVE_MODE`; startup `XZ_MODS_LAYERED_WAVE=0` veto | ALTERS STOCK BEHAVIOUR | MODS `wave_mode`, STOCK is off | Replaces stock visual content; both environment overrides removed. |
| Stem-row layout | S `stems_overlay`; STEMS button cycles same value; `XZ_MODS_INLINE=observe` suppresses drawing; local audio availability or PC source | ALTERS STOCK BEHAVIOUR | MODS `stems_overlay`, Hidden is off | Resizes stock waveform lanes. Button and profiles update the same value; observe veto removed. Audio/source readiness remains required. |
| Stem pad lights | S `pad_feedback`; active stem source, selected bank/page | ALTERS STOCK BEHAVIOUR | MODS `pad_feedback` | Changes stock-meaningful LEDs. Bank/page are mappings, not another enable flag. |
| Shift stem shortcuts | S `shift_pages`; stem engine state; pad-page mapping | ADDITIVE | Always available; saved order/bank retained | Standalone SHIFT + mode-button triggers have no separate stock shifted function. Ignore old Off; a gesture activates the same stem audio state when needed. Jump page 2 owns slot 3. |
| Manual key changes | `XZ_MODS_KEYSHIFT` controls engine/observe state; KEY +/-/RESET actions | ADDITIVE | MODS KEY +/-/RESET action; no master gate | These are explicit controls on a new MODS page. Their effect on audio does not change the trigger classification. |
| Key Sync shortcut | S `shift_keysync`; in-place SHIFT+SYNC and setup write it on; `XZ_MODS_KEYSHIFT` independently gates engine | ADDITIVE | Always available; setup verifies keys only | The standalone shifted gesture has no separate stock action. Ignore old Off; keep ordinary Sync and computer-controlled source behavior. |
| Effective-key feedback | S `key_feedback`, exposed on display and key-format pages | ALTERS STOCK BEHAVIOUR | MODS `key_feedback` | Replaces countdown text. Both controls already write the same value. |
| Browse key prefix | S `browse_key_prefix` | ALTERS STOCK BEHAVIOUR | MODS `browse_key_prefix` | Changes existing track-title text rather than filling unused space. |
| Browse compatible-key colors | S `browse_traffic` | ALTERS STOCK BEHAVIOUR | MODS `browse_traffic` | Changes stock key colors. Column order and key notation remain options. |
| Metrics | S `metrics_enabled`; MODS switch; top-bar switch; setup writes same value | ADDITIVE | Always available; METRICS opens setup | Readings occupy the added top-bar area; do not alter playback or stock controls. Sampler failures still offer setup. |
| Free-RAM information | S `ram_debug`; MODS display/RAM-page switches; hidden when metrics is shown | ADDITIVE | Always available within metrics; compact fallback retained | Empty-area information. Metrics already includes RAM, so avoid drawing both over each other. No independent saved enable gate. |
| Physical menu-key remap | S `rekordbox_menu` | ALTERS STOCK BEHAVIOUR | MODS `rekordbox_menu` | Reassigns a stock-meaningful button. Existing identifier retained for file compatibility. |
| Remote framebuffer view | S `fb_takeover`; selected physical/onscreen toggle writes it; post-restore `XZ_MODS_NATIVE_VIEW`; connection readiness | ALTERS STOCK BEHAVIOUR | MODS `fb_takeover` | Replaces stock display. Remove boot override. Connection is real availability, not a second view preference. SDK session temporarily owns/restores the same view. |
| Remote connection | Receiver-owned `connection.enabled`; MODS requests same receiver value | ALTERS STOCK BEHAVIOUR | Existing receiver-owned connection value | Opens remote service access. Model is reported receiver state, not a second stored flag; this build reports unavailable controls when host cannot mutate it. |
| Network discovery | Receiver-owned `connection.discoverable`; MODS requests same value | ALTERS STOCK BEHAVIOUR | Existing receiver-owned discoverable value | Advertises the device on the network. This is a separate network behavior, not a prerequisite for a known-address connection. |
| RAM tracks | LOAD/CANCEL/RELEASE actions; verified engine, track identity and memory capacity | ADDITIVE | MODS per-track load/release action; no master gate | Explicit controls on a new MODS page. Capacity, identity and engine readiness remain prerequisites. |
| Game/controller ownership | MODS launch/stop and foreground ownership; Kart additionally `XZ_KART_CONTROLS`; deck-mask option | ADDITIVE | MODS launch/stop session; no master gate | Explicit new-page launch claims controls only during that session. Stopped-deck, foreground, process identity and lease safeguards remain. |
| SDK app autoload | SDK manager `autoload_enabled`; setup and list write same value | ALTERS STOCK BEHAVIOUR | MODS SDK autoload value | Automatically starts apps. One manager-owned value; no duplicate feature gate. |
| Hot-cue preview | Capability bit `XZ_UI_PREVIEW`, setup/General controls; native runtime has no implementation | ADDITIVE | New-page actions and readiness setup; no master gate | The MODS-page trigger is additive. Remove the unused engine enable control; this native runtime still has no preview provider. |
| X-pad sampler | Capability bit `XZ_UI_SAMPLE`; per-pad hold/overdub/volume; native runtime has no implementation | ADDITIVE | New-page pad/strip actions and readiness setup; no master gate | New MODS-page triggers are additive. Remove engine enable gates; retain hold/overdub/volume options and actual provider/sample readiness. |
| DJ branding/profile selection | Active DJ identity and artwork selection; no enable flags | ALTERS STOCK BEHAVIOUR | Existing active profile selection | Changes displayed identity/artwork, with no hidden engine switch. `dj_profile.c`, `dj_profile_write.c` and desktop profile JSON contain no mod settings. |
| Legacy spare EQ | Read-only `spare_eq` compatibility slot; no runtime reader | ALTERS STOCK BEHAVIOUR | None, reserved wire slot only | Not an enabled feature or editable toggle; preserve old-file parsing. |

## Counts and compatibility

Removed **5 writable boolean settings**: `jump_shift_page`, `ram_debug`,
`metrics_enabled`, `shift_pages`, `shift_keysync`. Kept **10 writable boolean settings**, all controlling the
same native/receiver value rather than an extra desktop gate. The reserved
read-only EQ field is not counted. Choice settings with an off/stock value,
session actions and external receiver/SDK values are listed separately above.
Also removed **11 environment controls/overrides**: `XZ_MODS_STEMS`,
`XZ_MODS_STEMS_FORCE`, `XZ_MODS_GATE_CUE`, `XZ_MODS_SMART_CUE`,
`XZ_MODS_KEYSHIFT`, `XZ_MODS_THEME`, `XZ_MODS_WAVE_MODE`,
`XZ_MODS_NATIVE_VIEW`, `XZ_MODS_LAYERED_WAVE`, `XZ_MODS_INLINE`,
`XZ_KART_CONTROLS`. Stale launcher exports are harmless and ignored.

The five retired fields are absent from the active `fields` list. Their
`wire_order` positions and `retired_fields` descriptors preserve the exact
38-field version-1 format and every historical accepted prefix length. Native
parse, native overlay and direct runtime profile application normalize them.
The desktop backend accepts old form dictionaries carrying those fields,
discards their values, and writes compatibility constants. No USB/internal
settings format version change, positional shift, or rejection of old Off
values is required. Packaged older runtimes/schemas remain paired and unchanged;
new behavior requires packaging this source and runtime together.

Global `XZ_MODS_ENABLE`, `XZ_MODS_OBSERVER` and `XZ_MODS_UI` are development
runtime/observer attachment boundaries, not individual feature preferences.
`XZ_MODS_SETTINGS_READONLY` governs persistence, not feature availability.
USB-root, test-root, boot-proof, source-selection, audio/pad trace and diagnostic
CPU-affinity flags select resources/evidence rather than feature enablement.
`XZ_MODS_ARCADE_TRIAL` explicitly opens a test session. Firmware hashes, hook
ABIs, per-track cache/metadata readiness, SDK leases, deck safety and external
PC mapping acknowledgement remain real prerequisites. Build-time test defines,
codec/compiler flags and source inclusion do not create user-selectable runtime
feature gates. There is no feature selection list in desktop loadouts.

## Verification

- `tests/verify_feature_toggles.py --zig <zig.exe>` passes. It compiles the
  production settings parser/overlay/formatter up to its Linux filesystem
  boundary and the actual jump handler. All five legacy Off values normalize;
  all historical record lengths load; old desktop dictionaries serialize; both
  decks run page 2 with both old jump switches Off; page 1 stays stock when off.
  Source assertions cover removed environment prerequisites and menu switches.
- `mods/build.py --zig <zig.exe> --output <scratch>` passes. Runtime and display
  bridge pass ARM32, soft-float, GLIBC_2.4 and NODELETE gates; its eight ARM test
  programs compile. Those ARM binaries were not executed on the controller.
- `mods/ui/verify.py` passes its complete portable suite and assertion rejection.
  General-menu, metrics, key-feedback UI, key music, key runtime and key DSP
  verifiers pass. Shell-model, shell-runtime and native-surface host verifiers
  pass without editing shell sources.
- `cargo test --manifest-path src-tauri/Cargo.toml`: 29 passed, one existing
  browser fixture ignored. Every `ui/*.js` file passes `node --check`. There is
  no package.json or TypeScript frontend project in this checkout.
- Full builder unittest discovery: 361 run, 337 passed, 18 skipped, one failure
  and five errors. Five prepare-USB cases lack configured builder resources;
  one waveform test references a missing Python helper. Focused settings tests
  and the new current-schema migration verifier pass. The focused settings,
  managed-USB, version and DJ-profile suites pass all 48 tests.
- The full set of root `verify_*.py` and `tests/verify_*.py` was attempted.
  Linux-only profile, DJ profile/link/write, screen codec/server, flip, key
  feedback, input-regression and runtime-controls verifiers cannot launch their
  gcc/cc dependency on Windows. WSL itself fails with
  `Wsl/Service/CreateInstance/MountDisk/HCS/ERROR_PATH_NOT_FOUND` because its
  registered `ext4.vhdx` is missing. The receiver verifier also references the
  absent historical `packages/xdj-xz-toolkit/...` path. Hook-capacity verification
  needs the absent decrypted firmware fixture. These are unresolved verification
  gaps, not passing checks.
- No device restart, hardware playback claim, local installation, push, tag,
  release or publication. Installation belongs to the owning agent's integration
  pass; this implementation task explicitly requests local commits only.

Follow-up correction: the result of an action, including audio changes, does
not determine its class. The seven changed rows are Shift stem shortcuts, Key
Sync shortcut, manual key changes, RAM tracks, game/controller ownership,
hot-cue preview and X-pad sampler. Existing options and real readiness/safety
checks remain. Automatic stem replacement, pad lights, waveform/theme
replacement and gate/smart cue continue to use their MODS values.

Follow-up verification: migration/overlay tests cover all five old Off slots;
portable shifted-mode tests prove the retired flag cannot suppress any mapping;
desktop tests retain order, bank, pad page and jump-length options. The UI suite
also proves loaded samples on a ready new page work with the old engine bit off.
The full ARM build was attempted against concurrent shell work and currently
fails at `shell_runtime.c:408`: missing `notes` initializer under `-Werror`.
That excluded file was not edited. See the follow-up commit report for the
final focused checks; earlier verification above belongs to the initial audit.

Final follow-up checks: the complete portable UI suite and migration verifier
pass. All 49 focused desktop settings/managed-USB/version/DJ-profile tests pass.
All eight changed production/parser test units and the three Linux runtime/key
replay fixtures compile for ARM with active assertions and warnings as errors.
The Linux runtime fixtures were not executed; WSL remains unavailable. No
installation or controller acceptance is claimed.
