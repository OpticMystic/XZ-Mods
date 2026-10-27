# Stock warning priority, firmware 1.26

Live browse/status caution windows own the entire display and input in both
interfaces. Known per-deck counter errors have a narrower policy: CLASSIC keeps
the stock deck info/counter region visible and passes its touches to stock;
MODERN publishes exact firmware code/text through the existing per-deck shell
alert and CHOOSE TRACK control. Failed/torn reads and mismatched prologues fail
open, with a runtime diagnostic when the reason changes. Unmapped IDs are
ignored and logged once per ID. The observer writes no firmware memory and
installs no instruction hook.

## Native UI evidence

Read-only static inspection used the existing locally retained firmware ELF at
`E:/Claude/xz-baseline/packages/xdj-xz-toolkit/vendor/decrypted_iso/pdj/extracted/pdj/rbp`.
The ELF SHA-256 is `6571c40b0523954d4091a4649f8200c4c615492fc37bae7f86cc89c6289510d2`,
matching `abi/xz-1.26-symbols.json`. Only UI dialog, text lookup and
graphics-window functions were disassembled.
The evidence directory retains a rerunnable script and its disassembly.

- `ui_com_DrawCautionBrowse`, `0x146fcc`: at `0x146fd8` calls
  `Ui_CautionIDCheck_Browse`. Zero destroys the live window at
  `gExWaveSize + 0x720 = 0x01adae30`, then zeros the handle on successful
  destruction at `0x146ffc`. Nonzero destroys the play waveform at `0x14700c`.
- The same function copies the localized UTF-16 message into
  `sCautionBrowseString`, `0x01ada958`, capacity 512 bytes. At `0x1470f8`
  onward it substitutes runtime parameters and divides the string into up to
  six draw rows. At `0x147330..0x147338` it writes computed width/height and position to
  `BrowseCautionWinInfo`, `0x01adae3c`. The live GR window descriptor is
  the authority for the displayed rectangle.
- `ui_com_DrawCautionStatus`, `0x147f08`: at `0x147f10` calls
  `Ui_CautionIDCheck_Status`. Zero destroys and clears
  `stiCautionStatusWinID`, `0x01adaea4`, at `0x147f2c..0x147f34`.
  Localized UTF-16 text is `sCautionStatusString`, `0x01ada718`, 512 bytes.
  `StatusCautionWinInfo` at `0x01adaea8` carries its size and position;
  the live GR descriptor owns the displayed rectangle.
- `Ui_CautionIDCheck_Status`, `0xe1c6c`, reads the current status caution
  from `g_CmnSystemInfo + 0x153c`; stock owns timeout and clearing.
- `Ui_CautionIDCheck_Counter`, `0xe1d80`, reads the current deck caution
  IDs from `g_CmnSystemInfo + 0x154c + deck*4`, hence `0x0222c668/66c`.
  `ui_Counter_DrawCautionWindow`, `0x22e740`, resolves these through
  `CautionID_DOWN_StringID_Tbl`, `0x003e8ba0`. Its retained draw-cache IDs at
  `0x004d5990` are not the visibility authority. Stock builds each deck's
  UTF-16 `ErrorCautionString` at `0x01c87f40 + deck*400`.
- Counter IDs 101..105 map to E-7206, E-8302, E-8304, E-8305 and E-8306.
  Other counter messages include ordinary states such as END, NOW LOADING
  and Not Loaded. Only the explicit normal-state IDs are allowed through;
  unknown nonzero IDs yield too. The firmware's English string
  `STR_CHAR_DEV_UNSUPPORTED_0` at `0x003eaad8` is exactly
  `E-8307: UNSUPPORTED DEVICE`. No runtime code manufactures that string.
- `NS_GetString`, `0x182310`, resolves the current language table and returns
  UTF-16. `DS_GR_GetWindowInfo`, `0x155528`, validates GR bit `0x40000000`
  and reads dimensions from GR offsets 0/2. Existing wave and raw-skin
  adapters already use those guarded descriptor reads.

`classic_popup.h` describes a different list-popup ABI: 0x210-byte rows with
UTF-16 at +0x10, a 0x63 text tag and load-target kinds. It is not evidence that
caution text uses that row layout. Do not reinterpret caution buffers as load
popup rows. Existing native-skin wide-string/window hooks confirm the drawing
path, but do not associate a complete translated caution with its current
buttons, parameters and lifecycle.

