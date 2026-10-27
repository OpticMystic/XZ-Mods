# Stock warning priority, firmware 1.26

The mod compositor must yield the whole screen and unchanged input while stock
shows an error or warning. This includes CLASSIC panels, MODERN, games and video
takeover. A read failure also yields. This observer writes no firmware memory
and installs no instruction hook.

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

## Observation and fallback

`stock_dialog.c` checks exact 16-byte prologues for all three UI families and
reads each observation twice. Live browse/status handles yield immediately.
Counter warnings yield on either deck. The observer does not depend on the
latched `TotalCou_GetBrowseDispMessageID` at `0x03eaff7c`, which the existing
shell-model caution filter can acknowledge while it remains set.

The complete translated dialog cannot yet be mapped reliably into the shell's
single-line, per-deck alert model. Runtime substitutions, multi-row text, native
icons, global versus deck scope and dismissal controls have no complete proven
alert mapping. MODERN therefore takes the required native-screen fallback for
all these dialogs. It never supplies the shell renderer's default alert words
or invents an E-code. Stock alone dismisses its own warning.

No connector observation links a specific native warning to the four-deck
cable. Route-up and E-8307 occurring together do not prove that cable caused it.
There is no cable-specific explanatory sentence in this change.

## Verification and limits

`ui/verify.py` runs the portable guarded-read fixtures.
`ui/verify_stock_dialog_runtime.py` runs the real compositor and touch/key
entry points with ASan/UBSan and can write fixture PPM previews to its first
argument. The known native rectangles remain byte-identical while overlays
are enabled. Fixture previews are not controller screenshots.

Physical confirmation is still required for actual native-window lifetimes,
all languages, dialog-dismissal keys, game/video takeover transitions, USB
current warnings and the reported cable trigger. Software proof is not device
acceptance.