## Observation and policies

`stock_dialog.c` checks exact UI prologues and reads mutable fields twice. A
proven live browse OR status window yields the whole screen even when another
observation family is unreadable. If no window is proven live and an observation
fails, `xz_stock_dialog_observe` returns -1 and `xz_stock_dialog_active` returns
false. The runtime log records the actual failure family/reason once per change,
including recovery. Firmware mismatch therefore cannot permanently remove the
MODERN screen. The old shell caution filter and mixer's latched-ID gating are
no longer the runtime authority.

Only the complete, evidenced counter error IDs 101..105 are mapped, in
`stock_counter.h`:

| ID | Code | Exact English text | Shell kind |
| --- | --- | --- | --- |
| 101 | E-7206 | AUTH CHIP ERROR | DEVICE_ERROR |
| 102 | E-8302 | CANNOT PLAY TRACK | READ_ERROR |
| 103 | E-8304 | UNSUPPORTED | UNSUPPORTED_FILE |
| 104 | E-8305 | UNSUPPORTED | UNSUPPORTED_FILE |
| 105 | E-8306 | NO FILE | READ_ERROR |

Unknown or unmapped counter IDs produce no handoff, no protected region and no
alert. The native ID space is logged once per ID per boot; the diagnostic also
has a bounded set for non-native wider values. Parameterized strings are not
flattened into guessed alerts. The shell model reads the current counter IDs
with their guard and stable-read checks, and clears the alert on clear, unknown,
read failure or guard mismatch. The alert shows the exact code and English text
without a substitute default explanation.

CLASSIC protects x=0..399 / 400..799, y=311..479 for the corresponding deck.
The actual stock caution windows are 304x60 at (10,409) and (410,409), proven by
`ui_Counter_CreateCautionWindow` and constants at `0x46ffd0..0x46ffe7`.
Protecting the complete stock deck info/counter area also keeps its native context.
Only contacts starting in the protected region are routed to stock; an owned mod
drag remains with its owner. A stock contact retains ownership through release.

The display bridge captures the native backbuffer before its primary-Flip
suppression paths. The mod renderer restores only the affected region from that
capture after drawing, so a background video/presenter frame cannot substitute
its own pixels for the stock error. The rest of the interface continues drawing,
including the other deck, wave, menus and games. Native footer transforms leave
an affected deck's counter area alone. Runtime and display bridge from the same
build are required for this background-presenter protection.

Live global windows always use the native-screen fallback. Their translated
multi-row text, runtime substitutions, buttons and scope still have no complete
proven mapping to the per-deck alert model. Stock alone dismisses them.

MODERN opens the same MODS panel as CLASSIC (one MODS menu). A live window hides
it like any other layer: the screen, touches, keys and the encoder go to stock,
and the panel comes back open, on the same page, when stock closes the window.
A known counter error is never covered. On MODERN's play screen it shows on its
deck with CHOOSE TRACK. While MODS is open, the dock's title row turns into one red
strip: each deck's badge, the code and the firmware's own text, and SHOW DECK. A tap
on the strip closes MODS onto the play screen; the encoder never stops on it, and
the page's controls and the explanation below stay as they were. CLASSIC keeps the
affected stock deck area visible through MODS instead.

No connector observation links a specific native warning to the four-deck
cable. Route-up and E-8307 occurring together do not prove that cable caused it.
There is no cable-specific explanatory sentence in this change.

## Verification and limits

`ui/verify.py` runs the portable guarded-read fixtures.
`ui/verify_stock_dialog_runtime.py` runs the real compositor and touch/key
entry points with ASan/UBSan and can write fixture PPM previews to its first
argument. Protected CLASSIC counter regions and global-window frames preserve the supplied
native pixels; the rest of CLASSIC continues to draw. No-error frames are compared
byte-for-byte with the runtime from just before this work joined the one MODS menu (0318fe4). `test_stock_counter_model.c` exercises the
real model, real shell renderer and CHOOSE TRACK navigation. `tests/verify_stock_counter_bridge.py`
checks the actual backbuffer capture helper. Fixture previews are not controller screenshots.

Physical confirmation is still required for actual native-window lifetimes,
all languages, dialog-dismissal keys, game/video takeover transitions, USB
current warnings and the reported cable trigger. Software proof is not device
acceptance.
